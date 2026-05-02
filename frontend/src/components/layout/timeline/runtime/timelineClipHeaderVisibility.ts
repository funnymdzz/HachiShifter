export function resolveTimelineClipHeaderVisibility(widthPx: number): {
    showAny: boolean;
    showMute: boolean;
    showFormant: boolean;
    showGainKnob: boolean;
    showPlaybackRate: boolean;
    showGainLabel: boolean;
    showName: boolean;
} {
    const width = Math.max(0, widthPx);

    return {
        showAny: width >= 32,
        showMute: width >= 52,
        showFormant: width >= 68,
        showGainKnob: width >= 32,
        showGainLabel: width >= 96,
        showPlaybackRate: width >= 116,
        showName: width >= 152,
    };
}
