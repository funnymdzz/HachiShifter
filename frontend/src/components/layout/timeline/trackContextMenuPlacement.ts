export function getInsertBelowTargetIndex(
    tracks: Array<{ id: string }>,
    anchorTrackId: string,
): number {
    const anchorIndex = tracks.findIndex((track) => track.id === anchorTrackId);
    if (anchorIndex < 0) {
        return tracks.length;
    }
    return anchorIndex + 1;
}
