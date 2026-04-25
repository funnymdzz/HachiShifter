import { resolveTimelineClipHeaderVisibility } from "./timelineClipHeaderVisibility.js";

function assertEqual(actual: unknown, expected: unknown, label: string): void {
    const actualJson = JSON.stringify(actual);
    const expectedJson = JSON.stringify(expected);
    if (actualJson !== expectedJson) {
        throw new Error(`${label}: expected ${expectedJson}, received ${actualJson}`);
    }
}

assertEqual(
    resolveTimelineClipHeaderVisibility(24),
    {
        showAny: false,
        showMute: false,
        showGainKnob: false,
        showGainLabel: false,
        showName: false,
    },
    "very narrow clips hide header contents",
);

assertEqual(
    resolveTimelineClipHeaderVisibility(56),
    {
        showAny: true,
        showMute: true,
        showGainKnob: true,
        showGainLabel: false,
        showName: false,
    },
    "medium clips keep mute and gain knob always visible",
);

assertEqual(
    resolveTimelineClipHeaderVisibility(128),
    {
        showAny: true,
        showMute: true,
        showGainKnob: true,
        showGainLabel: true,
        showName: true,
    },
    "wide clips keep full header contents visible",
);

console.log("timelineClipHeaderVisibility checks passed");
