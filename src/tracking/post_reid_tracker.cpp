#include "tracking/post_reid_tracker.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <unordered_map>

PostReidTracker::PostReidTracker(std::unique_ptr<Tracker> base,
    std::unique_ptr<IReidPipeline> pipeline, ReidMatcherConfig matcher_config,
    int dimensions, ReidBenchmarkConfig crop_config)
    : base_(std::move(base)), pipeline_(std::move(pipeline)),
      matcher_(matcher_config,dimensions), crop_(crop_config)
{
    if(!base_ || !pipeline_ || crop_.max_crops_per_frame<=0 || crop_.min_crop_height<0)
        throw std::invalid_argument("Invalid post-ReID tracker components");
}

std::vector<Track> PostReidTracker::track(const std::vector<Detection>& detections)
{
    return base_->track(detections);
}

void PostReidTracker::reset() { base_->reset();matcher_.reset(); }

TrackingStep PostReidTracker::process(const DetectionResult& input)
{
    if(!input.frame) throw std::invalid_argument("Post-ReID requires a frame");
    TrackingStep result;
    const auto started=std::chrono::steady_clock::now();
    result.raw_tracks=base_->track(input.detections);
    result.association_ms=std::chrono::duration<double,std::milli>(
        std::chrono::steady_clock::now()-started).count();
    std::vector<const Track*> candidates;
    for(const auto& t:result.raw_tracks) candidates.push_back(&t);
    std::stable_sort(candidates.begin(),candidates.end(),[this](const Track* a,const Track* b) {
        return matcher_.needs_decision_feature(a->track_id) &&
            !matcher_.needs_decision_feature(b->track_id);
    });
    std::unordered_map<int,std::vector<float>> embeddings;
    for(const Track* ptr:candidates) {
        const Track& t=*ptr;
        if(t.predicted || t.class_id!=crop_.person_class_id || t.height<crop_.min_crop_height ||
            static_cast<int>(result.embeddings.size())>=crop_.max_crops_per_frame) continue;
        if(!std::isfinite(t.x)||!std::isfinite(t.y)||!std::isfinite(t.width)||
            !std::isfinite(t.height)||t.width<=0||t.height<=0) continue;
        const auto& image=input.frame->image;
        const int x1=static_cast<int>(std::clamp(t.x,0.0F,static_cast<float>(image.cols)));
        const int y1=static_cast<int>(std::clamp(t.y,0.0F,static_cast<float>(image.rows)));
        const int x2=static_cast<int>(std::clamp(t.x+t.width,0.0F,static_cast<float>(image.cols)));
        const int y2=static_cast<int>(std::clamp(t.y+t.height,0.0F,static_cast<float>(image.rows)));
        if(x2-x1<2||y2-y1<2) continue;
        const cv::Rect rect(x1,y1,x2-x1,y2-y1);
        const auto begin=std::chrono::steady_clock::now();
        auto feature=pipeline_->embed(image,rect);
        const double ms=std::chrono::duration<double,std::milli>(
            std::chrono::steady_clock::now()-begin).count();
        embeddings.emplace(t.track_id,feature);
        result.embeddings.push_back({0,t.track_id,rect,std::move(feature),ms});
        result.reid_ms+=ms;
    }
    const auto match_started=std::chrono::steady_clock::now();
    auto matched=matcher_.step(input.frame->metadata.frame_id,result.raw_tracks,embeddings);
    result.tracks=std::move(matched.tracks);
    result.matching_ms=std::chrono::duration<double,std::milli>(
        std::chrono::steady_clock::now()-match_started).count();
    for(const auto& m:matched.matches)
        result.association_events.push_back(std::to_string(input.frame->metadata.frame_id)+
            ",post_reconnect,"+std::to_string(m.tracker_id)+","+
            std::to_string(m.stable_id)+","+std::to_string(m.similarity));
    for(const auto& d:matched.diagnostics)
        result.association_events.push_back(std::to_string(input.frame->metadata.frame_id)+
            ","+d.reason+","+std::to_string(d.tracker_id)+","+
            std::to_string(d.stable_id)+","+std::to_string(d.similarity));
    return result;
}
