#include "inference/dxrt_session.hpp"

#include "common/dxrt_inference_option.hpp"

#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace {
constexpr const char* DXAPP_GREEN = "\033[1;32m";
constexpr const char* DXAPP_RESET = "\033[0m";

/**
 * @brief Compare two version strings (e.g., "3.0.0" >= "3.0.0")
 * @param v1 First version string
 * @param v2 Second version string
 * @return true if v1 >= v2
 */
bool isVersionGreaterOrEqual(const std::string& v1, const std::string& v2) {
    std::istringstream s1(v1), s2(v2);
    int num1 = 0, num2 = 0;
    char dot;

    while (s1.good() || s2.good()) {
        if (s1.good()) s1 >> num1;
        if (s2.good()) s2 >> num2;

        if (num1 < num2) return false;
        if (num1 > num2) return true;

        num1 = num2 = 0;
        if (s1.good()) s1 >> dot;
        if (s2.good()) s2 >> dot;
    }
    return true;
}

/**
 * @brief Check minimum version compatibility for RT and Compiler
 *
 * Validates that the DXRT library version is >= 3.4.0 and
 * the compiled model version is >= v7 (matching Legacy behavior).
 *
 * @param ie Pointer to InferenceEngine
 * @return true if versions are compatible
 */
bool minversionforRTandCompiler(dxrt::InferenceEngine* ie) {
    if (!ie) return false;

    std::string rt_version = dxrt::Configuration::GetInstance().GetVersion();
    std::string compiler_version = ie->GetModelVersion();

    if (isVersionGreaterOrEqual(rt_version, "3.4.0")) {
        if (isVersionGreaterOrEqual(compiler_version, "v7")) {
            return true;
        } else {
            std::cerr << "[DXAPP] [ERROR] Compiler version is too low. (required: "
                         ">= 7, current: "
                      << compiler_version << ")" << std::endl;
            std::cerr << DXAPP_GREEN << "[HINT] Model/compiler version mismatch. "
                         "Please download updated models: ./setup.sh --models <model_name>"
                      << DXAPP_RESET << std::endl;
        }
    } else {
        std::cerr << "[DXAPP] [ERROR] DXRT library version is too low. (required: "
                     ">= 3.4.0, current: "
                  << rt_version << ")" << std::endl;
        std::cerr << DXAPP_GREEN << "[HINT] Please update DXRT: ./install.sh --all"
                  << DXAPP_RESET << std::endl;
    }
    return false;
}
}

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
bool DxrtSession::supports_model_version() { return minversionforRTandCompiler(engine_.get()); }

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
