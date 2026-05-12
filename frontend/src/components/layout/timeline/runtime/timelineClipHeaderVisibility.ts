export function resolveTimelineClipHeaderVisibility(
    widthPx: number,
    isPitchAdjustment?: boolean,
): {
    showAny: boolean;
    showChain: boolean;
    showMute: boolean;
    showFormant: boolean;
    showGainKnob: boolean;
    showPlaybackRate: boolean;
    showGainLabel: boolean;
    showName: boolean;
} {
    const width = Math.max(0, widthPx);

    if (isPitchAdjustment) {
        return {
            showAny: width >= 52,
            showChain: width >= 52,
            showMute: width >= 52,
            showFormant: false,
            showGainKnob: false,
            showGainLabel: width >= 96,
            showPlaybackRate: width >= 116,
            showName: width >= 152,
        };
    }

    return {
        showAny: width >= 32,
        showChain: width >= 36,
        showMute: width >= 52,
        showFormant: width >= 68,
        showGainKnob: width >= 32,
        showGainLabel: width >= 96,
        showPlaybackRate: width >= 116,
        showName: width >= 152,
    };
}
