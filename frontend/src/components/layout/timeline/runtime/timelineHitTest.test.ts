import { buildTimelineHitTestIndex, hitTestTimeline } from "./timelineHitTest.js";

function assertEqual(actual: unknown, expected: unknown, label: string): void {
    const actualJson = JSON.stringify(actual);
    const expectedJson = JSON.stringify(expected);
    if (actualJson !== expectedJson) {
        throw new Error(`${label}: expected ${expectedJson}, received ${actualJson}`);
    }
}

const index = buildTimelineHitTestIndex({
    rowHeight: 48,
    pxPerSec: 100,
    visibleTracks: [{ id: "track-a", topPx: 0 }],
    visibleClips: [{ id: "clip-a", trackId: "track-a", startSec: 1, lengthSec: 2 }],
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
    hitTestTimeline({ screenX: 298, screenY: 20, scrollLeftPx: 0, scrollTopPx: 0 }, index),
    { trackId: "track-a", clipId: "clip-a", zone: "trim_right" },
    "right trim hit",
);

assertEqual(
    hitTestTimeline({ screenX: 20, screenY: 20, scrollLeftPx: 0, scrollTopPx: 0 }, index),
    { trackId: "track-a", clipId: null, zone: "empty" },
    "empty lane hit",
);

const overlappingIndex = buildTimelineHitTestIndex({
    rowHeight: 48,
    pxPerSec: 100,
    visibleTracks: [{ id: "track-a", topPx: 0 }],
    visibleClips: [
        { id: "clip-back", trackId: "track-a", startSec: 1, lengthSec: 4 },
        { id: "clip-front", trackId: "track-a", startSec: 2, lengthSec: 3 },
    ],
});

assertEqual(
    hitTestTimeline({ screenX: 250, screenY: 16, scrollLeftPx: 0, scrollTopPx: 0 }, overlappingIndex),
    { trackId: "track-a", clipId: "clip-front", zone: "body" },
    "top-most overlapping clip wins",
);

console.log("timelineHitTest checks passed");
