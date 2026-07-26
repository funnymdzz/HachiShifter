#!/usr/bin/env bash
set -euo pipefail

GAME_VERSION="${GAME_VERSION:-v1.0.3}"
GAME_VARIANT="${GAME_VARIANT:-all}"
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST_DIR="${GAME_MODEL_DIR:-${ROOT_DIR}/backend/src-tauri/resources/models/game}"
REQUIRED=(encoder.onnx segmenter.onnx estimator.onnx bd2dur.onnx dur2bd.onnx config.json)

scratch_root="${RUNNER_TEMP:-${TMPDIR:-/tmp}}"
scratch="$(mktemp -d "${scratch_root%/}/hachishifter-game.XXXXXX")"
trap 'rm -rf "${scratch}"' EXIT

fetch_variant() {
    local variant="$1"
    local archive="GAME-1.0.3-${variant}-onnx.zip"
    local url="https://github.com/openvpi/GAME/releases/download/${GAME_VERSION}/${archive}"
    local destination="${DEST_DIR}"
    if [[ "${variant}" == "small" ]]; then
        destination="${DEST_DIR}/small"
    fi

    local complete=1
    for name in "${REQUIRED[@]}"; do
        if [[ ! -f "${destination}/${name}" ]]; then
            complete=0
            break
        fi
    done
    if [[ "${complete}" == "1" ]]; then
        echo "GAME ${variant} model pack already present: ${destination}"
        return
    fi

    local extract_dir="${scratch}/extract-${variant}"
    echo "Downloading GAME ${GAME_VERSION} ${variant} ONNX model pack"
    curl --fail --location --retry 5 --retry-delay 3 --output "${scratch}/${archive}" "${url}"
    mkdir -p "${extract_dir}" "${destination}"
    unzip -q "${scratch}/${archive}" -d "${extract_dir}"
    for name in "${REQUIRED[@]}"; do
        source_path="$(find "${extract_dir}" -type f -name "${name}" -print -quit)"
        if [[ -z "${source_path}" ]]; then
            echo "GAME ${variant} archive is missing ${name}" >&2
            exit 1
        fi
        cp "${source_path}" "${destination}/${name}"
    done
    printf 'GAME %s model pack ready: %s\n' "${variant}" "${destination}"
}

case "${GAME_VARIANT}" in
    large) fetch_variant large ;;
    small) fetch_variant small ;;
    all) fetch_variant large; fetch_variant small ;;
    *) echo "GAME_VARIANT must be large, small, or all" >&2; exit 2 ;;
esac

du -sh "${DEST_DIR}" || true
