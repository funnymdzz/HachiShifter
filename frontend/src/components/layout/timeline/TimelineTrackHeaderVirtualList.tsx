import React from "react";

import type { TrackInfo } from "../../../features/session/sessionTypes";

export const TimelineTrackHeaderVirtualList: React.FC<{
    tracks: TrackInfo[];
    startIndex: number;
    endIndex: number;
    rowHeight: number;
    renderTrack: (track: TrackInfo) => React.ReactNode;
}> = ({ tracks, startIndex, endIndex, rowHeight, renderTrack }) => {
    const visibleTracks = tracks.slice(startIndex, endIndex + 1);

    return (
        <div style={{ position: "relative", height: tracks.length * rowHeight }}>
            <div style={{ transform: `translateY(${startIndex * rowHeight}px)` }}>
                {visibleTracks.map((track) => renderTrack(track))}
            </div>
        </div>
    );
};
