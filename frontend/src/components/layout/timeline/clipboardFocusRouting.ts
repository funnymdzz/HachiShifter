export function shouldRouteClipPasteToParamEditor(args: {
    inPianoRoll: boolean;
    inTrackHeader: boolean;
}): boolean {
    void args.inTrackHeader;
    return args.inPianoRoll;
}
