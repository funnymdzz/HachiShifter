function rollingAverage(values: number[]): number {
    if (values.length === 0) return 0;
    return values.reduce((sum, value) => sum + value, 0) / values.length;
}

export function createTimelinePerfProbe(limit = 60): {
    pushDrawMs: (value: number) => void;
    pushHitTestMs: (value: number) => void;
    getSnapshot: () => {
        avgDrawMs: number;
        avgHitTestMs: number;
    };
} {
    const drawMs: number[] = [];
    const hitTestMs: number[] = [];

    const push = (bucket: number[], value: number) => {
        bucket.push(value);
        if (bucket.length > limit) {
            bucket.shift();
        }
    };

    return {
        pushDrawMs(value) {
            push(drawMs, value);
        },
        pushHitTestMs(value) {
            push(hitTestMs, value);
        },
        getSnapshot() {
            return {
                avgDrawMs: rollingAverage(drawMs),
                avgHitTestMs: rollingAverage(hitTestMs),
            };
        },
    };
}
