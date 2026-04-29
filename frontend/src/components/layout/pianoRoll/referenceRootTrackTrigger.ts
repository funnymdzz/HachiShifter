import { createElement, type ReactElement } from "react";

export function buildReferenceRootTrackTriggerElement(label: string): ReactElement {
    return createElement("span", null, label);
}
