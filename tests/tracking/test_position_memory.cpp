#include "tracking/position_model.hpp"
#include "tracking/position_memory.hpp"

#include <array>
#include <cmath>
#include <filesystem>

namespace {
bool near(const std::array<double,4>& got,const std::array<double,4>& want) {
    for(int i=0;i<4;++i)if(std::abs(got[i]-want[i])>1e-8)return false;
    return true;
}
}

int main() {
    const auto root=std::filesystem::path(__FILE__).parent_path().parent_path().parent_path()/
        "config/tracking/position_weights";
    std::array<double,68> x{};
    for(int i=0;i<68;++i)x[i]=i/100.0;
    struct Fixture {const char* name;std::array<double,4> first,second;};
    const Fixture fixtures[]={
        {"gru_kpt",{.3274504782015816,-.11981386119268504,-.033421321868169235,-.17043842473781678},
            {.6532704996336947,-.1492984389198679,-.10798554906195187,-.37662817603653514}},
        {"mamba_kpt",{-.477286225927138,.3296553300358688,.059805381687335304,-.29803810338410924},
            {-.4956379567309852,.23607887042225234,-.07215604864501678,-.3037090041932164}},
        {"gru_near_kpt",{.3300498324470183,.03089527517363848,-.2086389171461859,-.19974796486616822},
            {.5955684935268889,.013404082464983279,-.16591468290771544,-.16942386985525162}}
    };
    for(const auto& f:fixtures) {
        PositionModel model((root/(std::string("pos_")+f.name+".bin")).string());
        PositionModel::State state;
        if(!near(model.step(x,state),f.first) || !near(model.step(x,state),f.second))return 1;
    }
    PositionMemory memory("gru_kpt");
    KalmanState state;state.xywh=true;
    state.mean={100,100,40,60,2,0,0,0};
    Detection d;d.x=80;d.y=70;d.width=40;d.height=60;
    d.keypoints={{100,80,1}};
    for(int i=0;i<4;++i){d.x+=2;state.mean[0]+=2;memory.observe(1,d,state);}
    KalmanState first=state;
    memory.lost(1,first,{},{});
    if(first.mean!=state.mean)return 2;
    KalmanState second=state;
    second.mean[0]+=4;
    memory.lost(1,second,{},{});
    if(!std::isfinite(second.mean[0]) || second.mean[0]==state.mean[0]+4)return 3;
    PositionMemory plain("kalman");
    plain.observe(1,d,state);
    const auto original=state.mean;
    plain.lost(1,state,{},{});
    if(state.mean!=original)return 4;
    return 0;
}
