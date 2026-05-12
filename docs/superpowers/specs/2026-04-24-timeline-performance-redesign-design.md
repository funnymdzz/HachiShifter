# Timeline Performance Redesign Design

## Goal

重建当前项目的 timeline 渲染与交互内核，使其在保持最终 UI 和功能一致的前提下，能够在高负载场景下持续流畅工作。

本次设计的硬性目标是：

- 在常见缩放级别下，`80` 轨 / `5000` clip 的场景中，滚动、框选、拖拽都接近 `60 FPS`
- 播放中自动跟随、播放头移动、缩放、右键命中、多选编辑等核心交互不能出现明显卡顿
- 最终用户可见的 UI、交互语义、快捷键、右键菜单和编辑结果与现有 timeline 保持一致

## Hard Requirements

### 1. UI And Behavior Parity

本次允许重建实现，不允许改变产品语义。

必须保持一致的内容包括：

- 轨道头布局、轨道高度缩放、轨道滚动同步
- clip 的视觉结构、选中态、多选态、重叠显示顺序
- 播放头、时间尺、自动跟随滚动
- 拖拽移动、跨轨移动、复制拖拽
- trim / stretch / fade / gain / mute / rename
- 框选、Shift 范围选、Ctrl 切换选、右键命中语义
- 现有 Redux action、remote thunk、后端命令的业务语义

### 2. Coordinate Axis Consistency

缩放和拖动的坐标轴必须与外界坐标轴保持严格一致。这是本次设计的硬约束，不允许出现“视觉上在一个坐标系、计算上在另一个坐标系”的分裂。

具体要求：

- timeline 内部所有横向计算统一以“秒”为唯一语义坐标
- `pxPerSec` 只负责“秒 -> 像素”的显示映射，不允许不同子模块自行定义偏移规则
- 时间尺、播放头、clip 几何、框选、ghost、命中测试、滚动锚点、缩放锚点必须共享同一套 world coordinate
- 缩放时的锚点必须稳定：
  - 指针缩放时，缩放前后该指针对应的时间位置不变
  - 播放头缩放时，缩放前后播放头对应的时间位置不变
- 拖动时的位移必须稳定：
  - 拖动计算使用 world coordinate delta
  - 不能因为滚动、缩放、虚拟化窗口变化导致 clip 在拖动过程中出现跳变
- 与外部模块交互时继续使用当前工程的时间语义：
  - 播放头位置
  - 导入位置
  - snap
  - 后端 timeline 命令

换句话说，新的渲染架构可以变，但 timeline 的世界坐标系只能有一套。

## Current State

当前 timeline 的主要性能瓶颈不是某一个慢函数，而是整体架构让高频状态穿透整棵 React 渲染树：

1. `TimelinePanel` 和 `useTimelineState` 直接绑定大块 `session` 状态
   - `playheadSec`
   - `runtime`
   - `tracks`
   - `clips`
   - `trackMeters`
   - 多选状态
   - 缩放和滚动派生值

2. 右侧时间轴仍然以“轨道 DOM + clip DOM 列表”为核心
   - 每条轨道由 `TrackLane` 渲染
   - 每个 clip 由 `ClipItem` 渲染
   - 高频状态变化时，React 仍要反复经过大量组件边界

3. 波形虽然已经做了“每轨一个 canvas”，但 clip 交互层仍以大量 DOM 为基础
   - 这限制了可扩展上限
   - 在 `80` 轨 / `5000` clip 目标下，继续打补丁很难稳定达标

4. 交互逻辑分散在多个组件和 hook 中
   - 拖拽
   - trim/stretch
   - fade/gain
   - 右键命中
   - 框选
   - 播放头拖动
   - 自动滚动

这会导致两个问题：

- 高频交互需要频繁回到 React/Redux 更新路径
- 坐标计算容易在多个局部实现里产生偏差

## Proposed Approach

推荐方案是：保留现有 timeline 的产品语义和业务接口，重建 timeline 渲染内核。

本方案不是继续在现有 DOM 架构上打补丁，也不是再开一套并行的 timeline2/timeline3，而是直接替换现有 timeline 的内部实现。

原则如下：

- 保留外部行为，重做内部渲染
- 保留业务接口，新增 timeline 专用的高性能派生层
- 保留现有视觉结果，重写绘制与命中测试路径
- 高频临时态不再通过整棵 React 树传播

## Architecture

新 timeline 分为三层：

### 1. Semantic Layer

这一层继续复用现有工程的业务语义：

- Redux session state
- clip / track 数据模型
- 当前 remote action 和 thunk
- 后端 timeline 命令
- 快捷键和上下文菜单语义

目标是避免“性能重构”顺带变成“业务协议重构”。

### 2. View Model Layer

新增一个 timeline 专用的派生快照层，将 Redux 原始状态转换为适合高性能渲染的结构。

核心数据：

- `tracksOrdered`
- `trackIndexById`
- `clipsByTrackId`
- `clipIndexById`
- `visibleTrackWindow`
- `visibleClipSlices`
- `timelineWorld`
- `selectionSnapshot`
- `overlaySnapshot`
- `hitTestIndex`

其中：

- `timelineWorld` 负责统一世界坐标定义
- `visibleTrackWindow` 负责纵向虚拟化
- `visibleClipSlices` 负责横向可见裁剪
- `hitTestIndex` 负责交互命中加速

这层必须做到“低频结构稳定、高频派生独立更新”。

### 3. Render Layer

渲染层拆成三部分：

1. 左侧轨道头虚拟化层
   - 只渲染可见轨道及缓冲区轨道
   - 保留现有轨道头外观和能力

2. 右侧时间轴主 canvas 层
   - 负责网格
   - 负责 clip body 与选中态
   - 负责重叠 clip 可视化顺序
   - 负责 ghost、框选框、播放头辅助线等高密度元素

3. 右侧交互浮层
   - 只保留必须使用 DOM 的元素
   - 例如重命名输入框、菜单锚点、必要的 hover/editor 覆盖层

结果是：

- React 管外层布局和低频 UI
- 高频绘制交给 canvas
- 高频交互由 controller 驱动

## World Coordinate Model

为了满足坐标轴一致性要求，timeline 必须显式定义统一的世界坐标模型。

### Coordinate Definitions

- `worldSec`: 世界时间坐标，所有横向语义的唯一真值
- `worldTrackIndex`: 轨道序号坐标，所有纵向命中的唯一真值
- `viewportScrollLeftPx`: 当前视口左边界对应的像素偏移
- `viewportScrollTopPx`: 当前视口上边界对应的像素偏移
- `pxPerSec`: 横向缩放比例
- `rowHeight`: 纵向缩放比例

统一转换规则：

- `screenX -> worldSec`
- `worldSec -> screenX`
- `screenY -> worldTrackIndex`
- `worldTrackIndex -> screenY`

所有模块必须调用共享转换函数，不允许自行复制坐标换算。

### Zoom Rules

缩放使用锚点保持策略：

- 指针缩放：
  - `anchorSec = screenToWorldX(pointerX)`
  - 更新 `pxPerSec`
  - 反算新的 `scrollLeft`
  - 使 `worldToScreenX(anchorSec)` 保持等于原始 `pointerX`

- 播放头缩放：
  - `anchorSec = playheadSec`
  - 更新 `pxPerSec`
  - 反算新的 `scrollLeft`
  - 使 `worldToScreenX(playheadSec)` 保持稳定

纵向缩放同理：

- 使用 `rowHeight` 和 pointer 所在轨道单位位置反算新的 `scrollTop`
- 保证缩放前后指针对应的轨道位置稳定

### Drag Rules

拖拽必须以世界坐标差值计算：

- 初始按下时记录：
  - `dragStartWorldSec`
  - `dragStartTrackIndex`
  - 被拖动对象的原始世界坐标

- 移动过程中：
  - 当前指针位置统一转换为世界坐标
  - 使用 `currentWorld - startWorld` 作为位移
  - snap 在世界坐标下执行

这能保证：

- 拖动过程中发生自动滚动时不跳变
- 拖动过程中发生缩放更新时不跳变
- 虚拟化窗口变更不会影响几何结果

## Rendering Strategy

### Left Track Header Virtualization

左侧轨道头将与右侧共享同一套纵向窗口信息。

要求：

- 只渲染可见轨道和上下缓冲区
- track meter 更新不能驱动整个轨道头列表全量重渲染
- 左右面板始终共享统一的 `scrollTop` 和可见轨道区间

### Right Timeline Canvas

主 canvas 负责时间轴主体绘制，优先覆盖：

- 背景网格
- clip 主体
- 选中态 / 多选态
- muted / ghost / overlap 视觉状态
- 播放头线
- selection rect
- 拖拽预览和编辑预览

绘制策略：

- 只绘制可见轨道窗口
- 每轨只绘制横向可见 clip 及缓冲区 clip
- 滚动时使用局部重绘和 rAF 合帧
- 缩放时重建可见切片，但不回退为全量 DOM

### DOM Overlay

保留最小化 DOM 浮层以支持：

- rename input
- context menu anchor
- 必要的无障碍焦点元素
- 某些必须原生处理的文本或输入交互

DOM overlay 的坐标必须来自统一 world coordinate，而不是局部估算。

## Interaction Model

新 timeline 的交互由统一 controller 负责，而不是分散在每个 clip DOM 上。

### Timeline Interaction Controller

统一接管：

- pointer down / move / up
- wheel
- keyboard shortcut bridge
- auto-scroll during drag
- context menu dispatch

controller 的职责是把原始输入事件转换成 timeline 语义事件。

例如：

- 命中 clip body
- 命中 trim handle
- 命中 fade handle
- 命中 gain region
- 命中空白轨道
- 命中时间尺

### Hit Test Index

命中测试使用轻量几何索引，而不是遍历全量 clip。

建议结构：

- 按轨道分桶
- 每轨 clip 按时间排序
- 对可见区 clip 切片后建立局部索引

典型命中流程：

1. 由 `screenY` 推导当前轨道
2. 读取该轨道当前可见 clip 列表
3. 在局部列表中进行二分或邻近扫描
4. 输出命中目标与子区域类型

### Overlay State

以下高频临时态不进入 Redux 主状态：

- hover clip
- drag ghost
- selection rect
- trim/stretch/fade/gain drag preview
- playhead dragging preview
- auto-scroll temporary state

这些状态由 timeline runtime/controller 持有，并直接触发局部重绘。

只有在交互完成时，最终结果才提交到 Redux 和后端。

## Data Flow

### Low-Frequency Data

以下数据通过 selector 和派生快照低频更新：

- 轨道结构
- clip 数据
- 选中结果
- 颜色和主题
- 工具模式
- 配置和快捷键

### High-Frequency Data

以下数据不应驱动整棵 React 树：

- `scrollLeft`
- `scrollTop`
- `playhead` 视觉位置
- hover
- drag preview
- selection rect
- wheel zoom in-flight state

这些数据应通过 runtime store、ref 或控制器内部状态更新，并由 canvas/overlay 直接消费。

## Migration Plan

迁移分五步完成，避免一次性替换风险过高。

### Phase 1: Build Timeline View Model And Runtime

新增：

- `TimelineViewportStore`
- `TimelineRenderModel`
- `TimelineHitTestIndex`
- 统一坐标转换工具

这一步先不改变 UI，只改变 timeline 内部的数据组织方式。

### Phase 2: Replace Right-Side Rendering With Canvas

使用新主 canvas 接管：

- 网格
- clip body
- 选中态
- 播放头线
- ghost
- selection rect

在视觉对齐稳定前，允许保留少量旧 DOM 辅助验证，但最终要移除大量 `ClipItem` 渲染路径。

### Phase 3: Move Interactions Into Controller

将以下交互从组件级事件迁移到统一 controller：

- clip 拖拽
- trim/stretch
- fade/gain
- 框选
- seek
- 右键命中

### Phase 4: Virtualize Left Track Header

让 `TrackList` 与右侧共享同一纵向窗口，避免几十个轨道头和 meter 更新拖慢整体渲染。

### Phase 5: Remove Obsolete DOM-Based Timeline Paths

删除或降级旧的：

- `TrackLane` 大规模 DOM 路径
- `ClipItem` 全量列表交互路径
- 依赖旧组件树的高频命中与拖拽机制

## Testing Strategy

### 1. Pure Logic Tests

为以下模块编写纯函数测试：

- world coordinate conversion
- anchored zoom calculation
- drag delta calculation
- snap behavior in world space
- visible track window calculation
- visible clip slice calculation
- hit test target resolution
- overlap ordering

重点验证：

- 缩放前后锚点稳定
- 滚动和缩放过程中拖拽无跳变
- 命中结果与旧行为一致

### 2. Timeline Behavior Regression

回归场景至少覆盖：

- 单选和多选
- Shift 范围选
- Ctrl 切换选
- 框选
- clip 拖拽与复制拖拽
- trim/stretch/fade/gain
- 右键命中重叠 clip
- rename
- 播放头点击、拖动、自动跟随
- 轨道头滚动同步

### 3. Performance Verification

新增 timeline 性能探针，至少记录：

- 可见轨道数
- 可见 clip 数
- 每帧 draw 耗时
- 每帧 hit test 耗时
- 滚动期间平均帧耗时
- 拖拽期间平均帧耗时

必须构造 `80` 轨 / `5000` clip 的基准数据场景，验证：

- 常见缩放级别下滚动接近 `60 FPS`
- 常见缩放级别下框选接近 `60 FPS`
- 常见缩放级别下拖拽接近 `60 FPS`

## Scope Boundaries

本次包含：

- timeline 渲染内核重建
- timeline 坐标系统一
- 左右面板虚拟化/窗口化
- hit test 和 interaction controller 重建
- timeline 性能探针和基准验证

本次不包含：

- 改变 timeline 的用户可见产品设计
- 改变 session/后端协议的核心语义
- 借性能重构顺带改造无关模块

## Risks

1. 旧交互细节较多
   - 如果没有系统化拆出 hit test 和 world coordinate，容易在边角行为上回归

2. 画布化后，文本编辑和菜单锚点处理更复杂
   - 必须通过最小 DOM overlay 解决，而不是回退到大量 clip DOM

3. 如果坐标转换仍然散落在多个模块
   - 会再次出现缩放、拖拽、滚动之间的轴不一致问题

## Recommendation

直接以“统一世界坐标 + 视图模型层 + 主 canvas + controller”作为新 timeline 内核，不再继续优化现有 `TimelinePanel -> TrackLane -> ClipItem` 的 DOM 主路径。

原因是本次目标不是局部提速，而是在保持 UI 和功能一致的前提下，把 timeline 的性能上限提升到可以稳定支撑 `80` 轨 / `5000` clip。
