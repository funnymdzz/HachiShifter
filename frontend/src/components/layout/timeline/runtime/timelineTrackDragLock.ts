function clamp(value: number, min: number, max: number): number {
    return Math.min(max, Math.max(min, value));
}

export function computeTimelineTrackDragLockThresholdPx(pxPerSec: number): number {
    const scaleAware = Number.isFinite(pxPerSec) ? pxPerSec * 0.06 : 24;
    return Math.round(clamp(scaleAware, 24, 32));
}

export function computeTimelineTrackDragLock(args: {
    initialTrackId: string;
    hoveredTrackId: string | null;
    horizontalDeltaPx: number;
    thresholdPx: number;
}): {
    locked: boolean;
    lockedTrackId: string | null;
} {
    if (!args.hoveredTrackId || args.hoveredTrackId === args.initialTrackId) {
        return { locked: false, lockedTrackId: null };
    }
    if (args.horizontalDeltaPx >= args.thresholdPx) {
        return { locked: false, lockedTrackId: null };
    }
    return {
        locked: true,
        lockedTrackId: args.hoveredTrackId,
    };
}
