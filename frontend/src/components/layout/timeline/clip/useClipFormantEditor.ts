import React from "react";
import type { ClipFormantMorph } from "../../../../features/session/sessionTypes";

const DEFAULT_FORMANT_MORPH: ClipFormantMorph = {
    enabled: false,
    targetF1Hz: 800,
    targetF2Hz: 1400,
    strength: 0.50,
};

export function debounceMs(): number {
    return 180;
}

export function useClipFormantEditor(params: {
    clipId: string;
    value: ClipFormantMorph | undefined;
    onCommit: (clipId: string, value: ClipFormantMorph, checkpoint: boolean) => void;
}) {
    const { clipId, value, onCommit } = params;
    const [draft, setDraft] = React.useState<ClipFormantMorph>(value ?? DEFAULT_FORMANT_MORPH);
    const timerRef = React.useRef<number | null>(null);
    const draftRef = React.useRef<ClipFormantMorph>(value ?? DEFAULT_FORMANT_MORPH);
    const clipIdRef = React.useRef(clipId);

    const commit = React.useCallback(
        (targetClipId: string, next: ClipFormantMorph, checkpoint: boolean) => {
            onCommit(targetClipId, next, checkpoint);
        },
        [onCommit],
    );

    const flush = React.useCallback(() => {
        if (timerRef.current !== null) {
            window.clearTimeout(timerRef.current);
            timerRef.current = null;
        }
        commit(clipIdRef.current, draftRef.current, true);
    }, [commit]);

    React.useEffect(() => {
        if (clipIdRef.current !== clipId) {
            flush();
            clipIdRef.current = clipId;
        }

        const nextDraft = value ?? DEFAULT_FORMANT_MORPH;
        draftRef.current = nextDraft;
        setDraft(nextDraft);
    }, [clipId, value, flush]);

    React.useEffect(() => {
        draftRef.current = draft;
    }, [draft]);

    React.useEffect(
        () => () => {
            if (timerRef.current !== null) {
                window.clearTimeout(timerRef.current);
            }
        },
        [],
    );

    const updateDraft = React.useCallback(
        (patch: Partial<ClipFormantMorph>) => {
            setDraft((prev) => {
                const next = { ...prev, ...patch };
                draftRef.current = next;
                if (timerRef.current !== null) {
                    window.clearTimeout(timerRef.current);
                }
                timerRef.current = window.setTimeout(() => {
                    commit(clipIdRef.current, draftRef.current, false);
                    timerRef.current = null;
                }, debounceMs());
                return next;
            });
        },
        [commit],
    );

    return { draft, updateDraft, flush };
}
