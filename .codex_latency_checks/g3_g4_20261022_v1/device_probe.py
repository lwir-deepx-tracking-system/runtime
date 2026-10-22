"""One-shot read-only target SDK/device inventory; no model or inference.

Run with the target's existing DX-RT Python. This is not a benchmark.
Optional output is created exclusively, never replaces an existing report.
"""
import argparse
import datetime
import importlib.metadata
import inspect
import json
import os
import platform
import signal
import subprocess
import sys
import time
from pathlib import Path


def package(name):
    try:
        return importlib.metadata.version(name)
    except importlib.metadata.PackageNotFoundError:
        return None


def inventory():
    result = dict(
        status='TARGET_READ_ONLY_INVENTORY',
        timestamp=datetime.datetime.now(datetime.timezone.utc).isoformat(),
        python_version=platform.python_version(),
        python_executable=sys.executable,
        machine=platform.machine(),
        kernel=platform.release(),
        CPU_count=os.cpu_count(),
        packages={name: package(name) for name in ('dx-engine', 'dx_engine', 'numpy', 'onnxruntime')},
        new_model_runs=0, new_training=0, new_compile=0,
    )
    try:
        import dx_engine as dx
    except Exception as exc:
        result.update(status='DXRT_IMPORT_FAILED', error_type=type(exc).__name__, error=str(exc))
        return result, 2
    result['SDK_module'] = str(getattr(dx, '__file__', None))
    result['SDK_version_attribute'] = str(getattr(dx, '__version__', None))
    needed = ('InferenceEngine', 'InferenceOption', 'Configuration', 'DeviceStatus')
    result['SDK_classes'] = {name: hasattr(dx, name) for name in needed}
    if not all(result['SDK_classes'].values()):
        result['status'] = 'SDK_REQUIRED_API_MISSING'
        return result, 2
    result['API_signatures'] = {}
    for name in ('run_async', 'wait', 'get_input_tensors_info', 'get_output_tensors_info'):
        member = getattr(dx.InferenceEngine, name, None)
        if member is None:
            result['status'] = 'SDK_REQUIRED_API_MISSING'
            return result, 2
        try:
            result['API_signatures'][name] = str(inspect.signature(member))
        except (ValueError, TypeError):
            result['API_signatures'][name] = 'SIGNATURE_NOT_INTROSPECTABLE_PRESENT_ONLY'
    cfg = dx.Configuration()
    result['versions'] = {name: getattr(cfg, name)() for name in
                          ('get_version', 'get_driver_version', 'get_pcie_driver_version')}
    count = dx.DeviceStatus.get_device_count()
    if isinstance(count, bool) or not isinstance(count, int) or not 0 <= count <= 64:
        raise ValueError('Invalid SDK device count')
    result['device_count'] = count
    # Probe at most the first two device IDs. No status polling within inference.
    result['sampled_devices'] = []
    for device_id in range(min(count, 2)):
        status = dx.DeviceStatus.get_current_status(device_id)
        actual = status.get_id()
        if actual != device_id:
            raise ValueError('SDK requested/returned device ID disagreement')
        result['sampled_devices'].append(dict(device_id=actual,
            memory_free=int(status.get_memory_free()), memory_used=int(status.get_memory_used()),
            NPU_clock_MHz=[int(status.get_npu_clock(i)) for i in range(3)],
            temperature_C=[int(status.get_temperature(i)) for i in range(3)]))
    # No engine constructor: option round-trip proves API access only, no binding.
    opt = dx.InferenceOption()
    opt.devices = [0]
    opt.bound_option = dx.InferenceOption.BOUND_OPTION.NPU_0
    opt.use_ort = True
    opt.buffer_count = 1
    result['option_roundtrip'] = dict(devices=opt.devices,
        core=str(opt.bound_option), use_ort=bool(opt.use_ort), buffer_count=int(opt.buffer_count))
    result['two_devices_visible'] = count >= 2
    result['physical_binding_confirmed'] = False
    result['model_ABI_verified'] = False
    result['parallel_latency_verified'] = False
    result['status'] = 'TARGET_DEVICE_INVENTORY_ONLY' if count >= 2 else 'FEWER_THAN_TWO_PHYSICAL_DEVICES'
    return result, 0 if count >= 2 else 3


def worker():
    started = time.monotonic()
    try:
        value, rc = inventory()
    except Exception as exc:
        value, rc = dict(status='TARGET_PROBE_FAILED', error_type=type(exc).__name__,
                        error=str(exc), new_model_runs=0, new_training=0, new_compile=0), 2
    value['seconds'] = time.monotonic() - started
    print(json.dumps(value, indent=2, allow_nan=False), flush=True)
    return rc


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output', type=Path)
    args = p.parse_args()
    if args.output is not None and args.output.exists():
        p.error('Output already exists; choose a fresh path')
    env = dict(os.environ, OMP_NUM_THREADS='1', MKL_NUM_THREADS='1', OPENBLAS_NUM_THREADS='1')
    started = time.monotonic()
    child = subprocess.Popen([sys.executable, '-B', str(Path(__file__).resolve()), '--worker'],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, env=env, start_new_session=True)
    try:
        stdout, stderr = child.communicate(timeout=25)
        rc = child.returncode
        # Preserve any SDK banner separately from the worker JSON.
        boundary = stdout.find('{\n')
        result = json.loads(stdout[boundary:]) if boundary >= 0 else dict(status='NO_WORKER_JSON')
        result['SDK_stdout_prefix'] = stdout[:boundary] if boundary >= 0 else stdout
        result['SDK_stderr'] = stderr
    except subprocess.TimeoutExpired:
        os.killpg(child.pid, signal.SIGTERM)
        try:
            stdout, stderr = child.communicate(timeout=3)
        except subprocess.TimeoutExpired:
            os.killpg(child.pid, signal.SIGKILL)
            stdout, stderr = child.communicate()
        result, rc = dict(status='TARGET_PROBE_TIMEOUT', SDK_stdout=stdout,
            SDK_stderr=stderr, new_model_runs=0, new_training=0, new_compile=0), 124
    result['supervisor'] = dict(exit_code=rc, seconds=time.monotonic()-started, limit_seconds=25)
    text = json.dumps(result, indent=2, allow_nan=False)
    if args.output is not None:
        with args.output.open('x', encoding='utf-8') as f:
            f.write(text)
    print(text)
    return rc


if __name__ == '__main__':
    sys.exit(worker() if sys.argv[1:] == ['--worker'] else main())
