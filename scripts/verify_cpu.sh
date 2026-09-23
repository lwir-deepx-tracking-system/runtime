#!/usr/bin/env bash
set -euo pipefail

build_dir="${1:-build}"

cmake -S . -B "${build_dir}" -DCMAKE_BUILD_TYPE=Debug -DLWIR_ENABLE_DX_APP=OFF
cmake --build "${build_dir}" -j2
ctest --test-dir "${build_dir}" --output-on-failure
