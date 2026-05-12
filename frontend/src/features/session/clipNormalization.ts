export interface ClipNormalizationInput {
    sourcePath?: string;
    durationSec?: number;
    lengthSec: number;
    sourceStartSec?: number;
    sourceEndSec?: number;
    playbackRate?: number;
}

type InterleavedSlice = {
    interleaved: ArrayLike<number>;
};

const MIN_NORMALIZED_GAIN = dbToGain(-12);
const MAX_NORMALIZED_GAIN = dbToGain(12);

function dbToGain(db: number): number {
    return 10 ** (db / 20);
}

export function computeNormalizationGainFromInterleaved(data: ArrayLike<number>): number | null {
    let peak = 0;
    for (let i = 0; i < data.length; i += 1) {
        const value = Math.abs(Number(data[i]) || 0);
        if (value > peak) peak = value;
    }
    if (peak <= 0) return null;
    return Math.min(Math.max(1 / peak, MIN_NORMALIZED_GAIN), MAX_NORMALIZED_GAIN);
}

export function computeClipNormalizationGain(
    clip: ClipNormalizationInput,
    deps: {
        getInterleavedSlice: (
            sourcePath: string,
            channel: number,
            sourceStartSec: number,
            sourceSpanSec: number,
        ) => InterleavedSlice | null;
        releaseInterleaved: (data: ArrayLike<number>) => void;
    },
): number | null {
    if (!clip.sourcePath || !clip.durationSec || clip.durationSec <= 0) {
        return null;
    }

    const sourceStartSec = Number(clip.sourceStartSec ?? 0) || 0;
    const sourceEndSec = Number(clip.sourceEndSec ?? clip.durationSec) || clip.durationSec;
    const playbackRate = Math.max(1e-6, Number(clip.playbackRate ?? 1) || 1);
    const clipSourceSpanSec = Math.max(
        0,
        Math.min(clip.lengthSec * playbackRate, sourceEndSec - sourceStartSec),
    );
    if (clipSourceSpanSec <= 0) {
        return null;
    }

    const slice = deps.getInterleavedSlice(clip.sourcePath, 0, sourceStartSec, clipSourceSpanSec);
    if (!slice || slice.interleaved.length < 2) {
        return null;
    }

    try {
        return computeNormalizationGainFromInterleaved(slice.interleaved);
    } finally {
        deps.releaseInterleaved(slice.interleaved);
    }
}
