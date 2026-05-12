# Timeline Performance Redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rebuild the timeline rendering and interaction core so the existing UI/behavior remains intact while `80` tracks / `5000` clips stays near `60 FPS` during scroll, box select, and drag at common zoom levels.

**Architecture:** Introduce a shared world-coordinate runtime, visible-window/render-model helpers, a single right-side canvas renderer, a controller-driven interaction layer, and a virtualized left track header list. Keep the current Redux/session actions and remote thunks as the semantic contract, but stop using `TrackLane -> ClipItem` as the primary high-frequency render path.

**Tech Stack:** React 19, Redux Toolkit, TypeScript, Vite, Canvas 2D, lightweight `tsx` script tests

---

## File Structure

### New runtime modules

- Create: `frontend/src/components/layout/timeline/runtime/timelineWorld.ts`
- Create: `frontend/src/components/layout/timeline/runtime/timelineWorld.test.ts`
- Create: `frontend/src/components/layout/timeline/runtime/timelineWindowing.ts`
- Create: `frontend/src/components/layout/timeline/runtime/timelineWindowing.test.ts`
- Create: `frontend/src/components/layout/timeline/runtime/timelineRenderModel.ts`
- Create: `frontend/src/components/layout/timeline/runtime/timelineRenderModel.test.ts`
- Create: `frontend/src/components/layout/timeline/runtime/timelineHitTest.ts`
- Create: `frontend/src/components/layout/timeline/runtime/timelineHitTest.test.ts`
- Create: `frontend/src/components/layout/timeline/runtime/timelineCanvasRenderer.ts`
- Create: `frontend/src/components/layout/timeline/runtime/timelineInteractionController.ts`
- Create: `frontend/src/components/layout/timeline/runtime/timelineInteractionController.test.ts`
- Create: `frontend/src/components/layout/timeline/runtime/timelinePerfProbe.ts`
- Create: `frontend/src/components/layout/timeline/runtime/timelinePerfProbe.test.ts`
- Create: `frontend/src/components/layout/timeline/runtime/timelinePerfScenario.ts`
- Create: `frontend/src/components/layout/timeline/runtime/timelinePerfScenario.test.ts`

### New React integration components

- Create: `frontend/src/components/layout/timeline/TimelineCanvasViewport.tsx`
- Create: `frontend/src/components/layout/timeline/TimelineTrackHeaderVirtualList.tsx`

### Existing files to adapt

- Modify: `frontend/src/components/layout/TimelinePanel.tsx`
- Modify: `frontend/src/components/layout/timeline/index.ts`
- Modify: `frontend/src/components/layout/timeline/TimeRuler.tsx`
- Modify: `frontend/src/components/layout/timeline/TimelineScrollArea.tsx`
- Modify: `frontend/src/components/layout/timeline/TrackList.tsx`
- Modify: `frontend/src/components/layout/timeline/hooks/useTimelineState.ts`
- Modify: `frontend/src/components/layout/timeline/hooks/useTimelineClipActions.ts`
- Modify: `frontend/src/components/layout/timeline/hooks/useTimelineEventHandlers.ts`
- Modify: `frontend/src/utils/timelineViewportBus.ts`

### Legacy path to remove from the critical render loop

- Modify: `frontend/src/components/layout/timeline/TrackLane.tsx`
- Modify: `frontend/src/components/layout/timeline/ClipItem.tsx`

---

### Task 1: Add Shared World-Coordinate Helpers

**Files:**
- Create: `frontend/src/components/layout/timeline/runtime/timelineWorld.ts`
- Test: `frontend/src/components/layout/timeline/runtime/timelineWorld.test.ts`

- [ ] **Step 1: Write the failing world-coordinate test**

```ts
import {
    createTimelineWorld,
    screenXToWorldSec,
    worldSecToScreenX,
    computeAnchoredScrollLeftPx,
    computeAnchoredScrollTopPx,
    computeWorldDragDelta,
} from "./timelineWorld.js";

function assertNear(actual: number, expected: number, label: string): void {
    if (Math.abs(actual - expected) > 1e-6) {
        throw new Error(`${label}: expected ${expected}, received ${actual}`);
    }
}

const world = createTimelineWorld({
    pxPerSec: 100,
    rowHeight: 48,
    scrollLeftPx: 250,
    scrollTopPx: 96,
});

assertNear(screenXToWorldSec(50, world), 3, "screen to world sec");
assertNear(worldSecToScreenX(3, world), 50, "world sec to screen");
assertNear(
    computeAnchoredScrollLeftPx({
        anchorSec: 3,
        anchorScreenX: 50,
        nextPxPerSec: 200,
    }, world),
    550,
    "anchored horizontal zoom",
);
assertNear(
    computeAnchoredScrollTopPx({
        anchorTrackUnit: 3.5,
        anchorScreenY: 24,
        nextRowHeight: 60,
    }, world),
    186,
    "anchored vertical zoom",
);

const drag = computeWorldDragDelta({
    startScreenX: 50,
    startScreenY: 24,
    currentScreenX: 130,
    currentScreenY: 120,
}, world);

assertNear(drag.deltaSec, 0.8, "drag delta sec");
assertNear(drag.deltaTrackUnits, 2, "drag delta tracks");

console.log("timelineWorld checks passed");
```

- [ ] **Step 2: Run test to verify it fails**

Run: `npx tsx frontend/src/components/layout/timeline/runtime/timelineWorld.test.ts`

Expected: FAIL with module-not-found or missing-export errors for `timelineWorld.ts`

- [ ] **Step 3: Write the minimal world-coordinate implementation**

```ts
export type TimelineWorld = {
    pxPerSec: number;
    rowHeight: number;
    scrollLeftPx: number;
    scrollTopPx: number;
};

export function createTimelineWorld(world: TimelineWorld): TimelineWorld {
    return { ...world };
}

export function screenXToWorldSec(screenX: number, world: TimelineWorld): number {
    return (world.scrollLeftPx + screenX) / Math.max(1e-9, world.pxPerSec);
}

export function worldSecToScreenX(worldSec: number, world: TimelineWorld): number {
    return worldSec * world.pxPerSec - world.scrollLeftPx;
}

export function computeAnchoredScrollLeftPx(
    args: { anchorSec: number; anchorScreenX: number; nextPxPerSec: number },
    world: TimelineWorld,
): number {
    return args.anchorSec * args.nextPxPerSec - args.anchorScreenX;
}
```

- [ ] **Step 4: Extend the implementation with vertical anchoring and drag deltas**

```ts
export function computeAnchoredScrollTopPx(
    args: { anchorTrackUnit: number; anchorScreenY: number; nextRowHeight: number },
    world: TimelineWorld,
): number {
    void world;
    return args.anchorTrackUnit * args.nextRowHeight - args.anchorScreenY;
}

export function computeWorldDragDelta(
    args: {
        startScreenX: number;
        startScreenY: number;
        currentScreenX: number;
        currentScreenY: number;
    },
    world: TimelineWorld,
): { deltaSec: number; deltaTrackUnits: number } {
    return {
        deltaSec: (args.currentScreenX - args.startScreenX) / Math.max(1e-9, world.pxPerSec),
        deltaTrackUnits:
            (args.currentScreenY - args.startScreenY) / Math.max(1e-9, world.rowHeight),
    };
}
```

- [ ] **Step 5: Run test to verify it passes**

Run: `npx tsx frontend/src/components/layout/timeline/runtime/timelineWorld.test.ts`

Expected: PASS with `timelineWorld checks passed`

- [ ] **Step 6: Commit**

```bash
git add frontend/src/components/layout/timeline/runtime/timelineWorld.ts frontend/src/components/layout/timeline/runtime/timelineWorld.test.ts
git commit -m "test: add timeline world coordinate helpers"
```

### Task 2: Add Visible Windowing And Render Model Builders

**Files:**
- Create: `frontend/src/components/layout/timeline/runtime/timelineWindowing.ts`
- Create: `frontend/src/components/layout/timeline/runtime/timelineRenderModel.ts`
- Test: `frontend/src/components/layout/timeline/runtime/timelineWindowing.test.ts`
- Test: `frontend/src/components/layout/timeline/runtime/timelineRenderModel.test.ts`

- [ ] **Step 1: Write failing tests for visible tracks and visible clips**

```ts
import { computeVisibleTrackWindow, sliceVisibleClipIds } from "./timelineWindowing.js";

function assertDeepEqual(actual: unknown, expected: unknown, label: string): void {
    const a = JSON.stringify(actual);
    const e = JSON.stringify(expected);
    if (a !== e) throw new Error(`${label}: expected ${e}, received ${a}`);
}

const windowed = computeVisibleTrackWindow({
    totalTracks: 80,
    rowHeight: 48,
    scrollTopPx: 96,
    viewportHeightPx: 240,
    overscanRows: 2,
});

assertDeepEqual(windowed, { startIndex: 0, endIndex: 9 }, "visible track window");

const clipIds = sliceVisibleClipIds({
    viewportStartSec: 10,
    viewportEndSec: 20,
    bufferSec: 2,
    clips: [
        { id: "a", startSec: 1, lengthSec: 3 },
        { id: "b", startSec: 8, lengthSec: 5 },
        { id: "c", startSec: 15, lengthSec: 2 },
        { id: "d", startSec: 25, lengthSec: 2 },
    ],
});

assertDeepEqual(clipIds, ["b", "c"], "visible clip ids");
console.log("timelineWindowing checks passed");
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `npx tsx frontend/src/components/layout/timeline/runtime/timelineWindowing.test.ts`

Expected: FAIL with module-not-found or missing-export errors for `timelineWindowing.ts`

- [ ] **Step 3: Write the windowing helpers**

```ts
export function computeVisibleTrackWindow(args: {
    totalTracks: number;
    rowHeight: number;
    scrollTopPx: number;
    viewportHeightPx: number;
    overscanRows: number;
}): { startIndex: number; endIndex: number } {
    const first = Math.floor(args.scrollTopPx / Math.max(1, args.rowHeight));
    const count = Math.ceil(args.viewportHeightPx / Math.max(1, args.rowHeight));
    return {
        startIndex: Math.max(0, first - args.overscanRows),
        endIndex: Math.min(args.totalTracks - 1, first + count + args.overscanRows),
    };
}

export function sliceVisibleClipIds(args: {
    viewportStartSec: number;
    viewportEndSec: number;
    bufferSec: number;
    clips: Array<{ id: string; startSec: number; lengthSec: number }>;
}): string[] {
    const minSec = args.viewportStartSec - args.bufferSec;
    const maxSec = args.viewportEndSec + args.bufferSec;
    return args.clips
        .filter((clip) => clip.startSec + clip.lengthSec >= minSec && clip.startSec <= maxSec)
        .map((clip) => clip.id);
}
```

- [ ] **Step 4: Add the failing render-model test**

```ts
import { buildTimelineRenderModel } from "./timelineRenderModel.js";

function assertEqual(actual: unknown, expected: unknown, label: string): void {
    if (JSON.stringify(actual) !== JSON.stringify(expected)) {
        throw new Error(`${label}: expected ${JSON.stringify(expected)}, received ${JSON.stringify(actual)}`);
    }
}

const model = buildTimelineRenderModel({
    tracks: [{ id: "t1" }, { id: "t2" }, { id: "t3" }],
    clips: [
        { id: "c1", trackId: "t1", startSec: 2, lengthSec: 1 },
        { id: "c2", trackId: "t2", startSec: 12, lengthSec: 5 },
    ],
    viewportStartSec: 10,
    viewportEndSec: 20,
    rowHeight: 48,
    scrollTopPx: 48,
    viewportHeightPx: 96,
});

assertEqual(model.visibleTrackIds, ["t1", "t2", "t3"], "visible track ids");
assertEqual(model.visibleClipIdsByTrackId.t2, ["c2"], "visible clip mapping");
console.log("timelineRenderModel checks passed");
```

- [ ] **Step 5: Implement the render-model builder**

```ts
import { computeVisibleTrackWindow, sliceVisibleClipIds } from "./timelineWindowing.js";

export function buildTimelineRenderModel(args: {
    tracks: Array<{ id: string }>;
    clips: Array<{ id: string; trackId: string; startSec: number; lengthSec: number }>;
    viewportStartSec: number;
    viewportEndSec: number;
    rowHeight: number;
    scrollTopPx: number;
    viewportHeightPx: number;
}) {
    const windowed = computeVisibleTrackWindow({
        totalTracks: args.tracks.length,
        rowHeight: args.rowHeight,
        scrollTopPx: args.scrollTopPx,
        viewportHeightPx: args.viewportHeightPx,
        overscanRows: 1,
    });
    const visibleTrackIds = args.tracks
        .slice(windowed.startIndex, windowed.endIndex + 1)
        .map((track) => track.id);
    const visibleClipIdsByTrackId = Object.fromEntries(
        visibleTrackIds.map((trackId) => [
            trackId,
            sliceVisibleClipIds({
                viewportStartSec: args.viewportStartSec,
                viewportEndSec: args.viewportEndSec,
                bufferSec: 1.5,
                clips: args.clips.filter((clip) => clip.trackId === trackId),
            }),
        ]),
    );
    return { ...windowed, visibleTrackIds, visibleClipIdsByTrackId };
}
```

- [ ] **Step 6: Run tests to verify they pass**

Run: `npx tsx frontend/src/components/layout/timeline/runtime/timelineWindowing.test.ts`

Expected: PASS with `timelineWindowing checks passed`

Run: `npx tsx frontend/src/components/layout/timeline/runtime/timelineRenderModel.test.ts`

Expected: PASS with `timelineRenderModel checks passed`

- [ ] **Step 7: Commit**

```bash
git add frontend/src/components/layout/timeline/runtime/timelineWindowing.ts frontend/src/components/layout/timeline/runtime/timelineWindowing.test.ts frontend/src/components/layout/timeline/runtime/timelineRenderModel.ts frontend/src/components/layout/timeline/runtime/timelineRenderModel.test.ts
git commit -m "test: add timeline windowing and render model helpers"
```

### Task 3: Add Hit-Test Indexes For Controller-Driven Interaction

**Files:**
- Create: `frontend/src/components/layout/timeline/runtime/timelineHitTest.ts`
- Test: `frontend/src/components/layout/timeline/runtime/timelineHitTest.test.ts`

- [ ] **Step 1: Write the failing hit-test cases**

```ts
import { buildTimelineHitTestIndex, hitTestTimeline } from "./timelineHitTest.js";

function assertEqual(actual: unknown, expected: unknown, label: string): void {
    if (JSON.stringify(actual) !== JSON.stringify(expected)) {
        throw new Error(`${label}: expected ${JSON.stringify(expected)}, received ${JSON.stringify(actual)}`);
    }
}

const index = buildTimelineHitTestIndex({
    rowHeight: 48,
    pxPerSec: 100,
    visibleTracks: [{ id: "track-a", topPx: 0 }],
    visibleClips: [
        { id: "clip-a", trackId: "track-a", startSec: 1, lengthSec: 2 },
    ],
});

assertEqual(
    hitTestTimeline({ screenX: 102, screenY: 20, scrollLeftPx: 0, scrollTopPx: 0 }, index),
    { trackId: "track-a", clipId: "clip-a", zone: "trim_left" },
    "left trim hit",
);

assertEqual(
    hitTestTimeline({ screenX: 240, screenY: 20, scrollLeftPx: 0, scrollTopPx: 0 }, index),
    { trackId: "track-a", clipId: "clip-a", zone: "body" },
    "body hit",
);

assertEqual(
    hitTestTimeline({ screenX: 20, screenY: 20, scrollLeftPx: 0, scrollTopPx: 0 }, index),
    { trackId: "track-a", clipId: null, zone: "empty" },
    "empty lane hit",
);

console.log("timelineHitTest checks passed");
```

- [ ] **Step 2: Run test to verify it fails**

Run: `npx tsx frontend/src/components/layout/timeline/runtime/timelineHitTest.test.ts`

Expected: FAIL with module-not-found or missing-export errors for `timelineHitTest.ts`

- [ ] **Step 3: Write the minimal hit-test index and resolver**

```ts
type VisibleTrack = { id: string; topPx: number };
type VisibleClip = { id: string; trackId: string; startSec: number; lengthSec: number };

export function buildTimelineHitTestIndex(args: {
    rowHeight: number;
    pxPerSec: number;
    visibleTracks: VisibleTrack[];
    visibleClips: VisibleClip[];
}) {
    return {
        rowHeight: args.rowHeight,
        pxPerSec: args.pxPerSec,
        tracksById: new Map(args.visibleTracks.map((track) => [track.id, track] as const)),
        clipsByTrackId: new Map(
            args.visibleTracks.map((track) => [
                track.id,
                args.visibleClips.filter((clip) => clip.trackId === track.id),
            ]),
        ),
    };
}
```

- [ ] **Step 4: Complete the hit-test zones using shared timeline geometry**

```ts
export function hitTestTimeline(
    point: { screenX: number; screenY: number; scrollLeftPx: number; scrollTopPx: number },
    index: ReturnType<typeof buildTimelineHitTestIndex>,
): { trackId: string | null; clipId: string | null; zone: "empty" | "body" | "trim_left" } {
    const track = [...index.tracksById.values()].find((candidate) => {
        const top = candidate.topPx - point.scrollTopPx;
        return point.screenY >= top && point.screenY < top + index.rowHeight;
    });
    if (!track) return { trackId: null, clipId: null, zone: "empty" };

    const worldSec = (point.scrollLeftPx + point.screenX) / Math.max(1e-9, index.pxPerSec);
    const clip = (index.clipsByTrackId.get(track.id) ?? []).find((candidate) => {
        return worldSec >= candidate.startSec && worldSec <= candidate.startSec + candidate.lengthSec;
    });
    if (!clip) return { trackId: track.id, clipId: null, zone: "empty" };

    return {
        trackId: track.id,
        clipId: clip.id,
        zone: worldSec - clip.startSec <= 0.08 ? "trim_left" : "body",
    };
}
```

- [ ] **Step 5: Run test to verify it passes**

Run: `npx tsx frontend/src/components/layout/timeline/runtime/timelineHitTest.test.ts`

Expected: PASS with `timelineHitTest checks passed`

- [ ] **Step 6: Commit**

```bash
git add frontend/src/components/layout/timeline/runtime/timelineHitTest.ts frontend/src/components/layout/timeline/runtime/timelineHitTest.test.ts
git commit -m "test: add timeline hit test index helpers"
```

### Task 4: Add Canvas Renderer And Perf Probe

**Files:**
- Create: `frontend/src/components/layout/timeline/runtime/timelineCanvasRenderer.ts`
- Create: `frontend/src/components/layout/timeline/runtime/timelinePerfProbe.ts`
- Create: `frontend/src/components/layout/timeline/runtime/timelinePerfProbe.test.ts`
- Create: `frontend/src/components/layout/timeline/TimelineCanvasViewport.tsx`
- Modify: `frontend/src/components/layout/timeline/index.ts`

- [ ] **Step 1: Write the failing perf-probe test**

```ts
import { createTimelinePerfProbe } from "./timelinePerfProbe.js";

function assertNear(actual: number, expected: number, label: string): void {
    if (Math.abs(actual - expected) > 1e-6) {
        throw new Error(`${label}: expected ${expected}, received ${actual}`);
    }
}

const probe = createTimelinePerfProbe(4);
probe.pushDrawMs(8);
probe.pushDrawMs(12);
probe.pushDrawMs(10);
probe.pushHitTestMs(2);
probe.pushHitTestMs(4);

assertNear(probe.getSnapshot().avgDrawMs, 10, "draw average");
assertNear(probe.getSnapshot().avgHitTestMs, 3, "hit-test average");
console.log("timelinePerfProbe checks passed");
```

- [ ] **Step 2: Run test to verify it fails**

Run: `npx tsx frontend/src/components/layout/timeline/runtime/timelinePerfProbe.test.ts`

Expected: FAIL with module-not-found or missing-export errors for `timelinePerfProbe.ts`

- [ ] **Step 3: Implement the perf probe**

```ts
function rollingAverage(values: number[]): number {
    if (values.length === 0) return 0;
    return values.reduce((sum, value) => sum + value, 0) / values.length;
}

export function createTimelinePerfProbe(limit = 60) {
    const drawMs: number[] = [];
    const hitTestMs: number[] = [];
    const push = (bucket: number[], value: number) => {
        bucket.push(value);
        if (bucket.length > limit) bucket.shift();
    };
    return {
        pushDrawMs(value: number) {
            push(drawMs, value);
        },
        pushHitTestMs(value: number) {
            push(hitTestMs, value);
        },
        getSnapshot() {
            return {
                avgDrawMs: rollingAverage(drawMs),
                avgHitTestMs: rollingAverage(hitTestMs),
            };
        },
    };
}
```

- [ ] **Step 4: Add the canvas draw entrypoint and viewport component**

```ts
// timelineCanvasRenderer.ts
export function drawTimelineCanvas(
    ctx: CanvasRenderingContext2D,
    args: {
        width: number;
        height: number;
        clips: Array<{ leftPx: number; topPx: number; widthPx: number; heightPx: number; selected: boolean }>;
        playheadX: number;
    },
): void {
    ctx.clearRect(0, 0, args.width, args.height);
    for (const clip of args.clips) {
        ctx.fillStyle = clip.selected ? "#5ea1ff" : "#54708d";
        ctx.fillRect(clip.leftPx, clip.topPx, clip.widthPx, clip.heightPx);
    }
    ctx.fillStyle = "#ff5d5d";
    ctx.fillRect(args.playheadX, 0, 1, args.height);
}
```

```tsx
// TimelineCanvasViewport.tsx
export const TimelineCanvasViewport: React.FC<{
    width: number;
    height: number;
    model: {
        drawClips: Array<{ leftPx: number; topPx: number; widthPx: number; heightPx: number; selected: boolean }>;
        playheadX: number;
    };
}> = ({ width, height, model }) => {
    const canvasRef = React.useRef<HTMLCanvasElement | null>(null);

    React.useLayoutEffect(() => {
        const canvas = canvasRef.current;
        if (!canvas) return;
        canvas.width = width;
        canvas.height = height;
        const ctx = canvas.getContext("2d");
        if (!ctx) return;
        drawTimelineCanvas(ctx, { width, height, clips: model.drawClips, playheadX: model.playheadX });
    }, [width, height, model]);

    return <canvas ref={canvasRef} className="absolute inset-0" />;
};
```

- [ ] **Step 5: Export the new viewport from the timeline barrel**

```ts
export * from "./TimelineCanvasViewport";
```

- [ ] **Step 6: Run test and typecheck to verify the new runtime compiles**

Run: `npx tsx frontend/src/components/layout/timeline/runtime/timelinePerfProbe.test.ts`

Expected: PASS with `timelinePerfProbe checks passed`

Run: `npx tsc -p frontend/tsconfig.app.json --noEmit`

Expected: PASS with no TypeScript errors

- [ ] **Step 7: Commit**

```bash
git add frontend/src/components/layout/timeline/runtime/timelineCanvasRenderer.ts frontend/src/components/layout/timeline/runtime/timelinePerfProbe.ts frontend/src/components/layout/timeline/runtime/timelinePerfProbe.test.ts frontend/src/components/layout/timeline/TimelineCanvasViewport.tsx frontend/src/components/layout/timeline/index.ts
git commit -m "feat: add timeline canvas viewport runtime"
```

### Task 5: Add Interaction Controller With Stable Zoom And Drag Axes

**Files:**
- Create: `frontend/src/components/layout/timeline/runtime/timelineInteractionController.ts`
- Test: `frontend/src/components/layout/timeline/runtime/timelineInteractionController.test.ts`
- Modify: `frontend/src/components/layout/timeline/TimelineCanvasViewport.tsx`
- Modify: `frontend/src/components/layout/timeline/TimelineScrollArea.tsx`
- Modify: `frontend/src/components/layout/timeline/TimeRuler.tsx`

- [ ] **Step 1: Write the failing interaction-controller test**

```ts
import {
    resolveWheelZoom,
    beginClipDrag,
    updateClipDrag,
} from "./timelineInteractionController.js";

function assertNear(actual: number, expected: number, label: string): void {
    if (Math.abs(actual - expected) > 1e-6) {
        throw new Error(`${label}: expected ${expected}, received ${actual}`);
    }
}

const zoom = resolveWheelZoom({
    anchorScreenX: 80,
    anchorSec: 4,
    nextPxPerSec: 240,
});
assertNear(zoom.nextScrollLeftPx, 880, "anchored zoom scrollLeft");

const drag = beginClipDrag({
    clipId: "clip-a",
    trackId: "track-a",
    startWorldSec: 4,
    startTrackIndex: 2,
});

const moved = updateClipDrag(drag, {
    currentWorldSec: 5.25,
    currentTrackIndex: 4,
});

assertNear(moved.deltaSec, 1.25, "drag delta sec");
assertNear(moved.deltaTrackIndex, 2, "drag delta track index");
console.log("timelineInteractionController checks passed");
```

- [ ] **Step 2: Run test to verify it fails**

Run: `npx tsx frontend/src/components/layout/timeline/runtime/timelineInteractionController.test.ts`

Expected: FAIL with module-not-found or missing-export errors for `timelineInteractionController.ts`

- [ ] **Step 3: Implement the pure controller helpers**

```ts
export function resolveWheelZoom(args: {
    anchorScreenX: number;
    anchorSec: number;
    nextPxPerSec: number;
}) {
    return {
        nextScrollLeftPx: args.anchorSec * args.nextPxPerSec - args.anchorScreenX,
    };
}

export function beginClipDrag(args: {
    clipId: string;
    trackId: string;
    startWorldSec: number;
    startTrackIndex: number;
}) {
    return { ...args };
}

export function updateClipDrag(
    drag: ReturnType<typeof beginClipDrag>,
    current: { currentWorldSec: number; currentTrackIndex: number },
) {
    return {
        clipId: drag.clipId,
        trackId: drag.trackId,
        deltaSec: current.currentWorldSec - drag.startWorldSec,
        deltaTrackIndex: current.currentTrackIndex - drag.startTrackIndex,
    };
}
```

- [ ] **Step 4: Wire the canvas viewport to use controller-owned pointer handlers**

```tsx
const onPointerDown = React.useCallback((event: React.PointerEvent<HTMLCanvasElement>) => {
    const bounds = event.currentTarget.getBoundingClientRect();
    const anchorScreenX = event.clientX - bounds.left;
    props.onPointerDown?.({
        screenX: anchorScreenX,
        screenY: event.clientY - bounds.top,
        clientX: event.clientX,
        clientY: event.clientY,
    });
}, [props]);

return <canvas ref={canvasRef} className="absolute inset-0" onPointerDown={onPointerDown} />;
```

- [ ] **Step 5: Switch `TimelineScrollArea` and `TimeRuler` to use shared world-anchor math**

```ts
const nextPxPerSec = clamp(pxPerSec * factor, MIN_PX_PER_SEC, MAX_PX_PER_SEC);
const zoom = resolveWheelZoom({
    anchorScreenX: clamp(e.clientX - bounds.left, 0, Math.max(1, bounds.width)),
    anchorSec,
    nextPxPerSec,
});
scroller.scrollLeft = zoom.nextScrollLeftPx;
```

```ts
const nextSec = screenXToWorldSec(e.clientX - bounds.left, {
    pxPerSec,
    rowHeight: 1,
    scrollLeftPx: currentScrollLeft,
    scrollTopPx: 0,
});
```

- [ ] **Step 6: Run tests and typecheck**

Run: `npx tsx frontend/src/components/layout/timeline/runtime/timelineInteractionController.test.ts`

Expected: PASS with `timelineInteractionController checks passed`

Run: `npx tsc -p frontend/tsconfig.app.json --noEmit`

Expected: PASS with no TypeScript errors

- [ ] **Step 7: Commit**

```bash
git add frontend/src/components/layout/timeline/runtime/timelineInteractionController.ts frontend/src/components/layout/timeline/runtime/timelineInteractionController.test.ts frontend/src/components/layout/timeline/TimelineCanvasViewport.tsx frontend/src/components/layout/timeline/TimelineScrollArea.tsx frontend/src/components/layout/timeline/TimeRuler.tsx
git commit -m "feat: add timeline interaction controller"
```

### Task 6: Integrate The New Runtime Into TimelinePanel And Virtualize Track Headers

**Files:**
- Create: `frontend/src/components/layout/timeline/TimelineTrackHeaderVirtualList.tsx`
- Modify: `frontend/src/components/layout/TimelinePanel.tsx`
- Modify: `frontend/src/components/layout/timeline/TrackList.tsx`
- Modify: `frontend/src/components/layout/timeline/hooks/useTimelineState.ts`
- Modify: `frontend/src/components/layout/timeline/hooks/useTimelineClipActions.ts`
- Modify: `frontend/src/components/layout/timeline/hooks/useTimelineEventHandlers.ts`

- [ ] **Step 1: Extend the render-model test to require shared visible track windows**

```ts
const model = buildTimelineRenderModel({
    tracks: Array.from({ length: 12 }, (_, index) => ({ id: `track-${index}` })),
    clips: [],
    viewportStartSec: 0,
    viewportEndSec: 10,
    rowHeight: 48,
    scrollTopPx: 96,
    viewportHeightPx: 192,
});

assertEqual(model.startIndex, 1, "virtual window start");
assertEqual(model.endIndex, 7, "virtual window end");
```

- [ ] **Step 2: Run the render-model test to verify it fails**

Run: `npx tsx frontend/src/components/layout/timeline/runtime/timelineRenderModel.test.ts`

Expected: FAIL because `buildTimelineRenderModel()` does not yet expose the shared track window contract

- [ ] **Step 3: Build the virtualized track-header component**

```tsx
export const TimelineTrackHeaderVirtualList: React.FC<{
    tracks: TrackInfo[];
    startIndex: number;
    endIndex: number;
    rowHeight: number;
    renderTrack: (track: TrackInfo) => React.ReactNode;
}> = ({ tracks, startIndex, endIndex, rowHeight, renderTrack }) => {
    const visible = tracks.slice(startIndex, endIndex + 1);
    return (
        <div style={{ position: "relative", height: tracks.length * rowHeight }}>
            <div style={{ transform: `translateY(${startIndex * rowHeight}px)` }}>
                {visible.map((track) => renderTrack(track))}
            </div>
        </div>
    );
};
```

- [ ] **Step 4: Replace `TrackLane` list rendering in `TimelinePanel` with the shared render model**

```tsx
const scrollTopPx = scrollRef.current?.scrollTop ?? 0;
const renderModel = React.useMemo(() => buildTimelineRenderModel({
    tracks: s.tracks,
    clips: s.clips,
    viewportStartSec,
    viewportEndSec,
    rowHeight,
    scrollTopPx,
    viewportHeightPx: scrollRef.current?.clientHeight ?? 0,
}), [s.tracks, s.clips, viewportStartSec, viewportEndSec, rowHeight, scrollTopPx]);

<TimelineTrackHeaderVirtualList
    tracks={s.tracks}
    startIndex={renderModel.startIndex}
    endIndex={renderModel.endIndex}
    rowHeight={rowHeight}
    renderTrack={(track) => renderTrackHeader(track)}
/>

<TimelineCanvasViewport
    width={viewportWidth}
    height={Math.max(1, trackGridHeight)}
    model={canvasModel}
    onPointerDown={handleCanvasPointerDown}
/>
```

- [ ] **Step 5: Move high-frequency scroll/playhead/overlay state out of wide React dependencies**

```ts
const sessionView = useAppSelector((state: RootState) => ({
    tracks: state.session.tracks,
    clips: state.session.clips,
    selectedTrackId: state.session.selectedTrackId,
    selectedClipId: state.session.selectedClipId,
    multiSelectedClipIds: state.session.multiSelectedClipIds,
    playheadSec: state.session.playheadSec,
    bpm: state.session.bpm,
}));
```

```ts
const overlayStateRef = useRef({
    hoverClipId: null as string | null,
    dragGhost: null as null | { clipIds: string[]; deltaSec: number; targetTrackId: string | null },
    selectionRect: null as null | { x1: number; y1: number; x2: number; y2: number },
});
```

- [ ] **Step 6: Run tests and typecheck**

Run: `npx tsx frontend/src/components/layout/timeline/runtime/timelineRenderModel.test.ts`

Expected: PASS with `timelineRenderModel checks passed`

Run: `npx tsc -p frontend/tsconfig.app.json --noEmit`

Expected: PASS with no TypeScript errors

- [ ] **Step 7: Commit**

```bash
git add frontend/src/components/layout/timeline/TimelineTrackHeaderVirtualList.tsx frontend/src/components/layout/TimelinePanel.tsx frontend/src/components/layout/timeline/TrackList.tsx frontend/src/components/layout/timeline/hooks/useTimelineState.ts frontend/src/components/layout/timeline/hooks/useTimelineClipActions.ts frontend/src/components/layout/timeline/hooks/useTimelineEventHandlers.ts frontend/src/components/layout/timeline/runtime/timelineRenderModel.ts frontend/src/components/layout/timeline/runtime/timelineRenderModel.test.ts
git commit -m "feat: integrate timeline canvas runtime into panel"
```

### Task 7: Add Performance Scenario, Remove Legacy Critical Path, And Verify Parity

**Files:**
- Create: `frontend/src/components/layout/timeline/runtime/timelinePerfScenario.ts`
- Test: `frontend/src/components/layout/timeline/runtime/timelinePerfScenario.test.ts`
- Modify: `frontend/src/components/layout/timeline/TrackLane.tsx`
- Modify: `frontend/src/components/layout/timeline/ClipItem.tsx`
- Modify: `frontend/src/utils/timelineViewportBus.ts`
- Modify: `frontend/src/components/layout/TimelinePanel.tsx`

- [ ] **Step 1: Write the failing perf-scenario test**

```ts
import { buildTimelinePerfScenario } from "./timelinePerfScenario.js";

function assertEqual(actual: unknown, expected: unknown, label: string): void {
    if (JSON.stringify(actual) !== JSON.stringify(expected)) {
        throw new Error(`${label}: expected ${JSON.stringify(expected)}, received ${JSON.stringify(actual)}`);
    }
}

const scenario = buildTimelinePerfScenario({
    trackCount: 80,
    clipsPerTrack: 62,
});

assertEqual(scenario.tracks.length, 80, "track count");
assertEqual(scenario.clips.length, 4960, "clip count");
assertEqual(scenario.clips[0]?.trackId, "track-0", "first clip track");
console.log("timelinePerfScenario checks passed");
```

- [ ] **Step 2: Run test to verify it fails**

Run: `npx tsx frontend/src/components/layout/timeline/runtime/timelinePerfScenario.test.ts`

Expected: FAIL with module-not-found or missing-export errors for `timelinePerfScenario.ts`

- [ ] **Step 3: Implement the perf-scenario builder**

```ts
export function buildTimelinePerfScenario(args: { trackCount: number; clipsPerTrack: number }) {
    const tracks = Array.from({ length: args.trackCount }, (_, index) => ({
        id: `track-${index}`,
        name: `Track ${index + 1}`,
    }));
    const clips = tracks.flatMap((track, trackIndex) =>
        Array.from({ length: args.clipsPerTrack }, (_, clipIndex) => ({
            id: `${track.id}-clip-${clipIndex}`,
            trackId: track.id,
            startSec: clipIndex * 1.5 + (trackIndex % 4) * 0.1,
            lengthSec: 1.2,
        })),
    );
    return { tracks, clips };
}
```

- [ ] **Step 4: Remove `TrackLane -> ClipItem` from the high-frequency viewport path**

```tsx
{false ? (
    s.tracks.map((track) => <TrackLane key={track.id} /* legacy debug path only */ />)
) : null}
```

```ts
// timelineViewportBus.ts
// Keep this bus only for legacy waveform fallback paths.
// The new canvas runtime should redraw from its own viewport store.
```

- [ ] **Step 5: Run the full verification set**

Run: `npx tsx frontend/src/components/layout/timeline/runtime/timelinePerfScenario.test.ts`

Expected: PASS with `timelinePerfScenario checks passed`

Run: `npx tsx frontend/src/components/layout/timeline/runtime/timelineWorld.test.ts`

Expected: PASS with `timelineWorld checks passed`

Run: `npx tsx frontend/src/components/layout/timeline/runtime/timelineWindowing.test.ts`

Expected: PASS with `timelineWindowing checks passed`

Run: `npx tsx frontend/src/components/layout/timeline/runtime/timelineRenderModel.test.ts`

Expected: PASS with `timelineRenderModel checks passed`

Run: `npx tsx frontend/src/components/layout/timeline/runtime/timelineHitTest.test.ts`

Expected: PASS with `timelineHitTest checks passed`

Run: `npx tsx frontend/src/components/layout/timeline/runtime/timelineInteractionController.test.ts`

Expected: PASS with `timelineInteractionController checks passed`

Run: `npx tsx frontend/src/components/layout/timeline/runtime/timelinePerfProbe.test.ts`

Expected: PASS with `timelinePerfProbe checks passed`

Run: `npx tsc -p frontend/tsconfig.app.json --noEmit`

Expected: PASS with no TypeScript errors

- [ ] **Step 6: Manual parity and performance verification**

Run the app and verify all of the following in the rebuilt timeline:

- Existing visual layout matches the old timeline
- Scroll, box select, drag, trim, fade, gain, rename, and context menu still work
- Pointer zoom and playhead zoom keep the same world-coordinate anchor before and after zoom
- Dragging remains stable while auto-scroll is active
- A synthetic `80` track / `5000` clip scenario stays near `60 FPS` at common zoom levels

- [ ] **Step 7: Commit**

```bash
git add frontend/src/components/layout/timeline/runtime/timelinePerfScenario.ts frontend/src/components/layout/timeline/runtime/timelinePerfScenario.test.ts frontend/src/components/layout/timeline/TrackLane.tsx frontend/src/components/layout/timeline/ClipItem.tsx frontend/src/utils/timelineViewportBus.ts frontend/src/components/layout/TimelinePanel.tsx
git commit -m "feat: finalize timeline performance runtime migration"
```
