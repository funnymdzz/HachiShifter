export function buildTimelinePerfScenario(args: {
    trackCount: number;
    clipsPerTrack: number;
}): {
    tracks: Array<{
        id: string;
        name: string;
    }>;
    clips: Array<{
        id: string;
        trackId: string;
        startSec: number;
        lengthSec: number;
    }>;
} {
    const tracks = Array.from({ length: args.trackCount }, (_, index) => ({
        id: `track-${index}`,
        name: `Track ${index + 1}`,
    }));

    const clips = tracks.flatMap((track, trackIndex) =>
        Array.from({ length: args.clipsPerTrack }, (_, clipIndex) => ({
            id: `${track.id}-clip-${clipIndex}`,
            trackId: track.id,
            startSec: clipIndex * 1.5 + (trackIndex % 4) * 0.1,
            lengthSec: 1.2,
        })),
    );

    return { tracks, clips };
}
