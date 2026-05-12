# HachiTune Tension 处理算法与机制详解

> 基于 HachiTune 项目源码分析：`Source/Audio/TensionProcessor.*`、`Source/Utils/HNSepCurveProcessor.*`、`Source/Audio/Synthesis/IncrementalSynthesizer.cpp`

---

## 1. 概述

Tension 是 HachiTune 中谐波-噪声分离（HNSep）音色控制系统的三条曲线之一，与 **voicingCurve**（谐波能量）和 **breathCurve**（噪声能量）共同构成完整的歌声音色编辑能力。

Tension 通过 **STFT 域频谱倾斜滤波** 改变谐波分量的频谱包络，让用户在「明亮尖锐」与「温暖柔和」之间连续调整——**不影响音高，不改变能量，只改音色光谱分布**。

---

## 2. 数据模型

### 2.1 参数定义

| 属性 | 值 |
|------|-----|
| 取值范围 | `[-100.0, 100.0]` |
| 默认值（中性） | `0.0` |
| 存储精度 | `float` (32-bit) |
| 时间单位 | 帧（44100Hz / 512 hop = ~11.6ms/帧） |

### 2.2 双层存储架构

```
┌──────────────────────────────────────────────────────────────┐
│                   Note 级别 (用户可编辑)                        │
│                                                              │
│  class Note {                                                │
│    std::vector<float> tensionCurve;  // 长度 = 音符输出帧数   │
│  };                                                          │
│                                                              │
│  每个音符独立拥有自己的 tensionCurve，用户在 UI 中编辑的       │
│  就是这个副本。                                               │
└──────────────────────────────────────────────────────────────┘
                            │
            rebuildCurves / extractNoteCurves
                            │
┌──────────────────────────────────────────────────────────────┐
│                   Dense 全局级别 (推理用)                       │
│                                                              │
│  struct AudioData {                                          │
│    std::vector<float> tensionCurve;  // 长度 = 整首音频总帧数 │
│  };                                                          │
│                                                              │
│  在合成前从各 Note 的曲线重建出来，供 TensionProcessor 直接    │
│  索引读取。                                                   │
└──────────────────────────────────────────────────────────────┘
```

**设计原因**：Note 级别的曲线随音符拉伸而伸缩，Dense 全局曲线则是按固定帧率展开，便于逐帧查询。

---

## 3. 曲线同步管理（HNSepCurveProcessor）

`HNSepCurveProcessor` 负责两层数据的一致性维护，提供四个核心操作：

### 3.1 初始化 (`initializeCurves`)

分析完成后调用：

```
1. 确保 AudioData 的三条 HNSep 曲线有正确长度（不足则填默认值）
2. 为每个音符从 AudioData 的对应帧范围 slice 出 note-local 副本
```

### 3.2 全局重建 (`rebuildCurvesFromNotes`)

用户编辑音符曲线后调用：

```
1. 将 AudioData.tensionCurve 全部恢复为默认值 (0.0)
2. 遍历每个音符，检查 overlap：
   - 将 note.tensionCurve 通过 CurveResampler::resampleLinear 调整到
     音符的当前输出时长（处理拉伸变化）
   - 写入 AudioData.tensionCurve 的对应帧区间
```

### 3.3 局部增量重建 (`rebuildCurvesForRange`)

增量合成前调用（核心优化）：

```
1. 只重置 [startFrame, endFrame) 区间的 AudioData.tensionCurve 为默认值
2. 只遍历与该区间相交的音符
3. 每个相交音符将其 tensionCurve resample 后写入 overlap 区间
```

**为什么需要局部重建**：因为拉伸一个音符会改变其 `durationFrames`，进而影响其后面音符的全局帧位置，但 HNSep 曲线本身只影响本音符。局部重建避免了 O(N) 遍历所有音符。

### 3.4 从全局还原 (`extractNoteCurvesFromMaster`)

项目加载/向后兼容时使用：从已存在的 Dense 全局曲线中 slice 出 note-local 副本。

### 3.5 是否有效编辑检测 (`hasActiveEdits`)

```cpp
bool hasActiveEdits(const Project &project, int startFrame, int endFrame) {
    return curveDiffersFrom(audioData.tensionCurve,  startFrame, endFrame, 0.0f) ||
           curveDiffersFrom(audioData.voicingCurve, startFrame, endFrame, 100.0f) ||
           curveDiffersFrom(audioData.breathCurve,  startFrame, endFrame, 100.0f);
}
```

三个曲线**任一偏离默认值**就认为有活跃编辑，需要走 HNSep 处理路径。

---

## 4. 核心算法：STFT 域频谱倾斜（TensionProcessor）

### 4.1 处理入口

```cpp
std::vector<float> TensionProcessor::processSegment(
    const float *harmonicData,
    const float *noiseData,
    int numSamples,
    const float *voicingCurve,
    const float *breathCurve,
    const float *tensionCurve,
    int numFrames) const;
```

### 4.2 完整处理流水线

```
输入: 谐波波形 + 噪声波形 + voicing/breath/tension 曲线
                                    │
                    ┌───────────────┴───────────────┐
                    │  步骤1: 逐样本振幅缩放          │
                    │                                │
                    │  for i in 0..numSamples:       │
                    │    frame = i / HOP_SIZE         │
                    │    harmonic *= voicing%/100      │
                    │    noise    *= breath%/100       │
                    │                                │
                    │  默认值: voicing=100, breath=100│
                    └───────────────┬───────────────┘
                                    │
              ┌─────────────────────┴─────────────────────┐
              │  步骤2: 如果有 tension 编辑                │
              │                                           │
              │  if any(|tension[frame]| > 0.001):        │
              │    preEmphasisBaseTensionSegment()         │
              │  else:                                    │
              │    processedHarmonic = scaledHarmonic      │
              └─────────────────────┬─────────────────────┘
                                    │
                    ┌───────────────┴───────────────┐
                    │  步骤3: 谐波 + 噪声混合        │
                    │                                │
                    │  result[i] = processedHarmonic  │
                    │            + scaledNoise        │
                    └───────────────┬───────────────┘
                                    │
                              输出波形
```

### 4.3 频谱倾斜核心算法 (`preEmphasisBaseTensionSegment`)

#### 4.3.1 参数映射

```
用户 tension ∈ [-100, 100]

转换:
  maxTiltDb = 12.0          // 最大倾斜幅度 (dB)
  b = -(tension / 100.0) × maxTiltDb

  所以:
  tension = +100  →  b = -12 dB  (高频衰减，声音变暖)
  tension = 0     →  b =   0 dB  (中性，不变)
  tension = -100  →  b = +12 dB  (高频提升，声音明亮)
```

#### 4.3.2 参考频率与倾斜斜率

```
参考频率: f₀ = 1500 Hz (交叉点，此频率处增益恒为 0dB)

nyquist = sampleRate / 2 = 22050 Hz
FFT Bin 数 = kFFTSize / 2 + 1 = 1025

x₀ = kFFTBin / (nyquist / 1500) = 1025 / (22050/1500) ≈ 69.7

对每个频点 k:
  filterDb(k) = (-b / x₀) × k + b

  物理含义:
  - 频率 0 Hz (k=0):     gain = b dB
  - 频率 1500 Hz (k≈70):  gain ≈ 0 dB (参考交叉点)
  - 频率 22050 Hz (k=1024): gain ≈ -b × 13.7 dB
```

#### 4.3.3 逐帧 STFT 处理

```
参数:
  FFT 尺寸: 2048
  Hop 尺寸: 512
  窗函数:   Hann (汉宁窗)
  窗长度:   2048

对每个 STFT 帧:
  1. 从 padded 数组中取 N=2048 个样本
  2. 乘以 Hann 窗
  3. 前向 FFT (radix-2 Cooley-Tukey, 手写实现)
  4. 对每个频点 k ∈ [0, 1024]:
     a. 计算 filterDb = clamp((-b/x₀)*k + b, -12, +12)
     b. 计算 filterGain = 10^(filterDb / 20)
     c. 频谱的实部和虚部同乘 filterGain
  5. 逆 FFT (共轭对称重建 + IFFT)
  6. 乘以 Hann 合成窗
  7. 重叠相加 (Overlap-Add, OLA) 到输出 buffer
```

#### 4.3.4 OLA 权重归一化

```cpp
// 积累窗口权重
output[idx]  += outFrame[n] * w
windowSum[idx] += w * w

// 最终归一化
output[i] /= windowSum[i]
```

使用 **Hann 窗的平方和** 做归一化，确保重叠区域能量守恒。

#### 4.3.5 RMS 能量归一化

```cpp
// 滤波前
originalRms = sqrt(Σ(sample²) / numSamples)

// 滤波后
filteredRms = sqrt(Σ(result²) / numSamples)

// 缩放对齐
if (filteredRms > 1e-10f) {
    scale = originalRms / filteredRms;
    for (auto &s : result) s *= scale;
}
```

**目的**：频谱倾斜改变了频域能量分布，RMS 归一化确保输出响度与输入一致，避免用户操作 tension 时感知到响度变化。

#### 4.3.6 峰值保护

```cpp
if (renormalizedMax > originalMax * 1.5f) {
    scale = (originalMax * 1.5f) / renormalizedMax;
    for (auto &s : result) s *= scale;
}
```

**目的**：极端的频谱倾斜可能导致某些样本峰值远超原始水平。峰值保护将输出峰值限制在原始峰值的 1.5 倍以内，防止爆音。

### 4.4 直观效果表

| Tension | b (dB) | 物理效果 | 听感描述 |
|---------|--------|---------|---------|
| **-100** | +12 | 高频 +12dB/oct | 极度明亮、尖锐、金属感 |
| **-50** | +6 | 高频 +6dB/oct | 明亮、清晰 |
| **-25** | +3 | 高频 +3dB/oct | 轻微明亮 |
| **0** | 0 | 无变化 | 原始音色 |
| **+25** | -3 | 高频 -3dB/oct | 轻微温暖 |
| **+50** | -6 | 高频 -6dB/oct | 温暖、柔和 |
| **+100** | -12 | 高频 -12dB/oct | 极度温暖、圆润、暗淡 |

---

## 5. 与 Vocoder 推理的集成时序

### 5.1 编辑触发

```
用户在 UI 中绘制 tension 曲线
    │
    ▼
Project::setParamDirtyRange(startFrame, endFrame)
    → 标记 paramDirtyStart / paramDirtyEnd
    │
    ▼
触发 IncrementalSynthesizer::synthesizeRegion()
```

### 5.2 合成管线

```
IncrementalSynthesizer::synthesizeRegion()
    │
    ├─ getDirtyFrameRange()         ← 获取脏帧范围
    ├─ computeSynthesisRange()      ← 扩展到完整voiced段 + padding
    │
    ├─ HNSepCurveProcessor::
    │     rebuildCurvesForRange()   ← 局部重建 AudioData.tensionCurve
    │
    ├─ hasActiveEdits() ← 检查vocing/breath/tension是否有编辑
    │
    ├─ [如果有编辑]
    │   ├─ TensionProcessor::
    │   │     hasActiveEdits()      ← 再次检查（带默认值回退）
    │   │
    │   ├─ TensionProcessor::
    │   │     processSegment()      ← 应用voicing/breath/tension到波形
    │   │                             在STFT域做频谱倾斜
    │   │
    │   └─ MelSpectrogram::
    │         compute()             ← 从处理后波形重新算mel谱
    │
    ├─ [如果无编辑]
    │   └─ 直接使用原始 melSpectrogram 切片（跳过HNSep处理）
    │
    └─ vocoder->inferAsync(newMel, adjustedF0) → PC-NSF-HiFiGAN → 输出
```

### 5.3 关键设计决策

**为什么在 vocoder 前修改 mel 谱，而不是 vocoder 后修改波形？**

1. PC-NSF-HiFiGAN 以 mel 频谱 + F0 作为输入
2. 在 mel 域前面修改比在波形域后面滤波更自然（避免了两个级联的频谱处理）
3. vocoder 会将修改后的 mel 特征「翻译」为更自然的语音信号

---

## 6. 数据结构全览

```
HachiTune Source Tree (tension 相关部分):

Source/
  Audio/
    TensionProcessor.h       ← 核心算法：STFT域频谱倾斜
    TensionProcessor.cpp     ← 实现：FFT/IFFT、滤波、RMS归一化
    Vocoder.h/cpp            ← Vocoder 推理入口
    Synthesis/
      IncrementalSynthesizer.h/cpp  ← 合成调度：集成HNSep处理
  Models/
    Note.h                   ← tensionCurve (note-local), voicingCurve, breathCurve
    Project.h                ← AudioData::tensionCurve (dense global)
  Utils/
    HNSepCurveProcessor.h/cpp  ← 曲线管理：两层数据同步
    CurveResampler.h/cpp         ← 曲线重采样：拉伸时保持曲线对齐
```

---

## 7. 与 Voicing/Breath 的协同关系

三条曲线共同作用，但处理路径不同：

| 曲线 | 作用域 | 处理方式 | 复杂度 |
|------|--------|---------|--------|
| **voicingCurve** | 谐波振幅 | 逐样本乘法（时域） | O(N) |
| **breathCurve** | 噪声振幅 | 逐样本乘法（时域） | O(N) |
| **tensionCurve** | 谐波频谱 | STFT 域线性倾斜滤波 | O(N_FRAMES × N·logN) |

三条曲线组合可实现：
- 清晰的强声 + 少量气声 + 明亮音色
- 虚弱的轻声 + 大量气声 + 温暖音色
- 任意组合，独立控制

---

## 8. 完整算法实现细节（附录）

> 以下内容包含从源码中提取的精确实现细节，足以独立复现。

### 8.1 FFT 实现：radix-2 Cooley-Tukey

TensionProcessor 手写了一个 2048 点 radix-2 FFT，核心由三个函数组成：

#### 8.1.1 位反转

```cpp
static int bitReverse(int x, int log2n) {
    int result = 0;
    for (int i = 0; i < log2n; ++i) {
        result = (result << 1) | (x & 1);
        x >>= 1;
    }
    return result;
}
```

#### 8.1.2 前向 FFT (`forwardFFT`)

```cpp
void TensionProcessor::forwardFFT(const float *frame,
                                  float *outReal, float *outImag) const
{
    const int N = kFFTSize;           // 2048
    const int log2N = 11;             // log2(2048) = 11

    std::vector<float> re(N);
    std::vector<float> im(N, 0.0f);

    // 步骤1: 位反转重排输入
    for (int i = 0; i < N; ++i) {
        const int j = bitReverse(i, log2N);
        re[j] = frame[i];
        // im[j] 已经是 0.0f
    }

    // 步骤2: log2N 级蝶形运算
    for (int s = 1; s <= log2N; ++s) {
        const int m = 1 << s;          // 本级大小
        const int halfM = m >> 1;      // 半级大小
        const double angle = -2.0 * PI / m;  // 负号 = 前向 FFT
        const float wRe = cos(angle);
        const float wIm = sin(angle);

        for (int k = 0; k < N; k += m) {
            float tRe = 1.0f;          // W^0 = 1+0i
            float tIm = 0.0f;
            for (int j = 0; j < halfM; ++j) {
                const size_t u = k + j;
                const size_t v = k + j + halfM;

                // 蝶形: U = A + WB, V = A - WB
                const float tmpRe = tRe * re[v] - tIm * im[v];
                const float tmpIm = tRe * im[v] + tIm * re[v];

                re[v] = re[u] - tmpRe;
                im[v] = im[u] - tmpIm;
                re[u] = re[u] + tmpRe;
                im[u] = im[u] + tmpIm;

                // 旋转因子递推: W^{j+1} = W^j × W^1
                const float newTRe = tRe * wRe - tIm * wIm;
                const float newTIm = tRe * wIm + tIm * wRe;
                tRe = newTRe;
                tIm = newTIm;
            }
        }
    }

    // 步骤3: 只输出前 kFFTBin = 1025 个频点
    for (int k = 0; k < kFFTBin; ++k) {
        outReal[k] = re[k];
        outImag[k] = im[k];
    }
}
```

**旋转因子递推优化**：每级蝶形中，W^j 不是每次都调用 `cos/sin` 重算，而是通过复数乘法 `W^{j+1} = W^j × W^1` 递推，将 O(N·logN) 次三角函数调用降为 O(logN) 次。

#### 8.1.3 逆 FFT (`inverseFFT`)

```cpp
void TensionProcessor::inverseFFT(const float *inReal, const float *inImag,
                                  float *outFrame) const
{
    const int N = kFFTSize;           // 2048
    const int log2N = 11;

    std::vector<float> re(N, 0.0f);
    std::vector<float> im(N, 0.0f);

    // 步骤1: 构建 Hermitian 共轭对称谱
    // 前半部分：Re[k] = inReal[k],  Im[k] = -inImag[k] (共轭)
    for (int k = 0; k < kFFTBin; ++k) {
        re[k] = inReal[k];
        im[k] = -inImag[k];  // 取共轭 = IFFT
    }

    // 后半部分：Re[N-k] = Re[k], Im[N-k] = -Im[k]
    // (共轭对称性，确保 IFFT 输出为实数)
    for (int k = 1; k < kFFTBin - 1; ++k) {
        re[N - k] = inReal[k];
        im[N - k] = inImag[k];  // 注意：原谱的 Im，不是共轭
    }

    // 步骤2: 位反转（注意这里先位反转再 butterfly，与 forward 不同）
    std::vector<float> reP(N, 0.0f);
    std::vector<float> imP(N, 0.0f);
    for (int i = 0; i < N; ++i) {
        const int j = bitReverse(i, log2N);
        reP[j] = re[i];
        imP[j] = im[i];
    }

    // 步骤3: log2N 级蝶形运算（与前向 FFT 相同，但 angle 也是负的）
    for (int s = 1; s <= log2N; ++s) {
        const int m = 1 << s;
        const int halfM = m >> 1;
        const double angle = -2.0 * PI / m;
        // ... 蝶形运算与前向 FFT 完全相同 ...
    }

    // 步骤4: 除以 N 得到最终结果
    const float invN = 1.0f / static_cast<float>(N);
    for (int i = 0; i < N; ++i)
        outFrame[i] = reP[i] * invN;
}
```

**注**：实际源码中 IFFT 使用了两次位反转（先位反转排布，butterfly，再除以 N），这与标准 Cooley-Tukey IFFT 一致。

---

### 8.2 STFT 帧的 Padding 策略

`processSegment` 中的关键填充变量：

```
kHopSize = 512
kWinSize = 2048
kFFTSize = 2048

stftFrames = (numSamples + kHopSize - 1) / kHopSize          // 向上取整的帧数
paddedLen  = stftFrames * kHopSize + kWinSize                // 总共需要的填充长度
offset     = kWinSize / 2 = 1024                              // 起始偏移

// 填充波形到 padded，前面 offset 个零，后面剩余补零
vector<float> padded(paddedLen, 0.0f);
for (int i = 0; i < numSamples; ++i)
    padded[offset + i] = scaledHarmonic[i];   // offset + 0 = 1024

// 对每个 STFT 帧 f (0 .. stftFrames-1):
//   frameStart = f * kHopSize
//   输入窗口: padded[frameStart .. frameStart + kWinSize - 1]
```

**为什么 offset = kWinSize/2 = 1024？**

```
┌──────────────────────────────────────────────────────┐
│         padded buffer (paddingLen)                   │
├──────┬──────────────────────────────┬────────────────┤
│ zero │        input_samples         │     zero       │
│ 1024 │        numSamples           │   (tail zero)  │
├──────┼──────────────────────────────┼────────────────┤
│      │← frame 0 (1024 .. 3071) →    │                │
│      │  ← frame 1 (1536 .. 3583) →  │                │
│      │    ...                       │                │
└──────┴──────────────────────────────┴────────────────┘
```

- `offset = 1024` 确保第 0 帧的窗函数中心与第 0 个输入样本对齐
- 帧 0 覆盖 `padded[0..2047]`，其中 `padded[1024]` 对应 `scaledHarmonic[0]`
- 这保证了第一帧和最后一帧的样本都被正确处理（不会丢失边界信息）
- 前后零填充提供了自然的反射边界处理

---

### 8.3 OLA 中的 COLA 归一化原理

```cpp
// 合成时（每个 STFT 帧）
for (int n = 0; n < kWinSize; ++n) {
    const float w = hannWindow[n];
    output[idx]    += outFrame[n] * w;        // 加权累加
    windowSum[idx] += w * w;                  // 累计窗口平方和
}

// 最终归一化
output[i] /= windowSum[i];
```

**为什么用 `∑(w²)` 而不是 `∑(w)`？**

对于 Hann 窗 `w[n] = 0.5 × (1 - cos(2πn/N))`，在 75% 重叠时（hop=512, win=2048），满足：

```
∑ w²[n - m·hop] = 1   (对所有 n，其中 m 覆盖所有重叠帧)
```

这是一个 **COLA (Constant Overlap-Add)** 条件。除以 `windowSum`（即重叠窗口的平方向的累积）可以完美重建，避免幅度调制伪影。

---

### 8.4 完整 STFT 滤波参数一览

| 参数 | 值 | 备注 |
|------|-----|------|
| FFT 大小 | 2048 | 2的幂，radix-2 |
| 窗函数 | Hann | `0.5×(1-cos(2πn/2048))` |
| Hop 大小 | 512 | 75% 重叠率 |
| 频点数 | 1025 | kFFTSize/2 + 1 |
| 采样率 | 44100 Hz | |
| Nyquist | 22050 Hz | |
| 参考频率 | 1500 Hz | 交叉点，增益=0dB |
| x₀ | ~69.7 | 1025/(22050/1500) |
| maxTiltDb | 12 dB | 最大倾斜幅度 |
| 增益 clamp | ±12 dB | filterDb clamp |
| RMS 归一化 | 是 | 保持能量一致 |
| 峰值保护阈值 | 原峰值 ×1.5 | 防爆音 |
| padding offset | 1024 样本 | kWinSize/2 |

---

## 9. 性能考量

| 环节 | 优化策略 |
|------|---------|
| FFT | 2048点 radix-2 Cooley-Tukey，手写实现避免第三方依赖 |
| 窗函数 | 预计算的 `hannWindow`，加载时初始化一次 |
| 是否有编辑检测 | 先做 O(N) 快速扫描，无编辑直接跳过整个 STFT 处理 |
| 局部重建 | `rebuildCurvesForRange` 只处理脏区域的帧 |
| 增量合成 | 只重合成脏音符，不重算整首音频 |
| RMS 归一化 | 两次遍历，每次 O(N)，总计 2N 次浮点运算 |
