#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
project_dir="$(cd -- "${script_dir}/.." && pwd)"
build_dir="${BUILD_DIR:-${project_dir}/build/runtime}"

cmake -S "${project_dir}" -B "${build_dir}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=OFF \
    -DLWIR_BUILD_RUNTIME=ON \
    -DLWIR_BUILD_BENCHMARK=OFF \
    -DLWIR_BUILD_CAPTURE=OFF \
    -DLWIR_ENABLE_I3_CAMERA=ON \
    -DLWIR_ENABLE_DX_APP=ON

cmake --build "${build_dir}" --target lwir_runtime --parallel
