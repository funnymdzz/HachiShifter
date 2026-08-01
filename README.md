# HachiShifter Next

HachiShifter Next 是面向人力 VOCALOID 制作流程的跨平台原生人声编辑器。
`next` 分支正在以 JUCE、C++20 和 CMake 重建应用，不再使用网页前端、Tauri
或 Rust 后端。

## 当前架构

```text
juce/
├── CMakeLists.txt
└── src/
    ├── backend/          音高、拉伸与离线渲染服务
    ├── AudioEngine.*     实时多轨播放与采样率转换
    ├── ProjectModel.*    工程、轨道、采样、音符和音高线数据
    ├── SampleSettings.*  扳手设定、HJM CSV 与 UTAU oto 互换
    ├── SettingsComponent.*
    ├── AssetManagerComponent.*
    ├── PianoRollComponent.*
    ├── TrackListComponent.*
    ├── MainComponent.*
    ├── Theme.*
    └── I18n.*
```

界面、工程模型、音频设备、实时播放、离线渲染和 mld5 频谱组件均为原生
C++ 实现。耗时渲染通过 JUCE `ThreadPool` 按设备 CPU 核心数并行执行。

## 已迁移内容

- JUCE 原生窗口、工具栏、轨道列表和钢琴卷帘；
- 深色界面与 `#7F69CA`、`#CBCBFA`、`#F4C000` 默认配色；
- 多轨采样播放、增益、声像、淡入淡出和重叠混音；时间线提供可拖动的采样
  淡入/淡出手柄和采样静音按钮，双击采样或使用轨道菜单可精确输入采样增益；
- 与输出设备采样率无关的音频读取；
- BPM、拍号和节拍原点；
- Melodyne `.mpd` 多轨工程、MIDI 和常见音频格式导入；
- 音符整体移调并保留原始音高调制；
- 自由绘制及直线工具直接编辑目标音高线，原始 F0 以独立虚线保留；
- 时间线采样左右手柄整体拉伸，以及音符拉伸、连接/分离、多选、批量移调与
  批量删除；采样拉伸会同步缩放音符、原始/手绘音高线、辅音、齿音标记和淡入淡出，
  同时保持源素材范围及 Attack 源边界不变；
- 音高调制平滑、Pitch Drift 漂移修正、呼吸、张力、共振峰、音量、
  Attack 边界及 Attack Speed 参数编辑；
- 辅音浅色区域、齿音标记、无声区断线和音符连接状态；
- 原始采样扳手编辑模式；
- 扳手内嵌分段、别名、起止、节奏对齐与辅音时间编辑，保存为
  `音频文件.hjm.csv`；原始波形上可直接拖动分段、固定辅音、对齐及结束
  竖线，并支持 UTAU `oto.ini` 读取/导出；
- 文件菜单原生设置窗口：界面语言与配色、音频设备、GAME/FCPE/HiFi-GAN
  模型目录和推理设备、操作方式、工程导入默认项及 UTAU 重采样器预留项；
- 音频设备状态跨启动保存；操作设置可控制空格播放以及编辑器滚轮缩放/滚动；
- 可注册音频并拖放到时间线或钢琴卷帘的素材管理器；
- 素材管理器可递归导入 UTAU 音源库，以 UTF-8 或 CP932/Shift-JIS 读取
  `oto.ini`，批量注册采样并生成对应 `.hjm.csv`；拖入已标注采样时会立即
  按 HJM 分段建立可编辑音符、辅音区与对齐信息；
- `.hspx` 原生工程保存与读取、撤销/重做及 WAV 混音导出；工程同时保存
  素材绝对/相对路径，整个工程目录移动后会自动按相对路径和文件名重定位；
- mld5 周期/非周期分离、共振峰包络保持、Attack 保护和块能量归一化；
- Melodyne 导入轨道仍可随时切换变调及拉伸算法；全局选择会写入全部轨道，
  之后切换 Compose 状态也会沿用已选后端；调制值为 0 的音符按工程数据恢复为
  精确拉平的目标音高线；文件导入设置可分别指定默认变调与拉伸算法；
- Melodyne 导入可选择保留工程修音/Attack/音量/音色编辑，或在保留工程排列和
  原始 F0 的同时将这些编辑恢复为中性值；
- Melodyne 原音高来源可选择工程内音高线，或通过统一 GAME+FCPE 分析入口重新
  分析素材 F0；设置、CLI 与 MCP 使用相同的 large/small 模型选择、模型路径和
  推理设备配置。无模型包使用 16 kHz、5 ms 精度的 C++ 原生分析回退，并保留
  工程目标音高及其它编辑；
- 原生 ONNX Runtime GAME 四阶段推理（encoder → segmenter → boundary-to-duration
  → estimator），根据模型实际输入元数据组装张量，避免把 `duration` 误传给
  boundary-to-duration；默认 large，性能模式使用 small。每个 GAME note 作为一个
  音节，note 起点即默认节奏对齐点；FCPE 读取 128-bin Slaney mel，以正确的 5 ms
  STFT 中心补偿恢复高精度原始 F0，再映射到 GAME 音节；GAME 分段阈值采用官方
  推理默认值 0.3、半径 2、8 步 D3PM，FCPE 长素材按带上下文的有界窗口推理；
- JSON-RPC MCP 工程读取、导入、编辑、预渲染、WAV 导出与播放控制接口，轨道
  和采样参数可独立修改，并公开原始音高线、最终目标音高线、渲染进度及实际后端；
  MCP 同时支持 HJM 分段读写、UTAU oto 导入/导出及整套音源注册；
- 简体中文、繁体中文、日语、韩语和英语界面文本；
- 设置窗口的主题、自动推理、滚轮行为、Compose 导入方式和原始 F0 来源等
  下拉选项会随界面语言同步切换；
- Linux 与 Windows 无模型 GitLab CI 构建；Windows 专项修复可只构建 Windows。

## 构建

GitHub 保存上游源码，服务器每五分钟把分支和标签同步到自托管 GitLab；编译只在
GitLab Runner 上执行，不再使用 GitHub Actions，也不要求开发设备下载 JUCE 或
生成应用构建目录。推送到 GitHub 的 `next` 分支后，GitLab CI 会生成：

- `HachiShifterNext-linux-x86_64-no-model.tar.gz`
- `HachiShifterNext-windows-x64-no-model.zip`

JUCE 依赖由 CMake FetchContent 在云端构建机获取，发布包不包含模型。

GitLab 项目与最新构建：

- [HachiShifter GitLab 仓库](https://35.194.110.220/funnymdzz/HachiShifter)
- [下载 Linux x86_64 无模型构建](https://35.194.110.220/funnymdzz/HachiShifter/-/jobs/artifacts/next/download?job=build_linux)
- [下载 Windows x64 无模型构建](https://35.194.110.220/funnymdzz/HachiShifter/-/jobs/artifacts/next/download?job=build_windows)

Windows 版本由 Debian Runner 使用 Clang/LLVM、微软 MSVC 头文件和 Windows SDK
交叉编译，产物采用 MSVC ABI 和静态 MSVC 运行库。

## 使用说明

## 许可证

项目代码依据 [LICENSE](LICENSE) 发布。构建和分发时同时遵循 JUCE 及所选音频
算法依赖的许可证要求。
