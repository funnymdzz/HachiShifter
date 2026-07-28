#!/usr/bin/env bash
set -euo pipefail

# Usage:
#   HACHISHIFTER_FCPE_ONNX=... \
#   HACHISHIFTER_NSF_HIFIGAN_MODEL_DIR=... \
#   scripts/compare_melodyne_reference.sh HachiShifter PROJECT.mpd REFERENCE.wav OUT_DIR

if [ "$#" -ne 4 ]; then
  echo "usage: $0 HACHISHIFTER_BINARY PROJECT_MPD REFERENCE_WAV OUTPUT_DIR" >&2
  exit 2
fi

binary=$1
project=$2
reference=$3
output_dir=$4
mkdir -p "$output_dir"

for algorithm in mld5 nsf-hifigan; do
  rendered="$output_dir/${algorithm}.wav"
  report="$output_dir/${algorithm}-fcpe.json"
  "$binary" --render-mpd-vocal "$project" "$rendered" "$algorithm"
  "$binary" --compare-vocal-f0 "$reference" "$rendered" >"$report"
  printf '%s\t' "$algorithm"
  cat "$report"
  if [ "$algorithm" = "nsf-hifigan" ]; then
    node "$(dirname "$0")/assert_nsf_hifigan_reference.js" "$report"
  fi
done
