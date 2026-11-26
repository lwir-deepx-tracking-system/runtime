#pragma once

#include <dxrt/dxrt_cxx_api.h>
#include "config/app_config.hpp"

dxrt::InferenceOption make_dxrt_inference_option(const InferenceConfig& config);
