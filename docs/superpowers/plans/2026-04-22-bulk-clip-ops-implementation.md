# Bulk Clip Ops Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add backend bulk clip update and bulk clip duplication APIs, then migrate Ctrl-drag clone, clipboard paste, and multi-select mute/gain/fade persistence to those bulk paths.

**Architecture:** Extend the existing timeline command/state stack with two backend-authoritative batch operations: one for patching many clips in one timeline transaction and one for duplicating many clips in one timeline transaction. Keep drag-time UI optimistic in the frontend, but replace per-clip persistence and per-clip clone creation with a single thunk call per user action.

**Tech Stack:** Rust/Tauri commands, frontend TypeScript thunks/hooks, Redux Toolkit, lightweight `tsx` script tests, TypeScript compile verification

---

### Task 1: Define frontend payload helpers for bulk clip updates and duplication

**Files:**
- Create: `frontend/src/components/layout/timeline/hooks/bulkClipRemotePayloads.ts`
- Test: `frontend/src/components/layout/timeline/hooks/bulkClipRemotePayloads.test.ts`

- [ ] **Step 1: Write the failing test**

```ts
import {
    buildBulkClipStateUpdates,
    buildDuplicateClipsBulkPayload,
} from "./bulkClipRemotePayloads.js";

function assertDeepEqual(actual: unknown, expected: unknown, label: string): void {
    const actualJson = JSON.stringify(actual);
    const expectedJson = JSON.stringify(expected);
    if (actualJson !== expectedJson) {
        throw new Error(`${label}: expected ${expectedJson}, received ${actualJson}`);
    }
}

const updates = buildBulkClipStateUpdates({
    clipIds: ["a", "b"],
    changesById: new Map([
        ["a", { gain: 1.2 }],
        ["b", { muted: true, fadeInSec: 0.4 }],
    ]),
});

assertDeepEqual(
    updates,
    [
        { clipId: "a", gain: 1.2 },
        { clipId: "b", muted: true, fadeInSec: 0.4 },
    ],
    "bulk state payload",
);

const duplicatePayload = buildDuplicateClipsBulkPayload({
    sourceClipIds: ["a", "b"],
    deltaSec: 1.5,
    copyLinkedParams: true,
    applyAutoCrossfade: true,
    trackMode: { kind: "offset_tracks", offset: 1 },
});

assertDeepEqual(
    duplicatePayload,
    {
        sourceClipIds: ["a", "b"],
        deltaSec: 1.5,
        copyLinkedParams: true,
        applyAutoCrossfade: true,
        selectCreatedClips: true,
        trackMode: { kind: "offset_tracks", offset: 1 },
    },
    "bulk duplicate payload",
);

console.log("bulk remote payload helper checks passed");
```

- [ ] **Step 2: Run test to verify it fails**

Run: `npx tsx src/components/layout/timeline/hooks/bulkClipRemotePayloads.test.ts`
Expected: FAIL with module-not-found for `bulkClipRemotePayloads.ts`

- [ ] **Step 3: Write minimal implementation**

```ts
export function buildBulkClipStateUpdates(args: {
    clipIds: string[];
    changesById: Map<string, Record<string, unknown>>;
}) {
    return args.clipIds.flatMap((clipId) => {
        const changes = args.changesById.get(clipId);
        if (!changes) return [];
        return [{ clipId, ...changes }];
    });
}

export function buildDuplicateClipsBulkPayload(args: {
    sourceClipIds: string[];
    deltaSec: number;
    copyLinkedParams: boolean;
    applyAutoCrossfade: boolean;
    trackMode: { kind: string; offset?: number };
}) {
    return {
        sourceClipIds: args.sourceClipIds,
        deltaSec: args.deltaSec,
        copyLinkedParams: args.copyLinkedParams,
        applyAutoCrossfade: args.applyAutoCrossfade,
        selectCreatedClips: true,
        trackMode: args.trackMode,
    };
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `npx tsx src/components/layout/timeline/hooks/bulkClipRemotePayloads.test.ts`
Expected: PASS with `bulk remote payload helper checks passed`

- [ ] **Step 5: Commit**

```bash
git add frontend/src/components/layout/timeline/hooks/bulkClipRemotePayloads.ts frontend/src/components/layout/timeline/hooks/bulkClipRemotePayloads.test.ts
git commit -m "test: cover bulk clip remote payload helpers"
```

### Task 2: Add backend bulk clip state update command

**Files:**
- Modify: `backend/src-tauri/src/state.rs`
- Modify: `backend/src-tauri/src/commands/timeline.rs`
- Modify: `backend/src-tauri/src/commands.rs`
- Modify: `backend/src-tauri/src/lib.rs`
- Test: `backend/src-tauri/src/state.rs`

- [ ] **Step 1: Write the failing test**

```rust
#[test]
fn patch_clips_state_updates_multiple_clips_in_one_pass() {
    let mut timeline = TimelineState::default();
    let track_id = timeline.add_track(Some("Track".to_string()), None, None);
    timeline.add_clip(Some(track_id.clone()), Some("A".into()), Some(0.0), Some(1.0), None);
    timeline.add_clip(Some(track_id), Some("B".into()), Some(1.0), Some(1.0), None);

    let ids: Vec<String> = timeline.clips.iter().map(|clip| clip.id.clone()).collect();
    timeline.patch_clips_state(&[
        BulkClipStatePatch {
            clip_id: ids[0].clone(),
            patch: ClipStatePatch {
                gain: Some(1.5),
                ..Default::default()
            },
        },
        BulkClipStatePatch {
            clip_id: ids[1].clone(),
            patch: ClipStatePatch {
                muted: Some(true),
                fade_in_sec: Some(0.25),
                ..Default::default()
            },
        },
    ]);

    assert_eq!(timeline.clips[0].gain, 1.5);
    assert!(timeline.clips[1].muted);
    assert_eq!(timeline.clips[1].fade_in_sec, 0.25);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cargo test patch_clips_state_updates_multiple_clips_in_one_pass`
Expected: FAIL because `patch_clips_state` / `BulkClipStatePatch` do not exist

- [ ] **Step 3: Write minimal implementation**

```rust
#[derive(Clone, Debug, Default, serde::Serialize, serde::Deserialize)]
pub struct BulkClipStatePatch {
    pub clip_id: String,
    pub patch: ClipStatePatch,
}

pub fn patch_clips_state(&mut self, updates: &[BulkClipStatePatch]) {
    for update in updates {
        self.patch_clip_state(&update.clip_id, update.patch.clone());
    }
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cargo test patch_clips_state_updates_multiple_clips_in_one_pass`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add backend/src-tauri/src/state.rs backend/src-tauri/src/commands/timeline.rs backend/src-tauri/src/commands.rs backend/src-tauri/src/lib.rs
git commit -m "feat: add bulk clip state update command"
```

### Task 3: Add backend bulk clip duplication command

**Files:**
- Modify: `backend/src-tauri/src/state.rs`
- Modify: `backend/src-tauri/src/commands/timeline.rs`
- Modify: `backend/src-tauri/src/commands.rs`
- Modify: `backend/src-tauri/src/lib.rs`
- Test: `backend/src-tauri/src/state.rs`

- [ ] **Step 1: Write the failing test**

```rust
#[test]
fn duplicate_clips_bulk_duplicates_multiple_clips_with_delta() {
    let mut timeline = TimelineState::default();
    let track_id = timeline.add_track(Some("Track".to_string()), None, None);
    timeline.add_clip(Some(track_id.clone()), Some("A".into()), Some(0.0), Some(1.0), None);
    timeline.add_clip(Some(track_id), Some("B".into()), Some(2.0), Some(1.5), None);

    let source_ids: Vec<String> = timeline.clips.iter().map(|clip| clip.id.clone()).collect();
    let created = timeline.duplicate_clips_bulk(
        &source_ids,
        1.25,
        DuplicateClipsTrackMode::SameTrack,
        false,
    );

    assert_eq!(created.len(), 2);
    assert_eq!(timeline.clips.len(), 4);
    assert!(timeline.clips.iter().any(|clip| (clip.start_sec - 1.25).abs() < 1e-6));
    assert!(timeline.clips.iter().any(|clip| (clip.start_sec - 3.25).abs() < 1e-6));
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cargo test duplicate_clips_bulk_duplicates_multiple_clips_with_delta`
Expected: FAIL because `duplicate_clips_bulk` / `DuplicateClipsTrackMode` do not exist

- [ ] **Step 3: Write minimal implementation**

```rust
pub enum DuplicateClipsTrackMode {
    SameTrack,
}

pub fn duplicate_clips_bulk(
    &mut self,
    source_clip_ids: &[String],
    delta_sec: f64,
    track_mode: DuplicateClipsTrackMode,
    _copy_linked_params: bool,
) -> Vec<String> {
    let mut created_ids = Vec::new();
    let source_clips: Vec<_> = self
        .clips
        .iter()
        .filter(|clip| source_clip_ids.iter().any(|id| id == &clip.id))
        .cloned()
        .collect();

    for source in source_clips {
        let mut duplicated = source.clone();
        duplicated.id = uuid::Uuid::new_v4().to_string();
        duplicated.start_sec = (duplicated.start_sec + delta_sec).max(0.0);
        created_ids.push(duplicated.id.clone());
        self.clips.push(duplicated);
    }

    created_ids
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cargo test duplicate_clips_bulk_duplicates_multiple_clips_with_delta`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add backend/src-tauri/src/state.rs backend/src-tauri/src/commands/timeline.rs backend/src-tauri/src/commands.rs backend/src-tauri/src/lib.rs
git commit -m "feat: add bulk clip duplication command"
```

### Task 4: Expose frontend API and thunks for the new bulk commands

**Files:**
- Modify: `frontend/src/services/api/timeline.ts`
- Modify: `frontend/src/services/webviewApi.ts`
- Modify: `frontend/src/features/session/thunks/timelineThunks.ts`
- Test: `frontend/src/components/layout/timeline/hooks/bulkClipRemotePayloads.test.ts`

- [ ] **Step 1: Extend the failing test with thunk-facing payload compatibility**

```ts
const stateUpdates = buildBulkClipStateUpdates({
    clipIds: ["clip-a"],
    changesById: new Map([["clip-a", { muted: false }]]),
});

assertDeepEqual(stateUpdates, [{ clipId: "clip-a", muted: false }], "single state update");
```

- [ ] **Step 2: Run test to verify it fails**

Run: `npx tsx src/components/layout/timeline/hooks/bulkClipRemotePayloads.test.ts`
Expected: FAIL because the helper shape no longer matches the thunk-facing payload contract

- [ ] **Step 3: Write minimal implementation**

```ts
setClipsStateBulk: (payload: { updates: Array<Record<string, unknown>> }) =>
    invoke<TimelineResult>("set_clips_state_bulk", payload.updates),

duplicateClipsBulk: (payload: {
    sourceClipIds: string[];
    deltaSec: number;
    trackMode: Record<string, unknown>;
    copyLinkedParams?: boolean;
    selectCreatedClips?: boolean;
    applyAutoCrossfade?: boolean;
    placeOnSelectedTrack?: boolean;
}) =>
    invoke<TimelineResult>("duplicate_clips_bulk", payload),
```

- [ ] **Step 4: Run test to verify it passes**

Run: `npx tsx src/components/layout/timeline/hooks/bulkClipRemotePayloads.test.ts`
Expected: PASS with `bulk remote payload helper checks passed`

- [ ] **Step 5: Commit**

```bash
git add frontend/src/services/api/timeline.ts frontend/src/services/webviewApi.ts frontend/src/features/session/thunks/timelineThunks.ts frontend/src/components/layout/timeline/hooks/bulkClipRemotePayloads.test.ts frontend/src/components/layout/timeline/hooks/bulkClipRemotePayloads.ts
git commit -m "feat: expose bulk clip timeline thunks"
```

### Task 5: Migrate timeline bulk edit persistence and clone flows to the new batch APIs

**Files:**
- Modify: `frontend/src/components/layout/timeline/hooks/useEditDrag.ts`
- Modify: `frontend/src/components/layout/timeline/hooks/useTimelineClipActions.ts`
- Modify: `frontend/src/components/layout/timeline/hooks/useClipDrag.ts`
- Modify: `frontend/src/features/session/thunks/timelineThunks.ts`
- Test: `frontend/src/components/layout/timeline/hooks/bulkClipRemotePayloads.test.ts`
- Test: `frontend/src/components/layout/timeline/hooks/copyDragTemplates.test.ts`

- [ ] **Step 1: Write the failing test additions**

```ts
const duplicatePayloadForPaste = buildDuplicateClipsBulkPayload({
    sourceClipIds: ["clip-1"],
    deltaSec: 0,
    copyLinkedParams: true,
    applyAutoCrossfade: false,
    trackMode: { kind: "same_track" },
});

assertDeepEqual(
    duplicatePayloadForPaste.trackMode,
    { kind: "same_track" },
    "paste can reuse duplicate bulk payload",
);
```

- [ ] **Step 2: Run test to verify it fails**

Run: `npx tsx src/components/layout/timeline/hooks/bulkClipRemotePayloads.test.ts`
Expected: FAIL because the helper and usage sites do not yet support the final migrated flow

- [ ] **Step 3: Write minimal implementation**

```ts
await dispatch(
    setClipsStateBulkRemote({
        updates: buildBulkClipStateUpdates({
            clipIds: drag.selectedClipIds,
            changesById,
        }),
    }),
).unwrap();
```

```ts
await dispatch(
    duplicateClipsBulkRemote(
        buildDuplicateClipsBulkPayload({
            sourceClipIds: drag.clipIds,
            deltaSec: drag.lastDeltaBeat,
            copyLinkedParams: sessionRef.current.lockParamLinesEnabled,
            applyAutoCrossfade: autoCrossfadeEnabled,
            trackMode: { kind: "offset_tracks", offset: drag.lastTrackOffset },
        }),
    ),
).unwrap();
```

- [ ] **Step 4: Run test to verify it passes**

Run: `npx tsx src/components/layout/timeline/hooks/bulkClipRemotePayloads.test.ts`
Expected: PASS with `bulk remote payload helper checks passed`

Run: `npx tsx src/components/layout/timeline/hooks/copyDragTemplates.test.ts`
Expected: PASS with `copy drag template helper checks passed`

- [ ] **Step 5: Commit**

```bash
git add frontend/src/components/layout/timeline/hooks/useEditDrag.ts frontend/src/components/layout/timeline/hooks/useTimelineClipActions.ts frontend/src/components/layout/timeline/hooks/useClipDrag.ts frontend/src/features/session/thunks/timelineThunks.ts frontend/src/components/layout/timeline/hooks/bulkClipRemotePayloads.test.ts frontend/src/components/layout/timeline/hooks/copyDragTemplates.test.ts
git commit -m "feat: migrate bulk clip edits and clone flows to batch APIs"
```
