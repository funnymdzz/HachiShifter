export type ClipFormantStatus = "ready" | "rebuilding" | "failed";

export interface ClipFormantButtonStyleInput {
    baseBackgroundColor: string;
    baseBorderColor: string;
    baseTextColor: string;
    enabled: boolean;
    status: ClipFormantStatus;
}

export interface ClipFormantButtonStyle {
    backgroundColor: string;
    borderColor: string;
    color: string;
}

function mix(tint: string, base: string, tintWeight: number): string {
    const clampedWeight = Math.max(0, Math.min(100, tintWeight));
    return `color-mix(in oklab, ${tint} ${clampedWeight}%, ${base} ${100 - clampedWeight}%)`;
}

export function getClipFormantButtonStyle(
    input: ClipFormantButtonStyleInput,
): ClipFormantButtonStyle {
    const { baseBackgroundColor, baseBorderColor, baseTextColor, enabled, status } = input;

    if (status === "failed") {
        return {
            backgroundColor: mix("var(--qt-danger-bg)", baseBackgroundColor, 72),
            borderColor: mix("var(--qt-danger-border)", baseBorderColor, 62),
            color: mix("var(--qt-danger-text)", baseTextColor, 78),
        };
    }

    if (status === "rebuilding") {
        return {
            backgroundColor: mix("var(--qt-warning-bg)", baseBackgroundColor, 68),
            borderColor: mix("var(--qt-warning-border)", baseBorderColor, 58),
            color: mix("var(--qt-warning-text)", baseTextColor, 72),
        };
    }

    if (enabled) {
        return {
            backgroundColor: mix("var(--qt-highlight)", baseBackgroundColor, 26),
            borderColor: mix("var(--qt-highlight)", baseBorderColor, 32),
            color: mix("white", baseTextColor, 28),
        };
    }

    return {
        backgroundColor: baseBackgroundColor,
        borderColor: baseBorderColor,
        color: baseTextColor,
    };
}
