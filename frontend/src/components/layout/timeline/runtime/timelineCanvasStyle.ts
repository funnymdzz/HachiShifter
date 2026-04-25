import { gainToDb } from "../math.js";
import { resolveTimelineClipHeaderVisibility } from "./timelineClipHeaderVisibility.js";

function clamp(value: number, min: number, max: number): number {
    return Math.min(max, Math.max(min, value));
}

function parseHexColor(color: string): { r: number; g: number; b: number } | null {
    if (!color.startsWith("#")) return null;
    const hex = color.slice(1);
    const normalized =
        hex.length === 3
            ? hex
                  .split("")
                  .map((part) => part + part)
                  .join("")
            : hex;
    if (normalized.length !== 6) return null;
    return {
        r: Number.parseInt(normalized.slice(0, 2), 16),
        g: Number.parseInt(normalized.slice(2, 4), 16),
        b: Number.parseInt(normalized.slice(4, 6), 16),
    };
}

function mixHexColor(
    color: string,
    target: { r: number; g: number; b: number },
    ratio: number,
): { r: number; g: number; b: number } {
    const base = parseHexColor(color) ?? { r: 104, g: 131, b: 157 };
    const t = clamp(ratio, 0, 1);
    return {
        r: Math.round(base.r * (1 - t) + target.r * t),
        g: Math.round(base.g * (1 - t) + target.g * t),
        b: Math.round(base.b * (1 - t) + target.b * t),
    };
}

function rgba(rgb: { r: number; g: number; b: number }, alpha: number): string {
    return `rgba(${rgb.r}, ${rgb.g}, ${rgb.b}, ${alpha})`;
}

function ellipsizeText(text: string, maxChars: number): string {
    if (maxChars <= 0) return "";
    if (text.length <= maxChars) return text;
    if (maxChars <= 3) return ".".repeat(maxChars);
    return `${text.slice(0, maxChars - 3)}...`;
}

export function computeTimelineFadeShadeRange(args: {
    widthPx: number;
    fadeInPx: number;
    fadeOutPx: number;
}): {
    startPx: number;
    endPx: number;
} | null {
    const widthPx = Math.max(1, args.widthPx);
    const startPx = clamp(args.fadeInPx, 0, widthPx);
    const endPx = clamp(widthPx - args.fadeOutPx, 0, widthPx);
    if (endPx <= startPx) return null;
    return { startPx, endPx };
}

export function buildTimelineClipVisualStyle(args: {
    widthPx: number;
    trackColor?: string;
    selected: boolean;
    muted: boolean;
    gain: number;
    name: string;
}): {
    headerFill: string;
    bodyFill: string;
    borderStroke: string;
    textFill: string;
    muteBadgeFill: string;
    muteBadgeStroke: string;
    muteBadgeTextFill: string;
    muteBadgeLabel: string;
    muteBadgeWidth: number;
    muteBadgeHeight: number;
    muteBadgeRadius: number;
    gainKnobFill: string;
    gainKnobStroke: string;
    gainKnobIndicator: string;
    gainKnobCoreFill: string;
    gainKnobAngleDeg: number;
    gainKnobRadius: number;
    gainKnobCenterOffsetX: number;
    gainKnobCenterOffsetY: number;
    gainLabel: string;
    displayName: string;
    mutedAlpha: number;
    leadingControlsWidth: number;
    showMuteBadge: boolean;
    showGainKnob: boolean;
    showGainLabel: boolean;
    showName: boolean;
} {
    const trackColor = args.trackColor ?? "#68839d";
    const headerRgb = mixHexColor(trackColor, { r: 255, g: 255, b: 255 }, 0.1);
    const bodyRgb = mixHexColor(trackColor, { r: 30, g: 38, b: 48 }, 0.34);
    const knobRgb = mixHexColor(trackColor, { r: 255, g: 255, b: 255 }, 0.28);
    const controlRgb = mixHexColor(trackColor, { r: 14, g: 18, b: 24 }, 0.58);
    const controlActiveRgb = mixHexColor(trackColor, { r: 168, g: 38, b: 46 }, 0.72);
    const { showMute, showGainKnob, showGainLabel, showName } =
        resolveTimelineClipHeaderVisibility(args.widthPx);
    const textStartPx = showMute ? 28 : 8;
    const trailingReservePx = showGainLabel ? 72 : showGainKnob ? 26 : 10;
    const maxChars = Math.max(1, Math.floor((args.widthPx - textStartPx - trailingReservePx) / 7));
    const gainDb = gainToDb(args.gain);
    const clampedGainDb = clamp(gainDb, -12, 12);
    const muteBadgeWidth = 18;
    const muteBadgeHeight = 13;
    const muteBadgeRadius = 3;
    const gainKnobRadius = 6;
    const gainKnobCenterOffsetX = showMute ? 35 : 17;
    const gainKnobCenterOffsetY = 9;
    const leadingControlsWidth = showMute
        ? showGainKnob
            ? 46
            : 28
        : showGainKnob
          ? 24
          : 8;

    return {
        headerFill: rgba(headerRgb, 0.94),
        bodyFill: rgba(bodyRgb, 0.38),
        borderStroke: rgba(headerRgb, 0.78),
        textFill: "rgba(241, 246, 250, 0.94)",
        muteBadgeFill: rgba(args.muted ? controlActiveRgb : controlRgb, args.muted ? 0.96 : 0.84),
        muteBadgeStroke: rgba(
            args.muted
                ? mixHexColor(trackColor, { r: 255, g: 214, b: 214 }, 0.25)
                : mixHexColor(trackColor, { r: 255, g: 255, b: 255 }, 0.18),
            args.muted ? 0.95 : 0.62,
        ),
        muteBadgeTextFill: args.muted ? "#fff1f1" : "rgba(244, 247, 250, 0.94)",
        muteBadgeLabel: "M",
        muteBadgeWidth,
        muteBadgeHeight,
        muteBadgeRadius,
        gainKnobFill: rgba(knobRgb, 0.96),
        gainKnobStroke: rgba(mixHexColor(trackColor, { r: 18, g: 24, b: 32 }, 0.35), 0.92),
        gainKnobIndicator: "rgba(248, 251, 255, 0.94)",
        gainKnobCoreFill: rgba(mixHexColor(trackColor, { r: 255, g: 255, b: 255 }, 0.42), 0.9),
        gainKnobAngleDeg: (clampedGainDb / 12) * 135,
        gainKnobRadius,
        gainKnobCenterOffsetX,
        gainKnobCenterOffsetY,
        gainLabel: `${gainDb >= 0 ? "+" : ""}${gainDb.toFixed(1)}dB`,
        displayName: ellipsizeText(args.name, maxChars),
        mutedAlpha: args.muted ? 0.52 : 1,
        leadingControlsWidth,
        showMuteBadge: showMute,
        showGainKnob,
        showGainLabel,
        showName,
    };
}
