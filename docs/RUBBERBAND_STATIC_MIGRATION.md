# Rubber Band 静态链接迁移完成

## 概述

HachiShifter 的 Rubber Band Library 集成已于 2026.03 从动态链接（DLL）迁移到编译时静态链接，与 WORLD vocoder 的集成方式保持一致。

## 主要变更

### 1. 构建系统（已完成）

- ✅ 在 `backend/src-tauri/build.rs` 中添加了 `build_rubberband_static()` 函数
- ✅ 使用 `cc` crate 编译 Rubber Band C++ 源码（v3.3.0）
- ✅ 源码位置：`backend/src-tauri/third_party/rubberband-static/rubberband/`
- ✅ 编译配置：
  - C++14 标准
  - 使用内置 FFT 和 BQResampler（无外部依赖）
  - R2 (faster) 和 R3 (finer) 双引擎支持
  - 平台特定标志（Windows: `/EHsc`, Unix: `-fPIC`）

### 2. FFI 层（已完成）

- ✅ `backend/src-tauri/src/rubberband.rs` 已重构为使用 `extern "C"` 静态 FFI 声明
- ✅ 移除了 `libloading` 动态加载代码
- ✅ `is_available()` 现在始终返回 `true`（静态链接保证可用）
- ✅ 所有 API 函数直接调用静态链接的 C 函数

### 3. 依赖管理（已完成）

- ✅ `backend/src-tauri/Cargo.toml` 中不再需要 `libloading` 依赖
- ✅ 编译时自dynamic动检查源码位置并提供清晰的错误提示

### 4. 构建体验

**首次构建**：
- 预计增加 2-5 分钟（编译 Rubber Band C++ 源码）
- 与 WORLD 编译并行进行

**增量构建**：
- Rubber Band 源码未修改时约 10-20 秒
- Cargo 的缓存机制确保高效增量编译

**编译验证**：
```powershell
cd backend/src-tauri
cargo check
# 或完整构建
cargo build --release
```

## 优势

1. **单一可执行文件**：不再需要外部 DLL，分发更简单
2. **消除运行时错误**：静态链接消除了"找不到 DLL"的问题
3. **性能提升**：静态链接可能带来更好的优化和更快的函数调用
4. **简化构建**：开发者只需 `git clone` 源码并 `cargo build`，无需手动运行 CMake 脚本
5. **跨平台一致性**：Windows/macOS/Linux 使用统一的构建流程

## 遗留清理（待完成）

以下项目已标记为 deprecated，但保留用于参考：

- `tools/build_rubberband_windows.cmd` - 旧的 DLL 构建脚本
- `tools/verify_rubberband_windows.cmd` - DLL 导出验证脚本
- `backend/src-tauri/third_party/rubberband/source/` - 旧的 DLL 相关文件

**文档更新待办**：
- [ ] 更新 `README.md` 移除 DLL 构建说明
- [ ] 更新 `DEVELOPMENT_zh.md` 中的 Rubber Band 章节
- [ ] 标注旧的环境变量（`HACHISHIFTER_RUBBERBAND_DLL`）已废弃

## 使用说明

### 首次设置（开发者）

```bash
# 1. 克隆主仓库
git clone https://github.com/funnymdzz/HachiShifter.git
cd HachiShifter

# 2. 克隆 Rubber Band 源码
cd backend/src-tauri/third_party/rubberband-static
git clone --depth 1 --branch v3.3.0 https://github.com/breakfastquay/rubberband.git rubberband
cd ../../../..

# 3. 克隆 WORLD 源码（如果还没有）
cd backend/src-tauri/third_party/world-static
git clone https://github.com/mmorise/World.git
cd ../../../..

# 4. 构建
cd backend/src-tauri
cargo build --release
```

### .gitignore 配置

`backend/src-tauri/third_party/rubberband-static/` 和 `backend/src-tauri/third_party/world-static/` 已添加到 `.gitignore`，避免将大量源码文件提交到仓库。

## 技术细节

### 源码补丁

`build.rs` 会自动修复 Rubber Band v3.3.0 的一个已知问题：
- `common/VectorOpsComplex.cpp` 中的错误 include 路径
- 将 `#include "system/sysutils.h"` 自动替换为 `#include "sysutils.h"`

### 编译文件列表

完整的编译包含：
- C API 包装器：`rubberband-c.cpp`, `RubberBandStretcher.cpp`
- 通用模块：`common/*.cpp`（10+ 文件）
- R2 引擎：`faster/*.cpp`（7 文件）
- R3 引擎：`finer/R3Stretcher.cpp`

### 链接顺序

最终链接包含：
1. `librubberband.a` - Rubber Band 静态库
2. `libworld.a` - WORLD vocoder 静态库
3. Rust 编译产物
4. 系统库（C++ 标准库等）

## 测试

编译成功后，验证功能：

1. **加载音频文件**
2. **创建剪辑并调整 playbackRate（速率 ≠ 1.0）**
3. **播放**：应听到高质量的变速不变调音频
4. **检查日志**：不应出现"rubberband DLL not found"或类似错误

## 参考

- Rubber Band Library: https://github.com/breakfastquay/rubberband
- WORLD vocoder: https://github.com/mmorise/World
- 设计文档：`openspec/changes/rubberband-static-linking/design.md`
- 任务列表：`openspec/changes/rubberband-static-linking/tasks.md`

---

迁移完成日期：2026年3月1日
