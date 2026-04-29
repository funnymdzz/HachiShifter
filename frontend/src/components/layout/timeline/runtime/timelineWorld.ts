export type TimelineWorld = {
    pxPerSec: number;
    rowHeight: number;
    scrollLeftPx: number;
    scrollTopPx: number;
};

export function createTimelineWorld(world: TimelineWorld): TimelineWorld {
    return { ...world };
}

export function screenXToWorldSec(screenX: number, world: TimelineWorld): number {
    return (world.scrollLeftPx + screenX) / Math.max(1e-9, world.pxPerSec);
}

export function worldSecToScreenX(worldSec: number, world: TimelineWorld): number {
    return worldSec * world.pxPerSec - world.scrollLeftPx;
}

export function computeAnchoredScrollLeftPx(
    args: {
        anchorSec: number;
        anchorScreenX: number;
        nextPxPerSec: number;
    },
    world: TimelineWorld,
): number {
    void world;
    return args.anchorSec * args.nextPxPerSec - args.anchorScreenX;
}

export function computeAnchoredScrollTopPx(
    args: {
        anchorTrackUnit: number;
        anchorScreenY: number;
        nextRowHeight: number;
    },
    world: TimelineWorld,
): number {
    void world;
    return args.anchorTrackUnit * args.nextRowHeight - args.anchorScreenY;
}

export function computeWorldDragDelta(
    args: {
        startScreenX: number;
        startScreenY: number;
        currentScreenX: number;
        currentScreenY: number;
    },
    world: TimelineWorld,
): {
    deltaSec: number;
    deltaTrackUnits: number;
} {
    return {
        deltaSec: (args.currentScreenX - args.startScreenX) / Math.max(1e-9, world.pxPerSec),
        deltaTrackUnits:
            (args.currentScreenY - args.startScreenY) / Math.max(1e-9, world.rowHeight),
    };
}
