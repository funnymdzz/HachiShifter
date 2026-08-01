#!/usr/bin/env python3
"""Optional product smoke test for a locally supplied NSF-HiFiGAN model pack."""

from __future__ import annotations

import json
import os
import pathlib
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


def peak_and_frames(path: pathlib.Path) -> tuple[float, int, int]:
    with wave.open(str(path), "rb") as audio:
        frames = audio.getnframes()
        channels = audio.getnchannels()
        width = audio.getsampwidth()
        data = audio.readframes(frames)
    assert width == 2, width
    values = struct.unpack(f"<{len(data) // 2}h", data)
    peak = max((abs(value) for value in values), default=0) / 32768.0
    return peak, frames, channels


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
        output = directory / "rendered.wav"
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
                    "duration_seconds": 0.35,
                    "midi": 62.0,
                },
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
            exported = json.loads(
                client.call("export_wav", {"path": str(output), "timeout_seconds": 180.0})
            )
            assert exported["size"] > 44, exported
        finally:
            client.close()

        peak, frames, channels = peak_and_frames(output)
        assert channels == 2, channels
        assert abs(frames - round(0.35 * 48_000)) <= 2, frames
        assert peak > 1.0e-4, peak
        print(json.dumps({"backend": status["backend"], "frames": frames, "peak": peak}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
