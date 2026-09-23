# LWIR dx_app Integration Implementation Plan

> **For agent-driven workers:** REQUIRED SUB-SKILL: Use dx-swe-subagent-dev (recommended) or dx-swe-executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the placeholder YOLOv8 detection stages in `lwir-runtime` with a dx_app-backed asynchronous detection stage that accepts `CV_16UC1` TE-EV1 frames and preserves the existing tracking/control pipeline.

**Architecture:** Add an external-frame/result-sink extension to dx_app's existing `AsyncDetectionRunner`, then connect it to `lwir-runtime` through a `DetectionThread`. A custom `LwirPreprocessor` handles the assumed `CV_16UC1` contract; dx_app continues to own DX-RT inference and YOLOv8 postprocessing. Product `FrameMetadata`, `Detection`, tracking, target-selection, control, and measurement output remain product-owned.

**Tech Stack:** C++14-compatible application code, CMake, OpenCV, yaml-cpp, DX-RT, dx_app IFactory/AsyncDetectionRunner, pthreads, CTest.

**Approved spec:** [2026-09-23-dx-app-integration-design.md](../specs/2026-09-23-dx-app-integration-design.md)

**Git note:** This repository's instructions reserve Git operations for the user, so this plan contains no `git add`, commit, merge, or push steps.

---

## File map

### dx_app framework

- Modify: `../src/cpp_example/common/runner/async_detection_runner.hpp`
  - Add a reusable external-frame entry point without changing existing CLI behavior.
- Test: `../tests/cpp_example/test_async_external_api.cpp`
  - Compile-time contract test for source/sink callback types.

### lwir-runtime product

- Create: `include/detection/detection_adapter.hpp`
- Create: `src/detection/detection_adapter.cpp`
- Create: `include/detection/lwir_preprocessor.hpp`
- Create: `src/detection/lwir_preprocessor.cpp`
- Create: `include/detection/lwir_yolov8_factory.hpp`
- Create: `include/detection/dxapp_detection_pipeline.hpp`
- Create: `src/detection/dxapp_detection_pipeline.cpp`
- Create: `include/pipeline/detection_thread.hpp`
- Create: `src/pipeline/detection_thread.cpp`
- Create: `tests/detection/test_detection_adapter.cpp`
- Create: `tests/detection/test_lwir_preprocessor.cpp`
- Modify: `include/common/detection.hpp`
- Modify: `include/app/application.hpp`
- Modify: `src/app/application.cpp`
- Modify: `include/factory/component_factory.hpp`
- Modify: `src/factory/component_factory.cpp`
- Modify: `include/app/app_config.hpp`
- Modify: `src/app/app_config.cpp`
- Modify: `config/model/yolov8n.yaml`
- Modify: `CMakeLists.txt`
- Modify: `README.md`

The placeholder preprocess/inference/postprocess files stay on disk during migration but are removed from the active CMake target after the replacement build passes.

---

### Task 1: Record baseline and prerequisite evidence

**Files:**
- Read: `CMakeLists.txt`
- Read: `config/runtime.yaml`
- Read: `config/model/yolov8n.yaml`

- [ ] **Step 1: Verify DX-RT, OpenCV, and CMake availability**

Run from the dx_app root:

```bash
bash ../../scripts/sanity_check.sh --dx_rt
ldconfig -p | rg 'libdxrt|libdx_engine'
pkg-config --exists opencv4
cmake --version
```

Expected: sanity check passes, a DX-RT library is visible, OpenCV exits zero, and CMake is at least 3.14.

- [ ] **Step 2: Capture the current product build result**

```bash
cmake -S lwir-runtime -B /tmp/lwir-runtime-baseline-build
cmake --build /tmp/lwir-runtime-baseline-build -j2
```

Expected: either a successful placeholder build or a concrete dependency error recorded before modification.

---

### Task 2: Define the product detection contract and adapter

**Files:**
- Modify: `include/common/detection.hpp`
- Create: `include/detection/detection_adapter.hpp`
- Create: `src/detection/detection_adapter.cpp`
- Create: `tests/detection/test_detection_adapter.cpp`

- [ ] **Step 1: Write the failing adapter test**

The test constructs one dx_app detection and requires field-for-field conversion:

```cpp
#include <cassert>
#include <vector>

#include "common/detection.hpp"
#include "detection/detection_adapter.hpp"

int main() {
    dxapp::DetectionResult input({10.0f, 20.0f, 30.0f, 40.0f}, 0.75f, 2, "target");
    const Detection output = to_product_detection(input);
    assert(output.x1 == 10.0f);
    assert(output.y1 == 20.0f);
    assert(output.x2 == 30.0f);
    assert(output.y2 == 40.0f);
    assert(output.confidence == 0.75f);
    assert(output.class_id == 2);
    assert(output.class_name == "target");
    return 0;
}
```

- [ ] **Step 2: Build the test and verify Red**

Run through CMake/CTest after adding only the test target.

Expected: compilation fails because `Detection` fields and `to_product_detection` do not yet exist.

- [ ] **Step 3: Implement the minimal product type and adapter**

`Detection` contains `x1`, `y1`, `x2`, `y2`, `confidence`, `class_id`, and `class_name`. `to_product_detection` validates that the dx_app box contains four coordinates, then copies all fields.

- [ ] **Step 4: Run the adapter test**

```bash
ctest --test-dir /tmp/lwir-runtime-build -R detection_adapter --output-on-failure
```

Expected: `detection_adapter` passes.

---

### Task 3: Implement and test the CV_16UC1 LWIR preprocessor

**Files:**
- Create: `include/detection/lwir_preprocessor.hpp`
- Create: `src/detection/lwir_preprocessor.cpp`
- Create: `tests/detection/test_lwir_preprocessor.cpp`
- Modify: `config/model/yolov8n.yaml`

- [ ] **Step 1: Extend model configuration**

Use an explicit, reproducible assumed input contract:

```yaml
camera_input:
  opencv_type: CV_16UC1
  width: 640
  height: 480
  clip_min: 0
  clip_max: 65535
  channel_mode: replicate_gray_to_rgb

input:
  width: 640
  height: 640

preprocess:
  resize: letterbox
  normalize: false
  pad_value: 114
```

- [ ] **Step 2: Write failing tests**

Test cases:

1. `CV_8UC1` input throws `std::invalid_argument`.
2. `CV_16UC1` 640x480 input produces a non-empty 640x640 three-channel image.
3. output type is `CV_8UC3`.
4. `PreprocessContext` records original size 640x480, input size 640x640, scale 1.0, and vertical padding 80.
5. pixels at `clip_min` map to 0 and pixels at `clip_max` map to 255 before padding.

- [ ] **Step 3: Verify Red**

```bash
cmake --build /tmp/lwir-runtime-build --target test_lwir_preprocessor -j2
ctest --test-dir /tmp/lwir-runtime-build -R lwir_preprocessor --output-on-failure
```

Expected: compilation fails because `LwirPreprocessor` does not exist.

- [ ] **Step 4: Implement `LwirPreprocessor`**

The class derives from dx_app `IPreprocessor` and implements:

```cpp
void process(const cv::Mat& input, cv::Mat& output, dxapp::PreprocessContext& ctx) override;
int getInputWidth() const override;
int getInputHeight() const override;
int getColorConversion() const override;
```

Processing sequence:

```text
validate CV_16UC1
-> convertTo(CV_8UC1, 255/(clip_max-clip_min), -clip_min*scale)
-> cvtColor(GRAY2RGB)
-> letterbox with aspect-ratio preservation and pad value 114
-> populate PreprocessContext
```

- [ ] **Step 5: Verify Green**

Run the test command from Step 3.

Expected: all `lwir_preprocessor` assertions pass.

---

### Task 4: Add an external-frame API to AsyncDetectionRunner

**Files:**
- Modify: `../src/cpp_example/common/runner/async_detection_runner.hpp`
- Create: `../tests/cpp_example/test_async_external_api.cpp`

- [ ] **Step 1: Write the compile-contract test**

Require these public aliases and method to exist:

```cpp
using ExternalFrameSource = std::function<bool(cv::Mat&, std::uint64_t&)>;
using ExternalDetectionSink = std::function<void(
    std::uint64_t,
    const std::vector<dxapp::DetectionResult>&,
    const dxapp::AsyncFrameMetrics&)>;

int run_external(
    const std::string& model_path,
    const std::string& config_path,
    ExternalFrameSource source,
    ExternalDetectionSink sink,
    std::size_t max_inflight);
```

- [ ] **Step 2: Verify Red**

Compile the contract test.

Expected: compilation fails because the aliases, `AsyncFrameMetrics`, and `run_external` are absent.

- [ ] **Step 3: Implement the narrow extension**

Add:

```cpp
struct AsyncFrameMetrics {
    double preprocess_ms = 0.0;
    double inference_ms = 0.0;
    double postprocess_ms = 0.0;
};
```

`run_external` must:

1. initialize the same DX-RT engine, factory, preprocessor, and postprocessor used by `run()`;
2. validate model/runtime compatibility;
3. read frames only through `ExternalFrameSource`;
4. apply existing back-pressure and preallocated input buffers;
5. submit through `InferenceEngine::RunAsync`;
6. invoke `ExternalDetectionSink` after postprocessing with the original frame ID;
7. wait for the last job and all callbacks before returning;
8. avoid visualization, file output, and CLI parsing;
9. leave existing `run(argc, argv)` behavior unchanged.

- [ ] **Step 4: Verify the compile contract and existing examples**

```bash
cmake --build build --target yolov8n_sync yolov8n_async -j2
```

Expected: both existing targets compile and the new contract test compiles.

---

### Task 5: Add the LWIR YOLOv8 factory and product pipeline

**Files:**
- Create: `include/detection/lwir_yolov8_factory.hpp`
- Create: `include/detection/dxapp_detection_pipeline.hpp`
- Create: `src/detection/dxapp_detection_pipeline.cpp`
- Create: `include/pipeline/detection_thread.hpp`
- Create: `src/pipeline/detection_thread.cpp`

- [ ] **Step 1: Implement `LwirYoloV8Factory`**

The factory derives from `dxapp::IDetectionFactory` and returns:

```text
createPreprocessor  -> LwirPreprocessor
createPostprocessor -> dxapp::YOLOv8Postprocessor
createVisualizer    -> dxapp::DetectionVisualizer
getModelName        -> "LWIR_YOLOv8n"
getTaskType         -> "object_detection"
```

`loadConfig` applies thresholds, class count, and class names.

- [ ] **Step 2: Implement `DxAppDetectionPipeline`**

Public contract:

```cpp
class DxAppDetectionPipeline {
public:
    DxAppDetectionPipeline(std::string model_path, std::string model_config_path,
                           std::size_t max_inflight);
    void run(ThreadSafeQueue<Frame>& input,
             ThreadSafeQueue<DetectionResult>& output,
             bool measurement_enabled,
             std::vector<StageMetric>& metrics);
};
```

The source callback pops `Frame`, caches metadata by frame ID, and returns the image. The sink callback converts detections, restores metadata, records detection-stage metrics, and pushes `DetectionResult`.

- [ ] **Step 3: Implement `DetectionThread`**

`DetectionThread` owns no inference engine. It starts one pthread whose `run()` delegates to `DxAppDetectionPipeline::run()` and closes the detection queue on exit.

- [ ] **Step 4: Compile the new files immediately**

```bash
cmake --build /tmp/lwir-runtime-build --target lwir_runtime -j2
```

Expected: new detection files compile before product migration continues.

---

### Task 6: Migrate Application and ComponentFactory

**Files:**
- Modify: `include/app/application.hpp`
- Modify: `src/app/application.cpp`
- Modify: `include/factory/component_factory.hpp`
- Modify: `src/factory/component_factory.cpp`
- Modify: `include/app/app_config.hpp`
- Modify: `src/app/app_config.cpp`
- Modify: `config/runtime.yaml`

- [ ] **Step 1: Replace three queues with the detection boundary**

Remove active members:

```text
model_input_queue_
model_output_queue_
```

Keep:

```text
frame_queue_
detection_queue_
track_queue_
target_selection_queue_
```

- [ ] **Step 2: Replace three components and threads**

Remove active ownership of `Preprocessor`, `Inference`, `Postprocessor`, `PreprocessThread`, `InferenceThread`, and `PostprocessThread`.

Add ownership of:

```cpp
std::unique_ptr<DxAppDetectionPipeline> detection_pipeline_;
std::unique_ptr<DetectionThread> detection_thread_;
```

- [ ] **Step 3: Start and join the new pipeline in dependency order**

Start consumers before producers:

```text
Control -> TargetSelection -> Tracking -> Detection -> Camera
```

Join in producer-to-consumer order:

```text
Camera -> Detection -> Tracking -> TargetSelection -> Control
```

- [ ] **Step 4: Update configuration**

Replace separate CPU preprocess/inference/postprocess selections with:

```yaml
detection:
  backend: dx_app_async
  max_inflight: 4
```

Retain detector model config and downstream tracking/control settings.

- [ ] **Step 5: Build immediately**

```bash
cmake --build /tmp/lwir-runtime-build --target lwir_runtime -j2
```

Expected: application compiles with the new ownership graph.

---

### Task 7: Update CMake and retire placeholders from the build

**Files:**
- Modify: `CMakeLists.txt`
- Create: `cmake/DxApp.cmake`

- [ ] **Step 1: Add dependency discovery**

`DxApp.cmake` defines a cache path and validates it:

```cmake
set(DX_APP_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." CACHE PATH "dx_app source root")
if(NOT EXISTS "${DX_APP_ROOT}/src/cpp_example/common/base/i_factory.hpp")
    message(FATAL_ERROR "DX_APP_ROOT does not point to dx_app: ${DX_APP_ROOT}")
endif()
find_package(dxrt REQUIRED)
```

- [ ] **Step 2: Replace source lists**

Remove placeholder preprocess/inference/postprocess and their three thread sources from `lwir_runtime`. Add detection sources and `detection_thread.cpp`.

- [ ] **Step 3: Link and include**

Add dx_app include directories plus `dxrt`, while preserving OpenCV, yaml-cpp, and Threads.

- [ ] **Step 4: Enable CTest targets**

Add `test_detection_adapter` and `test_lwir_preprocessor` and register both with `add_test`.

- [ ] **Step 5: Configure from a clean build directory**

```bash
cmake -S lwir-runtime -B /tmp/lwir-runtime-build \
  -DDX_APP_ROOT=/workspace/dx-all-suite/dx-runtime/dx_app
```

Expected: configuration succeeds without an absolute path embedded in source files.

---

### Task 8: Full TDD and completion verification

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Run unit tests**

```bash
cmake --build /tmp/lwir-runtime-build -j2
ctest --test-dir /tmp/lwir-runtime-build --output-on-failure
```

Expected: adapter, LWIR preprocessing, and existing enabled tests pass.

- [ ] **Step 2: Verify placeholder code is absent from the active target**

```bash
cmake --build /tmp/lwir-runtime-build --target lwir_runtime --verbose
```

Expected: compile commands do not include old preprocess, inference, postprocess, or their three old thread source files.

- [ ] **Step 3: Verify NPU readiness**

```bash
dxrt-cli -s
```

Expected: device status is healthy. If hardware is unavailable, record this as a hardware-gated verification rather than claiming runtime success.

- [ ] **Step 4: Verify model presence**

```bash
test -f lwir-runtime/models/yolov8n.dxnn
```

Expected: the fine-tuned model exists. If absent, build/tests can pass but inference execution remains model-gated.

- [ ] **Step 5: Update README**

Document `DX_APP_ROOT`, the assumed `CV_16UC1` contract, model placement, build/test commands, stage metrics, and the future TE-EV1 SDK verification step.

- [ ] **Step 6: Run final verification skill**

Invoke `dx-agent-verify` and report exact build/test/hardware/model evidence. Do not claim NPU inference success unless a real frame and DXNN were executed.
