#!/usr/bin/env python3
"""Model-free product smoke test for the JSON-RPC MCP render surface."""

from __future__ import annotations

import hashlib
import json
import math
import pathlib
import struct
import subprocess
import sys
import tempfile
import wave


def make_fixture(path: pathlib.Path) -> None:
    sample_rate = 48_000
    duration = 0.35
    with wave.open(str(path), "wb") as output:
        output.setparams((1, 2, sample_rate, 0, "NONE", "not compressed"))
        samples = bytearray()
        for index in range(round(sample_rate * duration)):
            phase = 2.0 * math.pi * 220.0 * index / sample_rate
            value = 0.28 * math.sin(phase) + 0.08 * math.sin(2.0 * phase)
            samples.extend(struct.pack("<h", round(max(-1.0, min(1.0, value)) * 32767)))
        output.writeframes(samples)


class McpClient:
    def __init__(self, binary: pathlib.Path) -> None:
        self.sequence = 0
        self.process = subprocess.Popen(
            [str(binary), "--mcp"],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1,
        )

    def call(self, name: str, arguments: dict | None = None) -> str:
        self.sequence += 1
        request = {
            "jsonrpc": "2.0",
            "id": self.sequence,
            "method": "tools/call",
            "params": {"name": name, "arguments": arguments or {}},
        }
        assert self.process.stdin is not None
        assert self.process.stdout is not None
        self.process.stdin.write(json.dumps(request, separators=(",", ":")) + "\n")
        self.process.stdin.flush()
        line = self.process.stdout.readline()
        if not line:
            raise RuntimeError("MCP process ended before responding: " + self.stderr())
        result = json.loads(line)["result"]
        text = result["content"][0]["text"]
        if result.get("isError"):
            raise RuntimeError(f"{name}: {text}")
        return text

    def stderr(self) -> str:
        if self.process.stderr is None:
            return ""
        return self.process.stderr.read().strip()

    def close(self) -> None:
        if self.process.stdin is not None:
            self.process.stdin.close()
        self.process.wait(timeout=15)
        if self.process.returncode != 0:
            raise RuntimeError(f"MCP exited with {self.process.returncode}: {self.stderr()}")


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: mcp_smoke.py HachiShifterNext")
    binary = pathlib.Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="hachishifter-mcp-") as directory_text:
        directory = pathlib.Path(directory_text)
        source = directory / "fixture.wav"
        make_fixture(source)
        client = McpClient(binary)
        analysis_status = client.call("analysis_status")
        assert "requested=GAME+FCPE" in analysis_status
        assert "game_variant=large" in analysis_status
        analysed = client.call("import_audio", {"path": str(source)})
        assert "backend=" in analysed and "notes=" in analysed
        project = json.loads(client.call("project_snapshot"))
        track = project["tracks"][0]
        clip = track["clips"][0]
        note_id = client.call(
            "add_note",
            {
                "clip_id": clip["id"],
                "start_seconds": 0.0,
                "duration_seconds": 0.55,
                "midi": 60.0,
            },
        ).split("=", 1)[1]
        client.call("transpose_note", {"note_id": note_id, "semitones": 4.0})
        client.call("set_note", {"note_id": note_id, "modulation": 0.0})
        client.call(
            "resize_clip",
            {"clip_id": clip["id"], "start_seconds": 0.0, "duration_seconds": 0.55},
        )

        def export_hash(name: str) -> str:
            output = directory / f"route-{name}.wav"
            result = json.loads(
                client.call("export_wav", {"path": str(output), "timeout_seconds": 120.0})
            )
            assert result["size"] > 44
            return hashlib.sha256(output.read_bytes()).hexdigest()

        route_names: list[str] = []
        pitch_hashes: dict[str, str] = {}
        for pitch in ("mld5", "nsf-hifigan", "world", "vslib"):
            client.call(
                "set_track",
                {
                    "track_id": track["id"],
                    "pitch_algorithm": pitch,
                    "stretch_algorithm": "melodyne-hybrid",
                },
            )
            status = json.loads(
                client.call("render_prepare", {"wait": True, "timeout_seconds": 120.0})
            )
            assert status["prepared"] and not status["rendering"]
            assert pitch in status["backend"].lower(), status
            route_names.append(status["backend"])
            pitch_hashes[pitch] = export_hash(f"pitch-{pitch}")
        # Route labels alone do not prove that the selected renderer reached
        # the audio path.  A stretched, transposed fixture must produce a
        # distinct product for every pitch backend.
        assert len(set(pitch_hashes.values())) == len(pitch_hashes), pitch_hashes

        client.call(
            "set_track", {"track_id": track["id"], "pitch_algorithm": "nsf-hifigan"}
        )
        stretch_hashes: dict[str, str] = {}
        for stretch in ("melodyne-hybrid", "variable-mel-hop", "loop", "soundtouch"):
            client.call("set_track", {"track_id": track["id"], "stretch_algorithm": stretch})
            status = json.loads(
                client.call("render_prepare", {"wait": True, "timeout_seconds": 120.0})
            )
            assert status["prepared"] and stretch in status["backend"].lower(), status
            route_names.append(status["backend"])
            stretch_hashes[stretch] = export_hash(f"stretch-{stretch}")
        assert len(set(stretch_hashes.values())) == len(stretch_hashes), stretch_hashes

        project = json.loads(client.call("project_snapshot"))
        note = next(
            note
            for note in project["tracks"][0]["clips"][0]["notes"]
            if note["id"] == note_id
        )
        targets = [point["rendered_target_cents"] for point in note["contour"]]
        assert note["flattened"] is True
        assert max(targets) - min(targets) < 1.0e-5

        exported = directory / "export.wav"
        export_result = json.loads(
            client.call("export_wav", {"path": str(exported), "timeout_seconds": 120.0})
        )
        assert export_result["size"] > 44
        with wave.open(str(exported), "rb") as rendered:
            assert rendered.getnchannels() == 2
            assert rendered.getframerate() == 48_000
            assert rendered.getnframes() > 0

        seek = json.loads(client.call("transport_seek", {"position_seconds": 0.1}))
        playing = json.loads(client.call("transport_play", {"timeout_seconds": 120.0}))
        stopped = json.loads(client.call("transport_stop"))
        assert abs(seek["position_seconds"] - 0.1) < 0.001
        assert playing["playing"] is True and stopped["playing"] is False

        client.call(
            "sample_settings_save",
            {
                "audio_path": str(source),
                "rows": [
                    {
                        "name": "fixture",
                        "region_start_seconds": 0.01,
                        "region_end_seconds": 0.32,
                        "alignment_seconds": 0.06,
                        "fixed_duration_seconds": 0.04,
                        "relative_pitch_cents": 25.0,
                    }
                ],
            },
        )
        regions = json.loads(
            client.call("sample_settings_read", {"audio_path": str(source)})
        )["rows"]
        assert len(regions) == 1 and regions[0]["name"] == "fixture"
        oto = directory / "oto.ini"
        client.call("oto_export", {"audio_path": str(source), "oto_path": str(oto)})
        assert oto.exists() and "fixture" in oto.read_text(encoding="utf-8")
        imported_oto = json.loads(
            client.call(
                "oto_import",
                {
                    "audio_path": str(source),
                    "oto_path": str(oto),
                    "save_sidecar": True,
                },
            )
        )
        assert len(imported_oto["rows"]) == 1
        client.close()

        print(
            json.dumps(
                {
                    "pitch_and_stretch_routes": route_names,
                    "pitch_route_hashes": pitch_hashes,
                    "stretch_route_hashes": stretch_hashes,
                    "flat_target_max_deviation_cents": max(targets) - min(targets),
                    "export_sha256": hashlib.sha256(exported.read_bytes()).hexdigest(),
                    "export_size": exported.stat().st_size,
                    "transport": True,
                    "sample_settings_and_oto": True,
                    "analysis_status": analysis_status,
                },
                separators=(",", ":"),
            )
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
