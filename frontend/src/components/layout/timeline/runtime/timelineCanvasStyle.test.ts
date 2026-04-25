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
    widthPx: 180,
    trackColor: "#ff7a00",
    selected: false,
    muted: false,
    gain: 1,
    name: "Lead Vocal Very Long Name",
});
const selectedStyle = buildTimelineClipVisualStyle({
    widthPx: 180,
    trackColor: "#ff7a00",
    selected: true,
    muted: false,
    gain: 1,
    name: "Lead Vocal Very Long Name",
});

assertEqual(style.showGainKnob, true, "gain knob visible");
assertEqual(style.showGainLabel, true, "gain label visible");
assertEqual(style.showName, true, "name visible");
assertEqual(style.showMuteBadge, true, "mute badge visible");
assertEqual(style.headerFill.startsWith("rgba("), true, "header uses mixed rgba color");
assertEqual(style.bodyFill.startsWith("rgba("), true, "body uses mixed rgba color");
assertEqual(style.displayName.endsWith("..."), true, "name is ellipsized");
assertEqual(style.muteBadgeLabel, "M", "mute badge uses M label");
assertEqual(style.gainKnobAngleDeg, 0, "unity gain knob stays centered");
assertEqual(style.muteBadgeFill.startsWith("rgba("), true, "mute badge fill is resolved");
assertEqual(style.gainKnobIndicator.startsWith("rgba("), true, "gain knob indicator is resolved");
assertEqual(style.leadingControlsWidth, 46, "leading controls reserve prevents title overlap");
assertEqual(style.muteBadgeWidth, 18, "mute badge is enlarged");
assertEqual(style.gainKnobRadius, 6, "gain knob is enlarged");
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
