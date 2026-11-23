#include "detection/dxrt_detection_pipeline.hpp"

#include <algorithm>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

#include <opencv2/core.hpp>

#include "inference/dxrt_session.hpp"

namespace {
/**
 * @brief Detect if a 4D input shape is NHWC layout.
 *
 * Heuristic: if the last dimension is small (≤4, typical for image channels)
 * and the second dimension is larger, it's likely NHWC [N,H,W,C].
 * Otherwise, assume NCHW [N,C,H,W].
 */
bool isInputNHWC(const std::vector<int64_t>& shape) {
    if (shape.size() >= 4) {
        return (shape[3] <= 4 && shape[1] > shape[3]);
    }
    return false;
}

/**
 * @brief Parse model input shape to get spatial dimensions (H, W).
 *
 * Handles NCHW [N,C,H,W] and NHWC [N,H,W,C] tensor layouts automatically.
 * For 3D shapes [N/C, H, W], uses shape[1] and shape[2].
 * For 2D shapes [H, W], uses shape[0] and shape[1].
 */
void parseInputShape(const std::vector<int64_t>& shape, int& width, int& height) {
    if (shape.size() >= 4) {
        if (isInputNHWC(shape)) {
            // NHWC: [N, H, W, C]
            height = static_cast<int>(shape[1]);
            width  = static_cast<int>(shape[2]);
        } else {
            // NCHW: [N, C, H, W]
            height = static_cast<int>(shape[2]);
            width  = static_cast<int>(shape[3]);
        }
    } else if (shape.size() == 3) {
        height = static_cast<int>(shape[1]);
        width  = static_cast<int>(shape[2]);
    } else if (shape.size() >= 2) {
        height = static_cast<int>(shape[0]);
        width  = static_cast<int>(shape[1]);
    } else {
        height = 0;
        width  = 0;
    }
}

/**
 * @brief Fill a flat float buffer from a single-channel uint8 image.
 */
void fillGrayscaleBuffer(const cv::Mat& img, int h, int w,
                                std::vector<float>& buf) {
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            buf[y * w + x] = img.at<uint8_t>(y, x) / 255.0f;
}

/**
 * @brief Fill a flat float buffer from a 3-channel image in NHWC (HWC) layout.
 */
void fillNHWCBuffer(const cv::Mat& img, int h, int w, int c,
                           std::vector<float>& buf) {
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            for (int ch = 0; ch < c; ++ch)
                buf[y * w * c + x * c + ch] = img.at<cv::Vec3b>(y, x)[ch] / 255.0f;
}

/**
 * @brief Fill a flat float buffer from a 3-channel image in NCHW (CHW) layout.
 */
void fillNCHWBuffer(const cv::Mat& img, int h, int w, int c,
                           std::vector<float>& buf) {
    for (int ch = 0; ch < c; ++ch)
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                buf[ch * h * w + y * w + x] = img.at<cv::Vec3b>(y, x)[ch] / 255.0f;
}

/**
 * @brief Convert uint8 image to float32 buffer with specified layout.
 * @param img Input image (CV_8UC3 or CV_8UC1)
 * @param nhwc If true, output is HWC layout; if false, CHW layout
 * @return Float buffer normalized to [0, 1]
 */
std::vector<float> convertToFloatBuffer(const cv::Mat& img, bool nhwc) {
    int h = img.rows, w = img.cols, c = img.channels();
    std::vector<float> buf(h * w * c);
    if (c == 1) {
        fillGrayscaleBuffer(img, h, w, buf);
    } else if (nhwc) {
        fillNHWCBuffer(img, h, w, c, buf);
    } else {
        fillNCHWBuffer(img, h, w, c, buf);
    }
    return buf;
}
}

struct DxrtDetectionPipeline::NpuState
{
  struct Request
  {
    std::size_t                    slot = 0;
    PreprocessContext              context;
    IDetectionPipeline::Completion completion;
    DetectionTiming                timing;
    std::vector<Detection>         detections;
    std::exception_ptr             error;

    std::mutex              job_mutex;
    std::condition_variable job_cv;
    int                     job_id = -1;
  };

  DxrtSession session;
  bool                  is_float_input = false;
  bool                  is_nhwc        = false;

  std::mutex              inflight_mutex;
  std::condition_variable inflight_cv;
  std::size_t             inflight     = 0;
  std::vector<std::vector<std::uint8_t>> input_buffers;
  std::vector<std::size_t> free_slots;

  std::mutex                         completed_mutex;
  std::condition_variable            completed_cv;
  std::vector<std::shared_ptr<Request>> completed_requests;
  bool                               stopping = false;
  std::thread                        completion_worker;

  NpuState(
      const ModelConfig &model_config,
      const InferenceConfig &inference_config)
      : session(model_config.path, inference_config, true)
  {
    if(!session.supports_model_version())
      throw std::runtime_error("DXRT runtime/compiler version is incompatible with this model");

    const auto &inputs = session.inputs();
    if(inputs.empty())
      throw std::runtime_error("DXNN model has no input tensor");

    const auto input_shape  = inputs.front().shape();
    int        input_width  = 0;
    int        input_height = 0;
    parseInputShape(input_shape, input_width, input_height);
    if(input_width != model_config.input.width ||
       input_height != model_config.input.height)
      throw std::runtime_error("DXNN input dimensions do not match model configuration");

    is_float_input = inputs.front().type() == dxrt::DataType::FLOAT;
    is_nhwc        = isInputNHWC(input_shape);

    const auto count = std::max<std::size_t>(1, inference_config.max_inflight);
    input_buffers.resize(count);
    free_slots.reserve(count);
    completed_requests.reserve(count);
    for (std::size_t slot = 0; slot < count; ++slot) {
      input_buffers[slot].resize(session.input_size());
      free_slots.push_back(slot);
    }

    completion_worker = std::thread([this]
                                    { complete_requests(); });
  }

  ~NpuState()
  {
    // 콜백 합류 실패를 삼킨 채 SDK 객체를 파괴하지 않는다.
    wait_idle();

    {
      std::lock_guard<std::mutex> lock(completed_mutex);
      stopping = true;
    }
    completed_cv.notify_all();
    if(completion_worker.joinable())
      completion_worker.join();
  }

  // A slot stays checked out through SDK wait and downstream completion.
  std::size_t acquire_slot()
  {
    std::unique_lock<std::mutex> lock(inflight_mutex);
    inflight_cv.wait(lock, [this] { return !free_slots.empty(); });
    const auto slot = free_slots.back();
    free_slots.pop_back();
    ++inflight;
    return slot;
  }

  void release_slot(std::size_t slot)
  {
    {
      std::lock_guard<std::mutex> lock(inflight_mutex);
      free_slots.push_back(slot);
      --inflight;
    }
    inflight_cv.notify_all();
  }

  void wait_idle() noexcept
  {
    std::unique_lock<std::mutex> lock(inflight_mutex);
    // 성공·오류 콜백 모두 반환한 뒤 release_slot()이 감소시킨 값만 확인한다.
    // SDK 작업/프로파일 오류는 complete_requests()에서 Completion으로 전달한다.
    inflight_cv.wait(lock, [this]
                     { return inflight == 0; });
  }

  static void populate_npu_timing(int job_id, DetectionTiming &timing)
  {
    const dxrt::JobMetrics metrics =
        dxrt::Profiler::GetInstance().GetJobMetrics(job_id);
    if(!metrics.valid)
      throw std::runtime_error("DXRT job profiler metrics are unavailable");

    bool   found_npu_metric  = false;
    for(const dxrt::TaskMetrics &task : metrics.tasks)
    {
      if(task.valid && task.task_name == "cpu_0")
      {
        timing.cpu_task_us = task.cpu_task_us;
        timing.cpu_task_valid = true;
      }
      for(const auto &device_entry : task.devices)
      {
        const dxrt::NpuDeviceMetrics &device = device_entry.second;
        if(!device.valid || device.inference_core_all_us <= 0.0)
          continue;
        timing.npu_input_format_duration_us += device.input_format_us;
        timing.h2d_duration_us += device.h2d_us;
        timing.npu_processing_duration_ms +=
            device.inference_core_all_us / 1000.0;
        timing.d2h_duration_us += device.d2h_us;
        timing.npu_output_format_duration_us += device.output_format_us;
        timing.npu_task_total_duration_us += device.total_us;
        timing.npu_devices.push_back({
            task.task_name, device_entry.first, device.input_format_us,
            device.h2d_us, device.inference_core_all_us,
            device.inference_core_0_us, device.inference_core_1_us,
            device.inference_core_2_us, device.d2h_us,
            device.output_format_us, device.total_us});
        found_npu_metric = true;
      }
    }

    if(!found_npu_metric)
      throw std::runtime_error("DXRT job has no valid NPU core timing");
    timing.npu_profiler_valid = true;
  }

  static int wait_for_job_id(const std::shared_ptr<Request> &request)
  {
    std::unique_lock<std::mutex> lock(request->job_mutex);
    request->job_cv.wait(lock, [&request]
                         { return request->job_id >= 0; });
    return request->job_id;
  }

  void enqueue_completed(const std::shared_ptr<Request> &request)
  {
    {
      std::lock_guard<std::mutex> lock(completed_mutex);
      completed_requests.push_back(request);
    }
    completed_cv.notify_one();
  }

  void complete_requests()
  {
    for(;;)
    {
      std::shared_ptr<Request> request;
      {
        std::unique_lock<std::mutex> lock(completed_mutex);
        completed_cv.wait(lock, [this]
                          { return stopping || !completed_requests.empty(); });
        if(stopping && completed_requests.empty())
          return;
        request = std::move(completed_requests.front());
        completed_requests.erase(completed_requests.begin());
      }

      try
      {
        const int job_id = wait_for_job_id(request);

        // callback 반환 뒤 Wait해야 job별 profiler 값이 완성된다.
        session.wait(job_id);
        populate_npu_timing(job_id, request->timing);
      }
      catch(...)
      {
        if(!request->error)
          request->error = std::current_exception();
      }

      try
      {
        request->completion(
            std::move(request->detections),
            request->error,
            request->timing);
      }
      catch(...)
      {
      }
      release_slot(request->slot);
    }
  }

  void run_async(
      std::size_t slot,
      const cv::Mat                 &model_input,
      const PreprocessContext       &context,
      DetectionTiming                timing,
      IDetectionPipeline::Completion completion)
  {
    if(model_input.empty() || !model_input.isContinuous())
      throw std::invalid_argument("Preprocessed NPU input must be non-empty and contiguous");

    auto request = std::make_shared<Request>();
    request->slot = slot;
    auto& input_buffer = input_buffers[slot];
    request->context    = context;
    request->completion = std::move(completion);
    request->timing     = timing;

    if(is_float_input)
    {
      const auto        float_buffer = convertToFloatBuffer(model_input, is_nhwc);
      const std::size_t byte_count   = float_buffer.size() * sizeof(float);
      if(byte_count != input_buffer.size())
        throw std::runtime_error("Float input tensor size does not match DXRT input size");
      std::memcpy(input_buffer.data(), float_buffer.data(), byte_count);
    }
    else
    {
      const std::size_t byte_count = model_input.total() * model_input.elemSize();
      if(byte_count != input_buffer.size())
        throw std::runtime_error("UInt8 input tensor size does not match DXRT input size");
      std::memcpy(input_buffer.data(), model_input.data, byte_count);
    }

    // 모델 입력 tensor 변환과 복사까지 전처리에 포함한다.
    const auto inference_started_at = DetectionTiming::Clock::now();
    request->timing.preprocess_finished_at = inference_started_at;
    request->timing.inference_started_at = inference_started_at;

    auto *user_data = new std::shared_ptr<Request>(request);
    int job_id = -1;
    try
    {
      job_id = session.submit(input_buffer.data(), user_data);
    }
    catch(...)
    {
      delete user_data;
      throw;
    }
    if(job_id < 0)
    {
      delete user_data;
      throw std::runtime_error("DXRT RunAsync failed to submit inference");
    }
    {
      std::lock_guard<std::mutex> lock(request->job_mutex);
      request->job_id = job_id;
    }
    request->job_cv.notify_all();
  }

  void register_callback(IPostprocessor *postprocessor)
  {
    session.set_callback(
        [this, postprocessor](dxrt::TensorPtrs &outputs, void *user_data) -> int
        {
          std::unique_ptr<std::shared_ptr<Request>> holder(
              static_cast<std::shared_ptr<Request> *>(user_data));
          const auto request = *holder;

          const auto postprocess_started_at = DetectionTiming::Clock::now();
          request->timing.inference_finished_at = postprocess_started_at;
          request->timing.postprocess_started_at = postprocess_started_at;

          std::vector<Detection> detections;
          std::exception_ptr     error;
          try
          {
            detections = postprocessor->process(outputs, request->context);
          }
          catch(...)
          {
            error = std::current_exception();
          }
          request->timing.postprocess_finished_at = DetectionTiming::Clock::now();
          request->error = error;
          request->detections = std::move(detections);
          // The queue reserves one entry per slot, so publishing never reallocates.
          enqueue_completed(request);
          return 0;
        });
  }
};

DxrtDetectionPipeline::DxrtDetectionPipeline(
    const ModelConfig     &model_config,
    const InferenceConfig &inference_config,
    std::unique_ptr<IPreprocessor> preprocessor,
    std::unique_ptr<IPostprocessor> postprocessor)
    : preprocessor_(std::move(preprocessor)), postprocessor_(std::move(postprocessor)),
      npu_(std::make_unique<NpuState>(model_config, inference_config))
{
  if (!preprocessor_ || !postprocessor_) throw std::invalid_argument("Detection strategies must not be null");
  npu_->register_callback(postprocessor_.get());
}

DxrtDetectionPipeline::~DxrtDetectionPipeline()
{
  wait_idle();
}

void DxrtDetectionPipeline::submit_async(
    const FrameContextPtr &frame,
    Completion            completion)
{
  if(!frame)
    throw std::invalid_argument("Cannot submit an empty frame");

  const auto slot = npu_->acquire_slot();
  try
  {
    cv::Mat           model_input;
    PreprocessContext context;
    DetectionTiming timing;
    timing.frame_id = frame->metadata.frame_id;
    timing.preprocess_started_at = DetectionTiming::Clock::now();
    preprocessor_->process(frame->image, model_input, context);
    npu_->run_async(
        slot, model_input, context, timing, std::move(completion));
  }
  catch(...)
  {
    npu_->release_slot(slot);
    throw;
  }
}

void DxrtDetectionPipeline::wait_idle() noexcept
{
  if(npu_)
    npu_->wait_idle();
}
