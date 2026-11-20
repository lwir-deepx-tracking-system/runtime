#pragma once

#include <dxrt/dxrt_cxx_api.h>
#include <functional>
#include <memory>
#include <string>
#include "inference/npu_core_reservation.hpp"

struct InferenceConfig;

// Owns one DXRT engine. Callers must drain their asynchronous jobs before destruction.
class DxrtSession final {
public:
    DxrtSession(const std::string& path, const InferenceConfig& config, bool enable_profiler = false);
    ~DxrtSession();
    DxrtSession(const DxrtSession&) = delete;
    DxrtSession& operator=(const DxrtSession&) = delete;

    dxrt::Tensors inputs();
    dxrt::Tensors outputs();
    std::uint64_t input_size();
    bool supports_model_version();

    dxrt::TensorPtrs run(void* input);
    int submit(void* input, void* user_data);
    void set_callback(std::function<int(dxrt::TensorPtrs&, void*)> callback);
    // Wait without fetching a second copy of the outputs received by the callback.
    void wait(int job_id);

private:
    NpuCoreReservation reservation_; // Released after engine destruction.
    std::unique_ptr<dxrt::InferenceEngine> engine_;
};
