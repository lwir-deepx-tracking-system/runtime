#pragma once
#include "config/app_config.hpp"

inline bool tracking_classes_match(int a, int b, const TrackingConfig& c)
{
    return !c.class_gate || ((a==c.frame.person_class_id)==(b==c.frame.person_class_id));
}

inline bool edge_birth_allowed(float x,float y,float w,float h,float score,
    float image_width,float image_height,const TrackingConfig& c)
{
    if(c.edge_birth_threshold<=0 || score>=c.edge_birth_threshold) return true;
    if(image_width<=0 || image_height<=0) return false; // A configured gate needs the image boundary.
    const float m=c.edge_margin_px;
    return x>m && y>m && x+w<image_width-m && y+h<image_height-m;
}
