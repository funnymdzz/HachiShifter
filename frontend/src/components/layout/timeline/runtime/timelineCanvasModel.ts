import { CLIP_BODY_PADDING_Y, CLIP_HEADER_HEIGHT } from "../constants.js";

type SparseRenderClip = {
    id: string;
    trackId: string;
    name: string;
    startSec: number;
    lengthSec: number;
    gain: number;
    muted: boolean;
    fadeInSec: number;
    fadeOutSec: number;
    fadeInCurve: "linear" | "sine" | "exponential" | "logarithmic" | "scurve";
    fadeOutCurve: "linear" | "sine" | "exponential" | "logarithmic" | "scurve";
};

export type TimelineCanvasClipModel = {
    id: string;
    trackId: string;
    name: string;
    leftPx: number;
    topPx: number;
    widthPx: number;
    heightPx: number;
    headerHeightPx: number;
    fadeInPx: number;
    fadeOutPx: number;
    fadeInCurve: "linear" | "sine" | "exponential" | "logarithmic" | "scurve";
    fadeOutCurve: "linear" | "sine" | "exponential" | "logarithmic" | "scurve";
    selected: boolean;
    muted: boolean;
    gain: number;
    trackColor?: string;
};

export function buildSparseClipRenderModel(args: {
    visibleTracks: Array<{ id: string; color?: string }>;
    visibleTrackClipsById: Record<string, SparseRenderClip[]>;
    pxPerSec: number;
    rowHeight: number;
    scrollLeft: number;
    selectedClipId: string | null;
    multiSelectedClipIds: string[];
    renamingClipId: string | null;
    hoveredClipId?: string | null;
}): {
    drawClips: TimelineCanvasClipModel[];
    overlayClipIdsByTrackId: Record<string, string[]>;
} {
    const overlayClipIds = new Set<string>();
    if (args.renamingClipId) {
        overlayClipIds.add(args.renamingClipId);
    }
    if (args.hoveredClipId) {
        overlayClipIds.add(args.hoveredClipId);
    }
    if (args.multiSelectedClipIds.length > 0) {
        for (const clipId of args.multiSelectedClipIds) {
            overlayClipIds.add(clipId);
        }
    } else if (args.selectedClipId) {
        overlayClipIds.add(args.selectedClipId);
    }

    const multiSelectedSet =
        args.multiSelectedClipIds.length > 0 ? new Set(args.multiSelectedClipIds) : null;

    const drawClips = args.visibleTracks.flatMap((track, visibleIndex) =>
        (args.visibleTrackClipsById[track.id] ?? []).map((clip) => ({
            id: clip.id,
            trackId: clip.trackId,
            name: clip.name,
            leftPx: clip.startSec * args.pxPerSec - args.scrollLeft,
            topPx: visibleIndex * args.rowHeight,
            widthPx: Math.max(1, clip.lengthSec * args.pxPerSec),
            heightPx: Math.max(1, args.rowHeight - CLIP_BODY_PADDING_Y),
            headerHeightPx: CLIP_HEADER_HEIGHT,
            fadeInPx: Math.max(0, clip.fadeInSec * args.pxPerSec),
            fadeOutPx: Math.max(0, clip.fadeOutSec * args.pxPerSec),
            fadeInCurve: clip.fadeInCurve,
            fadeOutCurve: clip.fadeOutCurve,
            selected:
                multiSelectedSet != null
                    ? multiSelectedSet.has(clip.id)
                    : args.selectedClipId === clip.id,
            muted: clip.muted,
            gain: clip.gain,
            trackColor: track.color,
        })),
    );

    const overlayClipIdsByTrackId = Object.fromEntries(
        args.visibleTracks.map((track) => [
            track.id,
            (args.visibleTrackClipsById[track.id] ?? [])
                .filter((clip) => overlayClipIds.has(clip.id))
                .map((clip) => clip.id),
        ]),
    ) as Record<string, string[]>;

    return {
        drawClips,
        overlayClipIdsByTrackId,
    };
}
