# 需求文档：音高修改链路改进

## 引言

HachiShifter 的音高修改链路（Pitch Pipeline）存在三类核心问题：

1. **前后端显示不同步**：前端参数面板的音高曲线刷新与后端 `pitch_orig_updated` 事件之间存在竞争，导致用户看到的曲线与实际处理状态不一致。
2. **延迟长**：`pitch_clip.rs` 中的 `get_or_compute_clip_pitch_midi_global` 在 pitch-stream worker 线程中同步执行 WORLD F0 分析，阻塞 ring buffer 写入，造成首次播放时长时间静音。
3. **分段卡顿声**：`vocode_pitch_shift_chunked` 的分块 crossfade 使用线性 overlap-add，能量在边界处不守恒，产生可听见的"咔哒/哑音"；pitch-stream 的 2s 大块处理策略在块间隙期间 ring buffer 停止写入，播放头追上后出现卡顿。

本文档描述针对上述三类问题的改进需求，覆盖后端 Rust 代码（`world_vocoder.rs`、`pitch_clip.rs`、`snapshot.rs`、`audio_engine/types.rs`）和前端 TypeScript 代码（`PitchStatusBadge.tsx`、`usePianoRollData.ts` 等）。

---

## 需求

### 需求 1：修复 WORLD 分块 crossfade 能量守恒问题

**用户故事：** 作为一名音频编辑用户，我希望在音高修改后的音频中，分段边界处不出现可听见的卡顿声或能量凹陷，以便获得平滑连续的音频输出。

#### 背景

当前 `world_vocoder.rs` 中 `vocode_pitch_shift_chunked` 的分块 crossfade 实现：
- 前一块在 `[chunk_start, chunk_start+overlap_len)` 区域写入 `v_prev * (1-w)`（fade-out）
- 当前块在同一区域写入 `v_curr * w`（fade-in）并通过 `out[dst_idx] = out[dst_idx] * (1.0 - w) + v * w` 叠加

问题：两者叠加后在边界中点处总能量为 `0.5 * v_prev + 0.5 * v_curr`，而不是期望的 `v_curr`，导致能量凹陷（约 -6dB），听感上表现为"卡顿/哑音"。

#### 验收标准

1. WHEN `vocode_pitch_shift_chunked` 处理多个分块时 THEN 系统 SHALL 在分块边界的 overlap 区域使用**等功率 crossfade**（equal-power crossfade），即前一块使用 `cos(w * π/2)` 权重，当前块使用 `sin(w * π/2)` 权重，确保 `cos²(w) + sin²(w) = 1` 能量守恒。
2. WHEN overlap 区域写入时 THEN 系统 SHALL 先读取前一块已写入的值，再与当前块的值做等功率混合，而不是直接 overlap-add。
3. IF `overlap_len == 0` THEN 系统 SHALL 跳过 crossfade 逻辑，直接写入当前块数据。
4. WHEN 等功率 crossfade 完成后 THEN 系统 SHALL 保证 overlap 区域的输出幅度与相邻非 overlap 区域的幅度连续，无突变。
5. WHEN 分块数量为 1（整段音频不超过 `chunk_sec`）时 THEN 系统 SHALL 不执行任何 crossfade，直接输出单块结果。

---

### 需求 2：将 clip pitch MIDI 计算改为异步预计算

**用户故事：** 作为一名音频编辑用户，我希望在开始播放后立即听到音高修改效果，而不是等待数秒的静音，以便获得低延迟的实时预览体验。

#### 背景

当前 `pitch_clip.rs` 中 `get_or_compute_clip_pitch_midi_global` 在缓存未命中时，在 pitch-stream worker 线程中**同步执行** WORLD F0 分析（包含音频解码、重采样、Harvest/DIO 分析），耗时可达数秒，期间 ring buffer 的 `write_frame` 不前进，触发 `pitch_callbacks_silenced_waiting` 计数飙升，用户听到长时间静音。

#### 验收标准

1. WHEN `EngineCommand::UpdateTimeline` 被处理时 THEN 系统 SHALL 在 `build_snapshot` 之前，异步触发所有可见 clip 的 `compute_clip_pitch_midi` 预计算（类似 `schedule_stretch_jobs` 的模式），不阻塞 snapshot 构建。
2. WHEN clip pitch MIDI 预计算完成时 THEN 系统 SHALL 通过新增的 `EngineCommand::ClipPitchReady { clip_id: String }` 命令通知引擎，触发一次 snapshot rebuild（类似 `StretchReady` 的处理方式）。
3. WHEN pitch-stream worker 调用 `maybe_apply_pitch_edit_to_clip_segment` 时，如果 clip pitch MIDI 缓存未命中 THEN 系统 SHALL 直接返回 `Ok(false)`（跳过本次 pitch edit），而不是同步计算，等待 `ClipPitchReady` 后下一次 pitch-stream 重建时再处理。
4. WHEN clip pitch MIDI 预计算任务已在 inflight 集合中时 THEN 系统 SHALL 不重复提交相同任务（使用现有的 `GLOBAL_CLIP_PITCH_INFLIGHT` 机制去重）。
5. IF clip 的 source_path 为空或文件不存在 THEN 系统 SHALL 跳过该 clip 的预计算，不产生错误。
6. WHEN `EngineCommand::ClipPitchReady` 触发 snapshot rebuild 时 THEN 系统 SHALL 复用现有的 `last_timeline` 状态，不需要前端重新发送 `UpdateTimeline`。

---

### 需求 3：pitch-stream WORLD worker 改为小块流水线处理

**用户故事：** 作为一名音频编辑用户，我希望在音高修改播放过程中，不出现因处理大块数据导致的周期性卡顿感，以便获得平滑的连续播放体验。

#### 背景

当前 `snapshot.rs` 中 pitch-stream WORLD worker 使用 `block_frames_normal = 2s` 的大块处理策略。每次处理完一个 2s 块后写入 ring，然后再处理下一个 2s 块。在处理下一块的间隙（WORLD 分析耗时），ring 的 `write_frame` 不前进，如果播放头追上了 `write_frame`，就会触发 hard-start 等待（静音），等下一块写完后突然出声，形成"卡顿感"。

#### 验收标准

1. WHEN pitch-stream WORLD worker 处于正常播放状态时 THEN 系统 SHALL 将处理块大小从 `block_frames_normal = 2s` 降低到 `block_frames_normal = 0.5s`，减少每块处理完成后的等待间隙。
2. WHEN pitch-stream WORLD worker 处于 warmup 阶段时 THEN 系统 SHALL 将 `warmup_block_frames` 保持为 `0.5s`（与正常块大小一致），`warmup_ahead_frames` 保持为 `0.5s`。
3. WHEN pitch-stream WORLD worker 正常运行时 THEN 系统 SHALL 将 `lookahead_frames_normal` 从 `3s` 降低到 `1.5s`，减少内存占用和不必要的超前渲染。
4. IF `vocode_pitch_shift_chunked` 的内部 `chunk_sec` 大于 pitch-stream 的 `block_frames_normal` 对应秒数时 THEN 系统 SHALL 将 `chunk_sec` 调整为不超过 `block_frames_normal` 对应秒数，避免 WORLD 内部分块与 pitch-stream 分块产生双重边界噪声。
5. WHEN 调整块大小后 THEN 系统 SHALL 保证 pitch-stream 的 ring buffer 容量（`cap_frames = 8s`）不变，确保有足够的缓冲空间。

---

### 需求 4：前端音高曲线刷新改为事件驱动

**用户故事：** 作为一名音频编辑用户，我希望在后端完成音高分析后，前端参数面板的音高曲线能立即同步更新，而不是依赖轮询延迟，以便获得准确的实时视觉反馈。

#### 背景

当前前端 `PitchStatusBadge` 采用轮询机制（初始 1200ms，逐步增大到 4000ms），而非事件驱动。当后端 `pitch_orig_updated` 事件触发后，前端参数面板的曲线刷新依赖 `usePianoRollData.ts` 的可见窗重拉取，但这个刷新路径与状态徽章的轮询是两条独立链路，容易出现"徽章已更新但曲线还没刷新"或反之的情况。

此外，当 `analysis_pending=true` 时，收到 `pitch_orig_updated` 后，如果用户正在编辑（`liveEditOverrideRef` 非空），前端会重新拉取曲线，可能覆盖掉用户刚刚绘制的内容，造成视觉跳变。

#### 验收标准

1. WHEN 后端发出 `pitch_orig_analysis_started` 事件时 THEN 前端 SHALL 立即将对应 root track 的状态徽章更新为"分析中"，不依赖轮询。
2. WHEN 后端发出 `pitch_orig_analysis_progress` 事件时 THEN 前端 SHALL 立即更新进度显示。
3. WHEN 后端发出 `pitch_orig_updated` 事件时 THEN 前端 SHALL 同时触发参数面板的音高曲线刷新（调用 `refreshNow()` 或等效方法），确保徽章状态和曲线显示同步更新，延迟不超过一个渲染帧（约 16ms）。
4. IF 用户正在编辑音高曲线（`liveEditOverrideRef` 非空，即 pointer 处于 down 状态）时收到 `pitch_orig_updated` 事件 THEN 前端 SHALL 延迟曲线刷新，直到用户完成编辑（`pointer-up` 事件触发后）再执行刷新。
5. WHEN 前端监听 Tauri 事件时 THEN 系统 SHALL 在组件卸载时正确清理事件监听器，避免内存泄漏。
6. IF 轮询机制已被事件驱动替代 THEN 系统 SHALL 移除 `PitchStatusBadge` 中的 `pollDelayMsRef` 轮询逻辑，保留事件监听作为唯一的状态更新来源。

---

### 需求 5：ONNX voiced/unvoiced 边界 crossfade 增强

**用户故事：** 作为一名使用 NSF-HiFiGAN ONNX 算法的用户，我希望在 voiced 段和 unvoiced 段之间的过渡处不出现明显的噪声或音色突变，以便获得更自然的音高修改效果。

#### 背景

当前 `pitch_stream_onnx.rs` 中 `crossfade_into_ring` 使用线性 crossfade（`w` 从 0 到 1），默认 `xfade_ms = 40ms`。在 voiced→unvoiced 或 unvoiced→voiced 边界处，两者的音色差异可能较大，40ms 的线性 crossfade 不足以掩盖过渡噪声。

#### 验收标准

1. WHEN `crossfade_into_ring` 执行 crossfade 时 THEN 系统 SHALL 将线性权重 `w` 替换为等功率权重：`w_curr = sin(w * π/2)`，`w_prev = cos(w * π/2)`，确保过渡区域能量守恒。
2. WHEN 默认 crossfade 时长时 THEN 系统 SHALL 将 `HACHISHIFTER_ONNX_VAD_XFADE_MS` 的默认值从 `40ms` 增大到 `80ms`，给过渡区域更多时间。
3. IF `prev_tail` 或 `curr_preroll` 长度不足 `xfade_frames` 时 THEN 系统 SHALL 使用实际可用长度进行 crossfade，不产生越界访问。
4. WHEN voiced 段推理完成后 THEN 系统 SHALL 对推理结果的首尾各做一次短时能量归一化（约 20ms 窗口），使其与原音的能量包络更平滑衔接，减少突然的音量跳变。

---

## 优先级与实施顺序

| 优先级 | 需求 | 解决问题 | 改动文件 |
|--------|------|----------|----------|
| P0 | 需求 1：WORLD crossfade 能量守恒 | 分段卡顿声 | `world_vocoder.rs` |
| P0 | 需求 2：clip pitch 异步预计算 | 延迟长、首次播放静音 | `pitch_clip.rs`、`snapshot.rs`、`audio_engine/types.rs`、`engine.rs` |
| P1 | 需求 3：pitch-stream 小块流水线 | 周期性卡顿感 | `snapshot.rs` |
| P1 | 需求 4：前端事件驱动刷新 | 前后端显示不同步 | 前端 `PitchStatusBadge.tsx`、`usePianoRollData.ts` 等 |
| P2 | 需求 5：ONNX crossfade 增强 | ONNX 边界噪声 | `pitch_stream_onnx.rs` |

---

## 技术约束

- 所有后端改动必须保持与现有 `EngineCommand` 枚举的向后兼容性（新增命令变体，不修改现有变体）。
- `vocode_pitch_shift_chunked` 的公开函数签名不变，仅修改内部实现。
- 前端改动不引入新的外部依赖，使用现有的 Tauri 事件 API。
- 所有改动必须通过现有的 `HACHISHIFTER_DEBUG_COMMANDS=1` 环境变量开关输出调试日志。
- 不修改 WORLD DLL 的调用接口（`world_vocoder.rs` 中的 FFI 绑定保持不变）。
