# 实施计划：音高修改链路改进

---

## P0：分段卡顿声 & 首次播放延迟

- [ ] 1. 修复 `world_vocoder.rs` 中 WORLD 分块 crossfade 能量守恒
   - 在 `vocode_pitch_shift_chunked` 的 overlap 区域，将线性 overlap-add 替换为等功率 crossfade：前一块权重 `cos(w * π/2)`，当前块权重 `sin(w * π/2)`
   - 修改写入逻辑：先读取前一块已写入的值，再与当前块做等功率混合，而非直接叠加
   - 处理边界情况：`overlap_len == 0` 时跳过 crossfade；分块数量为 1 时不执行任何 crossfade
   - _需求：1.1、1.2、1.3、1.4、1.5_

- [ ] 2. 在 `audio_engine/types.rs` 和 `engine.rs` 中新增 `ClipPitchReady` 命令
   - 在 `EngineCommand` 枚举中新增 `ClipPitchReady { clip_id: String }` 变体
   - 在 `engine.rs` 的命令处理循环中新增对 `ClipPitchReady` 的处理分支：复用 `last_timeline` 触发 snapshot rebuild（参照 `StretchReady` 的实现模式）
   - _需求：2.2、2.6_

- [ ] 3. 改造 `pitch_clip.rs` 中的 clip pitch MIDI 计算为异步预计算
   - 新增 `schedule_clip_pitch_jobs` 函数，遍历 timeline 中所有可见 clip，对缓存未命中的 clip 异步提交 `compute_clip_pitch_midi` 任务，任务完成后发送 `EngineCommand::ClipPitchReady`
   - 利用现有 `GLOBAL_CLIP_PITCH_INFLIGHT` 机制去重，跳过 source_path 为空或文件不存在的 clip
   - 修改 `get_or_compute_clip_pitch_midi_global`：缓存未命中时直接返回 `None`，不再同步计算
   - _需求：2.1、2.3、2.4、2.5_

- [ ] 4. 在 `snapshot.rs` 的 `UpdateTimeline` 处理中接入异步预计算调度
   - 在 `build_snapshot` 调用之前，调用 `schedule_clip_pitch_jobs` 触发所有可见 clip 的异步预计算
   - 在 `maybe_apply_pitch_edit_to_clip_segment` 中，当 `get_or_compute_clip_pitch_midi_global` 返回 `None` 时直接返回 `Ok(false)`，跳过本次 pitch edit
   - _需求：2.1、2.3_

---

## P1：周期性卡顿感 & 前后端显示不同步

- [ ] 5. 调整 `snapshot.rs` 中 pitch-stream WORLD worker 的分块参数
   - 将 `block_frames_normal` 从 `2s` 改为 `0.5s`，`warmup_block_frames` 同步调整为 `0.5s`
   - 将 `lookahead_frames_normal` 从 `3s` 降低到 `1.5s`
   - 确认 `vocode_pitch_shift_chunked` 的内部 `chunk_sec` 不超过新的 `block_frames_normal`（0.5s），如超过则同步调整
   - 验证 ring buffer 容量 `cap_frames = 8s` 不变
   - _需求：3.1、3.2、3.3、3.4、3.5_

- [ ] 6. 改造前端 `PitchStatusBadge.tsx` 为事件驱动
   - 移除 `pollDelayMsRef` 轮询逻辑及相关 `setTimeout` 调用
   - 新增对 `pitch_orig_analysis_started`、`pitch_orig_analysis_progress`、`pitch_orig_updated` 三个 Tauri 事件的监听，事件到达时立即更新徽章状态
   - 在组件卸载时（`useEffect` cleanup）正确调用 unlisten 清理监听器
   - _需求：4.1、4.2、4.5、4.6_

- [ ] 7. 改造前端曲线刷新逻辑，解决 `pitch_orig_updated` 与编辑状态的竞争
   - 在 `pitch_orig_updated` 事件处理中，同时触发参数面板音高曲线的 `refreshNow()`
   - 新增编辑保护：若 `liveEditOverrideRef` 非空（pointer 处于 down 状态），则将刷新操作推迟到 `pointer-up` 事件触发后执行
   - _需求：4.3、4.4_

---

## P2：ONNX voiced/unvoiced 边界噪声

- [ ] 8. 增强 `pitch_stream_onnx.rs` 中的 voiced/unvoiced 边界 crossfade
   - 在 `crossfade_into_ring` 中将线性权重替换为等功率权重：`w_curr = sin(w * π/2)`，`w_prev = cos(w * π/2)`
   - 将 `HACHISHIFTER_ONNX_VAD_XFADE_MS` 默认值从 `40ms` 增大到 `80ms`
   - 处理边界情况：`prev_tail` 或 `curr_preroll` 长度不足 `xfade_frames` 时使用实际可用长度，不产生越界
   - 在 voiced 段推理结果的首尾各做约 20ms 窗口的短时能量归一化，平滑衔接原音能量包络
   - _需求：5.1、5.2、5.3、5.4_
