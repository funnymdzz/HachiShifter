/**
 * Parameter editor playhead seek mapping helpers.
 *
 * Converts viewport client X coordinates to timeline seconds using pxPerSec
 * directly, avoiding transient BPM-related beat/sec conversion mismatches.
 */

export function secFromViewportClientX(input: {
    clientX: number;
    viewportLeft: number;
    scrollLeft: number;
    pxPerSec: number;
}): number {
    const { clientX, viewportLeft, scrollLeft, pxPerSec } = input;
    const safePxPerSec = Number.isFinite(pxPerSec) && pxPerSec > 1e-9 ? pxPerSec : 1e-9;
    const absoluteX = clientX - viewportLeft + scrollLeft;
    return Math.max(0, absoluteX / safePxPerSec);
}
