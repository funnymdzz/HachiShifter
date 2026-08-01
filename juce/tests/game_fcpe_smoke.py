#!/usr/bin/env python3
"""Optional end-to-end GAME+FCPE model-pack regression."""

from __future__ import annotations

import math
import os
import pathlib
import re
import struct
import subprocess
import sys
import tempfile
import wave


def output(command: list[str], env: dict[str, str]) -> str:
    return subprocess.run(
        command,
        check=True,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        env=env,
        timeout=300,
    ).stdout


def main() -> int:
    if len(sys.argv) != 5:
        raise SystemExit(
            "usage: game_fcpe_smoke.py HachiShifterNext GAME_DIR FCPE.onnx VOCAL.wav"
        )
    binary, game_dir, fcpe, vocal = map(lambda value: pathlib.Path(value).resolve(), sys.argv[1:])
    variant = "small" if game_dir.name.lower() == "small" else "large"
    env = os.environ.copy()
    env["HACHISHIFTER_GAME_MODEL"] = variant
    env[
        "HACHISHIFTER_GAME_SMALL_MODEL_DIR"
        if variant == "small"
        else "HACHISHIFTER_GAME_MODEL_DIR"
    ] = str(game_dir)
    env["HACHISHIFTER_FCPE_ONNX"] = str(fcpe)

    with tempfile.TemporaryDirectory(prefix="hachi-game-fcpe-") as temporary:
        tone = pathlib.Path(temporary) / "a3.wav"
        with wave.open(str(tone), "wb") as stream:
            stream.setparams((1, 2, 48_000, 0, "NONE", "not compressed"))
            stream.writeframes(
                b"".join(
                    struct.pack(
                        "<h",
                        round(
                            8_000
                            * (
                                math.sin(2.0 * math.pi * 220.0 * index / 48_000)
                                + 0.35
                                * math.sin(2.0 * math.pi * 440.0 * index / 48_000)
                            )
                        ),
                    )
                    for index in range(48_000)
                )
            )
        fcpe_report = output(
            [str(binary), "--inspect-fcpe", str(tone), str(fcpe)], env
        )
        match = re.search(r"median_midi=([-+0-9.]+)", fcpe_report)
        assert match and abs(float(match.group(1)) - 57.0) < 0.10, fcpe_report
        analysis_report = output([str(binary), "--inspect-audio", str(vocal)], env)
        assert "analysis=GAME+FCPE" in analysis_report, analysis_report
        notes = re.search(r"notes=(\d+)", analysis_report)
        assert notes and int(notes.group(1)) > 0, analysis_report
        print(fcpe_report.strip())
        print(analysis_report.strip())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
