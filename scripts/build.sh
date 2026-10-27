#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
build_root="${TANDO_BUILD_ROOT:-${TMPDIR:-/tmp}/tando-build}"
python3 "$repo_root/scripts/prepare_sketch.py" stage "$repo_root" "$build_root/sketch/tando_final_demo_15min"
build_root="$(cd -- "$build_root" && pwd)"

"${TANDO_ARDUINO_CLI:-arduino-cli}" compile \
  --fqbn "${TANDO_FQBN:-esp32:esp32:esp32s3}" \
  --jobs "${TANDO_BUILD_JOBS:-4}" --warnings default \
  --build-path "$build_root/build" \
  --output-dir "$build_root/artifacts" \
  "$build_root/sketch/tando_final_demo_15min"
test -s "$build_root/artifacts/tando_final_demo_15min.ino.bin"
test -s "$build_root/artifacts/tando_final_demo_15min.ino.elf"
