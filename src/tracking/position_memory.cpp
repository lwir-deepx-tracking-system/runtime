#include "tracking/position_memory.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <stdexcept>

namespace {
constexpr double clip(double x) { return x < -5 ? -5 : x > 5 ? 5 : x; }
std::array<double,4> box(const KalmanState& s) {
    const auto& m=s.mean;
    return {m[0],m[1],s.xywh?m[2]:m[2]*m[3],m[3]};
}
std::array<double,4> box(const Detection& d) {
    return {d.x+d.width/2,d.y+d.height/2,d.width,d.height};
}
void write_box(KalmanState& s,const std::array<double,4>& b) {
    s.mean[0]=b[0];s.mean[1]=b[1];s.mean[3]=b[3];
    s.mean[2]=s.xywh?b[2]:b[2]/b[3];
}
std::array<double,68> feature(const std::array<double,4>& b,
    const std::array<double,4>& prev,const std::array<double,2>& velocity,
    const std::array<double,4>& innovation,const Detection* det,bool edges,int step) {
    std::array<double,68> x{};
    const double h=std::max(b[3],1.0),w=std::max(b[2],1.0);
    x[0]=velocity[0]/h;x[1]=velocity[1]/h;
    for(int i=0;i<4;++i)x[2+i]=innovation[i];
    x[6]=(b[0]-prev[0])/h;x[7]=(b[1]-prev[1])/h;
    x[8]=std::log(w/std::max(prev[2],1.0));
    x[9]=std::log(h/std::max(prev[3],1.0));
    x[10]=std::log(h/100.0);x[11]=std::log(w/h);
    x[12]=edges?(b[0]-w/2)/h:5;
    x[13]=edges?(640-b[0]-w/2)/h:5;
    x[14]=edges?(b[1]-h/2-16)/h:5;
    x[15]=edges?(496-b[1]-h/2)/h:5;
    if(det) for(std::size_t i=0;i<std::min<std::size_t>(17,det->keypoints.size());++i) {
        const auto& k=det->keypoints[i];
        const double visible=k.visibility>=0.5f && std::isfinite(k.x) && std::isfinite(k.y) ? 1:0;
        x[16+3*i]=(k.x-b[0])/h*visible;
        x[17+3*i]=(k.y-b[1])/h*visible;
        x[18+3*i]=visible;
    }
    x[67]=static_cast<double>(step)/15;
    for(double& v:x)v=clip(v);
    return x;
}
void warp(std::array<double,4>& b,std::array<double,2>& v,const CameraMotion& m) {
    const double cx=b[0],cy=b[1],vx=v[0],vy=v[1];
    b[0]=m.a*cx+m.b*cy+m.tx;b[1]=m.c*cx+m.d*cy+m.ty;
    v[0]=m.a*vx+m.b*vy;v[1]=m.c*vx+m.d*vy;
}
}

PositionMemory::PositionMemory(const std::string& mode) {
    if(mode=="kalman")return;
    if(mode!="gru_kpt" && mode!="mamba_kpt" && mode!="gru_near_kpt")
        throw std::invalid_argument("Unsupported position memory: "+mode);
    const auto path=std::filesystem::path(LWIR_POSITION_WEIGHT_DIR)/("pos_"+mode+".bin");
    model_=std::make_unique<PositionModel>(path.string());
}

void PositionMemory::observe(int id,const Detection& det,const KalmanState& state,
    const KalmanState* prediction) {
    if(!model_)return;
    auto& p=tracks_[id];
    if(p.missed>0) {p=Entry{};}
    const auto z=box(det),pred=box(prediction ? *prediction : state);
    const double h=std::max(pred[3],1.0);
    std::array<double,4> innovation={(z[0]-pred[0])/h,(z[1]-pred[1])/h,
        std::log(std::max(z[2],1.0)/std::max(pred[2],1.0)),
        std::log(std::max(z[3],1.0)/h)};
    const auto prev=p.history.empty()?z:p.previous;
    const std::array<double,2> velocity={state.mean[4],state.mean[5]};
    p.history.push_back(feature(z,prev,velocity,innovation,&det,!model_->no_edges(),0));
    if(p.history.size()>20)p.history.pop_front();
    p.previous=z;p.base=box(state);p.velocity=velocity;
    p.size_velocity={state.xywh?state.mean[6]:state.mean[6]*state.mean[3]+state.mean[2]*state.mean[7],
        state.mean[7]};
    p.initial_height=std::max(z[3],1.0);
    p.last_has_keypoint=false;
    for(const auto& k:det.keypoints)if(k.visibility>=0.5f){p.last_has_keypoint=true;break;}
}

void PositionMemory::lost(int id,KalmanState& state,const CameraMotion& first_warp,
    const CameraMotion& current_warp) {
    if(!model_)return;
    auto it=tracks_.find(id);if(it==tracks_.end())return;
    auto& p=it->second;
    if(p.missed>=15){++p.missed;return;}
    ++p.missed;
    if(p.missed==1)return;
    if(p.history.size()<4 || (p.initial_height>=64 && !p.last_has_keypoint &&
        !model_->box_mode_targets()))return;
    try {
        if(p.missed==2) {
            p.recurrent={};for(const auto& x:p.history)model_->step(x,p.recurrent);
            p.plain=p.base;
            // The first missed frame was processed by Kalman before this call.
            warp(p.plain,p.velocity,first_warp);
            p.plain[0]+=p.velocity[0];p.plain[1]+=p.velocity[1];
            p.plain[2]+=p.size_velocity[0];p.plain[3]+=p.size_velocity[1];
            const auto prior=p.plain;
            std::array<double,4> zero{};
            model_->step(feature(p.plain,p.previous,p.velocity,zero,nullptr,
                !model_->no_edges(),1),p.recurrent);
            p.previous=prior;
        }
        warp(p.plain,p.velocity,current_warp);
        p.plain[0]+=p.velocity[0];p.plain[1]+=p.velocity[1];
        const auto x=feature(p.plain,p.previous,p.velocity,{},nullptr,!model_->no_edges(),p.missed);
        const auto out=model_->step(x,p.recurrent);
        std::array<double,4> corrected={p.plain[0]+p.initial_height*std::tanh(out[0]),
            p.plain[1]+p.initial_height*std::tanh(out[1]),
            p.plain[2]*std::exp(std::log(1.5)*std::tanh(out[2])),
            p.plain[3]*std::exp(std::log(1.5)*std::tanh(out[3]))};
        if(std::all_of(corrected.begin(),corrected.end(),[](double v){return std::isfinite(v);}) &&
            corrected[2]>1 && corrected[3]>1)write_box(state,corrected);
        p.previous=p.plain;
    }catch(const std::exception&) {p.missed=16;}
}

bool PositionMemory::enabled() const { return static_cast<bool>(model_); }

void PositionMemory::forget(int id) { tracks_.erase(id); }

void PositionMemory::reset() { tracks_.clear(); }
