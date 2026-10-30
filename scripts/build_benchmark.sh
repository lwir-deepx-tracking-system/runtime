#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
project_dir="$(cd -- "${script_dir}/.." && pwd)"
build_dir="${BUILD_DIR:-${project_dir}/build/benchmark}"

cmake -S "${project_dir}" -B "${build_dir}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=OFF \
    -DLWIR_BUILD_RUNTIME=OFF \
    -DLWIR_BUILD_BENCHMARK=ON \
    -DLWIR_BUILD_CAPTURE=OFF \
    -DLWIR_ENABLE_I3_CAMERA=OFF \
    -DLWIR_ENABLE_DX_APP=ON

cmake --build "${build_dir}" --target lwir_benchmark --parallel
