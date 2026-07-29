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
- 深色/橙色统一配色；
- 多轨采样播放、增益、声像、淡入淡出和重叠混音；
- 与输出设备采样率无关的音频读取；
- BPM、拍号和节拍原点；
- 音符整体移调并保留原始音高调制；
- 辅音浅色区域、齿音标记、无声区断线和音符连接状态；
- 原始采样扳手编辑模式；
- `.hspx` 原生工程保存与读取；
- mld5 周期/非周期分离、共振峰包络保持、Attack 保护和块能量归一化；
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
