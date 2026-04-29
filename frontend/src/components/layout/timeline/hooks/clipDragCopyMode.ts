export function resolveClipDragCopyMode(args: {
    existingCopyMode: boolean;
    ctrlKey: boolean;
    metaKey: boolean;
    modifierActive: boolean;
}): boolean {
    return args.existingCopyMode || args.ctrlKey || args.metaKey || args.modifierActive;
}
