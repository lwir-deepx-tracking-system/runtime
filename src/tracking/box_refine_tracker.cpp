#include "tracking/box_refine_tracker.hpp"
#include "tracking/box_body_table_20261116.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_set>

namespace {
constexpr double pi = 3.14159265358979323846;
double iou(const Track& a,const Track& b) {
    const double x1=std::max(a.x,b.x), y1=std::max(a.y,b.y);
    const double x2=std::min(a.x+a.width,b.x+b.width);
    const double y2=std::min(a.y+a.height,b.y+b.height);
    const double overlap=std::max(0.0,x2-x1)*std::max(0.0,y2-y1);
    const double area=a.width*a.height+b.width*b.height-overlap;
    return area>0 ? overlap/area : 0;
}
double alpha(double cutoff, double dt) { return 1.0/(1.0+1.0/(2*pi*cutoff*dt)); }
void step(double value, double dt, double& previous, double& derivative,
          double cutoff_min, double beta, double derivative_cutoff) {
    const double speed=(value-previous)/dt;
    derivative += alpha(derivative_cutoff,dt)*(speed-derivative);
    const double cutoff=cutoff_min+beta*std::abs(derivative);
    previous += alpha(cutoff,dt)*(value-previous);
}
void convert_body(Track& track,bool pose_family) {
    const double width=track.width, height=track.height;
    const double cx=track.x+width*0.5, cy=track.y+height*0.5;
    double dx=0,dy=0,lw=0,lh=0;
    if (!pose_family) {
        const auto& p=box_body_table::person;
        dy=-0.5+p[0]+p[1]*0.5;
        lw=std::log(p[2]*height/width);
        lh=std::log(p[1]);
    } else {
        const double log_h=std::log(std::max(height,1.0));
        const double aspect=width/std::max(height,1.0);
        auto bracket=[](const double* bins,int count,double value) {
            int i=0;
            while (i<count-2 && bins[i+1]<=value) ++i;
            const double t=std::clamp((value-bins[i])/(bins[i+1]-bins[i]),0.0,1.0);
            return std::pair<int,double>{i,t};
        };
        const auto [i,t]=bracket(box_body_table::lh_cen,11,log_h);
        const auto [j,u]=bracket(box_body_table::ar_cen,8,aspect);
        double delta[4]{};
        for(int k=0;k<4;++k)
            delta[k]=(1-t)*(1-u)*box_body_table::tab[i][j][k]+
                     (1-t)*u*box_body_table::tab[i][j+1][k]+
                     t*(1-u)*box_body_table::tab[i+1][j][k]+
                     t*u*box_body_table::tab[i+1][j+1][k];
        dx=delta[0];dy=delta[1];lw=delta[2];lh=delta[3];
    }
    const double new_w=width*std::exp(lw), new_h=height*std::exp(lh);
    track.width=static_cast<float>(new_w);
    track.height=static_cast<float>(new_h);
    track.x=static_cast<float>(cx+dx*height-new_w*0.5);
    track.y=static_cast<float>(cy+dy*height-new_h*0.5);
}
}

BoxRefineTracker::BoxRefineTracker(std::unique_ptr<Tracker> base, BoxRefineConfig config)
    : base_(std::move(base)), config_(std::move(config)) {
    if (!base_ || (config_.mode!="size" && config_.mode!="body") ||
        !std::isfinite(config_.aim_y) ||
        config_.aim_y<0 || config_.aim_y>1)
        throw std::invalid_argument("Invalid box refinement configuration");
}

std::vector<Track> BoxRefineTracker::track(const std::vector<Detection>& detections) {
    auto tracks=base_->track(detections);
    refine(tracks,std::chrono::steady_clock::now(),++next_frame_,pose_family_);
    return tracks;
}

TrackingStep BoxRefineTracker::process(const DetectionResult& input) {
    auto result=base_->process(input);
    result.unrefined_tracks=result.tracks;
    const auto time=input.frame && input.frame->metadata.captured_at.time_since_epoch().count()
        ? input.frame->metadata.captured_at : std::chrono::steady_clock::now();
    const auto frame_id=input.frame && input.frame->metadata.frame_id
        ? input.frame->metadata.frame_id : ++next_frame_;
    if (!input.detections.empty()) {
        pose_family_=std::any_of(input.detections.begin(),input.detections.end(),
            [](const Detection& d) { return d.class_id==0 && d.keypoints.size()==17; });
    }
    refine(result.tracks,time,frame_id,pose_family_);
    return result;
}

void BoxRefineTracker::reset() {
    base_->reset();sizes_.clear();next_frame_=0;pose_family_=false;
    switch_pending_=false;camera_log_scale_=0;
}

void BoxRefineTracker::reset_for_model_change() {
    base_->reset_for_model_change();
    switch_pending_=true;
    camera_log_scale_=0;
}

void BoxRefineTracker::set_camera_motion(const CameraMotion& motion) {
    const double determinant=motion.a*motion.d-motion.b*motion.c;
    const double scale=std::sqrt(std::abs(determinant));
    camera_log_scale_=std::isfinite(scale) && scale>1e-6 ? std::log(scale) : 0;
    base_->set_camera_motion(motion);
}

void BoxRefineTracker::refine(std::vector<Track>& tracks,
    std::chrono::steady_clock::time_point time,std::uint64_t frame_id,bool pose_family) {
    std::unordered_set<int> current_ids;
    for (const auto& track:tracks) current_ids.insert(track.track_id);
    bool saw_observed=false;
    for (auto& track:tracks) {
        if (track.track_id<=0 || track.class_id!=0 ||
            !std::isfinite(track.x) || !std::isfinite(track.y) ||
            !std::isfinite(track.width) || !std::isfinite(track.height) ||
            track.width<=0 || track.height<=0) continue;
        const Track original=track;
        auto it=sizes_.find(track.track_id);
        if (!track.predicted && it==sizes_.end()) {
            auto best=sizes_.end();double overlap=0.5;
            for(auto candidate=sizes_.begin();candidate!=sizes_.end();++candidate) {
                const auto& s=candidate->second;
                if (current_ids.count(candidate->first) || !s.has_last ||
                    frame_id<s.last_frame || frame_id-s.last_frame>3) continue;
                const double score=iou(original,s.last_input);
                if (score>overlap) { overlap=score;best=candidate; }
            }
            if (best!=sizes_.end()) {
                auto state=best->second;
                sizes_.erase(best);
                it=sizes_.emplace(track.track_id,std::move(state)).first;
            }
        }
        if (track.predicted && (!config_.lost_same || it==sizes_.end())) continue;
        if (!track.predicted && it!=sizes_.end() &&
            frame_id>it->second.last_frame && frame_id-it->second.last_frame>30) {
            sizes_.erase(it);
            it=sizes_.end();
        }
        if (!track.predicted && it!=sizes_.end() && it->second.has_last) {
            const auto& prior=it->second.last_input;
            const double jump=std::hypot(original.x+original.width*.5-prior.x-prior.width*.5,
                original.y+original.height*.5-prior.y-prior.height*.5);
            if (jump>1.5*std::max(original.height,prior.height)) {
                sizes_.erase(it);
                it=sizes_.end();
            }
        }
        if (config_.mode=="body") convert_body(track,pose_family);
        const double center_x=track.x+track.width*0.5;
        const double aim_y=track.y+track.height*config_.aim_y;
        const auto prior_time=it!=sizes_.end() ? it->second.time : time;
        if (it!=sizes_.end() && frame_id!=it->second.last_frame) {
            it->second.log_w+=camera_log_scale_;
            it->second.log_h+=camera_log_scale_;
        }
        if (!track.predicted && it==sizes_.end()) {
            SizeState state;
            state.log_w=std::log(track.width);
            state.log_h=std::log(track.height);
            state.time=time;
            state.last_frame=frame_id;
            state.pose_family=pose_family;
            it=sizes_.emplace(track.track_id,state).first;
        } else if (!track.predicted) {
            auto& state=it->second;
            double dt=std::chrono::duration<double>(time-state.time).count();
            if (!(dt>0) || dt>1.0) dt=1.0/30.0;
            dt=std::max(dt,1e-3);
            const double mc=config_.mode=="body"?box_body_table::euro[0]:5.0;
            const double beta=config_.mode=="body"?box_body_table::euro[1]:3.0;
            const double dcut=config_.mode=="body"?box_body_table::euro[2]:1.0;
            step(std::log(track.width),dt,state.log_w,state.dw,mc,beta,dcut);
            step(std::log(track.height),dt,state.log_h,state.dh,mc,beta,dcut);
            state.time=time;
            state.last_frame=frame_id;
        }
        const double width=std::exp(it->second.log_w);
        const double height=std::exp(it->second.log_h);
        track.width=static_cast<float>(width);
        track.height=static_cast<float>(height);
        track.x=static_cast<float>(center_x-width*0.5);
        track.y=static_cast<float>(aim_y-height*config_.aim_y);
        if (!track.predicted) {
            auto& state=it->second;
            const bool switched=(switch_pending_ || state.pose_family!=pose_family);
            const double gap=std::chrono::duration<double>(time-prior_time).count();
            if (switched && state.has_last && gap<=0.5) {
                state.blend_start=time;
                state.blend_x=state.last_output.x+state.last_output.width*.5-
                    (track.x+track.width*.5);
                state.blend_y=state.last_output.y+state.last_output.height*config_.aim_y-
                    (track.y+track.height*config_.aim_y);
            }
            if (state.blend_start.time_since_epoch().count()) {
                const double elapsed=std::chrono::duration<double>(time-state.blend_start).count();
                if (elapsed>=1.0) state.blend_start={};
                else {
                    const double weight=1.0-std::max(0.0,elapsed);
                    track.x+=static_cast<float>(state.blend_x*weight);
                    track.y+=static_cast<float>(state.blend_y*weight);
                }
            }
            state.last_input=original;
            state.last_output=track;
            state.has_last=true;
            state.pose_family=pose_family;
            saw_observed=true;
        }
    }
    for (auto it=sizes_.begin();it!=sizes_.end();) {
        if (frame_id>it->second.last_frame && frame_id-it->second.last_frame>
            (config_.lost_same ? 90U : 30U))
            it=sizes_.erase(it);
        else ++it;
    }
    camera_log_scale_=0;
    if (saw_observed) switch_pending_=false;
}

void BoxRefineTracker::set_image_size(float width, float height)  { base_->set_image_size(width,height); }

void BoxRefineTracker::set_confirmation_frames(int frames)  { base_->set_confirmation_frames(frames); }

int BoxRefineTracker::current_confirmation_frames() const  { return base_->current_confirmation_frames(); }
