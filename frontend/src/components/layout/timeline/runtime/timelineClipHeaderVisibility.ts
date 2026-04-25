export function resolveTimelineClipHeaderVisibility(widthPx: number): {
    showAny: boolean;
    showMute: boolean;
    showGainKnob: boolean;
    showGainLabel: boolean;
    showName: boolean;
} {
    const width = Math.max(0, widthPx);

    return {
        showAny: width >= 32,
        showMute: width >= 32,
        showGainKnob: width >= 52,
        showGainLabel: width >= 80,
        showName: width >= 120,
    };
}
