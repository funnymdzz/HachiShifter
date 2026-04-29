export function resolveTimelineFadeDisplay(args: { hovered: boolean }): {
    showGuide: boolean;
    showCurve: boolean;
    guideOpacity: number;
    curveOpacity: number;
} {
    return {
        showGuide: true,
        showCurve: true,
        guideOpacity: args.hovered ? 0.72 : 0.5,
        curveOpacity: args.hovered ? 0.9 : 0.58,
    };
}
