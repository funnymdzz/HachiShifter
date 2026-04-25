import React from "react";

import { drawTimelineCanvas } from "./runtime/timelineCanvasRenderer";
import type { TimelineCanvasClipModel } from "./runtime/timelineCanvasModel";

export const TimelineCanvasViewport: React.FC<{
    width: number;
    height: number;
    model: {
        drawClips: TimelineCanvasClipModel[];
    };
}> = ({ width, height, model }) => {
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
        });
    }, [height, model, width]);

    return (
        <canvas ref={canvasRef} className="absolute inset-0 pointer-events-none" />
    );
};
