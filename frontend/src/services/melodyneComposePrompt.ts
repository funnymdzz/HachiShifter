export interface MelodyneTrackChoice {
    index: number;
    name: string;
    suggestedCompose: boolean;
    elementCount: number;
}

export function requestMelodyneComposeSelection(
    tracks: MelodyneTrackChoice[],
): Promise<{
    composeTrackIndices: number[];
    processingOrder: "note_first" | "track_first";
    pitchSource: "project" | "game_fcpe";
}> {
    return new Promise((resolve) => {
        window.dispatchEvent(
            new CustomEvent("hachi:melodyne-compose-selection", {
                detail: { tracks, resolve },
            }),
        );
    });
}
