#pragma once

#include <dxrt/dxrt_cxx_api.h>
#include "config/app_config.hpp"

inline dxrt::InferenceOption make_dxrt_inference_option(const InferenceConfig& config)
{
    dxrt::InferenceOption option;
    option.bufferCount = config.buffer_count;
    option.useORT = config.use_ort;
    switch (config.npu_cores)
    {
    case NpuCoreBinding::All: option.boundOption = dxrt::InferenceOption::NPU_ALL; break;
    case NpuCoreBinding::Core0: option.boundOption = dxrt::InferenceOption::NPU_0; break;
    case NpuCoreBinding::Core1: option.boundOption = dxrt::InferenceOption::NPU_1; break;
    case NpuCoreBinding::Core2: option.boundOption = dxrt::InferenceOption::NPU_2; break;
    case NpuCoreBinding::Core01: option.boundOption = dxrt::InferenceOption::NPU_01; break;
    case NpuCoreBinding::Core12: option.boundOption = dxrt::InferenceOption::NPU_12; break;
    case NpuCoreBinding::Core02: option.boundOption = dxrt::InferenceOption::NPU_02; break;
    }
    return option;
}
