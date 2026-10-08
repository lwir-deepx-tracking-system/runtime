#include "detection/dxapp_detection_pipeline.hpp"

#include <cstdint>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <utility>
#include "common/threadsafequeue.hpp"

struct DxAppDetectionPipeline::NpuState
{
    struct Request
    {
        ThreadSafeQueue<bool> completed;
        std::vector<Detection> detections;
        std::exception_ptr error;
        dxapp::PreprocessContext context;
    };

    dxrt::InferenceOption inference_option;
    dxrt::InferenceEngine engine;
    std::unique_ptr<dxapp::IPostprocessor<dxapp::DetectionResult>> postprocessor;
    std::vector<std::uint8_t> input_buffer;
    bool is_float_input = false;
    bool is_nhwc = false;

    NpuState(const AppConfig& config, const std::string& model_path)
        : inference_option{}, engine(model_path, inference_option)
    {
        if (!dxapp::minversionforRTandCompiler(&engine))
            throw std::runtime_error("DXRT runtime/compiler version is incompatible with this model");

        const auto& inputs = engine.GetInputs();
        if (inputs.empty())
            throw std::runtime_error("DXNN model has no input tensor");

        const auto input_shape = inputs.front().shape();
        int input_width = 0;
        int input_height = 0;
        ::parseInputShape(input_shape, input_width, input_height);
        if (input_width != config.model.input.width || input_height != config.model.input.height)
            throw std::runtime_error("DXNN input dimensions do not match model configuration");

        is_float_input = inputs.front().type() == dxrt::DataType::FLOAT;
        is_nhwc = ::isInputNHWC(input_shape);
        postprocessor = std::make_unique<dxapp::YOLOv8Postprocessor>(
            input_width, input_height,
            config.model.postprocess.confidence_threshold,
            config.model.postprocess.nms_threshold,
            engine.IsOrtConfigured(),
            config.model.postprocess.num_classes,
            config.model.postprocess.class_names);
        input_buffer.resize(engine.GetInputSize());
    }

    std::vector<Detection> infer(const cv::Mat& input, const LwirPreprocessContext& lwir_context)
    {
        if (input.empty() || !input.isContinuous())
            throw std::invalid_argument("Preprocessed NPU input must be non-empty and contiguous");

        if (is_float_input)
        {
            const auto float_buffer = convertToFloatBuffer(input, is_nhwc);
            const std::size_t byte_count = float_buffer.size() * sizeof(float);
            if (byte_count != input_buffer.size())
                throw std::runtime_error("Float input tensor size does not match DXRT input size");
            std::memcpy(input_buffer.data(), float_buffer.data(), byte_count);
        }
        else
        {
            const std::size_t byte_count = input.total() * input.elemSize();
            if (byte_count != input_buffer.size())
                throw std::runtime_error("UInt8 input tensor size does not match DXRT input size");
            std::memcpy(input_buffer.data(), input.data, byte_count);
        }

        Request request;
        request.context.pad_x = lwir_context.pad_x;
        request.context.pad_y = lwir_context.pad_y;
        request.context.scale = lwir_context.scale;
        request.context.original_width = lwir_context.original_width;
        request.context.original_height = lwir_context.original_height;
        request.context.input_width = lwir_context.input_width;
        request.context.input_height = lwir_context.input_height;

        const int job_id = engine.RunAsync(input_buffer.data(), &request);
        if (job_id < 0)
            throw std::runtime_error("DXRT RunAsync failed to submit inference");
        engine.Wait(job_id);

        bool completed = false;
        if (!request.completed.pop(completed))
            throw std::runtime_error("Inference completion queue was closed");
        if (request.error)
            std::rethrow_exception(request.error);
        return std::move(request.detections);
    }

    void register_callback()
    {
      engine.RegisterCallback(
          [this](dxrt::TensorPtrs &outputs, void *user_data) -> int
          {
            auto &request = *static_cast<Request *>(user_data);
            try
            {
              const auto results = postprocessor->process(outputs, request.context);
              request.detections.reserve(results.size());
              for(const auto &result : results)
              {
                if(result.box.size() < 4)
                  continue;
                Detection detection;
                detection.x          = result.box[0];
                detection.y          = result.box[1];
                detection.width      = result.box[2] - result.box[0];
                detection.height     = result.box[3] - result.box[1];
                detection.class_id   = result.class_id;
                detection.confidence = result.confidence;
                detection.class_name = result.class_name;
                request.detections.push_back(std::move(detection));
              }
            }
            catch(...)
            {
              request.error = std::current_exception();
            }
            request.completed.push(true);
            return 0;
          });
    }
};
// Keep the NPU engine and callback state alive for every request.
DxAppDetectionPipeline::DxAppDetectionPipeline(
    const ModelConfig& model_config,
    const DetectionConfig& detection_config)
    : preprocessor_(model_config),
      model_path_(model_config.path),
      max_inflight_(detection_config.max_inflight),
      npu_(std::make_unique<NpuState>(config, model_path_))
{
    npu_->register_callback();
}

DxAppDetectionPipeline::~DxAppDetectionPipeline() = default;

std::vector<Detection> DxAppDetectionPipeline::detect(const FrameContext& frame)
{
    cv::Mat model_input;
    LwirPreprocessContext context;
    preprocessor_.process(frame.image, model_input, context);
    return npu_->infer(model_input, context);
}
