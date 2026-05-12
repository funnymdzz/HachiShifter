import { computeVisibleTrackWindow, sliceVisibleClipIds } from "./timelineWindowing.js";

export function buildTimelineRenderModel(args: {
    tracks: Array<{ id: string }>;
    clips: Array<{
        id: string;
        trackId: string;
        startSec: number;
        lengthSec: number;
    }>;
    viewportStartSec: number;
    viewportEndSec: number;
    rowHeight: number;
    scrollTopPx: number;
    viewportHeightPx: number;
}): {
    startIndex: number;
    endIndex: number;
    visibleTrackIds: string[];
    visibleClipIdsByTrackId: Record<string, string[]>;
} {
    const visibleTrackWindow = computeVisibleTrackWindow({
        totalTracks: args.tracks.length,
        rowHeight: args.rowHeight,
        scrollTopPx: args.scrollTopPx,
        viewportHeightPx: args.viewportHeightPx,
        overscanRows: 1,
    });

    const visibleTrackIds = args.tracks
        .slice(visibleTrackWindow.startIndex, visibleTrackWindow.endIndex + 1)
        .map((track) => track.id);
    const clipsByTrackId = new Map<string, typeof args.clips>();

    for (const clip of args.clips) {
        const next = clipsByTrackId.get(clip.trackId);
        if (next) {
            next.push(clip);
        } else {
            clipsByTrackId.set(clip.trackId, [clip]);
        }
    }

    const visibleClipIdsByTrackId = Object.fromEntries(
        visibleTrackIds.map((trackId) => [
            trackId,
            sliceVisibleClipIds({
                viewportStartSec: args.viewportStartSec,
                viewportEndSec: args.viewportEndSec,
                bufferSec: 1.5,
                clips: clipsByTrackId.get(trackId) ?? [],
            }),
        ]),
    ) as Record<string, string[]>;

    return {
        ...visibleTrackWindow,
        visibleTrackIds,
        visibleClipIdsByTrackId,
    };
}
