export const CLIP_FORMANT_ACTIVE_ATTR = "data-hs-clip-formant-tool-active";
export const CLIP_FORMANT_FOCUS_WINDOW = "clipFormant";

export function formantChartPointerDownShouldPreventDefault(params: {
    disabled: boolean;
}): boolean {
    return !params.disabled;
}

export function shouldBlockPlaybackToggleForFormantWindow(params: {
    key: string;
    formantToolActive: boolean;
    focusWindow: string | null;
}): boolean {
    return (
        params.formantToolActive &&
        params.focusWindow === CLIP_FORMANT_FOCUS_WINDOW &&
        params.key === "space"
    );
}

export function shouldSuppressFormantToolSpaceDefault(params: {
    code: string;
    key: string;
}): boolean {
    return params.code === "Space" || params.key === " ";
}
