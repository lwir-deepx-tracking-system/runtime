# dx_app Integration Design for LWIR Runtime

## 1. Goal

Refactor `lwir-runtime` so that its object-detection stage uses the validated
dx_app C++ framework for YOLOv8n inference and postprocessing, while preserving
the existing LWIR camera, ByteTrack, target-selection, gimbal-control, queue,
metadata, and measurement responsibilities.

The product application remains `lwir-runtime`. dx_app is consumed as the
detection framework; the dx_app demo application does not replace the product.

## 2. Confirmed Requirements

- Language: C++
- Task: object detection
- Model family: YOLOv8n, fine-tuned for LWIR and compiled to DXNN
- Input device: i3system TE-EV1 LWIR camera (640x480). For this integration,
  the camera-to-runtime contract is explicitly `CV_16UC1`. Camera acquisition
  must validate that contract at runtime and reject mismatched frames with a
  diagnostic. The assumption can be revised later when the vendor SDK is
  integrated and measured.
- Priorities: throughput optimization, latency analysis, and reproducible
  performance measurements
- Existing downstream pipeline to preserve:
  `Detection -> ByteTrack -> TargetSelection -> Control`
- Framework constraint: inference applications use the dx_app `IFactory` and
  `AsyncDetectionRunner` pattern rather than exposing direct DX-RT calls across
  product code.

## 3. Current State

The repository already has a useful product-level pipeline:

```text
CameraThread
  -> PreprocessThread
  -> InferenceThread
  -> PostprocessThread
  -> TrackingThread
  -> TargetSelectionThread
  -> ControlThread
```

However, the following detection components are placeholders:

- `ModelInput` and `ModelOutput` contain metadata only.
- `YoloV8Preprocessor::preprocess()` does not create a tensor.
- `Inference::run()` does not load or invoke DX-RT.
- `YoloV8Postprocessor::postprocess()` does not decode YOLO output or run NMS.
- CMake does not link DX-RT or the dx_app framework.

## 4. Selected Architecture

Collapse the three placeholder detection workers into one product-facing
`DetectionThread`, backed by a dx_app detection pipeline:

```text
CameraThread
  -> Frame queue
  -> DetectionThread
       -> external-frame adapter
       -> AsyncDetectionRunner<YoloV8LwirFactory>
            -> LwirPreprocessor
            -> DX-RT inference
            -> dx_app YOLOv8Postprocessor
       -> Detection adapter
  -> Detection queue
  -> TrackingThread
  -> TargetSelectionThread
  -> ControlThread
```

### 4.1 Framework extension boundary

The current `AsyncDetectionRunner` owns CLI parsing and OpenCV input capture and
does not expose a public external-frame/result-callback API. Integration must add
a narrow, reusable extension point to the runner rather than introducing a
second custom inference runner:

- external frame source: supplies a `cv::Mat` plus `FrameMetadata`
- detection sink: receives ordered `std::vector<dxapp::DetectionResult>` plus
  the matching metadata

The existing CLI behavior must remain unchanged for current dx_app examples.

### 4.2 LWIR preprocessing

`LwirPreprocessor` implements the dx_app preprocessor interface. It is
responsible for:

1. validating the `CV_16UC1` contract, dimensions, buffer size, and stride at
   the acquisition boundary;
2. rejecting unsupported or inconsistent buffers with an explicit diagnostic;
3. applying configured clipping/AGC rules;
4. converting to the bit depth and channel count used during training;
5. applying YOLO letterbox resize;
6. preserving scale and padding in `PreprocessContext`;
7. applying normalization only when it is not already compiled into the DXNN.

The numerical LWIR transform must be configuration-driven because the exact
clipping and normalization policy is part of the model contract.

### 4.3 Postprocessing

Reuse dx_app `YOLOv8Postprocessor` when the fine-tuned model retains the normal
YOLOv8 output head. Configure class count, class names, score threshold, and NMS
threshold from the model YAML.

If the compiled model is a PPU model, select the matching PPU postprocessor;
do not apply the normal CPU YOLOv8 decoder a second time.

### 4.4 Result adaptation

Keep the current product-level `Detection` and `DetectionResult` types. A small
adapter copies dx_app fields into the product type and preserves frame metadata:

- bounding box
- confidence
- class ID
- class name
- frame ID and timestamps

## 5. Target File Structure

```text
lwir-runtime/
├── cmake/
│   └── DxApp.cmake
├── config/
│   └── model/
│       └── lwir_yolov8n.yaml
├── include/
│   ├── detection/
│   │   ├── detection_adapter.hpp
│   │   ├── dxapp_detection_pipeline.hpp
│   │   ├── lwir_preprocessor.hpp
│   │   └── lwir_yolov8_factory.hpp
│   └── pipeline/
│       └── detection_thread.hpp
└── src/
    ├── detection/
    │   ├── detection_adapter.cpp
    │   ├── dxapp_detection_pipeline.cpp
    │   └── lwir_preprocessor.cpp
    └── pipeline/
        └── detection_thread.cpp
```

The existing camera, tracking, target, control, logging, configuration, and
measurement directories remain in place.

## 6. Migration Mapping

### Preserve

- `camera/`
- `tracking/`
- `target/`
- `control/`
- `Frame`, `FrameMetadata`, `Detection`, `Track`, `StageMetric`
- downstream queues and CSV measurement output

### Replace after the new path is verified

- `preprocess/YoloV8Preprocessor`
- `inference/Inference`
- `postprocess/YoloV8Postprocessor`
- `PreprocessThread`
- `InferenceThread`
- `PostprocessThread`
- placeholder `ModelInput` and `ModelOutput`

Old files are first removed from the build graph. Physical deletion occurs only
after equivalent tests and the new pipeline build pass.

### Modify

- `Application`: own one detection pipeline/thread instead of three placeholder
  detection stages.
- `ComponentFactory`: create the detection pipeline as one unit.
- `CMakeLists.txt`: find/link DX-RT and include the selected dx_app framework
  sources through `cmake/DxApp.cmake`.
- model YAML: add LWIR transform, tensor contract, class metadata, and PPU/CPU
  postprocess selection.

## 7. Dependency Strategy

During integration, use a CMake cache variable `DX_APP_ROOT` and reference the
checked-out dx_app source tree. Do not duplicate framework sources while the
required dependency set is still changing.

After validation, package the exact required framework sources into a pinned
vendor dependency or a separately versioned library so `lwir-runtime` can build
outside the suite. The final package must not depend on an absolute developer
path.

## 8. Performance Measurement

Responsibility is split to avoid double-counting:

- dx_app detection stage: preprocessing, NPU inference, postprocessing,
  in-flight depth, and detection throughput;
- lwir-runtime: camera acquisition, queue wait, tracking, target selection,
  control, dropped frames, and end-to-end latency.

Every measurement record retains the original camera frame ID and acquisition
timestamp. Queue policy and maximum in-flight depth are configuration values.

## 9. Verification Strategy

1. Unit-test camera-format validation plus supported LWIR conversion paths,
   channel conversion, letterbox dimensions, padding, and coordinate
   restoration.
2. Unit-test dx_app-to-product detection conversion and metadata preservation.
3. Build without NPU hardware to validate interfaces and linkage where possible.
4. Run the existing downstream tracking tests unchanged.
5. On NPU hardware, verify device availability with `dxrt-cli -s`.
6. Run a known LWIR frame through sync mode for deterministic differential
   validation.
7. Run async mode and record stage latency, throughput, queue depth, drop count,
   and end-to-end latency.
8. Compare CPU and PPU postprocessing only when matching compiled models exist.

## 10. Acceptance Criteria

- `lwir-runtime` builds with DX-RT and the dx_app detection framework.
- No placeholder inference or YOLO decode path remains in the active build.
- A frame matching the verified camera SDK format can traverse detection and
  reach the existing tracking queue with its original metadata.
- YOLOv8 decode/NMS comes from the appropriate dx_app implementation.
- Tracking, target selection, and control interfaces remain compatible.
- Per-stage and end-to-end metrics remain available without duplicate timing.
- Existing tests pass, and new preprocessing/adapter tests pass.
- No absolute developer path is required in the final packaged build.

## 11. Open Model-Contract Values

Implementation can proceed structurally, but final inference correctness
requires these values from the fine-tuning/export pipeline:

- LWIR clipping or AGC algorithm and limits
- camera SDK byte order, row stride, and buffer ownership/lifetime
- model input channel count (1 or replicated 3)
- normalization range
- input tensor datatype and layout
- number and names of trained classes
- whether preprocessing is baked into the DXNN
- whether the DXNN uses CPU output tensors or PPU output

These values belong in `config/model/lwir_yolov8n.yaml` and must not be guessed
or hardcoded.
