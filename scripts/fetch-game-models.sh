#!/usr/bin/env bash
set -euo pipefail

GAME_VERSION="${GAME_VERSION:-v1.0.2}"
GAME_ARCHIVE="${GAME_ARCHIVE:-GAME-1.0-small-onnx.zip}"
GAME_URL="${GAME_URL:-https://github.com/openvpi/GAME/releases/download/${GAME_VERSION}/${GAME_ARCHIVE}}"
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST_DIR="${GAME_MODEL_DIR:-${ROOT_DIR}/backend/src-tauri/resources/models/game}"
REQUIRED=(encoder.onnx segmenter.onnx estimator.onnx bd2dur.onnx dur2bd.onnx config.json)

complete=1
for name in "${REQUIRED[@]}"; do
    if [[ ! -f "${DEST_DIR}/${name}" ]]; then
        complete=0
        break
    fi
done
if [[ "${complete}" == "1" ]]; then
    echo "GAME model pack already present: ${DEST_DIR}"
    exit 0
fi

scratch_root="${RUNNER_TEMP:-${TMPDIR:-/tmp}}"
scratch="$(mktemp -d "${scratch_root%/}/hachishifter-game.XXXXXX")"
trap 'rm -rf "${scratch}"' EXIT

echo "Downloading GAME ${GAME_VERSION} compact ONNX model pack"
curl --fail --location --retry 5 --retry-delay 3 --output "${scratch}/${GAME_ARCHIVE}" "${GAME_URL}"
mkdir -p "${scratch}/extract" "${DEST_DIR}"
unzip -q "${scratch}/${GAME_ARCHIVE}" -d "${scratch}/extract"

for name in "${REQUIRED[@]}"; do
    source_path="$(find "${scratch}/extract" -type f -name "${name}" -print -quit)"
    if [[ -z "${source_path}" ]]; then
        echo "GAME archive is missing ${name}" >&2
        exit 1
    fi
    cp "${source_path}" "${DEST_DIR}/${name}"
done

printf 'GAME model pack ready: %s\n' "${DEST_DIR}"
du -sh "${DEST_DIR}" || true
