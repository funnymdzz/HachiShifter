export function resolveQuickExportClipIds(args: {
    contextClipId: string;
    multiSelectedClipIds: string[];
}): string[] {
    const { contextClipId, multiSelectedClipIds } = args;
    if (multiSelectedClipIds.length >= 2 && multiSelectedClipIds.includes(contextClipId)) {
        return [...multiSelectedClipIds];
    }
    return [contextClipId];
}

export function buildQuickExportFileName(projectName: string): string {
    const normalized = projectName.trim();
    if (!normalized) return "quick_export.wav";
    return `${normalized}_quick_export.wav`;
}
