#!/usr/bin/env python3
"""Optional product smoke test for a locally supplied NSF-HiFiGAN model pack."""

from __future__ import annotations

import json
import os
import pathlib
import hashlib
import struct
import subprocess
import sys
import tempfile
import wave

from mcp_smoke import McpClient, make_fixture


class ModelMcpClient(McpClient):
    def __init__(self, binary: pathlib.Path, model_directory: pathlib.Path) -> None:
        self.sequence = 0
        environment = os.environ.copy()
        environment["HACHISHIFTER_NSF_HIFIGAN_MODEL_DIR"] = str(model_directory)
        self.process = subprocess.Popen(
            [str(binary), "--mcp"],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1,
            env=environment,
        )


def audio_metrics(path: pathlib.Path) -> dict[str, float | int]:
    with wave.open(str(path), "rb") as audio:
        frames = audio.getnframes()
        channels = audio.getnchannels()
        sample_rate = audio.getframerate()
        width = audio.getsampwidth()
        data = audio.readframes(frames)
    if width == 2:
        interleaved = struct.unpack(f"<{len(data) // 2}h", data)
        decoded = [value / 32768.0 for value in interleaved]
    elif width == 3:
        decoded = [
            int.from_bytes(data[index : index + 3], "little", signed=True) / 8388608.0
            for index in range(0, len(data), 3)
        ]
    elif width == 4:
        interleaved = struct.unpack(f"<{len(data) // 4}i", data)
        decoded = [value / 2147483648.0 for value in interleaved]
    else:
        raise AssertionError(width)
    values = [decoded[index] for index in range(0, len(decoded), channels)]
    peak = max((abs(value) for value in values), default=0.0)
    dc = abs(sum(values) / max(1, len(values)))
    edge = min(len(values) // 2, round(sample_rate * 0.004))
    edge_ranges = (values[:edge], values[-edge:]) if edge else (values,)
    edge_jump = max(
        (
            abs(part[index] - part[index - 1])
            for part in edge_ranges
            for index in range(1, len(part))
        ),
        default=0.0,
    )
    jumps = [abs(values[index] - values[index - 1]) for index in range(1, len(values))]
    return {
        "peak": peak,
        "frames": frames,
        "channels": channels,
        "dc": dc,
        "edge_jump": edge_jump,
        "maximum_jump": max(jumps, default=0.0),
        "first": abs(values[0]) if values else 0.0,
        "last": abs(values[-1]) if values else 0.0,
    }


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit(
            "usage: nsf_hifigan_model_smoke.py HachiShifterNext MODEL_DIRECTORY"
        )
    binary = pathlib.Path(sys.argv[1]).resolve()
    model_directory = pathlib.Path(sys.argv[2]).resolve()
    assert (model_directory / "pc_nsf_hifigan.onnx").is_file()
    assert (model_directory / "config.json").is_file()

    with tempfile.TemporaryDirectory(prefix="hachishifter-nsf-") as directory_text:
        directory = pathlib.Path(directory_text)
        source = directory / "source.wav"
        standard_output = directory / "rendered-standard.wav"
        variable_output = directory / "rendered-variable-hop.wav"
        make_fixture(source)
        client = ModelMcpClient(binary, model_directory)
        try:
            client.call("import_audio", {"path": str(source)})
            project = json.loads(client.call("project_snapshot"))
            track = project["tracks"][0]
            clip = track["clips"][0]
            client.call(
                "add_note",
                {
                    "clip_id": clip["id"],
                    "start_seconds": 0.0,
                    "duration_seconds": 0.55,
                    "midi": 62.0,
                },
            )
            client.call(
                "resize_clip",
                {"clip_id": clip["id"], "start_seconds": 0.0, "duration_seconds": 0.55},
            )
            client.call(
                "set_track",
                {
                    "track_id": track["id"],
                    "pitch_algorithm": "nsf-hifigan",
                    "stretch_algorithm": "melodyne-hybrid",
                },
            )
            status = json.loads(
                client.call("render_prepare", {"wait": True, "timeout_seconds": 180.0})
            )
            assert status["prepared"] and not status["rendering"], status
            assert "nsf-hifigan-onnx" in status["backend"].lower(), status
            standard_status = status
            exported = json.loads(
                client.call(
                    "export_wav",
                    {"path": str(standard_output), "timeout_seconds": 180.0},
                )
            )
            assert exported["size"] > 44, exported
            client.call(
                "set_track",
                {"track_id": track["id"], "stretch_algorithm": "variable-mel-hop"},
            )
            variable_status = json.loads(
                client.call("render_prepare", {"wait": True, "timeout_seconds": 180.0})
            )
            assert variable_status["prepared"] and not variable_status["rendering"], variable_status
            assert "nsf-hifigan-onnx" in variable_status["backend"].lower(), variable_status
            assert "variable-mel-hop" in variable_status["backend"].lower(), variable_status
            exported = json.loads(
                client.call(
                    "export_wav",
                    {"path": str(variable_output), "timeout_seconds": 180.0},
                )
            )
            assert exported["size"] > 44, exported
        finally:
            client.close()

        metrics = {
            "standard": audio_metrics(standard_output),
            "variable_hop": audio_metrics(variable_output),
        }
        for route, values in metrics.items():
            assert values["channels"] == 2, (route, values)
            assert abs(values["frames"] - round(0.55 * 48_000)) <= 2, (route, values)
            assert values["peak"] > 1.0e-4, (route, values)
            assert values["dc"] < 0.02, (route, values)
            assert values["first"] < 0.08 and values["last"] < 0.08, (route, values)
            assert values["edge_jump"] < 0.45, (route, values)
            assert values["maximum_jump"] < 0.82, (route, values)
        assert hashlib.sha256(standard_output.read_bytes()).digest() != hashlib.sha256(
            variable_output.read_bytes()
        ).digest()
        print(
            json.dumps(
                {
                    "standard_backend": standard_status["backend"],
                    "variable_backend": variable_status["backend"],
                    "metrics": metrics,
                }
            )
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
