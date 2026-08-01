#!/usr/bin/env python3
"""Regression check for imported Melodyne edits and render-route overrides."""

from __future__ import annotations

import json
import pathlib
import sys

from mcp_smoke import McpClient


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit("usage: melodyne_import_smoke.py HachiShifterNext PROJECT.mpd")

    binary = pathlib.Path(sys.argv[1]).resolve()
    project_file = pathlib.Path(sys.argv[2]).resolve()
    client = McpClient(binary)
    try:
        client.call(
            "import_melodyne",
            {
                "path": str(project_file),
                "preserve_edits": True,
                "recursive_media": True,
            },
        )
        project = json.loads(client.call("project_snapshot"))
        assert project["tracks"], "Melodyne project contains no imported tracks"
        notes = [
            note
            for track in project["tracks"]
            for clip in track["clips"]
            for note in clip["notes"]
        ]
        assert notes, "Melodyne project contains no imported notes"
        pitch_points = 0
        unvoiced_pitch_points = 0
        for note in notes:
            contour = note["contour"]
            times = [point["time_seconds"] for point in contour]
            assert all(a < b for a, b in zip(times, times[1:])), note["id"]
            pitch_points += len(contour)
            unvoiced_pitch_points += sum(not point["voiced"] for point in contour)

        mapped_clips = 0
        source_time_points = 0
        for track in project["tracks"]:
            for clip in track["clips"]:
                time_map = clip.get("source_time_map", [])
                if not time_map:
                    continue
                mapped_clips += 1
                source_time_points += len(time_map)
                assert len(time_map) >= 2, time_map
                targets = [point["target_seconds"] for point in time_map]
                sources = [point["source_seconds"] for point in time_map]
                assert all(a < b for a, b in zip(targets, targets[1:])), time_map
                assert all(a <= b for a, b in zip(sources, sources[1:])), time_map
                assert targets[0] >= -1.0e-9 and targets[-1] <= clip["duration_seconds"] + 1.0e-9
                assert sources[0] >= -1.0e-9 and sources[-1] <= clip["source_duration_seconds"] + 1.0e-9

        flattened = [note for note in notes if note["flattened"]]
        assert flattened, "fixture contains no imported zero-modulation note"
        maximum_flat_deviation = max(
            (
                abs(point["rendered_target_cents"])
                for note in flattened
                for point in note["contour"]
                if point["voiced"]
            ),
            default=0.0,
        )
        assert maximum_flat_deviation < 1.0e-5, maximum_flat_deviation

        track_id = project["tracks"][0]["id"]
        selected_routes: list[tuple[str, str]] = []
        for pitch in ("mld5", "nsf-hifigan", "world", "vslib"):
            client.call(
                "set_track", {"track_id": track_id, "pitch_algorithm": pitch}
            )
            selected = json.loads(client.call("project_snapshot"))["tracks"][0]
            assert selected["pitch_algorithm"] == pitch, selected
            selected_routes.append((pitch, selected["stretch_algorithm"]))

        print(
            json.dumps(
                {
                    "project": project["name"],
                    "bpm": project["bpm"],
                    "tracks": len(project["tracks"]),
                    "notes": len(notes),
                    "flattened_notes": len(flattened),
                    "mapped_clips": mapped_clips,
                    "source_time_points": source_time_points,
                    "pitch_points": pitch_points,
                    "unvoiced_pitch_points": unvoiced_pitch_points,
                    "flat_target_max_deviation_cents": maximum_flat_deviation,
                    "selected_routes": selected_routes,
                },
                separators=(",", ":"),
            )
        )
    finally:
        client.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
