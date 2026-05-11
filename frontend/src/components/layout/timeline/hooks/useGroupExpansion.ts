import type { ClipInfo } from "../../../../features/session/sessionTypes";

/**
 * If anchorClipId belongs to a group, return all clip IDs in that group.
 * Otherwise return undefined (caller falls back to multi-select or single clip).
 */
export function getGroupClipIds(
    anchorClipId: string,
    clips: ClipInfo[],
    disabledGroupIds?: string[],
): string[] | undefined {
    const anchor = clips.find((c) => c.id === anchorClipId);
    if (!anchor?.groupId) return undefined;
    if (disabledGroupIds?.includes(anchor.groupId)) return undefined;
    return clips.filter((c) => c.groupId === anchor.groupId).map((c) => c.id);
}

/**
 * Expand a set of clip IDs to include all group members.
 * For each input clip ID that belongs to a non-disabled group,
 * all clips in that group are included in the result.
 */
export function expandClipIdsWithGroups(
    clipIds: string[],
    clips: Array<{ id: string; groupId?: string }>,
    ignoreGrouping: boolean,
    disabledGroupIds?: string[],
): string[] {
    if (ignoreGrouping || clipIds.length === 0) return [...clipIds];

    const expandedSet = new Set(clipIds);
    const processedGroups = new Set<string>();

    for (const clipId of clipIds) {
        const clip = clips.find((c) => c.id === clipId);
        if (!clip?.groupId) continue;
        if (disabledGroupIds?.includes(clip.groupId)) continue;
        if (processedGroups.has(clip.groupId)) continue;
        processedGroups.add(clip.groupId);

        for (const c of clips) {
            if (c.groupId === clip.groupId) {
                expandedSet.add(c.id);
            }
        }
    }

    return [...expandedSet];
}
