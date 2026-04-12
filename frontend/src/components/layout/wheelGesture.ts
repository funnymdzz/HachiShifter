const WHEEL_AXIS_EPSILON = 0.5;
const VIBRATO_TOUCHPAD_DELTA_THRESHOLD = 220;
const VIBRATO_TOUCHPAD_FREQUENCY_AXIS_RATIO = 0.75;

function isLikelyDiscreteWheelStep(absDelta: number): boolean {
    const rounded = Math.round(absDelta);
    if (Math.abs(absDelta - rounded) > WHEEL_AXIS_EPSILON) {
        return false;
    }

    return rounded % 100 === 0 || rounded % 120 === 0;
}

export type ParamEditorWheelAction =
    | "free-scroll"
    | "horizontal-scroll"
    | "vertical-pan"
    | "vertical-zoom"
    | "horizontal-zoom";

export type TimelineWheelAction =
    | "free-scroll"
    | "horizontal-scroll"
    | "vertical-scroll"
    | "vertical-zoom"
    | "horizontal-zoom"
    | "native";

export type VibratoDragWheelTarget = "amplitude" | "frequency" | "none";

function isLikelyTouchpadWheelGesture(input: {
    deltaX: number;
    deltaY: number;
    deltaMode: number;
}): boolean {
    const absX = Math.abs(input.deltaX);
    const absY = Math.abs(input.deltaY);

    if (absX > WHEEL_AXIS_EPSILON) {
        return true;
    }

    if (input.deltaMode !== 0) {
        return false;
    }

    if (absY <= WHEEL_AXIS_EPSILON) {
        return false;
    }

    if (!Number.isInteger(input.deltaY)) {
        return true;
    }

    if (isLikelyDiscreteWheelStep(absY)) {
        return false;
    }

    return absY <= VIBRATO_TOUCHPAD_DELTA_THRESHOLD;
}

export function getVibratoDragWheelTarget(input: {
    deltaX: number;
    deltaY: number;
    deltaMode: number;
    amplitudeRequested: boolean;
    frequencyRequested: boolean;
}): VibratoDragWheelTarget {
    const absX = Math.abs(input.deltaX);
    const absY = Math.abs(input.deltaY);

    if (absX <= WHEEL_AXIS_EPSILON && absY <= WHEEL_AXIS_EPSILON) {
        return "none";
    }

    // Touchpad gestures do not require modifiers while dragging with line/vibrato tool.
    if (isLikelyTouchpadWheelGesture(input)) {
        if (absX > WHEEL_AXIS_EPSILON && absX >= absY * VIBRATO_TOUCHPAD_FREQUENCY_AXIS_RATIO) {
            return "frequency";
        }
        return "amplitude";
    }

    if (input.frequencyRequested) {
        return "frequency";
    }

    if (input.amplitudeRequested) {
        return "amplitude";
    }

    return "none";
}

export function getWheelGestureAxis(input: {
    deltaX: number;
    deltaY: number;
}): "horizontal" | "vertical" {
    const absX = Math.abs(input.deltaX);
    const absY = Math.abs(input.deltaY);

    if (absX > WHEEL_AXIS_EPSILON && absX > absY) {
        return "horizontal";
    }

    return "vertical";
}

export function getParamEditorWheelAction(input: {
    deltaX: number;
    deltaY: number;
    horizontalScrollRequested: boolean;
    verticalPanRequested: boolean;
    verticalZoomRequested: boolean;
    horizontalZoomRequested: boolean;
}): ParamEditorWheelAction {
    if (input.horizontalScrollRequested || input.verticalPanRequested) {
        return "free-scroll";
    }

    if (input.horizontalScrollRequested) {
        return "horizontal-scroll";
    }

    const axis = getWheelGestureAxis(input);
    if (axis === "horizontal") {
        return "horizontal-scroll";
    }

    if (input.verticalPanRequested) {
        return "vertical-pan";
    }

    if (input.verticalZoomRequested) {
        return "vertical-zoom";
    }

    if (input.horizontalZoomRequested) {
        return "horizontal-zoom";
    }

    return "horizontal-zoom";
}

export function getTimelineWheelAction(input: {
    deltaX: number;
    deltaY: number;
    horizontalScrollRequested: boolean;
    verticalScrollRequested: boolean;
    verticalZoomRequested: boolean;
    horizontalZoomRequested: boolean;
}): TimelineWheelAction {
    if (input.horizontalScrollRequested || input.verticalScrollRequested) {
        return "free-scroll";
    }

    if (input.horizontalScrollRequested) {
        return "horizontal-scroll";
    }

    const axis = getWheelGestureAxis(input);
    if (axis === "horizontal") {
        return "horizontal-scroll";
    }

    if (input.verticalScrollRequested) {
        return "vertical-scroll";
    }

    if (input.verticalZoomRequested) {
        return "vertical-zoom";
    }

    if (input.horizontalZoomRequested) {
        return "horizontal-zoom";
    }

    return "native";
}
