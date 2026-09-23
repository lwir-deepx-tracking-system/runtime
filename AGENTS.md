  # lwir-runtime

  ## Project

  - C++ LWIR object-detection runtime
  - Model: fine-tuned and optimized YOLOv8n
  - Camera input contract: OpenCV CV_16UC1
  - Target runtime: DEEPX NPU

  ## Architecture

  - Model-specific settings are stored in config/model/*.yaml.
  - AppConfig owns all YAML parsing and validation.
  - Processing classes receive typed configuration structures.
  - LwirPreprocessor must not open or parse YAML files directly.
  - Detection output must use original-frame pixel coordinates.

  ## Preprocessing

  - Validate CV_16UC1 input.
  - Convert configured 16-bit LWIR range to 8-bit.
  - Convert single-channel input to three channels.
  - Apply letterbox resize to the model input dimensions.
  - Runtime preprocessing must match training/export preprocessing.

  ## Hardware availability

  - NPU hardware is currently unavailable.
  - Source editing, CPU-only tests and compilation may proceed without NPU.
  - Do not create fake inference outputs.
  - Mark DXNN loading, inference, tensor validation and NPU benchmarks as unverified.
  - Do not claim hardware inference passed without actual execution.

  ## Safety

  - Preserve existing user changes.
  - Inspect git status before editing.
  - Do not run git reset, checkout, clean, commit or push.
