export function resolveWheelZoom(args: {
    anchorScreenX: number;
    anchorSec: number;
    nextPxPerSec: number;
}): {
    nextScrollLeftPx: number;
} {
    return {
        nextScrollLeftPx: args.anchorSec * args.nextPxPerSec - args.anchorScreenX,
    };
}

export function beginClipDrag(args: {
    clipId: string;
    trackId: string;
    startWorldSec: number;
    startTrackIndex: number;
}): {
    clipId: string;
    trackId: string;
    startWorldSec: number;
    startTrackIndex: number;
} {
    return { ...args };
}

export function updateClipDrag(
    drag: ReturnType<typeof beginClipDrag>,
    current: {
        currentWorldSec: number;
        currentTrackIndex: number;
    },
): {
    clipId: string;
    trackId: string;
    deltaSec: number;
    deltaTrackIndex: number;
} {
    return {
        clipId: drag.clipId,
        trackId: drag.trackId,
        deltaSec: current.currentWorldSec - drag.startWorldSec,
        deltaTrackIndex: current.currentTrackIndex - drag.startTrackIndex,
    };
}
