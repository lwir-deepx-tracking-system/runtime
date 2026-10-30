#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
project_dir="$(cd -- "${script_dir}/.." && pwd)"
build_dir="${BUILD_DIR:-${project_dir}/build/test}"

cmake -S "${project_dir}" -B "${build_dir}" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DBUILD_TESTING=ON \
    -DLWIR_BUILD_RUNTIME=OFF \
    -DLWIR_BUILD_BENCHMARK=OFF \
    -DLWIR_BUILD_CAPTURE=OFF \
    -DLWIR_ENABLE_I3_CAMERA=OFF \
    -DLWIR_ENABLE_DX_APP=OFF

cmake --build "${build_dir}" --parallel
ctest --test-dir "${build_dir}" --output-on-failure
