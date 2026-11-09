#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
project_dir="$(cd -- "${script_dir}/.." && pwd)"
runtime_bin="${RUNTIME_BIN:-${project_dir}/build/runtime/lwir_runtime}"
runtime_config="${RUNTIME_CONFIG:-${project_dir}/config/runtime.yaml}"
duration="${RUNTIME_TEST_SECONDS:-15}"
stamp="$(date +%Y%m%d_%H%M%S)_$$"
run_root="${RUNTIME_OUTPUT_ROOT:-${project_dir}/build/runtime-output-check}/${stamp}"
result_root="${run_root}/results"
config_copy="${run_root}/runtime.yaml"

if [[ ! -x "${runtime_bin}" ]]; then
    echo "Runtime binary not found or not executable: ${runtime_bin}" >&2
    echo "Build it with scripts/build_runtime.sh first." >&2
    exit 2
fi
if [[ ! -f "${runtime_config}" ]]; then
    echo "Runtime config not found: ${runtime_config}" >&2
    exit 2
fi
mkdir -p "${run_root}"
sed -E "s#^([[:space:]]*output_root:)[[:space:]]*.*#\1 ${result_root}#"     "${runtime_config}" > "${config_copy}"

cd "${project_dir}"
set +e
timeout --signal=INT --kill-after=10s "${duration}s"     "${runtime_bin}" "${config_copy}"
runtime_status=$?
set -e
if [[ ${runtime_status} -ne 0 && ${runtime_status} -ne 124 ]]; then
    echo "Runtime exited with status ${runtime_status}; see logs above." >&2
    exit "${runtime_status}"
fi

metrics_file="$(find "${result_root}" -type f -name metrics.csv -print -quit)"
if [[ -z "${metrics_file}" ]]; then
    echo "Runtime did not save metrics.csv under ${result_root}" >&2
    exit 1
fi
result_dir="$(dirname "${metrics_file}")"
python3 "${project_dir}/tests/runtime/check_runtime_output.py" "${result_dir}"
echo "Runtime output: ${result_dir}"
