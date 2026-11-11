#include "tracking/frame_policy_tracker.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/video/tracking.hpp>

double warmth_percentile(const cv::Mat& raw, const cv::Rect& roi)
{
    if (raw.type() != CV_16UC1 || roi.empty() || (roi & cv::Rect(0,0,raw.cols,raw.rows)) != roi)
        throw std::invalid_argument("Warmth requires a bounded CV_16UC1 ROI");
    std::vector<std::uint16_t> values;
    values.reserve(((roi.width+1)/2)*((roi.height+1)/2));
    for (int y=roi.y;y<roi.y+roi.height;y+=2)
        for (int x=roi.x;x<roi.x+roi.width;x+=2) values.push_back(raw.at<std::uint16_t>(y,x));
    const double pos=.95*(values.size()-1);
    const auto lo=static_cast<std::size_t>(pos), hi=static_cast<std::size_t>(std::ceil(pos));
    std::nth_element(values.begin(),values.begin()+lo,values.end());
    const double lower=values[lo];
    if (lo==hi) return lower;
    std::nth_element(values.begin()+lo+1,values.begin()+hi,values.end());
    return lower+(pos-lo)*(values[hi]-lower);
}

FramePolicyTracker::FramePolicyTracker(std::unique_ptr<Tracker> base, TrackingFrameConfig config)
    : base_(std::move(base)), config_(config)
{
    if (!base_) throw std::invalid_argument("Frame policy requires a tracker");
}

void FramePolicyTracker::reset() { base_->reset();previous_.release();points_.clear(); }

std::vector<Track> FramePolicyTracker::track(const std::vector<Detection>& detections)
{
    if (config_.gmc || config_.warmth_min || config_.warmth_max)
        throw std::logic_error("Configured image policies require Tracker::process(frame)");
    return base_->track(detections);
}

CameraMotion FramePolicyTracker::estimate(const cv::Mat& raw)
{
    cv::Mat gray, current;
    raw.convertTo(gray,CV_8U,255.0/65535.0);
    const int ds=config_.gmc_downscale;
    cv::resize(gray,current,cv::Size(std::max(1,gray.cols/ds),std::max(1,gray.rows/ds)));
    std::vector<cv::Point2f> next_points;
    cv::goodFeaturesToTrack(current,next_points,config_.gmc_max_corners,.01,config_.gmc_min_distance);
    CameraMotion motion;
    if (!previous_.empty() && previous_.size()==current.size() && !points_.empty()) {
        std::vector<cv::Point2f> moved, from, to;
        std::vector<unsigned char> status;
        std::vector<float> error;
        cv::calcOpticalFlowPyrLK(previous_,current,points_,moved,status,error);
        for (size_t i=0;i<status.size();++i) if(status[i] && std::isfinite(moved[i].x) && std::isfinite(moved[i].y)) {
            from.push_back(points_[i]);to.push_back(moved[i]);
        }
        if (from.size()>4) {
            const cv::Mat affine=cv::estimateAffinePartial2D(from,to,cv::noArray(),cv::RANSAC);
            if (!affine.empty() && cv::checkRange(affine)) {
                motion={affine.at<double>(0,0),affine.at<double>(0,1),affine.at<double>(0,2)*ds,
                    affine.at<double>(1,0),affine.at<double>(1,1),affine.at<double>(1,2)*ds};
            }
        }
    }
    previous_=std::move(current);points_=std::move(next_points);
    return motion;
}

TrackingStep FramePolicyTracker::process(const DetectionResult& input)
{
    if (!input.frame || input.frame->image.empty() || input.frame->image.type()!=CV_16UC1)
        throw std::invalid_argument("Tracking image policies require a CV_16UC1 frame");
    const auto started=std::chrono::steady_clock::now();
    const auto& image=input.frame->image;
    base_->set_image_size(image.cols,image.rows);
    if (config_.gmc) base_->set_camera_motion(estimate(image));
    DetectionResult filtered=input; // Shares FrameContext; only detection descriptors are copied.
    std::vector<std::size_t> source_indices(input.detections.size());
    std::iota(source_indices.begin(),source_indices.end(),0);
    if (config_.warmth_min || config_.warmth_max) {
        const auto rejected=[&](const Detection& d) {
            if (d.class_id!=config_.person_class_id) return false;
            if (!std::isfinite(d.x)||!std::isfinite(d.y)||!std::isfinite(d.width)||!std::isfinite(d.height)) return true;
            const int x=static_cast<int>(std::clamp(d.x,0.0F,float(image.cols)));
            const int y=static_cast<int>(std::clamp(d.y,0.0F,float(image.rows)));
            const int right=static_cast<int>(std::clamp(d.x+d.width,0.0F,float(image.cols)));
            const int bottom=static_cast<int>(std::clamp(d.y+d.height,0.0F,float(image.rows)));
            if(right-x<4 || bottom-y<4) return false; // Python small-crop fallback.
            const double p=warmth_percentile(image,{x,y,right-x,bottom-y});
            return p<config_.warmth_min || (config_.warmth_max>0 && p>config_.warmth_max);
        };
        filtered.detections.clear();
        source_indices.clear();
        for (std::size_t i=0;i<input.detections.size();++i) {
            if (rejected(input.detections[i])) continue;
            filtered.detections.push_back(input.detections[i]);
            source_indices.push_back(i);
        }
    }
    const double policy_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
    auto result=base_->process(filtered);
    result.association_ms+=policy_ms;
    // Feature source indices remain indices in the caller's original DetectionResult.
    if (filtered.detections.size()!=input.detections.size()) {
        for (auto& e:result.embeddings) if(e.track_id<0) {
            e.source_index=source_indices.at(e.source_index);
        }
        // BoT-SORT diagnostics: frame,stage,track_id,detection_index.
        // Post-ReID uses five fields with stable IDs and is left unchanged.
        for (auto& event:result.association_events) {
            if (std::count(event.begin(),event.end(),',')!=3) continue;
            const auto offset=event.rfind(',')+1;
            const auto index=event.substr(offset);
            if (index.empty() || index.find_first_not_of("0123456789")!=std::string::npos) continue;
            event.replace(offset,std::string::npos,std::to_string(source_indices.at(std::stoull(index))));
        }
    }
    return result;
}
