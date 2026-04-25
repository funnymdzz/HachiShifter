type VisibleTrack = {
    id: string;
    topPx: number;
};

type VisibleClip = {
    id: string;
    trackId: string;
    startSec: number;
    lengthSec: number;
};

export function buildTimelineHitTestIndex(args: {
    rowHeight: number;
    pxPerSec: number;
    visibleTracks: VisibleTrack[];
    visibleClips: VisibleClip[];
}): {
    rowHeight: number;
    pxPerSec: number;
    tracksById: Map<string, VisibleTrack>;
    clipsByTrackId: Map<string, VisibleClip[]>;
} {
    return {
        rowHeight: args.rowHeight,
        pxPerSec: args.pxPerSec,
        tracksById: new Map(args.visibleTracks.map((track) => [track.id, track] as const)),
        clipsByTrackId: new Map(
            args.visibleTracks.map((track) => [
                track.id,
                args.visibleClips.filter((clip) => clip.trackId === track.id),
            ]),
        ),
    };
}

export function hitTestTimeline(
    point: {
        screenX: number;
        screenY: number;
        scrollLeftPx: number;
        scrollTopPx: number;
    },
    index: ReturnType<typeof buildTimelineHitTestIndex>,
): {
    trackId: string | null;
    clipId: string | null;
    zone: "empty" | "body" | "trim_left";
} {
    const track = [...index.tracksById.values()].find((candidate) => {
        const topPx = candidate.topPx - point.scrollTopPx;
        return point.screenY >= topPx && point.screenY < topPx + index.rowHeight;
    });

    if (!track) {
        return {
            trackId: null,
            clipId: null,
            zone: "empty",
        };
    }

    const worldSec = (point.scrollLeftPx + point.screenX) / Math.max(1e-9, index.pxPerSec);
    const clip = (index.clipsByTrackId.get(track.id) ?? []).find(
        (candidate) =>
            worldSec >= candidate.startSec && worldSec <= candidate.startSec + candidate.lengthSec,
    );

    if (!clip) {
        return {
            trackId: track.id,
            clipId: null,
            zone: "empty",
        };
    }

    return {
        trackId: track.id,
        clipId: clip.id,
        zone: worldSec - clip.startSec <= 0.08 ? "trim_left" : "body",
    };
}
