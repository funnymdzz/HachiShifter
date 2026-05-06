import { clamp, dbToGain, gainToDb } from "../math";

type BulkEditableArgs = {
    activeClipId: string;
    multiSelectedClipIds: string[];
    multiSelectedSet: Set<string>;
};

type ClipGainLike = {
    gain?: number;
};

type ClipLengthLike = {
    lengthSec?: number;
};

export function getBulkEditableClipIds(args: BulkEditableArgs): string[] {
    const { activeClipId, multiSelectedClipIds, multiSelectedSet } = args;
    if (multiSelectedClipIds.length > 0 && multiSelectedSet.has(activeClipId)) {
        return [...multiSelectedClipIds];
    }
    return [activeClipId];
}

export function applyBulkFadeValue(args: {
    clipIds: string[];
    clipsById: Map<string, ClipLengthLike>;
    target: "fadeInSec" | "fadeOutSec";
    nextValue: number;
}): Array<{ clipId: string; fadeInSec?: number; fadeOutSec?: number }> {
    const { clipIds, clipsById, target, nextValue } = args;
    return clipIds.flatMap((clipId) => {
        const clip = clipsById.get(clipId);
        if (!clip) return [];
        const lengthSec = Math.max(0, Number(clip.lengthSec ?? 0) || 0);
        const value = clamp(nextValue, 0, lengthSec);
        return [
            target === "fadeInSec" ? { clipId, fadeInSec: value } : { clipId, fadeOutSec: value },
        ];
    });
}

export function applyBulkGainDeltaDb(args: {
    clipIds: string[];
    clipsById: Map<string, ClipGainLike>;
    deltaDb: number;
    minDb: number;
    maxDb: number;
}): Array<{ clipId: string; gain: number }> {
    const { clipIds, clipsById, deltaDb, minDb, maxDb } = args;
    return clipIds.flatMap((clipId) => {
        const clip = clipsById.get(clipId);
        if (!clip) return [];
        const baseGain = Number(clip.gain ?? 1) || 1;
        const nextDb = clamp(gainToDb(baseGain) + deltaDb, minDb, maxDb);
        const gain = clamp(dbToGain(nextDb), dbToGain(minDb), dbToGain(maxDb));
        return [{ clipId, gain }];
    });
}
