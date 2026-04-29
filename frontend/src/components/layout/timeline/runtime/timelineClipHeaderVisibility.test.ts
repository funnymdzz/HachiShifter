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
        showPlaybackRate: false,
        showName: false,
    },
    "very narrow clips hide header contents",
);

assertEqual(
    resolveTimelineClipHeaderVisibility(40),
    {
        showAny: true,
        showMute: false,
        showGainKnob: true,
        showGainLabel: false,
        showPlaybackRate: false,
        showName: false,
    },
    "narrow clips prioritize gain knob before mute",
);

assertEqual(
    resolveTimelineClipHeaderVisibility(56),
    {
        showAny: true,
        showMute: true,
        showGainKnob: true,
        showGainLabel: false,
        showPlaybackRate: false,
        showName: false,
    },
    "medium clips keep mute and gain knob visible",
);

assertEqual(
    resolveTimelineClipHeaderVisibility(120),
    {
        showAny: true,
        showMute: true,
        showGainKnob: true,
        showGainLabel: true,
        showPlaybackRate: true,
        showName: false,
    },
    "playback rate appears before name when width is limited",
);

assertEqual(
    resolveTimelineClipHeaderVisibility(160),
    {
        showAny: true,
        showMute: true,
        showGainKnob: true,
        showGainLabel: true,
        showPlaybackRate: true,
        showName: true,
    },
    "wide clips keep full header contents visible",
);

console.log("timelineClipHeaderVisibility checks passed");
