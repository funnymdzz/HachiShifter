import { debounceMs } from "./useClipFormantEditor";

if (debounceMs() !== 180) {
    throw new Error(`expected debounceMs() to equal 180, got ${debounceMs()}`);
}
console.log("useClipFormantEditor debounce checks passed");
