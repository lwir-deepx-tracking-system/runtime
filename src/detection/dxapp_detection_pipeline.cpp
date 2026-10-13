#include "detection/dxapp_detection_pipeline.hpp"

#include <algorithm>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

#include "common/common_util.hpp"

struct DxAppDetectionPipeline::NpuState
{
  struct Request
  {
    std::vector<std::uint8_t>      input_buffer;
    PreprocessContext              context;
    IDetectionPipeline::Completion completion;
    DetectionTiming                timing;
    std::vector<Detection>         detections;
    std::exception_ptr             error;

    std::mutex              job_mutex;
    std::condition_variable job_cv;
    int                     job_id = -1;
  };

  dxrt::InferenceOption inference_option;
  dxrt::InferenceEngine engine;
  bool                  is_float_input = false;
  bool                  is_nhwc        = false;

  std::mutex              inflight_mutex;
  std::condition_variable inflight_cv;
  std::size_t             inflight     = 0;
  std::size_t             max_inflight = 1;

  std::mutex                         completed_mutex;
  std::condition_variable            completed_cv;
  std::deque<std::shared_ptr<Request>> completed_requests;
  bool                               stopping = false;
  std::thread                        completion_worker;

  static dxrt::InferenceOption make_inference_option()
  {
    // job별 NPU core 시간을 얻기 위해 engine 생성 전에 profiler를 켠다.
    dxrt::Configuration::GetInstance().SetEnable(
        dxrt::Configuration::ITEM::PROFILER, true);

    dxrt::InferenceOption option;
    // CPU fallback을 끄고 모델의 NPU task만 실행한다.
    option.useORT = false;
    return option;
  }

  NpuState(const ModelConfig &config, std::size_t limit)
      : inference_option(make_inference_option()),
        engine(config.path, inference_option),
        max_inflight(std::max<std::size_t>(1, limit))
  {
    if(!dxapp::minversionforRTandCompiler(&engine))
      throw std::runtime_error("DXRT runtime/compiler version is incompatible with this model");

    const auto &inputs = engine.GetInputs();
    if(inputs.empty())
      throw std::runtime_error("DXNN model has no input tensor");

    const auto input_shape  = inputs.front().shape();
    int        input_width  = 0;
    int        input_height = 0;
    ::parseInputShape(input_shape, input_width, input_height);
    if(input_width != config.input.width || input_height != config.input.height)
      throw std::runtime_error("DXNN input dimensions do not match model configuration");

    is_float_input = inputs.front().type() == dxrt::DataType::FLOAT;
    is_nhwc        = ::isInputNHWC(input_shape);

    completion_worker = std::thread([this]
                                    { complete_requests(); });
  }

  ~NpuState()
  {
    try
    {
      wait_idle();
    }
    catch(...)
    {
    }

    {
      std::lock_guard<std::mutex> lock(completed_mutex);
      stopping = true;
    }
    completed_cv.notify_all();
    if(completion_worker.joinable())
      completion_worker.join();
  }

  void acquire_slot()
  {
    std::unique_lock<std::mutex> lock(inflight_mutex);
    inflight_cv.wait(lock, [this]
                     { return inflight < max_inflight; });
    ++inflight;
  }

  void release_slot()
  {
    {
      std::lock_guard<std::mutex> lock(inflight_mutex);
      if(inflight > 0)
        --inflight;
    }
    inflight_cv.notify_all();
  }

  void wait_idle()
  {
    std::unique_lock<std::mutex> lock(inflight_mutex);
    inflight_cv.wait(lock, [this]
                     { return inflight == 0; });
  }

  static double get_npu_processing_ms(int job_id)
  {
    const dxrt::JobMetrics metrics =
        dxrt::Profiler::GetInstance().GetJobMetrics(job_id);
    if(!metrics.valid)
      throw std::runtime_error("DXRT job profiler metrics are unavailable");

    double inference_core_us = 0.0;
    bool   found_npu_metric  = false;
    for(const dxrt::TaskMetrics &task : metrics.tasks)
    {
      for(const auto &device_entry : task.devices)
      {
        const dxrt::NpuDeviceMetrics &device = device_entry.second;
        if(!device.valid || device.inference_core_all_us <= 0.0)
          continue;
        inference_core_us += device.inference_core_all_us;
        found_npu_metric = true;
      }
    }

    if(!found_npu_metric)
      throw std::runtime_error("DXRT job has no valid NPU core timing");
    return inference_core_us / 1000.0;
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
        completed_requests.pop_front();
      }

      try
      {
        const int job_id = wait_for_job_id(request);

        // callback 반환 뒤 Wait해야 job별 profiler 값이 완성된다.
        const dxrt_status_t wait_status =
            dxrt_engine_wait(engine.handle(), job_id, nullptr);
        if(wait_status != DXRT_OK)
        {
          const char *message = dxrt_last_error_message();
          throw std::runtime_error(
              message ? message : "DXRT failed while waiting for a job");
        }
        request->timing.npu_processing_duration_ms =
            get_npu_processing_ms(job_id);
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
      release_slot();
    }
  }

  void run_async(
      const cv::Mat                 &model_input,
      const PreprocessContext       &context,
      DetectionTiming                timing,
      IDetectionPipeline::Completion completion)
  {
    if(model_input.empty() || !model_input.isContinuous())
      throw std::invalid_argument("Preprocessed NPU input must be non-empty and contiguous");

    auto request = std::make_shared<Request>();
    request->input_buffer.resize(engine.GetInputSize());
    request->context    = context;
    request->completion = std::move(completion);
    request->timing     = timing;

    if(is_float_input)
    {
      const auto        float_buffer = convertToFloatBuffer(model_input, is_nhwc);
      const std::size_t byte_count   = float_buffer.size() * sizeof(float);
      if(byte_count != request->input_buffer.size())
        throw std::runtime_error("Float input tensor size does not match DXRT input size");
      std::memcpy(request->input_buffer.data(), float_buffer.data(), byte_count);
    }
    else
    {
      const std::size_t byte_count = model_input.total() * model_input.elemSize();
      if(byte_count != request->input_buffer.size())
        throw std::runtime_error("UInt8 input tensor size does not match DXRT input size");
      std::memcpy(request->input_buffer.data(), model_input.data, byte_count);
    }

    // 모델 입력 tensor 변환과 복사까지 전처리에 포함한다.
    const auto inference_started_at = DetectionTiming::Clock::now();
    request->timing.preprocess_finished_at = inference_started_at;
    request->timing.inference_started_at = inference_started_at;

    auto *user_data = new std::shared_ptr<Request>(request);
    int job_id = -1;
    try
    {
      job_id = engine.RunAsync(request->input_buffer.data(), user_data);
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

  bool ort_configured() const { return engine.IsOrtConfigured(); }

  void register_callback(IPostprocessor *postprocessor)
  {
    engine.RegisterCallback(
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
          try
          {
            enqueue_completed(request);
          }
          catch(...)
          {
            try
            {
              request->completion(
                  {}, std::current_exception(), request->timing);
            }
            catch(...)
            {
            }
            release_slot();
          }
          return 0;
        });
  }
};

DxAppDetectionPipeline::DxAppDetectionPipeline(
    const ModelConfig     &model_config,
    const DetectionConfig &detection_config)
    : preprocessor_(std::make_unique<LwirPreprocessor>(model_config)),
      npu_(std::make_unique<NpuState>(model_config, detection_config.max_inflight))
{
  postprocessor_ =
      std::make_unique<Postprocessor>(model_config, npu_->ort_configured());
  npu_->register_callback(postprocessor_.get());
}

DxAppDetectionPipeline::~DxAppDetectionPipeline()
{
  wait_idle();
}

void DxAppDetectionPipeline::submit_async(
    const FrameContextPtr &frame,
    Completion            completion)
{
  if(!frame)
    throw std::invalid_argument("Cannot submit an empty frame");

  npu_->acquire_slot();
  try
  {
    cv::Mat           model_input;
    PreprocessContext context;
    DetectionTiming timing;
    timing.frame_id = frame->metadata.frame_id;
    timing.preprocess_started_at = DetectionTiming::Clock::now();
    preprocessor_->process(frame->image, model_input, context);
    npu_->run_async(
        model_input, context, timing, std::move(completion));
  }
  catch(...)
  {
    npu_->release_slot();
    throw;
  }
}

void DxAppDetectionPipeline::wait_idle()
{
  if(npu_)
    npu_->wait_idle();
}
