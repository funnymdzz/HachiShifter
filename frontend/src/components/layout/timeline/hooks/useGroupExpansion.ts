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
