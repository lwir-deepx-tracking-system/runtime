#include "tracking/tracking_policy.hpp"
#include "tracking/botsort_tracker.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace {
using Clock = std::chrono::steady_clock;

float overlap(const Detection& a, const Detection& b)
{
    const float x = std::max(0.0F, std::min(a.x+a.width,b.x+b.width)-std::max(a.x,b.x));
    const float y = std::max(0.0F, std::min(a.y+a.height,b.y+b.height)-std::max(a.y,b.y));
    const float area = a.width*a.height+b.width*b.height-x*y;
    return area > 0 ? x*y/area : 0;
}

KalmanMeasurement measure(const Detection& d)
{
    return {d.x+d.width/2, d.y+d.height/2, d.width, d.height};
}

Detection predicted(const KalmanState& k, const Detection& d)
{
    Detection result = d;
    result.width = static_cast<float>(std::max(1.0,k.mean[2]));
    result.height = static_cast<float>(std::max(1.0,k.mean[3]));
    result.x = static_cast<float>(k.mean[0]-result.width/2);
    result.y = static_cast<float>(k.mean[1]-result.height/2);
    return result;
}

bool valid_feature(const std::vector<float>& f)
{
    if (f.empty()) return false;
    double norm = 0;
    for (float x : f) { if (!std::isfinite(x)) return false; norm += x*x; }
    return norm > 0 && std::isfinite(norm);
}

std::vector<float> normalized(const std::vector<float>& f)
{
    if (!valid_feature(f)) return {};
    double norm = 0;
    for (float x : f) norm += x*x;
    std::vector<float> out = f;
    for (float& x : out) x = static_cast<float>(x/std::sqrt(norm));
    return out;
}

float appearance_distance(const std::vector<float>& a, const std::vector<float>& b)
{
    if (a.empty() || b.empty() || a.size() != b.size()) return 1;
    float dot = 0;
    for (size_t i=0;i<a.size();++i) dot += a[i]*b[i];
    return (1-std::clamp(dot,-1.0F,1.0F))/2;
}

// Rectangular minimum-cost assignment. Invalid pairs receive a large cost.
std::vector<std::pair<size_t,size_t>> assign(
    const std::vector<std::vector<float>>& cost, size_t rows, size_t cols, float limit)
{
    if (!rows || !cols) return {};
    const bool transposed = rows > cols;
    const size_t n = transposed ? cols : rows, m = transposed ? rows : cols;
    std::vector<double> u(n+1), v(m+1), minv(m+1);
    std::vector<size_t> p(m+1), way(m+1);
    for (size_t i=1;i<=n;++i)
    {
        p[0]=i;
        std::fill(minv.begin(),minv.end(),1e20);
        std::vector<bool> used(m+1);
        size_t j0=0;
        do {
            used[j0]=true;
            const size_t i0=p[j0];
            double delta=1e20; size_t j1=0;
            for(size_t j=1;j<=m;++j) if(!used[j]) {
                const float c=transposed?cost[j-1][i0-1]:cost[i0-1][j-1];
                const double cur=(c>limit?1e5:c)-u[i0]-v[j];
                if(cur<minv[j]) {minv[j]=cur;way[j]=j0;}
                if(minv[j]<delta) {delta=minv[j];j1=j;}
            }
            for(size_t j=0;j<=m;++j) {
                if(used[j]) {u[p[j]]+=delta;v[j]-=delta;}
                else minv[j]-=delta;
            }
            j0=j1;
        } while(p[j0]);
        do { const size_t j1=way[j0];p[j0]=p[j1];j0=j1; } while(j0);
    }
    std::vector<std::pair<size_t,size_t>> result;
    for(size_t j=1;j<=m;++j) if(p[j]) {
        const size_t r=transposed?j-1:p[j]-1, c=transposed?p[j]-1:j-1;
        if(cost[r][c]<=limit) result.emplace_back(r,c);
    }
    return result;
}

cv::Rect crop_rect(const Detection& d, const cv::Mat& image)
{
    if (!std::isfinite(d.x) || !std::isfinite(d.y) || !std::isfinite(d.width) ||
        !std::isfinite(d.height) || d.width<=0 || d.height<=0) return {};
    const int x1=static_cast<int>(std::clamp(d.x,0.0F,static_cast<float>(image.cols)));
    const int y1=static_cast<int>(std::clamp(d.y,0.0F,static_cast<float>(image.rows)));
    const int x2=static_cast<int>(std::clamp(d.x+d.width,0.0F,static_cast<float>(image.cols)));
    const int y2=static_cast<int>(std::clamp(d.y+d.height,0.0F,static_cast<float>(image.rows)));
    return x2-x1>=2 && y2-y1>=2 ? cv::Rect(x1,y1,x2-x1,y2-y1) : cv::Rect{};
}
}

BotSortTracker::BotSortTracker(TrackingConfig config,
    std::unique_ptr<IReidPipeline> reid, int person_class_id)
    : config_(std::move(config)), reid_(std::move(reid)), person_class_id_(person_class_id)
{
    if(config_.type!=TrackerType::BotSort) throw std::invalid_argument("BoT-SORT config type mismatch");
    if(config_.kalman_format!="xywh") throw std::invalid_argument("BoT-SORT requires xywh Kalman format");
}

void BotSortTracker::reset() { targets_.clear();frame_=0;next_id_=0;motion_={}; }

std::vector<Track> BotSortTracker::track(const std::vector<Detection>& detections)
{
    return track_with_features(detections, std::vector<std::vector<float>>(detections.size())).tracks;
}

TrackingStep BotSortTracker::track_with_features(const std::vector<Detection>& detections,
    const std::vector<std::vector<float>>& features)
{
    if(features.size()!=detections.size())
        throw std::invalid_argument("BoT-SORT features must align with detection indices");
    return associate(detections,features);
}

TrackingStep BotSortTracker::process(const DetectionResult& input)
{
    if(!input.frame) throw std::invalid_argument("BoT-SORT requires a frame");
    TrackingStep result;
    std::vector<std::vector<float>> features(input.detections.size());
    if(reid_) {
        int crops=0;
        for(size_t i=0;i<input.detections.size();++i) {
            const auto& d=input.detections[i];
            if(d.class_id!=person_class_id_ || d.confidence<config_.low_threshold ||
                d.height<config_.appearance_min_crop_height ||
                (config_.appearance_max_crops_per_frame>0 &&
                 crops>=config_.appearance_max_crops_per_frame)) continue;
            const cv::Rect rect=crop_rect(d,input.frame->image);
            if(rect.empty()) continue;
            const auto started=Clock::now();
            features[i]=reid_->embed(input.frame->image,rect);
            const double ms=std::chrono::duration<double,std::milli>(Clock::now()-started).count();
            result.embeddings.push_back({i,-1,rect,features[i],ms});
            result.reid_ms+=ms;
            ++crops;
        }
    }
    auto associated=associate(input.detections,features);
    associated.embeddings=std::move(result.embeddings);
    associated.reid_ms=result.reid_ms;
    return associated;
}

TrackingStep BotSortTracker::associate(const std::vector<Detection>& detections,
    const std::vector<std::vector<float>>& features)
{
    const auto started=Clock::now();
    ++frame_;
    TrackingStep result;
    std::vector<size_t> high,low,active,unconfirmed;
    for(size_t i=0;i<detections.size();++i) {
        const auto& d=detections[i];
        if(d.width<=0 || d.height<=0 || !std::isfinite(d.x) || !std::isfinite(d.y) ||
            !std::isfinite(d.width) || !std::isfinite(d.height)) continue;
        if(d.confidence>=config_.track_threshold) high.push_back(i);
        else if(d.confidence>config_.low_threshold) low.push_back(i);
    }
    for(size_t i=0;i<targets_.size();++i) if(targets_[i].state!=State::Removed) {
        if(targets_[i].state==State::Lost && frame_-targets_[i].last_frame>config_.track_buffer) {
            targets_[i].state=State::Removed;continue;
        }
        if(!targets_[i].confirmed) { apply_camera_motion(targets_[i].kf,motion_); unconfirmed.push_back(i);continue; }
        if(targets_[i].state==State::Lost) { targets_[i].kf.mean[6]=0;targets_[i].kf.mean[7]=0; }
        kalman_.predict(targets_[i].kf);
        apply_camera_motion(targets_[i].kf,motion_);
        active.push_back(i);
    }
    motion_={};
    std::vector<bool> used(detections.size());
    std::vector<bool> matched(targets_.size());
    auto update=[&](size_t ti,size_t di,const char* stage) {
        auto& t=targets_[ti];const auto& d=detections[di];
        kalman_.update(t.kf,measure(d));
        t.observation=d;t.last_frame=frame_;t.state=State::Tracked;
        ++t.hits;t.confirmed=t.confirmed || t.hits>=config_.confirmation_frames;
        const auto f=normalized(features[di]);
        if(!f.empty()) {
            if(t.feature.empty() || t.feature.size()!=f.size()) t.feature=f;
            else {
                for(size_t k=0;k<f.size();++k)
                    t.feature[k]=config_.feature_alpha*t.feature[k]+(1-config_.feature_alpha)*f[k];
                t.feature=normalized(t.feature);
            }
        }
        used[di]=true;matched[ti]=true;
        result.association_events.push_back(std::to_string(frame_)+","+stage+","+
            std::to_string(t.id)+","+std::to_string(di));
    };
    auto stage=[&](const std::vector<size_t>& rows,const std::vector<size_t>& cols,
                   bool appearance,float limit,const char* label) {
        std::vector<std::vector<float>> costs(rows.size(),std::vector<float>(cols.size(),2));
        for(size_t r=0;r<rows.size();++r) for(size_t c=0;c<cols.size();++c) {
            const auto& t=targets_[rows[r]];const auto& d=detections[cols[c]];
            if(config_.class_gate ? !tracking_classes_match(t.observation.class_id,d.class_id,config_) : t.observation.class_id!=d.class_id) {
                result.association_events.push_back(std::to_string(frame_)+",class_gate,"+
                    std::to_string(t.id)+","+std::to_string(cols[c]));
                continue;
            }
            const float motion=1-overlap(predicted(t.kf,t.observation),d);
            float cost=config_.fuse_score && appearance ? 1-(1-motion)*d.confidence : motion;
            if(appearance && motion<=config_.proximity_threshold &&
                !t.feature.empty() && !features[cols[c]].empty()) {
                const float ad=appearance_distance(t.feature,normalized(features[cols[c]]));
                if(ad<=config_.appearance_threshold) cost=std::min(cost,ad);
                else result.association_events.push_back(std::to_string(frame_)+
                    ",appearance_gate,"+std::to_string(t.id)+","+std::to_string(cols[c]));
            }
            else if(appearance) result.association_events.push_back(std::to_string(frame_)+
                (motion>config_.proximity_threshold ? ",proximity_gate," : ",feature_missing,")+
                std::to_string(t.id)+","+std::to_string(cols[c]));
            if(cost>limit) result.association_events.push_back(std::to_string(frame_)+
                ",cost_gate,"+std::to_string(t.id)+","+std::to_string(cols[c]));
            costs[r][c]=cost;
        }
        for(const auto& [r,c]:assign(costs,rows.size(),cols.size(),limit))
            update(rows[r],cols[c],label);
    };
    stage(active,high,true,config_.match_threshold,"high");
    std::vector<size_t> remaining,remaining_low;
    for(size_t i:active) if(!matched[i] && targets_[i].state==State::Tracked) remaining.push_back(i);
    for(size_t i:low) if(!used[i]) remaining_low.push_back(i);
    stage(remaining,remaining_low,false,config_.low_match_threshold,"low");
    std::vector<size_t> remaining_high;
    for(size_t i:high) if(!used[i]) remaining_high.push_back(i);
    stage(unconfirmed,remaining_high,true,config_.unconfirmed_match_threshold,"confirm");
    for(size_t i:unconfirmed) if(!matched[i]) {
        targets_[i].state=State::Removed;
        result.association_events.push_back(std::to_string(frame_)+",removed,"+
            std::to_string(targets_[i].id)+",-1");
    }
    for(size_t i:active) if(!matched[i] && targets_[i].state==State::Tracked) {
        targets_[i].state=State::Lost;
        result.association_events.push_back(std::to_string(frame_)+",lost,"+
            std::to_string(targets_[i].id)+",-1");
    }
    const float new_threshold=config_.new_track_threshold>0?config_.new_track_threshold:
        std::min(1.0F,config_.track_threshold+0.1F);
    for(size_t i:high) if(!used[i] && detections[i].confidence>=new_threshold) {
        const auto& d=detections[i];
        if(!edge_birth_allowed(d.x,d.y,d.width,d.height,d.confidence,width_,height_,config_)) continue;
        Target t;t.id=++next_id_;t.last_frame=frame_;t.observation=detections[i];
        t.confirmed=(frame_==1 || config_.confirmation_frames==1);
        t.kf=kalman_.initiate(measure(detections[i]));t.feature=normalized(features[i]);
        targets_.push_back(std::move(t));
        result.association_events.push_back(std::to_string(frame_)+",new,"+
            std::to_string(next_id_)+","+std::to_string(i));
    }
    for(const auto& t:targets_) if(t.state==State::Tracked && t.confirmed) {
        const auto& d=t.observation;
        result.tracks.push_back({t.id,d.x,d.y,d.width,d.height,d.class_id,d.confidence,false});
    }
    result.raw_tracks=result.tracks;
    targets_.erase(std::remove_if(targets_.begin(),targets_.end(),
        [](const Target& t) { return t.state==State::Removed; }),targets_.end());
    result.association_ms=std::chrono::duration<double,std::milli>(Clock::now()-started).count();
    return result;
}
