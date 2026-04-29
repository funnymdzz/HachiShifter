import {
    buildTimelineClipVisualStyle,
    computeTimelineFadeShadeRange,
} from "./timelineCanvasStyle.js";

function assertEqual(actual: unknown, expected: unknown, label: string): void {
    const actualJson = JSON.stringify(actual);
    const expectedJson = JSON.stringify(expected);
    if (actualJson !== expectedJson) {
        throw new Error(`${label}: expected ${expectedJson}, received ${actualJson}`);
    }
}

const style = buildTimelineClipVisualStyle({
    widthPx: 160,
    trackColor: "#ff7a00",
    selected: false,
    muted: false,
    gain: 1,
    playbackRate: 1,
    name: "Lead Vocal Very Long Name For Playback Rate Header",
});
const compactStyle = buildTimelineClipVisualStyle({
    widthPx: 96,
    trackColor: "#ff7a00",
    selected: false,
    muted: false,
    gain: 1,
    playbackRate: 1,
    name: "Lead Vocal Very Long Name For Playback Rate Header",
});
const selectedStyle = buildTimelineClipVisualStyle({
    widthPx: 160,
    trackColor: "#ff7a00",
    selected: true,
    muted: false,
    gain: 1,
    playbackRate: 1,
    name: "Lead Vocal Very Long Name For Playback Rate Header",
});

assertEqual(style.showGainKnob, true, "gain knob visible");
assertEqual(style.showGainLabel, true, "gain label visible");
assertEqual(style.showName, true, "name visible");
assertEqual(style.showMuteBadge, true, "mute badge visible");
assertEqual(style.headerFill.startsWith("rgba("), true, "header uses mixed rgba color");
assertEqual(style.bodyFill.startsWith("rgba("), true, "body uses mixed rgba color");
assertEqual(style.displayName.length > 0, true, "name display is produced");
assertEqual(style.muteBadgeLabel, "M", "mute badge uses M label");
assertEqual(style.gainKnobAngleDeg, 0, "unity gain knob stays centered");
assertEqual(style.playbackRateLabel, "x1.00", "playback rate label is formatted");
assertEqual(style.showPlaybackRate, true, "playback rate shows on sufficiently wide clips");
assertEqual(compactStyle.showPlaybackRate, false, "playback rate hides before overlapping controls");
assertEqual(style.muteBadgeFill.startsWith("rgba("), true, "mute badge fill is resolved");
assertEqual(style.gainKnobIndicator.startsWith("rgba("), true, "gain knob indicator is resolved");
assertEqual(style.leadingControlsWidth, 58, "leading controls reserve prevents title overlap");
assertEqual(style.muteBadgeWidth, 20, "mute badge is enlarged");
assertEqual(style.gainKnobRadius, 7, "gain knob is enlarged");
assertEqual(style.gainKnobCenterOffsetX, 15, "gain knob sits at the far left of the header");
assertEqual(selectedStyle.headerFill, style.headerFill, "selected header keeps default visual");
assertEqual(selectedStyle.bodyFill, style.bodyFill, "selected body keeps default visual");
assertEqual(selectedStyle.borderStroke, style.borderStroke, "selected border keeps default visual");
assertEqual(selectedStyle.textFill, style.textFill, "selected text keeps default visual");

assertEqual(
    computeTimelineFadeShadeRange({
        widthPx: 200,
        fadeInPx: 40,
        fadeOutPx: 30,
    }),
    {
        startPx: 40,
        endPx: 170,
    },
    "shade range sits outside fade areas",
);

console.log("timelineCanvasStyle checks passed");
