export function resolveTimelineMinPxPerSec(args: {
    baseMinPxPerSec: number;
    projectSec: number;
    viewportWidthPx: number;
}): number {
    const base = Math.max(0.5, args.baseMinPxPerSec);
    const projectSec = Math.max(1, args.projectSec);
    const viewportWidthPx = Math.max(1, args.viewportWidthPx);

    if (projectSec * base <= viewportWidthPx) {
        return 0.5;
    }

    return base;
}
