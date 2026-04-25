import { gainToDb } from "../math.js";

function toRgba(color: string, alpha: number): string {
    if (color.startsWith("#")) {
        const hex = color.slice(1);
        const normalized =
            hex.length === 3
                ? hex
                      .split("")
                      .map((part) => part + part)
                      .join("")
                : hex;
        if (normalized.length === 6) {
            const r = Number.parseInt(normalized.slice(0, 2), 16);
            const g = Number.parseInt(normalized.slice(2, 4), 16);
            const b = Number.parseInt(normalized.slice(4, 6), 16);
            return `rgba(${r}, ${g}, ${b}, ${alpha})`;
        }
    }
    return color;
}

export function drawTimelineCanvas(
    ctx: CanvasRenderingContext2D,
    args: {
        width: number;
        height: number;
        clips: Array<{
            id: string;
            leftPx: number;
            topPx: number;
            widthPx: number;
            heightPx: number;
            headerHeightPx: number;
            fadeInPx: number;
            fadeOutPx: number;
            selected: boolean;
            muted: boolean;
            gain: number;
            name: string;
            trackColor?: string;
        }>;
        playheadX: number;
    },
): void {
    ctx.clearRect(0, 0, args.width, args.height);

    for (const clip of args.clips) {
        const clipLeft = clip.leftPx;
        const clipTop = clip.topPx;
        const clipWidth = Math.max(1, clip.widthPx);
        const clipHeight = Math.max(1, clip.heightPx);
        const headerHeight = Math.max(1, Math.min(clip.heightPx, clip.headerHeightPx));
        const bodyTop = clipTop + headerHeight;
        const bodyHeight = Math.max(1, clipHeight - headerHeight);
        const accent = clip.trackColor ?? (clip.selected ? "#5ea1ff" : "#68839d");
        const headerColor = clip.selected ? accent : toRgba(accent, 0.9);
        const bodyColor = clip.selected ? "rgba(94, 161, 255, 0.34)" : "rgba(84, 112, 141, 0.48)";
        const borderColor = clip.selected ? "#9cc4ff" : "rgba(197, 212, 228, 0.42)";
        const textColor = clip.selected ? "#f6fbff" : "rgba(233, 241, 249, 0.90)";
        const mutedAlpha = clip.muted ? 0.45 : 1;

        ctx.save();
        ctx.globalAlpha = mutedAlpha;

        ctx.fillStyle = headerColor;
        ctx.fillRect(clipLeft, clipTop, clipWidth, headerHeight);

        ctx.fillStyle = bodyColor;
        ctx.fillRect(clipLeft, bodyTop, clipWidth, bodyHeight);

        if (clip.fadeInPx > 0) {
            ctx.fillStyle = "rgba(0, 0, 0, 0.22)";
            ctx.beginPath();
            ctx.moveTo(clipLeft, clipTop + clipHeight);
            ctx.lineTo(clipLeft, clipTop);
            ctx.lineTo(clipLeft + Math.min(clipWidth, clip.fadeInPx), clipTop + clipHeight);
            ctx.closePath();
            ctx.fill();
        }
        if (clip.fadeOutPx > 0) {
            ctx.fillStyle = "rgba(0, 0, 0, 0.22)";
            ctx.beginPath();
            ctx.moveTo(clipLeft + clipWidth, clipTop + clipHeight);
            ctx.lineTo(clipLeft + clipWidth, clipTop);
            ctx.lineTo(
                clipLeft + clipWidth - Math.min(clipWidth, clip.fadeOutPx),
                clipTop + clipHeight,
            );
            ctx.closePath();
            ctx.fill();
        }

        ctx.strokeStyle = borderColor;
        ctx.lineWidth = clip.selected ? 2 : 1;
        ctx.strokeRect(clipLeft + 0.5, bodyTop + 0.5, Math.max(0, clipWidth - 1), Math.max(0, bodyHeight - 1));

        ctx.fillStyle = "rgba(0, 0, 0, 0.24)";
        ctx.fillRect(clipLeft, clipTop + headerHeight, clipWidth, 1);

        if (clipWidth >= 32 && clip.muted) {
            ctx.fillStyle = "rgba(120, 16, 16, 0.86)";
            ctx.fillRect(clipLeft + 6, clipTop + 3, 14, 11);
            ctx.fillStyle = "#ffe7e7";
            ctx.font = "bold 9px sans-serif";
            ctx.textBaseline = "middle";
            ctx.fillText("M", clipLeft + 10, clipTop + 8.5);
        }

        if (clipWidth >= 80) {
            const gainDb = gainToDb(clip.gain);
            const gainLabel = `${gainDb >= 0 ? "+" : ""}${gainDb.toFixed(1)}dB`;
            ctx.fillStyle = textColor;
            ctx.font = "10px sans-serif";
            ctx.textBaseline = "middle";
            const metrics = ctx.measureText(gainLabel);
            ctx.fillText(gainLabel, clipLeft + clipWidth - metrics.width - 6, clipTop + 9);
        }

        if (clipWidth >= 120) {
            const textStartX = clipLeft + (clip.muted ? 26 : 8);
            const textEndX = clipWidth >= 80 ? clipLeft + clipWidth - 56 : clipLeft + clipWidth - 8;
            const availableWidth = Math.max(0, textEndX - textStartX);
            if (availableWidth > 12) {
                ctx.save();
                ctx.beginPath();
                ctx.rect(textStartX, clipTop, availableWidth, headerHeight);
                ctx.clip();
                ctx.fillStyle = textColor;
                ctx.font = "12px sans-serif";
                ctx.textBaseline = "middle";
                ctx.fillText(clip.name, textStartX, clipTop + 9);
                ctx.restore();
            }
        }

        ctx.restore();
    }

    ctx.fillStyle = "#ff5d5d";
    ctx.fillRect(args.playheadX, 0, 1, args.height);
}
