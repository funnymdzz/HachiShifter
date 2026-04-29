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
    playbackRate: number;
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
    muteBadgeOffsetX: number;
    muteBadgeOffsetY: number;
    gainKnobFill: string;
    gainKnobStroke: string;
    gainKnobIndicator: string;
    gainKnobCoreFill: string;
    gainKnobAngleDeg: number;
    gainKnobRadius: number;
    gainKnobCenterOffsetX: number;
    gainKnobCenterOffsetY: number;
    showPlaybackRate: boolean;
    playbackRateLabel: string;
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
    const headerRgb = mixHexColor(trackColor, { r: 160, g: 171, b: 183 }, 0.16);
    const bodyRgb = mixHexColor(trackColor, { r: 58, g: 63, b: 71 }, 0.68);
    const borderRgb = mixHexColor(trackColor, { r: 133, g: 144, b: 156 }, 0.3);
    const knobRgb = mixHexColor(trackColor, { r: 205, g: 212, b: 220 }, 0.24);
    const controlRgb = mixHexColor(trackColor, { r: 40, g: 46, b: 55 }, 0.52);
    const controlActiveRgb = mixHexColor(trackColor, { r: 120, g: 64, b: 69 }, 0.4);
    const { showMute, showGainKnob, showPlaybackRate, showGainLabel, showName } =
        resolveTimelineClipHeaderVisibility(args.widthPx);
    const textStartPx = showGainKnob ? (showMute ? 58 : 28) : showMute ? 34 : 8;
    const trailingReservePx = showGainLabel
        ? showPlaybackRate
            ? 124
            : 72
        : showGainKnob
          ? 26
          : 10;
    const maxChars = Math.max(1, Math.floor((args.widthPx - textStartPx - trailingReservePx) / 7));
    const gainDb = gainToDb(args.gain);
    const clampedGainDb = clamp(gainDb, -12, 12);
    const playbackRate = Number.isFinite(args.playbackRate) && args.playbackRate > 0 ? args.playbackRate : 1;
    const muteBadgeWidth = 20;
    const muteBadgeHeight = 14;
    const muteBadgeRadius = 4;
    const gainKnobRadius = 7;
    const gainKnobCenterOffsetX = 15;
    const gainKnobCenterOffsetY = 10;
    const muteBadgeOffsetX = showGainKnob ? 28 : 8;
    const muteBadgeOffsetY = 3;
    const leadingControlsWidth = showGainKnob ? (showMute ? 58 : 28) : showMute ? 34 : 8;

    return {
        headerFill: rgba(headerRgb, 0.95),
        bodyFill: rgba(bodyRgb, 0.74),
        borderStroke: rgba(borderRgb, 0.74),
        textFill: "rgba(241, 245, 249, 0.94)",
        muteBadgeFill: rgba(args.muted ? controlActiveRgb : controlRgb, args.muted ? 0.96 : 0.9),
        muteBadgeStroke: rgba(
            args.muted
                ? mixHexColor(trackColor, { r: 216, g: 187, b: 191 }, 0.26)
                : mixHexColor(trackColor, { r: 182, g: 193, b: 206 }, 0.18),
            args.muted ? 0.95 : 0.66,
        ),
        muteBadgeTextFill: args.muted ? "#fbebeb" : "rgba(244, 247, 250, 0.94)",
        muteBadgeLabel: "M",
        muteBadgeWidth,
        muteBadgeHeight,
        muteBadgeRadius,
        muteBadgeOffsetX,
        muteBadgeOffsetY,
        gainKnobFill: rgba(knobRgb, 0.94),
        gainKnobStroke: rgba(mixHexColor(trackColor, { r: 34, g: 40, b: 48 }, 0.46), 0.92),
        gainKnobIndicator: "rgba(248, 251, 255, 0.94)",
        gainKnobCoreFill: rgba(mixHexColor(trackColor, { r: 246, g: 250, b: 255 }, 0.38), 0.9),
        gainKnobAngleDeg: (clampedGainDb / 12) * 135,
        gainKnobRadius,
        gainKnobCenterOffsetX,
        gainKnobCenterOffsetY,
        showPlaybackRate,
        playbackRateLabel: `x${playbackRate.toFixed(2)}`,
        gainLabel: `${gainDb >= 0 ? "+" : ""}${gainDb.toFixed(1)}dB`,
        displayName: ellipsizeText(args.name, maxChars),
        mutedAlpha: args.muted ? 0.58 : 1,
        leadingControlsWidth,
        showMuteBadge: showMute,
        showGainKnob,
        showGainLabel,
        showName,
    };
}
