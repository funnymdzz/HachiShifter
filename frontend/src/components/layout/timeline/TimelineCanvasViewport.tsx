import React from "react";

import { drawTimelineCanvas } from "./runtime/timelineCanvasRenderer";
import type { TimelineCanvasClipModel } from "./runtime/timelineCanvasModel";

export const TimelineCanvasViewport: React.FC<{
    width: number;
    height: number;
    model: {
        drawClips: TimelineCanvasClipModel[];
        playheadX: number;
    };
    onPointerDown?: (payload: {
        screenX: number;
        screenY: number;
        clientX: number;
        clientY: number;
    }) => void;
}> = ({ width, height, model, onPointerDown }) => {
    const canvasRef = React.useRef<HTMLCanvasElement | null>(null);

    React.useLayoutEffect(() => {
        const canvas = canvasRef.current;
        if (!canvas) return;

        canvas.width = width;
        canvas.height = height;

        const ctx = canvas.getContext("2d");
        if (!ctx) return;

        drawTimelineCanvas(ctx, {
            width,
            height,
            clips: model.drawClips,
            playheadX: model.playheadX,
        });
    }, [height, model, width]);

    const handlePointerDown = React.useCallback(
        (event: React.PointerEvent<HTMLCanvasElement>) => {
            const bounds = event.currentTarget.getBoundingClientRect();
            onPointerDown?.({
                screenX: event.clientX - bounds.left,
                screenY: event.clientY - bounds.top,
                clientX: event.clientX,
                clientY: event.clientY,
            });
        },
        [onPointerDown],
    );

    return (
        <canvas
            ref={canvasRef}
            className="absolute inset-0 pointer-events-none"
            onPointerDown={handlePointerDown}
        />
    );
};
