# HachiTune HiFi-GAN 推理优化：分块推理、交叉淡入淡出、异步推理机制

> 基于 HachiTune 项目源码分析：`Source/Audio/Vocoder.*`、`Source/Audio/Synthesis/IncrementalSynthesizer.*`

---

## 1. 概述

HachiTune 使用 **PC-NSF-HiFiGAN** 作为实时神经声码器，以 ONNX Runtime 在 C++ 层面原生执行推理。为了让这个模型在**实时交互编辑场景**中低延迟运行，项目实现了一套完整的推理优化管线：

1. **ONNX Runtime 图优化**：算子融合、常量折叠、内存模式优化
2. **GPU 多后端加速**：CUDA / DirectML / CoreML / TensorRT 可插拔
3. **分块推理 + 交叉淡入淡出**：长音频拆分处理，避免 OOM
4. **异步推理管线**：后台线程执行，主线程非阻塞回调
5. **增量合成**：仅重合成被编辑的「脏区域」
6. **Scratch Buffer 预分配**：热路径零动态分配

---

## 2. ONNX Runtime 集成

### 2.1 模型格式

```
前端训练 (PyTorch)
    │
    ▼ 导出为 ONNX
pc_nsf_hifigan.onnx  ← 固化计算图
    │
    ▼ ONNX Runtime (C++)
跨平台推理引擎
```

模型输入/输出：

| 方向 | 名称 | Shape | 含义 |
|------|------|-------|------|
| input | mel | `[1, numMels, frames]` | Mel频谱 (列主序) |
| input | f0 | `[1, frames]` | 基频 (Hz) |
| output | waveform | `[1, samples]` | 合成波形 |

### 2.2 Session 配置优化

```cpp
Ort::SessionOptions Vocoder::createSessionOptions()
{
    Ort::SessionOptions sessionOptions;

    // 图优化：激活所有优化级别（算子融合、常量折叠、layout优化等）
    sessionOptions.SetGraphOptimizationLevel(
        GraphOptimizationLevel::ORT_ENABLE_ALL);

    // 内存模式优化：复用内存分配
    sessionOptions.EnableMemPattern();

    // CPU 内存 arena：大块预分配 + 复用
    sessionOptions.EnableCpuMemArena();

    // GPU 后端下减少 CPU 线程池
    if (executionDevice != "CPU") {
        sessionOptions.SetIntraOpNumThreads(1);
        sessionOptions.SetInterOpNumThreads(1);
    } else {
        int numThreads = max(1, hardware_concurrency) / 2;
        sessionOptions.SetIntraOpNumThreads(max(numThreads, 2));
    }

    // 添加执行 provider (CUDA / DirectML / CoreML / TensorRT)
    // ... (见 2.3 节)

    return sessionOptions;
}
```

### 2.3 GPU 多后端可插拔架构

```
                     ┌──────────────┐
                     │  Vocoder 层  │ (设备不可知)
                     └──────┬───────┘
                            │
            ┌───────────────┼───────────────┐
            │               │               │
   ┌────────▼──────┐ ┌─────▼──────┐ ┌─────▼──────┐
   │ CUDA Provider │ │ DML Provider│ │ CoreML     │
   │ (NVIDIA GPU)  │ │ (DirectX 12)│ │ (Apple NE) │
   └───────────────┘ └────────────┘ └────────────┘
         Windows         Windows        macOS
```

| Provider | 平台 | 编译开关 | 内存策略 |
|----------|------|---------|---------|
| **CUDA** | Windows (NVIDIA) | `-DUSE_CUDA=ON` | IoBinding |
| **DirectML** | Windows (AMD/Intel/NVIDIA) | `-DUSE_DIRECTML=ON` | 顺序执行 + 禁用MemPattern |
| **CoreML** | macOS | 自动检测 | IoBinding |
| **TensorRT** | 通用 (可选) | `-DUSE_TENSORRT` | IoBinding |
| **CPU** (fallback) | 全平台 | 默认 | CPU Arena |

**CUDA 和 DirectML 互斥**：不能同时开启，`#ifdef` 互斥编译。

**Fallback 机制**：每个 GPU Provider 加载失败时，自动降级到 CPU：

```cpp
try {
    sessionOptions.AppendExecutionProvider_CUDA(cudaOptions);
} catch (const Ort::Exception &e) {
    log("Failed to add CUDA provider, falling back to CPU");
}
```

### 2.4 IoBinding：GPU 内存优化

GPU 后端下使用 **IoBinding** 替代标准 `Run()` 来减少 CPU↔GPU 之间的数据拷贝：

```cpp
bool Vocoder::loadModel(...) {
    // ... 创建 session 后 ...
    
    if (executionDevice != "CPU") {
        ioBinding = std::make_unique<Ort::IoBinding>(*onnxSession);
    }
}

// 推理时
if (canUseIoBinding) {
    ioBinding->ClearBoundInputs();
    ioBinding->ClearBoundOutputs();
    
    // 绑定输入张量到 GPU 内存
    for (size_t i = 0; i < inputNames.size(); ++i)
        ioBinding->BindInput(inputNames[i], inputTensorScratch[i]);
    
    // 绑定输出到 CPU 可访问内存
    ioBinding->BindOutput(outputNames.front(), cpuMemoryInfo);
    
    onnxSession->Run(runOptions, *ioBinding);
    outputTensor = &ioBinding->GetOutputValues().front();
}
```

如果 IoBinding 失败，自动 fallback 到标准 Run API。

---

## 3. 分块推理 + 交叉淡入淡出

### 3.1 问题

PC-NSF-HiFiGAN 的推理计算量与输入帧数成正比。长音频直接推理会遇到：
- **CoreML**：超过一定 tensor 尺寸抛出 `"Error in building plan"`
- **GPU 内存**：单次 tensor 过大导致 OOM
- **延迟可预测性**：长推理阻塞用户交互

### 3.2 解决方案：分块 + 交叉

```
┌──────────────────────────────────────────────────────────────┐
│                       输入音频 (N 帧)                        │
├──────┬──────┬──────┬──────┬──────┬──────┬──────┬──────┬──────┤
│Chunk0│      │Chunk1│      │Chunk2│      │Chunk3│      │Chunk4│
│      │16帧  │      │16帧  │      │16帧  │      │16帧  │      │
└──────┼──────┴──────┼──────┴──────┼──────┴──────┼──────┴──────┘
       │   overlap   │   overlap   │   overlap   │   overlap
       │   (16帧)    │             │             │
       ▼             ▼             ▼             ▼
   跨块线性交叉淡入淡出 (linear crossfade)
```

#### 参数配置

```cpp
// 单块最大帧数：512 帧 ≈ 5.9 秒 (44100/512)
constexpr size_t kMaxChunkFrames = 512;

// 相邻块重叠帧数：16 帧 ≈ 186 ms
constexpr size_t kOverlapFrames = 16;
```

#### 为什么是 512 帧？
- 512 帧 × 128 mels × 4 bytes ≈ 262KB / chunk（mel 输入）
- 512 帧 × 512 hop × 4 bytes ≈ 1MB / chunk（波形输出）
- 经验值：在所有后端（CoreML、DirectML、CUDA）上安全

#### 为什么是 16 帧 overlap？
- 16 帧 × 11.6ms/帧 ≈ 186ms 的渐变窗口
- 足够平滑，听众听不到接缝
- 不会过度引入冗余计算

### 3.3 算法实现

```cpp
std::vector<float> Vocoder::infer(const std::vector<std::vector<float>> &mel,
                                  const std::vector<float> &f0)
{
    const size_t numFrames = min(mel.size(), f0.size());

    // ── 短输入：单次推理 ──
    if (numFrames <= kMaxChunkFrames) {
        return inferChunkLocked(mel, f0, numFrames);
    }

    // ── 长输入：分块 ──
    const size_t step = kMaxChunkFrames - kOverlapFrames; // 496 帧步进
    const size_t totalSamples = numFrames * hopSize;

    std::vector<float> waveform(totalSamples, 0.0f);

    for (size_t frameOff = 0; frameOff < numFrames; frameOff += step)
    {
        const size_t chunkEnd   = min(frameOff + kMaxChunkFrames, numFrames);
        const size_t chunkFrames = chunkEnd - frameOff;

        // 1. 切片 mel 和 f0
        std::vector<std::vector<float>> chunkMel(
            mel.begin() + frameOff, mel.begin() + chunkEnd);
        std::vector<float> chunkF0(
            f0.begin() + frameOff, f0.begin() + chunkEnd);

        // 2. 推理
        auto chunkWav = inferChunkLocked(chunkMel, chunkF0, chunkFrames);

        // 3. 写入结果
        if (chunkIdx == 0) {
            // 第一块：直接拷贝
            copy(chunkWav, waveform);
        } else {
            // 后续块：交叉淡入淡出 overlap 区域
            for (size_t i = 0; i < overlapSamples; ++i) {
                float t = (float)i / overlapSamples;  // 0 → 1
                waveform[dst + i] = waveform[dst + i] * (1 - t)  // 旧块 fade out
                                  + chunkWav[i] * t;              // 新块 fade in
            }
            // 非重叠尾部直接拷贝
            copy(tail_of_chunk, waveform_tail);
        }
    }

    // 最终 clamp
    for (auto &s : waveform)
        s = clamp(s, -1.0f, 1.0f);

    return waveform;
}
```

### 3.4 交叉淡入淡出示意图

```
块 N-1 的输出: ████████████████████████████████████▓▓▓▓▓▓▓▓
                                                         (fade out)
块 N 的输出:                       ░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░
                                   (fade in)
                                   
叠加区域:                           ████████████████████████████
                                   ← overlapSamples →
                                   (线性 crossfade)
```

### 3.5 性能日志

使用环境变量 `HACHITUNE_VOCODER_TRACE=1` 开启逐块计时：

```
Vocoder chunked infer [2048 frames, 5 chunks] total=342ms
  chunk 0/5 frames [0..512) -> 262144 samples
  chunk 1/5 frames [496..1008) -> 262144 samples
  ...
```

---

## 4. 异步推理管线

### 4.1 问题

在实时交互应用中，HiFi-GAN 推理即使经过优化也可能耗时数百毫秒。如果在音频线程上同步执行，会导致：
- **UI 卡顿**：编辑器无法响应
- **音频丢帧**：DAW 音频回调超时
- **用户体验差**：操作后有明显延迟

### 4.2 解决方案：后台线程 + 异步回调

```
┌──────────────────┐     ┌──────────────────┐     ┌──────────────────┐
│  主线程 (UI)     │     │  工作线程         │     │  JUCE Message     │
│                  │     │  (asyncWorker)    │     │  Thread          │
├──────────────────┤     ├──────────────────┤     ├──────────────────┤
│                  │     │                  │     │                  │
│ inferAsync() ────┼──→  │ queue.push()     │     │                  │
│                  │     │  condition.notify │     │                  │
│                  │     │       │          │     │                  │
│                  │     │       ▼          │     │                  │
│                  │     │  infer(mel, f0)  │     │                  │
│                  │     │       │          │     │                  │
│                  │     │       ▼          │     │                  │
│                  │ ◄───┼── callback ──────┼──→  │ callback()       │
│                  │     │                  │     │                  │
└──────────────────┘     └──────────────────┘     └──────────────────┘
```

### 4.3 核心数据结构

```cpp
class Vocoder {
private:
    struct AsyncTask {
        std::vector<std::vector<float>> mel;
        std::vector<float> f0;
        std::function<void(std::vector<float>)> callback;
        std::shared_ptr<std::atomic<bool>> cancelFlag;  // 可取消
    };

    std::thread asyncWorker;                    // 专用后台线程
    std::deque<AsyncTask> asyncQueue;           // 任务队列
    std::mutex asyncMutex;                      // 队列互斥锁
    std::condition_variable asyncCondition;     // 条件变量 (避免忙等)
    
    std::atomic<bool> isShuttingDown{false};    // 优雅关闭标志
    std::atomic<int> activeAsyncTasks{0};       // 活跃任务计数
};
```

### 4.4 工作线程实现

```cpp
Vocoder::Vocoder() {
    asyncWorker = std::thread([this]() {
        for (;;) {
            AsyncTask task;
            {
                std::unique_lock<std::mutex> lock(asyncMutex);
                
                // 条件变量等待：队列非空 或 正在关闭
                asyncCondition.wait(lock, [this]() {
                    return isShuttingDown || !asyncQueue.empty();
                });

                // 优雅关闭：队列空且 shutting down → 退出
                if (isShuttingDown && asyncQueue.empty())
                    return;

                task = std::move(asyncQueue.front());
                asyncQueue.pop_front();
            }

            // ── 取消检查 ──
            if (task.cancelFlag && task.cancelFlag->load()) {
                activeAsyncTasks.fetch_sub(1);
                // 仍然回调（空结果），让调用方清理状态
                juce::MessageManager::callAsync([cb = task.callback]() {
                    if (cb) cb({});
                });
                continue;
            }

            // ── 执行推理 ──
            auto result = infer(task.mel, task.f0);

            activeAsyncTasks.fetch_sub(1);

            // ── 主线程回调 ──
            juce::MessageManager::callAsync(
                [cb = task.callback, result = std::move(result)]() {
                    if (cb) cb(std::move(result));
                });
        }
    });
}
```

### 4.5 取消机制

```cpp
// 提交任务时传入 cancelFlag
void Vocoder::inferAsync(mel, f0, callback, cancelFlag);

// 调用方取消：
cancelFlag->store(true);
// → 工作线程检测到 → 跳过推理 → 回调空结果 → 调用方清理

// 新版任务自动取消旧版：
void IncrementalSynthesizer::synthesizeRegion(...) {
    if (cancelFlag)
        cancelFlag->store(true);            // 取消旧任务
    cancelFlag = make_shared<atomic<bool>>(false); // 新 cancelFlag
    uint64_t currentJobId = ++jobId;        // 递增 jobId
    // ...
    vocoder->inferAsync(mel, f0, callback, cancelFlag);
}

// 回调中验证 jobId：
if (currentJobId != jobId.load())
    return;  // 过时任务，丢弃结果
```

### 4.6 优雅关闭

```cpp
Vocoder::~Vocoder() {
    isShuttingDown.store(true);       // 1. 设置关闭标志
    
    {
        lock_guard lock(asyncMutex);
        asyncCondition.notify_all();  // 2. 唤醒工作线程
    }
    
    if (asyncWorker.joinable())
        asyncWorker.join();           // 3. 等待线程退出
    // → 线程检测到 isShuttingDown && 队列空 → 退出循环
}
```

---

## 5. Scratch Buffer 预分配

### 5.1 问题

每帧 ONNX 推理需要临时 buffer（输入张量、shape 信息、输出占位）。如果每次推理都动态 `new/malloc`，会产生大量堆分配，导致：
- 分配/释放开销
- 内存碎片
- GC/分配器锁竞争
- 不可预测的延迟尖刺

### 5.2 解决方案：模型加载时预分配，推理时复用

```cpp
class Vocoder {
private:
    // 模型加载时预分配（loadModel 中）
    
    // Shape 暂存
    std::vector<int64_t> melShapeScratch;   // [1, numMels, frames]
    std::vector<int64_t> f0ShapeScratch;    // [1, frames]
    
    // 数据暂存
    std::vector<float> melScratch;          // mel 数据 (列主序)
    std::vector<float> f0Scratch;           // f0 数据
    
    // ONNX 张量暂存
    std::vector<Ort::Value> inputTensorScratch;   // 输入 Ort::Value
    std::vector<Ort::Value> outputTensorScratch;  // 输出 Ort::Value
};
```

### 5.3 热路径：零堆分配

```cpp
std::vector<float> Vocoder::inferChunkLocked(
    const std::vector<std::vector<float>> &mel,
    const std::vector<float> &f0, size_t numFrames)
{
    // 1. 更新 shape（栈上操作）
    melShapeScratch[0] = 1;
    melShapeScratch[1] = static_cast<int64_t>(numMels);
    melShapeScratch[2] = static_cast<int64_t>(numFrames);

    // 2. 复用预分配 buffer（可能 resize，但频率低）
    melScratch.resize(melElementCount);  // 只在 chunk 大小变化时分配

    // 3. 填充数据（直接写入预分配内存）
    for (size_t frame = 0; frame < numFrames; ++frame) {
        for (int m = 0; m < numMels; ++m) {
            melScratch[dstIndex] = mel[frame][m];  // 列主序排列
        }
    }

    // 4. 复用 inputTensorScratch
    inputTensorScratch.clear();  // O(1) 只清空 vector，不释放内存
    inputTensorScratch.emplace_back(
        Ort::Value::CreateTensor<float>(cpuMemoryInfo,
            melScratch.data(), melScratch.size(),
            melShapeScratch.data(), melShapeScratch.size()));
    // ↑ CreateTensor 只是包装已有内存，不拷贝

    // 5. 推理
    onnxSession->Run(...);

    // 6. 读取输出（outputTensorScratch 复用）
}
```

**关键**：`Ort::Value::CreateTensor` 使用 `data()` 指针，**不拷贝**数据——melScratch 在整个推理周期中保持有效即可。

### 5.4 Mel 数据布局：列主序（Column-Major）

```cpp
// 标准行主序:  mel[frame][mel_band]
// ONNX 列主序:  mel[mel_band][frame]

// 填充代码:
for (size_t frame = 0; frame < numFrames; ++frame) {
    int m = 0;
    size_t dstIndex = frame;  // 列首
    for (; m < numMels; ++m, dstIndex += numFrames) {  // 每次跳 numFrames
        melScratch[dstIndex] = clamp(mel[frame][m], -15.0f, 5.0f);
    }
}
```

```
列主序列 view:
  melScratch = [band0_frame0, band0_frame1, ..., band0_frameN,
                band1_frame0, band1_frame1, ..., band1_frameN,
                ...]
```

---

## 6. 增量合成（Dirty Region Synthesis）

### 6.1 问题

传统 vocoder 对整首音频重新推理。但在编辑场景中，用户只修改了几个音符。全量重合成浪费 90%+ 的计算。

### 6.2 解决方案

```
用户修改音符/参数
    │
    ▼
标记脏区域 (dirty frame range)
    │
    ▼
computeSynthesisRange() → 扩展到完整 voiced 段 + padding
    │
    ▼
只对 [startFrame, endFrame) 范围切片 mel/f0
    │
    ▼
vocoder->inferAsync(slicedMel, slicedF0)
    │
    ▼
blendMask 混合 (voiced→合成, unvoiced→原始)
    │
    ▼
composeGlobalWaveform() → 拼回全局波形
```

### 6.3 合成范围扩展

```cpp
std::pair<int, int> 
IncrementalSynthesizer::computeSynthesisRange(int dirtyStart, int dirtyEnd)
{
    constexpr int kPadFrames = 24;       // 上下文padding
    constexpr int kGapBridgeFrames = 16;  // 桥接短UV间隙

    // 向后扩展：包含相邻voiced段，桥接≤16帧的UV间隙
    int start = dirtyStart;
    while (start > 0) {
        if (isVoiced(start-1)) { --start; continue; }  // voiced → 包含
        if (gap < 16) { --start; ++gap; continue; }    // 短UV → 桥接
        break;                                          // 长UV → 停止
    }
    start = max(0, start - kPadFrames);

    // 向前对称扩展
    int end = dirtyEnd;
    // ... 同理
    
    return {start, end};
}
```

**为什么桥接短UV间隙**：避免相邻音符间的 vocoder 相位重置。短 unvoiced 段（≤16帧 ≈ 186ms）被合并到同一块中推理，跨块 crossfade 更自然。

### 6.4 Voiced-Only Blend 策略

```
vocoder 推理输出 (synthesizedWav)
    │
    ▼
blendMask: voiced→1.0 (全用合成), unvoiced→0.0 (全用原始)
    │
    ▼
result[i] = synthesized[i] × mask[i] + original[i] × (1 - mask[i])
```

```cpp
std::vector<float> IncrementalSynthesizer::generateBlendMask(
    int startFrame, int endFrame, int hopSize)
{
    // 步骤1: 稳定性优先的帧级mask
    // 默认整个区域都用合成音频（避免内部的orig/synth comb artifacts）
    vector<float> frameMask(numFrames, 1.0f);

    // 只有长UV段（≥24帧）才保留原始音频（如清晰的呼吸/静音）
    constexpr int kKeepOriginalUnvoicedFrames = 24;
    for (each UV run) {
        if (runLen >= 24)
            fill(frameMask[runStart..runEnd], 0.0f);
    }

    // 步骤2: 帧级mask → 样本级mask (sample-and-hold)
    for (each frame) {
        mask[frameStart..frameEnd] = frameMask[i];
    }

    // 步骤3: 边界处线性斜坡过渡
    constexpr int kMinRampSamples = 512;  // ≈ 11.6ms
    for (each mask transition) {
        for (s in ramp):
            t = (s - rampStart) / (rampEnd - rampStart);
            mask[s] = fromVal + (toVal - fromVal) * t;
    }

    return mask;
}
```

### 6.5 全局波形拼合

```cpp
void Project::composeGlobalWaveform() {
    // 1. 基底层：原始波形
    audioData.waveform = audioData.originalWaveform;

    // 2. 叠加层：每个音符的 synthWaveform
    for (auto &note : notes) {
        if (!note.hasSynthWaveform()) continue;

        int noteStartSample = note.getStartFrame() * HOP_SIZE;
        int preroll = note.getSynthPreroll();  // 256样本的margin

        // Crossfade 过渡区
        for (int i = 0; i < preroll; ++i) {
            float t = (float)i / preroll;
            waveform[globalIdx] = original × (1-t) + synth × t;
        }

        // 音符主体
        copy(synth[preroll..], waveform[noteStartSample..]);
    }
}
```

每个音符的 `synthWaveform` 前后各有 256 样本的 margin，拼合时在 margin 区域与原始波形做 crossfade，确保编辑与非编辑区域的无缝衔接。

---

## 7. 完整推理管线全景

```
┌─────────────────────────────────────────────────────────────────────┐
│                        HachiTune Vocoder Pipeline                    │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│  用户操作 (UI)                                                      │
│    │                                                                │
│    ├─ 调整音符音高 / 时值 / 参数曲线                                  │
│    └─ Project→markDirty() → paramDirtyRange                         │
│                                                                     │
│  ┌─────────────────────────────────────────────────────────────┐    │
│  │ IncrementalSynthesizer::synthesizeRegion()                   │    │
│  │                                                              │    │
│  │  1. computeSynthesisRange()  → 扩展脏区域                    │    │
│  │  2. generateBlendMask()      → voiced/unvoiced 混合权重       │    │
│  │  3. HNSep 曲线重建 (如有编辑)                                  │    │
│  │  4. mel/f0 切片                                              │    │
│  └───────────────────────────┬─────────────────────────────────┘    │
│                              │                                       │
│                              ▼                                       │
│  ┌─────────────────────────────────────────────────────────────┐    │
│  │ Vocoder::inferAsync(mel, f0, callback)                       │    │
│  │                                                              │    │
│  │  → asyncQueue.push({mel, f0, callback, cancelFlag})         │    │
│  │  → asyncCondition.notify_one()                               │    │
│  └───────────────────────────┬─────────────────────────────────┘    │
│                              │                                       │
│  ┌───────────────────────────▼─────────────────────────────────┐    │
│  │ asyncWorker 线程                                             │    │
│  │                                                              │    │
│  │  infer(mel, f0):                                            │    │
│  │    if (numFrames > 512):                                    │    │
│  │      for chunk in chunks:                                   │    │
│  │        inferChunkLocked(chunkMel, chunkF0, chunkFrames)     │    │
│  │        crossfade(chunk, previous)                           │    │
│  │    else:                                                    │    │
│  │      inferChunkLocked(mel, f0, numFrames)                  │    │
│  └───────────────────────────┬─────────────────────────────────┘    │
│                              │                                       │
│  ┌───────────────────────────▼─────────────────────────────────┐    │
│  │ inferChunkLocked(mel, f0, numFrames)                         │    │
│  │                                                              │    │
│  │  1. melScratch 填充 (列主序列)                               │    │
│  │  2. f0Scratch 填充 + clamp(20-2000Hz)                        │    │
│  │  3. 创建 Ort::Value 张量 (复用 scratch)                       │    │
│  │  4. IoBinding? → GPU绑定运行                                  │    │
│  │     否则 → 标准 Run()                                        │    │
│  │  5. 读取输出 → clamp(-1, 1) → 返回                            │    │
│  └───────────────────────────┬─────────────────────────────────┘    │
│                              │                                       │
│                              ▼                                       │
│  回调 → blendMask 混合 → composeGlobalWaveform → 更新UI              │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘
```

---

## 8. 完整实现细节附录

> 以下为从源码提取的精确实现细节，补充主文档中的关键常量、边界条件、错误处理逻辑。

### 8.1 完整常量定义

```cpp
// Vocoder 层常量
constexpr float kMelMinClamp = -15.0f;       // mel 值下限
constexpr float kMelMaxClamp = 5.0f;          // mel 值上限
constexpr float kF0MinValid = 20.0f;          // F0 有效下限 (Hz)
constexpr float kF0MaxValid = 2000.0f;        // F0 有效上限 (Hz)
constexpr size_t kMaxChunkFrames = 512;       // 单块最大帧数
constexpr size_t kOverlapFrames = 16;         // 块间重叠帧数
constexpr int kSynthMarginSamples = 256;      // 每个音符 synth 的 margin
constexpr int kKeepOriginalUnvoicedFrames = 24; // blend 中保留原始 UV 的阈值
constexpr int kMinRampSamples = 512;          // blend boundary 最小斜坡样本
constexpr int kPadFrames = 24;                // 合成范围 padding
constexpr int kGapBridgeFrames = 16;          // UV 间隙桥接阈值

// MelSpectrogram 参数 (Constants.h)
constexpr int SAMPLE_RATE = 44100;
constexpr int HOP_SIZE = 512;
constexpr int WIN_SIZE = 2048;
constexpr int N_FFT = 2048;
constexpr int NUM_MELS = 128;
constexpr float FMIN = 40.0f;
constexpr float FMAX = 16000.0f;
```

### 8.2 CpuMemoryInfo 创建模式

```cpp
// 在 inferChunkLocked 中，作为 static 局部变量只创建一次
static const auto cpuMemoryInfo =
    Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
```

**关键**：`static const` 确保整个程序生命周期只创建一次 `Ort::MemoryInfo`，在推理热路径中直接复用。

### 8.3 outputTensorScratch 的 null 重置模式

标准 `Run()` 路径需要在每次调用前将 outputTensorScratch 中的 `Ort::Value` 恢复为 null：

```cpp
if (!ranWithIoBinding) {
    // 确保 outputTensorScratch 大小正确
    if (outputTensorScratch.size() != outputNames.size()) {
        outputTensorScratch.clear();
        outputTensorScratch.reserve(outputNames.size());
        for (size_t i = 0; i < outputNames.size(); ++i) {
            outputTensorScratch.emplace_back(nullptr);
        }
    }

    // 每次推理前重置所有输出值为 null
    for (auto &outputValue : outputTensorScratch) {
        outputValue = Ort::Value{nullptr};
    }

    // ORT 会把输出写入这些已重置的位置
    onnxSession->Run(runOptions,
                     inputNames.data(), inputTensorScratch.data(),
                     inputTensorScratch.size(),
                     outputNames.data(), outputTensorScratch.data(),
                     outputTensorScratch.size());
}
```

**为什么要重置？** ONNX Runtime 的 `Run()` API 只能创建新的输出 tensor 放入 `Ort::Value` 中；如果之前已有值，不能覆盖。置为 null 后 `Run()` 会创建新的。

**对比 IoBinding 路径**：IoBinding 不需要这个步骤，因为输出通过 `ioBinding->GetOutputValues()` 获取，每次 Run 返回新的 vector。

### 8.4 DirectML Provider 完整配置

DirectML 比 CUDA 需要更多的 SessionOptions 配置：

```cpp
#ifdef USE_DIRECTML
if (executionDevice == "DirectML") {
    try {
        // 1. 获取 DirectML API
        const OrtApi &ortApi = Ort::GetApi();
        const OrtDmlApi *ortDmlApi = nullptr;
        Ort::ThrowOnError(ortApi.GetExecutionProviderApi(
            "DML", ORT_API_VERSION,
            reinterpret_cast<const void **>(&ortDmlApi)));

        // 2. DirectML 不支持 MemPattern（必须禁用）
        sessionOptions.DisableMemPattern();

        // 3. DirectML 需要顺序执行模式
        sessionOptions.SetExecutionMode(ORT_SEQUENTIAL);

        // 4. 添加 DirectML provider（可指定 device_id）
        Ort::ThrowOnError(
            ortDmlApi->SessionOptionsAppendExecutionProvider_DML(
                sessionOptions, executionDeviceId));

        log("DirectML execution provider added (device " +
            std::to_string(executionDeviceId) + ")");
    } catch (const Ort::Exception &e) {
        log("Failed to add DirectML provider: " + std::string(e.what()));
        log("Falling back to CPU");
    }
}
#endif
```

**三个关键差异与 CUDA**：
1. 必须 `DisableMemPattern()`——DML 与 MemPattern 不兼容
2. 必须 `SetExecutionMode(ORT_SEQUENTIAL)`——DML 不支持并行执行
3. API 获取方式不同：CUDA 直接调用 `AppendExecutionProvider_CUDA`，DML 需要先获取 `OrtDmlApi` 接口

### 8.5 正弦波降级 Fallback

当 ONNX Runtime 不可用时，Vocoder 使用纯正弦波合成作为降级方案：

```cpp
std::vector<float> Vocoder::generateSineFallback(const std::vector<float> &f0) {
    size_t numFrames = f0.size();
    size_t numSamples = numFrames * hopSize;  // hopSize = 512

    std::vector<float> waveform(numSamples, 0.0f);
    float phase = 0.0f;

    for (size_t frame = 0; frame < numFrames; ++frame) {
        float freq = f0[frame];
        if (freq <= 0.0f) freq = 0.0f;  // 无基频 → 静音

        for (int s = 0; s < hopSize; ++s) {
            size_t sampleIdx = frame * hopSize + s;
            if (sampleIdx >= numSamples) break;

            if (freq > 0.0f) {
                // 振幅固定为 0.3, 相位连续跨越帧边界
                waveform[sampleIdx] = 0.3f * std::sin(phase);
                phase += 2.0f * PI * freq / sampleRate;  // sampleRate = 44100
                if (phase > 2.0f * PI)
                    phase -= 2.0f * PI;  // 相位 wrap
            }
        }
    }
    return waveform;
}
```

**关键**：相位 `phase` 在帧间**连续**，避免每帧 reset 造成的点击噪声。

### 8.6 双重任务取消保护

IncrementalSynthesizer 使用 **两层取消机制**确保过时任务不会破坏状态：

```
第1层: cancelFlag (std::shared_ptr<std::atomic<bool>>)
  → 旧任务的 cancelFlag 设为 true
  → Vocoder worker 线程检测到 → 跳过推理 → 回调空结果

第2层: jobId (std::atomic<uint64_t>)
  → 新任务获得递增的 jobId
  → 回调中验证: if (currentJobId != jobId.load()) return;
  → 即使第1层失效，第2层也能拦截过时回调
```

```cpp
// IncrementalSynthesizer::synthesizeRegion() 中:
void IncrementalSynthesizer::synthesizeRegion(...) {
    // 第1层: 取消旧任务
    if (cancelFlag)
        cancelFlag->store(true);

    // 创建新 cancelFlag 和新 jobId
    cancelFlag = std::make_shared<std::atomic<bool>>(false);
    uint64_t currentJobId = ++jobId;

    // 提交推理
    vocoder->inferAsync(mel, f0, [this, ...](auto result) {
        // 第2层: jobId 验证
        if (currentJobId != jobId.load())
            return;  // 过时回调，丢弃

        if (capturedCancelFlag->load()) {
            isBusy = false;
            return;
        }

        // ... 处理结果
    });
}
```

### 8.7 MelSpectrogram 构造参数

HNSep 路径中重新计算 mel 谱时使用的参数：

```cpp
// IncrementalSynthesizer 中:
MelSpectrogram melComputer(audioData.sampleRate);
// 等价于:
// MelSpectrogram melComputer(44100, 2048, 512, 128, 40.0f, 16000.0f);
//                            ^sr   ^nfft ^hop ^mels ^fmin ^fmax
```

这些参数必须与 PC-NSF-HiFiGAN 训练时使用的参数完全一致，否则 vocoder 输出会有严重失真。

### 8.8 HNSep 完整处理路径（IncrementalSynthesizer）

当 HNSep 曲线有活跃编辑时，在 vocoder 推理前的完整步骤：

```cpp
// Step A: 检查是否需要 HNSep 处理
const bool hasGlobalHNSep = audioData.harmonicWaveform.getNumSamples() > 0 &&
                             audioData.noiseWaveform.getNumSamples() > 0;
bool hasAnyHNSepCurves = false;

if (hasGlobalHNSep &&
    !audioData.voicingCurve.empty() &&
    !audioData.breathCurve.empty() &&
    !audioData.tensionCurve.empty() &&
    HNSepCurveProcessor::hasActiveEdits(*project, startFrame, endFrame)) {

    // Step B: 检查该范围内是否有非中性参数
    TensionProcessor tensionProc;
    hasAnyHNSepCurves = tensionProc.hasActiveEdits(
        audioData.voicingCurve.data() + startFrame,
        audioData.breathCurve.data() + startFrame,
        audioData.tensionCurve.data() + startFrame,
        endFrame - startFrame);
}
```

`TensionProcessor::hasActiveEdits` 的定义：

```cpp
bool TensionProcessor::hasActiveEdits(
    const float *voicingCurve, const float *breathCurve,
    const float *tensionCurve, int numFrames) const
{
    for (int i = 0; i < numFrames; ++i) {
        const float voicing = voicingCurve ? voicingCurve[i] : 100.0f;
        const float breath  = breathCurve  ? breathCurve[i]  : 100.0f;
        const float tension = tensionCurve ? tensionCurve[i] : 0.0f;
        if (std::abs(voicing - 100.0f) > 0.001f ||
            std::abs(breath  - 100.0f) > 0.001f ||
            std::abs(tension)          > 0.001f)
            return true;
    }
    return false;
}
```

**注意阈值**：`0.001f` 用于浮点比较——任何值偏离默认值超过千分之一就认为有活跃编辑。这比 `!= 0` 更鲁棒，能容忍浮点累积误差。

### 8.9 音符 SynthWaveform 分布的边际逻辑

每个音符在合成后获得自己的 `synthWaveform`，包括前后 256 样本的 margin 用于与原始波形无缝 crossfade：

```
            ← leftMargin →  ← noteSamples →  ← rightMargin →

synthWaveform:   [preroll |   note body    | postroll]
                   256         noteSamples     256

            leftMargin 从 targetSegment 中取
            对应 global 位置: [noteStartSample-256, noteStartSample)

            note body 对应 global: [noteStartSample, noteEndSample)
            优先用 synthesized，超出范围的用 srcClip

            rightMargin 对应 global: [noteEndSample, noteEndSample+256)
```

```cpp
for (auto &note : notes) {
    if (!note.isDirty() && !note.isSynthDirty() &&
        !paramDirtyOverlap && note.hasSynthWaveform())
        continue;  // 跳过不需要更新的音符

    const int noteSamples = noteEndSample - noteStartSample;
    const int leftMarginAvail = noteStartSample - targetStartSample;
    const int leftMargin = max(0, min(kSynthMarginSamples, leftMarginAvail));
    const int rightMarginAvail = targetEndSample - noteEndSample;
    const int rightMargin = max(0, min(kSynthMarginSamples, rightMarginAvail));

    const int totalSynthLen = leftMargin + noteSamples + rightMargin;
    std::vector<float> noteSynth(totalSynthLen, 0.0f);

    // 从 targetSegment 拷贝对应区域
    // ...

    // 音符主体: 合成范围外用 srcClip 填充
    if (note.hasSrcClipWaveform()) {
        const auto &srcClip = note.getSrcClipWaveform();
        for (int i = 0; i < noteSamples; ++i) {
            int globalFrame = (noteStartSample + i) / hopSize;
            if (globalFrame >= overlapStart && globalFrame < overlapEnd)
                continue;  // 合成覆盖范围，跳过

            // map source position (处理 stretch)
            float srcPos = (float)i * srcSamples / noteSamples;
            int srcIdx = (int)srcPos;
            if (srcIdx >= 0 && srcIdx < srcSamples)
                noteSynth[leftMargin + i] = srcClip[srcIdx];
        }
    }

    note.setSynthWaveform(std::move(noteSynth), leftMargin);
}
```

**边际存在的意义**：`composeGlobalWaveform()` 在拼合音符到全局波形时，在 margin 区域与原始音频做线性 crossfade，避免合成/非合成边界的硬切 pop 声。

### 8.10 ONNX 输入输出名称的动态获取

Vocoder 在模型加载时动态获取输入输出名称，而非硬编码：

```cpp
// 获取输入名称
size_t numInputs = onnxSession->GetInputCount();
for (size_t i = 0; i < numInputs; ++i) {
    auto namePtr = onnxSession->GetInputNameAllocated(i, *allocator);
    inputNameStrings.push_back(namePtr.get());  // std::string
}
for (auto &name : inputNameStrings) {
    inputNames.push_back(name.c_str());  // const char* (for ORT API)
}

// 获取输出名称（同理）
size_t numOutputs = onnxSession->GetOutputCount();
for (size_t i = 0; i < numOutputs; ++i) {
    auto namePtr = onnxSession->GetOutputNameAllocated(i, *allocator);
    outputNameStrings.push_back(namePtr.get());
}
for (auto &name : outputNameStrings) {
    outputNames.push_back(name.c_str());
}
```

**设计原因**：不同版本的 PC-NSF-HiFiGAN ONNX 模型可能有不同的输入/输出命名。动态获取确保兼容性。

### 8.11 推理互斥锁（inferenceMutex）

```cpp
class Vocoder {
private:
    mutable std::mutex inferenceMutex;  // 保护 ONNX session 并发访问

    std::vector<float> inferChunkLocked(...) {
        // 调用方必须持有 inferenceMutex
    }
};

std::vector<float> Vocoder::infer(...) {
    std::lock_guard<std::mutex> lock(inferenceMutex);
    // ... 分块/单块推理 ...
    // inferChunkLocked() 调用在此锁保护下
}
```

`inferenceMutex` 在 `infer()` 层获取，覆盖整个推理过程（包括分块循环）。这确保：
1. 工作线程 (`asyncWorker`) 中的推理是互斥的
2. `reloadModel()` 调用 `inferenceMutex` 时等待当前推理完成
3. 同步 `infer()` 调用也不会并发执行

---

## 9. 性能总结

| 优化技术 | 效果 | 适用场景 |
|---------|------|---------|
| **ONNX Runtime 全图优化** | 算子融合、常量折叠 | 所有推理 |
| **GPU 多后端** | 3-10x 推理加速 | 实时交互 |
| **IoBinding** | 减少 CPU↔GPU 拷贝 | GPU 后端 |
| **分块推理** | 避免 OOM，支持任意长度 | 长音频合成 |
| **交叉淡入淡出** | 无缝拼接，无接缝伪影 | 分块推理 |
| **异步推理** | 主线程不阻塞，UI 流畅 | 实时交互 |
| **可取消任务** | 避免过期计算浪费 | 频繁编辑 |
| **增量合成** | 避免全量重算，节省 90%+ | 编辑场景 |
| **Scratch Buffer 预分配** | 热路径零分配，延迟稳定 | 所有推理 |
| **Voiced-Only Blend** | 保留原始气息/噪声质感 | unvoiced 段 |
