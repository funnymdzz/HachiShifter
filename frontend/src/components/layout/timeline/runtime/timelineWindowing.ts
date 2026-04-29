export function computeVisibleTrackWindow(args: {
    totalTracks: number;
    rowHeight: number;
    scrollTopPx: number;
    viewportHeightPx: number;
    overscanRows: number;
}): { startIndex: number; endIndex: number } {
    const firstVisibleIndex = Math.floor(args.scrollTopPx / Math.max(1, args.rowHeight));
    const visibleCount = Math.ceil(args.viewportHeightPx / Math.max(1, args.rowHeight));

    return {
        startIndex: Math.max(0, firstVisibleIndex - args.overscanRows),
        endIndex: Math.min(
            args.totalTracks - 1,
            firstVisibleIndex + visibleCount + args.overscanRows,
        ),
    };
}

export function sliceVisibleClipIds(args: {
    viewportStartSec: number;
    viewportEndSec: number;
    bufferSec: number;
    clips: Array<{
        id: string;
        startSec: number;
        lengthSec: number;
    }>;
}): string[] {
    const minSec = args.viewportStartSec - args.bufferSec;
    const maxSec = args.viewportEndSec + args.bufferSec;

    return args.clips
        .filter((clip) => clip.startSec + clip.lengthSec >= minSec && clip.startSec <= maxSec)
        .map((clip) => clip.id);
}
