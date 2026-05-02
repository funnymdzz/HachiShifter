type ReferenceTrackLike = {
    id: string;
    name: string;
    parentId?: string | null;
    color?: string | null;
};

function parseHexColor(input: string): { r: number; g: number; b: number } | null {
    const normalized = input.trim();
    const match = normalized.match(/^#([0-9a-fA-F]{6})$/);
    if (!match) return null;
    const hex = match[1];
    return {
        r: Number.parseInt(hex.slice(0, 2), 16),
        g: Number.parseInt(hex.slice(2, 4), 16),
        b: Number.parseInt(hex.slice(4, 6), 16),
    };
}

export function buildReferencePitchStrokeColor(
    trackColor: string | null | undefined,
    highlighted: boolean,
): string {
    const rgb = parseHexColor(trackColor ?? "") ?? { r: 120, g: 160, b: 220 };
    return `rgba(${rgb.r}, ${rgb.g}, ${rgb.b}, ${highlighted ? 1 : 0.85})`;
}

export function listReferenceRootTracks(args: {
    tracks: ReferenceTrackLike[];
    currentRootTrackId: string | null;
}): Array<{ id: string; name: string; color: string | null }> {
    const { tracks, currentRootTrackId } = args;
    return tracks
        .filter((track) => !track.parentId && track.id !== currentRootTrackId)
        .map((track) => ({
            id: track.id,
            name: track.name,
            color: track.color ?? null,
        }));
}

export function cleanupVisibleReferenceRootTrackIds(args: {
    tracks: ReferenceTrackLike[];
    currentRootTrackId: string | null;
    visibleReferenceRootTrackIds: string[];
}): string[] {
    const validIds = new Set(
        listReferenceRootTracks({
            tracks: args.tracks,
            currentRootTrackId: args.currentRootTrackId,
        }).map((track) => track.id),
    );

    return args.visibleReferenceRootTrackIds.filter((trackId) => validIds.has(trackId));
}
