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
- 多轨采样播放、增益、声像、淡入淡出和重叠混音；
- 与输出设备采样率无关的音频读取；
- BPM、拍号和节拍原点；
- Melodyne `.mpd` 多轨工程、MIDI 和常见音频格式导入；
- 音符整体移调并保留原始音高调制；
- 自由绘制及直线工具直接编辑目标音高线，原始 F0 以独立虚线保留；
- 音符拉伸、连接/分离、多选、批量移调与批量删除；
- 音高调制平滑、Pitch Drift 漂移修正、呼吸、张力、共振峰、音量、
  Attack 边界及 Attack Speed 参数编辑；
- 辅音浅色区域、齿音标记、无声区断线和音符连接状态；
- 原始采样扳手编辑模式；
- 扳手内嵌分段、别名、起止、节奏对齐与辅音时间编辑，保存为
  `音频文件.hjm.csv`；原始波形上可直接拖动分段、固定辅音、对齐及结束
  竖线，并支持 UTAU `oto.ini` 读取/导出；
- 文件菜单原生设置窗口：界面语言与配色、音频设备、GAME/HiFi-GAN
  模型目录和推理设备、操作方式、工程导入默认项及 UTAU 重采样器预留项；
- 音频设备状态跨启动保存；操作设置可控制空格播放以及编辑器滚轮缩放/滚动；
- 可注册音频并拖放到时间线或钢琴卷帘的素材管理器；
- 素材管理器可递归导入 UTAU 音源库，以 UTF-8 或 CP932/Shift-JIS 读取
  `oto.ini`，批量注册采样并生成对应 `.hjm.csv`；拖入已标注采样时会立即
  按 HJM 分段建立可编辑音符、辅音区与对齐信息；
- `.hspx` 原生工程保存与读取、撤销/重做及 WAV 混音导出；
- mld5 周期/非周期分离、共振峰包络保持、Attack 保护和块能量归一化；
- JSON-RPC MCP 工程读取、导入与编辑接口，轨道算法可按指定轨道修改；
- 简体中文、繁体中文、日语、韩语和英语界面文本；
- Linux 与 Windows 模型-free GitHub Actions 构建。

## 构建

仓库使用 GitHub Actions 构建，不要求在本地下载 JUCE 或生成应用构建目录。
推送到 `next` 后由 `Compile HachiShifter Next (JUCE)` 工作流生成：

- `HachiShifterNext-linux-x86_64-no-model.tar.gz`
- `HachiShifterNext-windows-x64-no-model.zip`

JUCE 依赖由 CMake FetchContent 在云端构建机获取，发布包不包含模型。

## 使用说明

## 许可证

项目代码依据 [LICENSE](LICENSE) 发布。构建和分发时同时遵循 JUCE 及所选音频
算法依赖的许可证要求。
