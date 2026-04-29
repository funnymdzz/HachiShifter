# Timeline Bulk Edit Small Bugs Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix timeline multi-select clip editing gaps, reduce copy-drag lag for large clip selections, and add a track context-menu action that creates a new track directly below the selected track.

**Architecture:** Keep the fixes inside the existing timeline editing hooks instead of reshaping the feature structure. Extract only the new decision-making logic that benefits from lightweight script-style regression tests, then wire those helpers back into `useEditDrag`, `useTimelineClipActions`, `useClipDrag`, and `TrackList`.

**Tech Stack:** React 19, Redux Toolkit, TypeScript, Vite, lightweight Node-driven `.test.ts` scripts

---

### Task 1: Add testable helpers for bulk clip edit targeting

**Files:**
- Create: `frontend/src/components/layout/timeline/hooks/bulkClipEdit.ts`
- Test: `frontend/src/components/layout/timeline/hooks/bulkClipEdit.test.ts`

- [ ] **Step 1: Write the failing test**

```ts
import {
    getBulkEditableClipIds,
    applyBulkFadeValue,
    applyBulkGainDeltaDb,
} from "./bulkClipEdit.js";

function assertDeepEqual(actual: unknown, expected: unknown, label: string): void {
    const actualJson = JSON.stringify(actual);
    const expectedJson = JSON.stringify(expected);
    if (actualJson !== expectedJson) {
        throw new Error(`${label}: expected ${expectedJson}, received ${actualJson}`);
    }
}

const selectedIds = getBulkEditableClipIds({
    activeClipId: "b",
    multiSelectedClipIds: ["a", "b", "c"],
    multiSelectedSet: new Set(["a", "b", "c"]),
});

assertDeepEqual(selectedIds, ["a", "b", "c"], "bulk-selected ids");

const singleIds = getBulkEditableClipIds({
    activeClipId: "x",
    multiSelectedClipIds: ["a", "b", "c"],
    multiSelectedSet: new Set(["a", "b", "c"]),
});

assertDeepEqual(singleIds, ["x"], "single fallback ids");

const fadeUpdates = applyBulkFadeValue({
    clipIds: ["a", "b"],
    clipsById: new Map([
        ["a", { lengthSec: 3 }],
        ["b", { lengthSec: 1.25 }],
    ]),
    target: "fadeOutSec",
    nextValue: 2,
});

assertDeepEqual(
    fadeUpdates,
    [
        { clipId: "a", fadeOutSec: 2 },
        { clipId: "b", fadeOutSec: 1.25 },
    ],
    "fade updates clamp per clip",
);

const gainUpdates = applyBulkGainDeltaDb({
    clipIds: ["a", "b"],
    clipsById: new Map([
        ["a", { gain: 1 }],
        ["b", { gain: 0.5 }],
    ]),
    deltaDb: 6,
    minDb: -12,
    maxDb: 12,
});

assertDeepEqual(
    gainUpdates.map((entry) => ({
        clipId: entry.clipId,
        gain: Number(entry.gain.toFixed(4)),
    })),
    [
        { clipId: "a", gain: 1.9953 },
        { clipId: "b", gain: 0.9976 },
    ],
    "gain updates preserve per-clip relative delta",
);

console.log("bulk clip edit helpers checks passed");
```

- [ ] **Step 2: Run test to verify it fails**

Run: `npx tsx src/components/layout/timeline/hooks/bulkClipEdit.test.ts`
Expected: FAIL with module-not-found or missing-export error for `bulkClipEdit.ts`

- [ ] **Step 3: Write minimal implementation**

```ts
type BulkEditableArgs = {
    activeClipId: string;
    multiSelectedClipIds: string[];
    multiSelectedSet: Set<string>;
};

export function getBulkEditableClipIds(args: BulkEditableArgs): string[] {
    const { activeClipId, multiSelectedClipIds, multiSelectedSet } = args;
    if (multiSelectedClipIds.length > 0 && multiSelectedSet.has(activeClipId)) {
        return [...multiSelectedClipIds];
    }
    return [activeClipId];
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `npx tsx src/components/layout/timeline/hooks/bulkClipEdit.test.ts`
Expected: PASS with `bulk clip edit helpers checks passed`

- [ ] **Step 5: Commit**

```bash
git add frontend/src/components/layout/timeline/hooks/bulkClipEdit.ts frontend/src/components/layout/timeline/hooks/bulkClipEdit.test.ts
git commit -m "test: cover timeline bulk clip edit helpers"
```

### Task 2: Make multi-selected gain, fade, and mute edits act on the whole selection

**Files:**
- Modify: `frontend/src/components/layout/timeline/hooks/useEditDrag.ts`
- Modify: `frontend/src/components/layout/timeline/hooks/useTimelineClipActions.ts`
- Modify: `frontend/src/components/layout/TimelinePanel.tsx`
- Test: `frontend/src/components/layout/timeline/hooks/bulkClipEdit.test.ts`

- [ ] **Step 1: Extend the failing test with mute targeting and zero/length clamp cases**

```ts
const fadeInUpdates = applyBulkFadeValue({
    clipIds: ["a", "b"],
    clipsById: new Map([
        ["a", { lengthSec: 0.4 }],
        ["b", { lengthSec: 2.5 }],
    ]),
    target: "fadeInSec",
    nextValue: -1,
});

assertDeepEqual(
    fadeInUpdates,
    [
        { clipId: "a", fadeInSec: 0 },
        { clipId: "b", fadeInSec: 0 },
    ],
    "fade values clamp to zero",
);
```

- [ ] **Step 2: Run test to verify it fails**

Run: `npx tsx src/components/layout/timeline/hooks/bulkClipEdit.test.ts`
Expected: FAIL because the new helper behavior is not implemented yet

- [ ] **Step 3: Write minimal implementation**

```ts
const selectedClipIds = getBulkEditableClipIds({
    activeClipId: drag.clipId,
    multiSelectedClipIds,
    multiSelectedSet,
});

const clipsById = new Map(sessionRef.current.clips.map((clip) => [clip.id, clip]));
const fadeUpdates = applyBulkFadeValue({
    clipIds: selectedClipIds,
    clipsById,
    target: "fadeInSec",
    nextValue: next,
});

batch(() => {
    for (const update of fadeUpdates) {
        dispatch(setClipFades(update));
    }
});
```

- [ ] **Step 4: Run test to verify it passes**

Run: `npx tsx src/components/layout/timeline/hooks/bulkClipEdit.test.ts`
Expected: PASS with `bulk clip edit helpers checks passed`

- [ ] **Step 5: Commit**

```bash
git add frontend/src/components/layout/timeline/hooks/useEditDrag.ts frontend/src/components/layout/timeline/hooks/useTimelineClipActions.ts frontend/src/components/layout/TimelinePanel.tsx frontend/src/components/layout/timeline/hooks/bulkClipEdit.ts frontend/src/components/layout/timeline/hooks/bulkClipEdit.test.ts
git commit -m "fix: support bulk clip gain fade and mute edits"
```

### Task 3: Extract copy-drag template building so large selections avoid serial slow paths

**Files:**
- Create: `frontend/src/components/layout/timeline/hooks/copyDragTemplates.ts`
- Modify: `frontend/src/components/layout/timeline/hooks/useClipDrag.ts`
- Test: `frontend/src/components/layout/timeline/hooks/copyDragTemplates.test.ts`

- [ ] **Step 1: Write the failing test**

```ts
import { buildCopyDragTemplates } from "./copyDragTemplates.js";

function assertEqual(actual: unknown, expected: unknown, label: string): void {
    if (actual !== expected) {
        throw new Error(`${label}: expected ${String(expected)}, received ${String(actual)}`);
    }
}

const templates = await buildCopyDragTemplates({
    templateInputs: [
        {
            id: "clip-a",
            initial: { startSec: 1, trackId: "track-1" },
            now: {
                name: "A",
                lengthSec: 2,
                sourcePath: "a.wav",
                durationSec: 2,
                gain: 1,
                muted: false,
                sourceStartSec: 0,
                sourceEndSec: 2,
                playbackRate: 1,
                fadeInSec: 0,
                fadeOutSec: 0,
            },
            targetTrackId: "track-2",
        },
    ],
    deltaSec: 3,
    linkedParamsResults: [{ ok: true, linkedParams: { pitch: [1, 2] } }],
});

assertEqual(templates[0]?.trackId, "track-2", "target track");
assertEqual(templates[0]?.startSec, 4, "shifted start");
assertEqual(Array.isArray(templates[0]?.linkedParams?.pitch), true, "linked params kept");

console.log("copy drag template helper checks passed");
```

- [ ] **Step 2: Run test to verify it fails**

Run: `npx tsx src/components/layout/timeline/hooks/copyDragTemplates.test.ts`
Expected: FAIL with module-not-found or missing-export error for `copyDragTemplates.ts`

- [ ] **Step 3: Write minimal implementation**

```ts
export async function buildCopyDragTemplates(args: {
    templateInputs: Array<{
        id: string;
        initial: { startSec: number; trackId: string };
        now: {
            name: string;
            lengthSec: number;
            sourcePath?: string;
            durationSec?: number;
            gain?: number;
            muted?: boolean;
            sourceStartSec?: number;
            sourceEndSec?: number;
            playbackRate?: number;
            fadeInSec?: number;
            fadeOutSec?: number;
            fadeInCurve?: string;
            fadeOutCurve?: string;
        };
        targetTrackId: string;
    }>;
    deltaSec: number;
    linkedParamsResults: Array<{ ok?: boolean; linkedParams?: unknown }>;
}) {
    return args.templateInputs.map((input, index) => ({
        trackId: input.targetTrackId,
        name: String(input.now.name),
        startSec: Math.max(0, input.initial.startSec + args.deltaSec),
        lengthSec: Number(input.now.lengthSec),
        sourcePath: input.now.sourcePath,
        durationSec: input.now.durationSec,
        gain: Number(input.now.gain ?? 1) || 1,
        muted: Boolean(input.now.muted),
        sourceStartSec: Number(input.now.sourceStartSec ?? 0) || 0,
        sourceEndSec: Number(input.now.sourceEndSec ?? 0) || 0,
        playbackRate: Number(input.now.playbackRate ?? 1) || 1,
        fadeInSec: Number(input.now.fadeInSec ?? 0) || 0,
        fadeOutSec: Number(input.now.fadeOutSec ?? 0) || 0,
        fadeInCurve: input.now.fadeInCurve,
        fadeOutCurve: input.now.fadeOutCurve,
        linkedParams: args.linkedParamsResults[index]?.ok
            ? args.linkedParamsResults[index]?.linkedParams
            : undefined,
    }));
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `npx tsx src/components/layout/timeline/hooks/copyDragTemplates.test.ts`
Expected: PASS with `copy drag template helper checks passed`

- [ ] **Step 5: Commit**

```bash
git add frontend/src/components/layout/timeline/hooks/copyDragTemplates.ts frontend/src/components/layout/timeline/hooks/copyDragTemplates.test.ts frontend/src/components/layout/timeline/hooks/useClipDrag.ts
git commit -m "perf: streamline clip copy-drag template creation"
```

### Task 4: Add track context-menu action for creating a new track below the selected track

**Files:**
- Modify: `frontend/src/components/layout/timeline/TrackList.tsx`
- Modify: `frontend/src/components/layout/TimelinePanel.tsx`
- Test: `frontend/src/components/layout/timeline/trackContextMenuPlacement.test.ts`

- [ ] **Step 1: Write the failing test**

```ts
import { getInsertBelowTargetIndex } from "./trackContextMenuPlacement.js";

function assertEqual(actual: unknown, expected: unknown, label: string): void {
    if (actual !== expected) {
        throw new Error(`${label}: expected ${String(expected)}, received ${String(actual)}`);
    }
}

assertEqual(
    getInsertBelowTargetIndex([
        { id: "track-1" },
        { id: "track-2" },
        { id: "track-3" },
    ], "track-2"),
    2,
    "insert below selected track",
);

console.log("track placement helper checks passed");
```

- [ ] **Step 2: Run test to verify it fails**

Run: `npx tsx src/components/layout/timeline/trackContextMenuPlacement.test.ts`
Expected: FAIL with module-not-found or missing-export error for `trackContextMenuPlacement.ts`

- [ ] **Step 3: Write minimal implementation**

```ts
export function getInsertBelowTargetIndex(
    tracks: Array<{ id: string }>,
    anchorTrackId: string,
): number {
    const anchorIndex = tracks.findIndex((track) => track.id === anchorTrackId);
    if (anchorIndex < 0) {
        return tracks.length;
    }
    return anchorIndex + 1;
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `npx tsx src/components/layout/timeline/trackContextMenuPlacement.test.ts`
Expected: PASS with `track placement helper checks passed`

- [ ] **Step 5: Commit**

```bash
git add frontend/src/components/layout/timeline/TrackList.tsx frontend/src/components/layout/TimelinePanel.tsx frontend/src/components/layout/timeline/trackContextMenuPlacement.ts frontend/src/components/layout/timeline/trackContextMenuPlacement.test.ts
git commit -m "feat: add create-track-below timeline action"
```
