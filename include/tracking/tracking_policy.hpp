#pragma once
#include "config/app_config.hpp"

bool tracking_classes_match(int a, int b, const TrackingConfig& c);

bool edge_birth_allowed(float x,float y,float w,float h,float score,
    float image_width,float image_height,const TrackingConfig& c);
