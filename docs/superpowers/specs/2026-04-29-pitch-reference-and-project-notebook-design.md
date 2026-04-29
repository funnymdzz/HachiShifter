# Pitch Reference And Project Notebook Design

## Goal

为 `PianoRoll` 增加“其他轨道组音高线参考”能力，并新增一个项目级右侧记事本面板。

这两个功能都属于编辑辅助能力，但边界不同：

- 轨道组参考音高线：帮助用户在编辑当前 root track 的音高时，对照其他 root track 的 pitch 走势
- 项目记事本：给当前工程提供 Markdown 记事与预览能力，并随工程一起保存

本次目标是先把第一版基础能力做稳，不在首版引入过多智能行为或复杂排版能力。

## Scope

本设计包含两个相对独立的子系统：

1. `PianoRoll` 的“参考轨道组”显示
2. 工程级右侧 `记事本` 面板

它们可以在同一轮开发中实现，但实现计划应按子系统拆任务，避免交叉改动扩大风险。

## Current State

### 1. PianoRoll 音高线现状

- 当前 `PianoRoll` 以 `resolveRootTrackId()` 解析当前编辑上下文，参数数据读取围绕一个 root track 展开
- `usePianoRollData.ts` 已具备：
  - 主参数视图读取
  - secondary parameter overlays 读取
  - 当前可视时间窗的数据刷新
- `render.ts` 已支持：
  - 当前参数曲线绘制
  - 检测音高曲线绘制
  - secondary overlays 绘制

当前缺口是：

- 还没有“跨 root track 的 pitch 参考线”数据结构
- 也没有“按 root track 勾选显示哪些参考线”的 UI

### 2. 右侧面板现状

- `App.tsx` 当前只有一个可收起的右侧面板：文件管理器
- `ActionBar.tsx` 已有文件管理器开关按钮
- 右侧停靠区目前是单列条件渲染，不支持多个独立面板并排

### 3. 工程数据现状

- 工程文件 `ProjectFile` 当前保存：
  - timeline
  - scale
  - grid
  - media registry
  - synth config
- 还没有项目级 notes / markdown 字段
- 前端也没有 notebook state 或 markdown preview 能力

## Proposed Approach

### A. 参考轨道组音高线

采用“工具栏下拉勾选 root track”的方案。

理由：

- 最贴合当前 `PianoRoll` 顶部工具栏结构
- 改动小于常驻侧栏
- 交互明确，不会像自动显示那样不可预测
- 内部状态设计可兼容后续升级成常驻面板

### B. 项目级记事本

采用“独立右侧停靠面板”的方案。

理由：

- 与现有文件管理器的交互模型一致
- 满足“按钮在文件管理器旁边、也是可关闭面板”的要求
- 第一版最容易被理解

Markdown 编辑方式采用“编辑 / 预览切换”，不做首版双栏同步视图。

理由：

- 明确符合“Markdown 文本，并带预览切换”
- 比双栏模式更省空间
- 首版实现风险更低

## Architecture

### A. 参考轨道组音高线

### UI State

前端 session UI 状态新增：

- `visibleReferenceRootTrackIds: string[]`
- 可选扩展：
  - `referenceRootTrackHoverId: string | null`

其中：

- `visibleReferenceRootTrackIds` 持久化到 UI settings
- 状态维度按 root track，而不是单个子轨道

### Data Flow

1. `PianoRollPanel` 解析当前 `rootTrackId`
2. 顶部工具栏展示 `参考轨道组` 按钮
3. 下拉面板列出所有 root tracks，排除当前 root track
4. 用户勾选后更新 `visibleReferenceRootTrackIds`
5. `usePianoRollData` 基于当前可视时间窗，对这些 root tracks 额外拉取 `pitch` param frames
6. `render.ts` 将这些数据作为只读 reference overlays 绘制

### Data Model

新增前端参考线视图模型，建议按 root track 聚合：

- `referencePitchViewsByRootTrackId: Record<string, ParamViewSegment>`

可选补充 root track 元信息：

- name
- color

root track 名称和颜色优先复用现有 `tracks` 列表，不重复存储。

### Rendering

当前编辑音高线仍为主视觉层。

参考线视觉规则：

- 颜色：继承 root track.color，但统一降饱和与提亮
- 线宽：比当前线细
- 线型：虚线
- 透明度：低于主线

推荐默认：

- 当前线：实线、现有主样式
- 参考线：`1px` 到 `1.5px` 细虚线，`0.35` 到 `0.45` alpha

hover 行为：

- 在下拉列表中 hover 某个 root track 时，对应参考线临时提高透明度
- 不改变其它参考线顺序与编辑能力

### UI Composition

`PianoRoll` 顶部新增一个按钮：

- 文案：`参考轨道组`
- 点击后弹出下拉面板

面板内容：

- `全选`
- `清空`
- root track 列表

每项包含：

- 勾选框
- root track 名称
- 颜色标识
- 可选弱提示：当前视窗是否存在可显示 pitch 数据

### Constraints

第一版限制：

- 仅 `editParam === "pitch"` 时显示该入口
- 仅显示其他 root track，不显示当前 root track
- 仅做参考，不允许编辑
- 仅拉取当前时间窗数据
- 不做自动智能显示
- 不做单组透明度调节
- 不做非 pitch 参数的跨组参考

### B. 项目级右侧记事本

### UI State

前端新增 notebook 面板状态，建议独立于 file browser：

- `visible: boolean`
- `mode: "edit" | "preview"`

如果项目级内容也先缓存到前端 store，则额外包含：

- `markdown: string`
- `dirtySinceLastProjectSync: boolean`

右侧停靠面板的可见性不应塞进 file browser slice。
建议拆出独立 `notebook` slice，或新增一个轻量 `rightDock` slice 统一管理多个右侧面板的可见性。

### Project Data

工程文件 `ProjectFile` 新增字段：

- `notes_markdown: String`

规则：

- 新工程默认空字符串
- 打开工程时读入
- 保存工程时写回
- 旧工程反序列化时通过 `#[serde(default)]` 兼容为空

### Layout

`App.tsx` 右侧区域从“单一文件管理器列”扩展为“右侧停靠面板容器”：

- 文件管理器可见时显示一列
- 记事本可见时显示一列
- 两者同时可见时并排显示两列

首版宽度先固定，不引入新的横向 splitter。

### Panel Composition

记事本面板包含：

- 顶部标题栏
  - 标题：`记事本`
  - 模式切换：`编辑` / `预览`
  - 关闭按钮
- 主体内容区
  - 编辑模式：纯文本 Markdown 输入框
  - 预览模式：只读 Markdown 渲染

第一版不做双栏同步显示。

### Markdown Behavior

首版支持常用 Markdown：

- 标题
- 段落
- 列表
- 引用
- 代码块
- 行内代码
- 分隔线
- 链接

首版不支持：

- 原始 HTML 直通
- 富文本编辑
- 图片上传
- 任务列表高级交互

若仓库没有现成 Markdown 渲染依赖，则新增一个轻量方案；渲染时需要默认进行安全转义，不允许任意 HTML 注入。

### Save Behavior

保存规则：

- 文本编辑后立即同步到前端状态
- 工程 `dirty` 状态应被置脏
- `Ctrl+S` / 菜单保存工程时，一并写入工程文件
- 关闭面板不丢内容
- 打开其它工程时切换到对应工程内容

未保存到磁盘的新工程：

- 内容先驻留在前端状态和内存中的项目状态
- 一旦用户保存工程，随工程文件落盘

## Components And File Impact

预计主要改动点如下。

### 参考轨道组音高线

- `frontend/src/components/layout/PianoRollPanel.tsx`
  - 顶部按钮与下拉面板
  - hover / select 交互胶水

- `frontend/src/components/layout/pianoRoll/usePianoRollData.ts`
  - 扩展参考 root track 的 pitch 请求链路

- `frontend/src/components/layout/pianoRoll/render.ts`
  - 增加 reference pitch overlays 绘制

- `frontend/src/features/session/sessionSlice.ts`
  - 新增 UI 状态与 reducer
  - 接入 UI settings 持久化

- `frontend/src/services/api/settings.ts`
  - 新增持久化字段

- `backend/src-tauri/src/config.rs`
  - UI settings 序列化支持

### 项目级记事本

- `frontend/src/components/layout/ActionBar.tsx`
  - 新增 notebook toggle 按钮

- `frontend/src/App.tsx`
  - 右侧停靠面板容器扩展
  - 条件渲染 notebook panel

- `frontend/src/components/layout/NotebookPanel.tsx`
  - 新增右侧记事本面板组件

- `frontend/src/features/notebook/*` 或 `frontend/src/features/rightDock/*`
  - 新增状态管理

- `frontend/src/services/api/project.ts`
  - 项目 meta / open / save 对 notes 字段的类型补齐

- `backend/src-tauri/src/project.rs`
  - `ProjectFile` 新增 `notes_markdown`
  - 新工程默认值
  - 打开 / 保存序列化兼容

- `backend/src-tauri/src/models.rs`
  - `ProjectMetaPayload` 增加 notes 字段，若前端需要从 meta 初始化

## Error Handling

### 参考轨道组音高线

- 某个 reference root track 拉取失败：
  - 不影响主编辑轨道
  - 对应参考线静默不显示
  - 保留用户勾选状态

- root track 被删除或不存在：
  - 自动从 `visibleReferenceRootTrackIds` 清理

- 当前 root track 切换：
  - 若某项变成“当前 root track”，本轮面板中不再显示该项
  - 不把它作为参考线渲染

### 项目级记事本

- Markdown 渲染失败：
  - 预览区显示安全的错误占位，不影响原始文本

- 旧工程没有 `notes_markdown`：
  - 默认空字符串

- 工程保存失败：
  - 记事本内容保留在前端状态
  - 不清空 dirty 状态

## Testing Strategy

### Frontend

新增轻量测试覆盖：

- root track 参考线勾选状态 reducer
- 参考 root track 列表过滤逻辑
- reference overlay 样式辅助逻辑
- notebook panel 模式切换 reducer
- markdown 内容脏状态与项目切换同步逻辑

优先做可脱离 DOM 的纯逻辑测试，减少大体积组件测试成本。

### Backend

新增单元测试覆盖：

- `ProjectFile` 的 `notes_markdown` 默认反序列化
- 旧工程文件兼容打开
- 带 `notes_markdown` 的工程文件保存与再读取一致

### Verification

实现后至少验证：

- `PianoRoll` 中勾选多个其他 root track，可显示不同样式参考线
- 切换当前 root track 后，参考列表与渲染正确更新
- 打开 / 关闭文件管理器与记事本时，右侧面板可并排存在
- 记事本编辑、切换预览、保存工程、重新打开工程后内容保持一致
- `frontend` TypeScript 编译通过
- 后端工程文件读写相关检查通过

## Open Decisions Closed In This Spec

以下问题已在本 spec 中固定，避免实现阶段再次摇摆：

- 参考线选择维度：按 root track
- 参考线入口：工具栏下拉勾选
- 参考线样式：继承轨道组颜色系，但降饱和、细线、虚线、低透明度
- 记事本布局：独立右侧停靠面板
- 记事本入口：位于文件管理器按钮旁边
- 记事本内容：项目级 Markdown
- 记事本预览：编辑 / 预览切换，不做首版双栏
