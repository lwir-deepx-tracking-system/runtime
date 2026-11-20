#include "inference/dxrt_session.hpp"

#include "common/common_util.hpp"
#include "common/dxrt_inference_option.hpp"

#include <mutex>
#include <stdexcept>
#include <utility>

DxrtSession::DxrtSession(const std::string& path, const InferenceConfig& config, bool enable_profiler)
    : reservation_(config.npu_cores, path)
{
    // DXRT profiler configuration is process-wide and must precede engine creation.
    if (enable_profiler) {
        static std::once_flag profiler;
        std::call_once(profiler, [] {
            dxrt::Configuration::GetInstance().SetEnable(dxrt::Configuration::ITEM::PROFILER, true);
        });
    }
    auto options = make_dxrt_inference_option(config);
    engine_ = std::make_unique<dxrt::InferenceEngine>(path, options);
}

DxrtSession::~DxrtSession() = default;

dxrt::Tensors DxrtSession::inputs() { return engine_->GetInputs(); }
dxrt::Tensors DxrtSession::outputs() { return engine_->GetOutputs(); }
std::uint64_t DxrtSession::input_size() { return engine_->GetInputSize(); }
bool DxrtSession::supports_model_version() { return dxapp::minversionforRTandCompiler(engine_.get()); }

dxrt::TensorPtrs DxrtSession::run(void* input) { return engine_->Run(input); }
int DxrtSession::submit(void* input, void* user_data) { return engine_->RunAsync(input, user_data); }

void DxrtSession::set_callback(std::function<int(dxrt::TensorPtrs&, void*)> callback)
{
    engine_->RegisterCallback(std::move(callback));
}

void DxrtSession::wait(int job_id)
{
    if (dxrt_engine_wait(engine_->handle(), job_id, nullptr) != DXRT_OK) {
        const char* message = dxrt_last_error_message();
        throw std::runtime_error(message ? message : "DXRT failed while waiting for a job");
    }
}
