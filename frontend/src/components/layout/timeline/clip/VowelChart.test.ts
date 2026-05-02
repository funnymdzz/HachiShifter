import { chartPointToFormants, formantsToChartPoint } from "./VowelChart";

function assert(condition: unknown, message: string): void {
    if (!condition) {
        throw new Error(message);
    }
}

const point = formantsToChartPoint(800, 1400, 540, 2600, 250, 1000, 196, 146);
const roundTrip = chartPointToFormants(point.x, point.y, 196, 146, 540, 2600, 250, 1000);

assert(Math.abs(roundTrip.f1 - 800) < 5, `expected f1≈800, got ${roundTrip.f1}`);
assert(Math.abs(roundTrip.f2 - 1400) < 5, `expected f2≈1400, got ${roundTrip.f2}`);

console.log("VowelChart helpers round-trip checks passed");
