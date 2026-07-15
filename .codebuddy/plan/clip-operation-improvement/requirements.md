# 需求文档：前端 Clip 操作优化

## 引言

HachiShifter 的时间轴（Timeline）是用户进行音频编辑的核心工作区。当前 clip 操作链路存在以下几类可改进的问题：

1. **右键菜单功能单薄**：`GlueContextMenu` 仅提供"胶合"一个操作，用户无法通过右键菜单完成删除、静音、重命名、复制等高频操作，必须依赖键盘快捷键或其他入口，操作路径长。
2. **多选操作反馈不足**：多选 clip 后，除拖拽移动外，缺少批量静音、批量删除的视觉确认流程；多选状态在执行操作后没有自动清除的统一策略。
3. **Clip 颜色不可自定义**：`ClipInfo` 中已有 `color` 字段（`emerald/blue/violet/amber`），但 UI 中没有提供修改入口，用户无法通过颜色区分不同 clip 的语义。
4. **Clip 名称不可在线编辑**：`ClipHeader` 中 clip 名称仅作为只读文本展示，用户无法双击重命名，必须通过其他途径修改。
5. **增益拖拽交互不直观**：`ClipHeader` 中增益调节使用一个小圆点作为拖拽把手（`ns-resize`），视觉上不明显，且没有数值输入框作为备选，精确调节困难。
6. **`createClipsRemote` 串行创建性能差**：`timelineThunks.ts` 中 `createClipsRemote` 对多个 clip 模板逐个串行调用 `addClip` + `setClipState`，粘贴或 Ctrl+拖拽复制多个 clip 时延迟随数量线性增长。
7. **波形峰值缓存无持久化**：`useClipWaveformPeaks` 中 `peaksSegmentCache` 是内存 Map，页面刷新后全部失效，每次重新打开项目都需要重新请求所有 clip 的波形峰值。
8. **`TimelinePanel.tsx` 过于臃肿**：单文件 2060 行，包含拖拽逻辑、键盘快捷键、文件拖放、播放头控制等所有逻辑，维护困难，难以单独测试。

本文档描述针对上述问题的改进需求，优先聚焦于用户可感知的交互体验提升（需求 1-5）和性能优化（需求 6-7），代码结构重构（需求 8）作为工程质量改进项。

---

## 需求

### 需求 1：丰富右键上下文菜单

**用户故事：** 作为一名音频编辑用户，我希望在 clip 上右键时能看到完整的操作菜单，以便不依赖键盘快捷键就能完成所有常用操作。

#### 验收标准

1. WHEN 用户在单个 clip 上右键时 THEN 系统 SHALL 显示包含以下操作的上下文菜单：**删除**、**静音/取消静音**、**重命名**、**复制**、**分割**（在播放头处）、**颜色**（子菜单，含 emerald/blue/violet/amber 四个选项）。
2. WHEN 用户在多选状态下右键任意已选 clip 时 THEN 系统 SHALL 显示包含以下批量操作的菜单：**删除所选**、**静音所选**、**取消静音所选**、**胶合**（仅当所有选中 clip 在同一轨道时可用）。
3. WHEN 用户点击菜单中的"删除"时 THEN 系统 SHALL 删除对应 clip 并关闭菜单。
4. WHEN 用户点击菜单中的"静音/取消静音"时 THEN 系统 SHALL 切换 clip 的静音状态并关闭菜单。
5. WHEN 用户点击菜单中的"分割"时 THEN 系统 SHALL 在当前播放头位置分割 clip 并关闭菜单；IF 播放头不在该 clip 范围内 THEN 该菜单项 SHALL 显示为禁用状态。
6. WHEN 用户选择颜色子菜单中的某个颜色时 THEN 系统 SHALL 调用 `setClipStateRemote` 更新 clip 颜色并关闭菜单。
7. WHEN 用户点击菜单外部区域时 THEN 系统 SHALL 关闭菜单，不执行任何操作。

---

### 需求 2：Clip 名称双击内联编辑

**用户故事：** 作为一名音频编辑用户，我希望能直接在 clip 头部双击修改名称，以便快速为 clip 添加语义标注。

#### 验收标准

1. WHEN 用户在 `ClipHeader` 的名称文本区域双击时 THEN 系统 SHALL 将名称文本切换为可编辑的 `<input>` 输入框，并自动全选当前名称内容。
2. WHEN 输入框处于编辑状态时 THEN 系统 SHALL 阻止双击事件冒泡，避免触发 clip 拖拽或 seek 操作。
3. WHEN 用户按下 `Enter` 键或输入框失去焦点时 THEN 系统 SHALL 提交新名称，调用 `setClipStateRemote` 将名称同步到后端，并切换回只读显示模式。
4. WHEN 用户按下 `Escape` 键时 THEN 系统 SHALL 放弃编辑，恢复原始名称，切换回只读显示模式。
5. IF 用户提交的名称为空字符串 THEN 系统 SHALL 保留原始名称，不调用后端接口。
6. WHEN 输入框处于编辑状态时 THEN 系统 SHALL 显示与 clip 背景色对比度足够的输入框样式，宽度自适应 clip 宽度。

---

### 需求 3：Clip 颜色快速切换

**用户故事：** 作为一名音频编辑用户，我希望能通过右键菜单或 clip 头部的颜色指示器快速切换 clip 颜色，以便用颜色区分不同类型的音频片段。

#### 验收标准

1. WHEN 用户通过右键菜单选择颜色时（见需求 1.6）THEN 系统 SHALL 立即在前端乐观更新 clip 颜色（不等待后端响应），同时异步调用 `setClipStateRemote`。
2. WHEN `setClipStateRemote` 返回成功时 THEN 系统 SHALL 以后端返回的 timeline 状态为准，覆盖乐观更新的颜色值。
3. IF `setClipStateRemote` 返回失败 THEN 系统 SHALL 回滚到修改前的颜色值。
4. WHEN clip 颜色为 `emerald` 时 THEN 系统 SHALL 使用绿色系主题色；`blue` 使用蓝色系；`violet` 使用紫色系；`amber` 使用琥珀色系，与现有 `--qt-highlight` CSS 变量体系保持一致。

---

### 需求 4：增益调节交互改进

**用户故事：** 作为一名音频编辑用户，我希望能更直观地调节 clip 的增益，以便精确控制每个片段的音量。

#### 验收标准

1. WHEN 用户在 `ClipHeader` 的增益区域进行垂直拖拽时 THEN 系统 SHALL 保持现有的 `ns-resize` 拖拽行为，每像素对应 0.25dB 的变化量（与现有实现一致）。
2. WHEN 用户双击 `ClipHeader` 的增益显示文本（`+X.X dB`）时 THEN 系统 SHALL 弹出一个小型数值输入框，允许用户直接输入 dB 值（范围 -24 到 +12）。
3. WHEN 用户在数值输入框中按下 `Enter` 或失去焦点时 THEN 系统 SHALL 将输入值 clamp 到 [-24, 12] 范围后转换为线性增益，调用 `setClipStateRemote` 提交。
4. WHEN 用户按下 `Escape` 时 THEN 系统 SHALL 关闭输入框，不修改增益值。
5. WHEN 用户在增益拖拽把手上悬停时 THEN 系统 SHALL 显示 tooltip 提示"上下拖拽调节增益 / 双击输入精确值"。

---

### 需求 5：批量操作后多选状态自动管理

**用户故事：** 作为一名音频编辑用户，我希望在执行批量操作后，多选状态能自动更新为合理的状态，以便继续流畅地进行后续编辑。

#### 验收标准

1. WHEN 用户执行批量删除操作后 THEN 系统 SHALL 自动清空 `multiSelectedClipIds`。
2. WHEN 用户执行批量静音/取消静音操作后 THEN 系统 SHALL 保持当前多选状态不变，以便用户继续对同一批 clip 执行其他操作。
3. WHEN 用户执行粘贴（Ctrl+V）或 Ctrl+拖拽复制操作后 THEN 系统 SHALL 自动将新创建的 clip 设为多选状态（现有行为，保持不变）。
4. WHEN 用户点击空白区域时 THEN 系统 SHALL 清空多选状态（现有行为，保持不变）。

---

### 需求 6：`createClipsRemote` 改为批量接口

**用户故事：** 作为一名音频编辑用户，我希望粘贴或复制多个 clip 时操作能立即完成，而不是等待数秒，以便保持流畅的编辑节奏。

#### 验收标准

1. WHEN 后端提供批量创建 clip 的接口时 THEN `createClipsRemote` thunk SHALL 改为单次调用批量接口，而非循环串行调用 `addClip` + `setClipState`。
2. IF 后端尚未提供批量接口 THEN 前端 SHALL 将多个 `addClip` + `setClipState` 调用改为并行执行（`Promise.all`），而非串行 `await`，以减少总等待时间。
3. WHEN 批量创建完成时 THEN 系统 SHALL 返回所有新创建 clip 的 ID 列表，与现有 `createdClipIds` 字段保持兼容。
4. IF 批量创建中任意一个 clip 失败 THEN 系统 SHALL 通过 `rejectWithValue` 返回错误，已成功创建的 clip 不回滚（保持现有行为）。

---

### 需求 7：波形峰值缓存持久化到 sessionStorage

**用户故事：** 作为一名音频编辑用户，我希望在同一会话内重新打开项目时，波形能立即显示而不需要重新加载，以便节省等待时间。

#### 验收标准

1. WHEN `useClipWaveformPeaks` 成功获取到峰值数据时 THEN 系统 SHALL 同时将结果写入 `sessionStorage`，key 格式为 `hs_peaks_v1|{sourcePath}|{startSec}|{durationSec}|{columns}`。
2. WHEN `useClipWaveformPeaks` 发起请求前 THEN 系统 SHALL 先检查 `sessionStorage` 中是否有对应 key 的缓存，命中时直接使用缓存数据，不发起网络请求。
3. WHEN `sessionStorage` 中的缓存条目总数超过 512 条时 THEN 系统 SHALL 按 LRU 策略删除最旧的条目，避免 sessionStorage 占用过多空间。
4. IF `sessionStorage` 写入失败（如存储空间不足）THEN 系统 SHALL 静默忽略错误，不影响正常的峰值加载流程。

---

### 需求 8：TimelinePanel 逻辑拆分重构

**用户故事：** 作为一名前端开发者，我希望 `TimelinePanel.tsx` 的逻辑被拆分为独立的 hook 模块，以便单独测试和维护各个功能模块。

#### 验收标准

1. WHEN 重构完成时 THEN 系统 SHALL 将以下逻辑从 `TimelinePanel.tsx` 中提取为独立 hook：
   - `useClipDrag`：clip 拖拽移动和 Ctrl+拖拽复制逻辑
   - `useEditDrag`：trim/stretch/fade/gain 边缘拖拽逻辑
   - `useSlipDrag`：Alt+拖拽 slip-edit 逻辑
   - `useKeyboardShortcuts`：键盘快捷键（Delete、Ctrl+C/V、S 分割等）
2. WHEN 重构完成时 THEN 系统 SHALL 保证所有现有功能行为不变，不引入回归。
3. WHEN 重构完成时 THEN `TimelinePanel.tsx` 的行数 SHALL 减少到 600 行以内。
4. WHEN 各 hook 提取完成时 THEN 每个 hook 文件 SHALL 放置在 `components/layout/timeline/hooks/` 目录下，与现有 `clip/` 子目录并列。
