# 采样分段、自动音符与 UTAU oto 转换

## 标注文件

每个导入的音频采样使用一个同目录 sidecar 文件：

```text
sample.wav
sample.wav.hachi.csv
```

CSV 固定使用以下五列，时间单位均为秒，且相对于源音频文件：

```csv
name,region_start_sec,region_end_sec,note_alignment_sec,fixed_duration_sec
a,0.08,0.72,0.19,0.14
```

- `name`：自定义选区或发音名称。
- `region_start_sec`、`region_end_sec`：该发音使用的源音频范围。
- `note_alignment_sec`：音符在素材中的对齐点。
- `fixed_duration_sec`：从选区起点开始、不参与时间拉伸的持续时间。

因此固定段为
`[region_start_sec, region_start_sec + fixed_duration_sec]`，其后的部分直到
`region_end_sec` 为可拉伸段。一个采样有多个发音或时间选区时，每个选区写一行。

## UTAU oto 映射

转换器递归读取音源库中的 `oto.ini`，默认按 Shift-JIS 解码，也识别声明为 UTF-8
的文件。oto 字段：

```text
wav=alias,offset,consonant,cutoff,preutter,overlap
```

映射规则如下：

| CSV 字段 | oto 来源 |
| --- | --- |
| `name` | `alias`，为空时使用 WAV 文件名 |
| `region_start_sec` | `offset / 1000` |
| `region_end_sec` | `cutoff < 0` 时为 `offset + (-cutoff)`；否则为音频时长减 `cutoff` |
| `note_alignment_sec` | `(offset + preutter) / 1000` |
| `fixed_duration_sec` | `consonant / 1000` |

`overlap` 没有对应的目标 CSV 字段，因此不会写入。数值会被限制在有效音频范围内。

## 自动检测和编辑

初次导入时会建立 sidecar。打开钢琴卷帘或标注编辑器时，程序使用 RMS
检测发音区间，以归一化自相关估计浊音起点和粗略音高，再把相邻稳定音高帧合并为音符块。
该检测用于给人工编辑提供起点，不替代精细的音素标注。

在时间线右键单个普通音频块，选择 `编辑采样分段与音符…`：

1. 拖动四条滑块或直接输入秒数，调整选区起点、固定段终点、对齐点和选区终点。
2. 可添加、删除多行标注，并选择当前片段使用的标注行。
3. “粗略重新识别”只更新尚未保存的候选值，可先人工修正；点击“保存”后才覆盖 CSV。
4. 保存后，实时播放、单片段播放和导出都会保持固定段 1:1，只拉伸剩余部分。

钢琴卷帘中的自动音符可单击建立时间选区，也可上下拖动以整体调整选区音高。

## Linux / WSL2 构建

`vslib` 是 Windows 专用 feature。Linux/WSL2 使用：

```bash
cd backend/src-tauri
cargo build --target x86_64-unknown-linux-gnu --no-default-features --features onnx
```

开发启动：

```bash
TAURI_UI_MODE=build cargo tauri dev --target x86_64-unknown-linux-gnu --no-default-features --features onnx
```

实现参考了 OpenUtau 的 oto 字段语义以及 HachiTune/GAME 的自动音符工作流；本项目的检测器和界面为独立实现。
