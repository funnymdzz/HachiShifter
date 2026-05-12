# Bulk Clip Ops Design

## Goal

为时间轴提供真正的批量 clip 操作后端通道，统一支撑以下场景：

- 多选 clip 的批量 `mute / gain / fade` 持久化
- `Ctrl` 拖动复制的大批量克隆
- 普通复制粘贴的批量创建

目标是减少前端逐 clip `dispatch / invoke / timeline apply` 带来的卡顿，同时把 undo、linked params、轨道映射这些批量语义收束到后端一次性完成。

## Current State

当前实现有两个核心问题：

1. 批量编辑仍然以“多个单 clip 请求”的方式持久化
   - 多选 `mute / gain / fade` 在前端已经有部分批量本地更新
   - 但结束时仍会变成多个 `setClipStateRemote`
   - 导致多次 invoke、多次 timeline 同步、多次 UI 覆盖

2. 批量克隆仍然是前端主导的串行创建
   - `createClipsRemote` 本质是循环：
     - `addClip`
     - `setClipState`
     - `applyClipLinkedParams`
   - `Ctrl` 拖动复制和普通复制粘贴都绕不过这条链路
   - 数量一大时，linked params 获取、模板构建、逐次后端提交都会堆积

仓库已经有可复用的模式：

- 后端已有 `move_clips`、`remove_clips` 这种“单次 checkpoint + 单次 timeline update”的批量命令
- 前端时间轴逻辑已经拆到 hook 中，便于将 bulk 持久化切换到统一 thunk

## Proposed Approach

本次引入两个新的后端批处理命令，并让前端统一迁移到它们：

### 1. `set_clips_state_bulk`

用途：

- 一次性持久化多个 clip 的状态更新
- 首批覆盖：
  - `gain`
  - `muted`
  - `fade_in_sec`
  - `fade_out_sec`

前端传入：

- `updates: Vec<ClipStateBulkPatch>`

每个 patch 只包含：

- `clip_id`
- 本次变更涉及的字段

后端职责：

- 单次 checkpoint
- 遍历更新多个 clip
- 单次 `audio_engine.update_timeline`
- 返回一次 timeline payload

### 2. `duplicate_clips_bulk`

用途：

- 统一处理所有“由现有 clip 派生出新 clip”的场景

首批接入：

- `Ctrl` 拖动复制
- 普通复制粘贴

前端传入：

- `source_clip_ids`
- `delta_sec`
- `track_mode`
- `copy_linked_params`
- `select_created_clips`
- `apply_auto_crossfade`
- `place_on_selected_track`（用于普通粘贴）

`track_mode` 支持：

- `same_track`
- `offset_tracks`
- `explicit_mapping`
- `new_tracks`

后端职责：

- 根据源 clip 批量生成新 clip
- 一次性分配 ID
- 一次性复制 linked params
- 一次性处理目标轨道映射 / 新轨创建
- 一次性应用自动交叉淡化
- 单次 checkpoint
- 单次 `audio_engine.update_timeline`
- 返回一次 timeline payload 和 `created_clip_ids`

## Architecture

### Frontend

新增或改造：

- `frontend/src/services/api/timeline.ts`
  - 新增 `setClipsStateBulk`
  - 新增 `duplicateClipsBulk`

- `frontend/src/services/webviewApi.ts`
  - 暴露上述两个 facade

- `frontend/src/features/session/thunks/timelineThunks.ts`
  - 新增 `setClipsStateBulkRemote`
  - 新增 `duplicateClipsBulkRemote`
  - `createClipsRemote` 调整为：
    - 保留给“外部模板导入”使用
    - 普通复制粘贴改走 `duplicateClipsBulkRemote`

- `frontend/src/components/layout/timeline/hooks/useEditDrag.ts`
  - 拖动中仍走本地 bulk reducer
  - 结束时改成一次 `setClipsStateBulkRemote`

- `frontend/src/components/layout/timeline/hooks/useTimelineClipActions.ts`
  - `mute / 数值 gain` 改成一次 `setClipsStateBulkRemote`

- `frontend/src/components/layout/timeline/hooks/useClipDrag.ts`
  - `Ctrl` 拖动复制释放时改成一次 `duplicateClipsBulkRemote`
  - 继续保留 ghost 预览逻辑

### Backend

新增或改造：

- `backend/src-tauri/src/commands/timeline.rs`
  - 新增 `set_clips_state_bulk`
  - 新增 `duplicate_clips_bulk`

- `backend/src-tauri/src/commands.rs`
  - 暴露 tauri command

- `backend/src-tauri/src/state.rs`
  - 复用现有 clip 更新能力，增加批量版本
  - 新增批量 duplicate 的 timeline 变更逻辑

- linked params 复制逻辑
  - 优先复用现有 `extract_clip_linked_params / apply_clip_linked_params`
  - 若现有接口过于前端导向，则在 timeline/state 层抽一个内部复制 helper

## Behavior Details

### Bulk State Update

- 前端拖动中只更新本地 Redux，保证交互帧率
- 用户释放手柄后，前端收集最终 patch 列表，一次提交
- 返回的 timeline 是唯一权威状态

### Duplicate on Ctrl-Drag

- 拖动过程中仍然只显示 ghost，不创建真实 clip
- 释放时一次调用 `duplicate_clips_bulk`
- 如果拖到新轨区域，由后端负责新轨创建和映射
- 若启用了自动交叉淡化，由后端统一计算并应用

### Paste

- 剪贴板不再要求保存“完整可创建模板”
- 前端可继续保存显示/回显所需的模板信息
- 真正创建时，将其转换成 bulk duplicate 输入
- `place_on_selected_track` 逻辑移动到后端执行

## Error Handling

- 任一 bulk 请求失败时，前端保留当前本地交互结果直到后端响应
- 如果后端返回失败：
  - 对于批量编辑：使用后端返回或重新获取的 timeline 纠正 UI
  - 对于批量克隆：不创建任何新 clip，保持原始状态

后端原则：

- bulk 请求应尽量原子
- 避免部分成功、部分失败
- 若无法保证完全成功，则整体回滚并返回错误

## Testing Strategy

### Frontend

新增轻量脚本测试，覆盖：

- bulk payload 构建
- duplicate 请求参数构建
- 轨道映射辅助逻辑

### Backend

优先补单元或集成测试覆盖：

- `set_clips_state_bulk`
  - 多 clip 更新 gain / mute / fade
  - 单次 checkpoint 生效

- `duplicate_clips_bulk`
  - 同轨复制
  - 跨轨 offset 复制
  - 新轨复制
  - linked params 复制
  - 自动交叉淡化联动

### Verification

实现后至少执行：

- 新增前端脚本测试
- `npx tsc -p frontend/tsconfig.app.json --noEmit`
- 与 Rust bulk 命令相关的测试命令

## Scope Boundaries

本次包含：

- bulk clone API
- bulk clip state update API
- 接入 `Ctrl` 拖动复制
- 接入普通复制粘贴
- 接入多选 `mute / gain / fade` 持久化

本次不包含：

- split/glue/replace source 的 bulk 化
- normalize 的后端化
- 全量重写剪贴板格式

## Risks

1. 复制粘贴和 `Ctrl` 拖动复制之前共享不完全相同的前端流程
   - 需要避免为了统一接口而破坏现有粘贴语义

2. linked params 复制逻辑现在偏前端驱动
   - 迁到后端时要确认数据结构和生命周期一致

3. 自动交叉淡化如果继续散在前端处理，会削弱 bulk 方案收益
   - 推荐尽量并入后端 bulk duplicate 流程

## Recommendation

先做后端 bulk 能力，再切前端调用方，不做“只在前端包一层 Promise.allSettled”的过渡方案。

原因是这次目标不是只减少一点点前端卡顿，而是把批量操作真正变成：

- 一次请求
- 一次 timeline 变更
- 一次 undo
- 一次最终同步

这才是对大量 clip 操作最有效、也最可持续的结构。
