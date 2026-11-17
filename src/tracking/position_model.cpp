#include "tracking/position_model.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace {
double sigmoid(double x) {return 1.0/(1.0+std::exp(-x));}
double silu(double x) {return x*sigmoid(x);}
double softplus(double x) {return x>20 ? x : std::log1p(std::exp(x));}
template<std::size_t N> void rms(std::array<double,N>& x,const double* gain) {
    double energy=0;
    for(double value:x) energy+=value*value;
    const double factor=1.0/std::sqrt(energy/N+1e-5);
    for(std::size_t i=0;i<N;++i) x[i]*=factor*gain[i];
}
template<std::size_t R,std::size_t C> std::array<double,R> linear(
    const double* weight,const std::array<double,C>& input,const double* bias=nullptr) {
    std::array<double,R> output{};
    for(std::size_t r=0;r<R;++r) {
        double sum=bias?bias[r]:0;
        for(std::size_t c=0;c<C;++c) sum+=weight[r*C+c]*input[c];
        output[r]=sum;
    }
    return output;
}
}

PositionModel::PositionModel(const std::string& path) {
    std::ifstream stream(path,std::ios::binary);
    if(!stream) throw std::invalid_argument("Position model file missing: "+path);
    const std::vector<char> bytes((std::istreambuf_iterator<char>(stream)),{});
    if(bytes.size()<16 || std::memcmp(bytes.data(),"LWPM68V1",8)!=0)
        throw std::invalid_argument("Invalid position model header");
    std::memcpy(&kind_,bytes.data()+8,4);
    std::memcpy(&flags_,bytes.data()+12,4);
    // Fixed export sizes; fail closed on copied or mismatched weights.
    constexpr std::size_t gru=192*68+192*64+192+192+4*64+4;
    constexpr std::size_t mamba=48*68+48+48+192*48+96*4+96+19*96+
        96*3+96+96*8+96+48*96+48+4*48+4;
    const auto count=kind_==1 ? gru : kind_==2 ? mamba : 0;
    if (!count || flags_ & ~7u || bytes.size()!=16+count*sizeof(double))
        throw std::invalid_argument("Invalid position model shape");
    weights_.resize(count);
    std::memcpy(weights_.data(),bytes.data()+16,count*sizeof(double));
    if (!std::all_of(weights_.begin(),weights_.end(),[](double v){return std::isfinite(v);}))
        throw std::invalid_argument("Nonfinite position model weight");
}

std::array<double,4> PositionModel::step(const std::array<double,68>& input,State& state) const {
    const double* p=weights_.data();
    auto take=[&](std::size_t n) {const double* start=p;p+=n;return start;};
    if (kind_==1) {
        const auto wi=take(192*68), wh=take(192*64), bi=take(192),bh=take(192);
        const auto wo=take(4*64),bo=take(4);
        const auto gi=linear<192,68>(wi,input,bi);
        const auto gh=linear<192,64>(wh,state.hidden,bh);
        std::array<double,64> updated{};
        for(int i=0;i<64;++i) {
            const double r=sigmoid(gi[i]+gh[i]);
            const double z=sigmoid(gi[64+i]+gh[64+i]);
            const double candidate=std::tanh(gi[128+i]+r*gh[128+i]);
            updated[i]=(1-z)*candidate+z*state.hidden[i];
        }
        state.hidden=updated;
        return linear<4,64>(wo,state.hidden,bo);
    }
    const auto ew=take(48*68),eb=take(48),n1=take(48),ip=take(192*48);
    const auto cw=take(96*4),cb=take(96),xp=take(19*96),dw=take(96*3);
    const auto db=take(96),alog=take(96*8),D=take(96),op=take(48*96);
    const auto n2=take(48),hw=take(4*48),hb=take(4);
    auto u=linear<48,68>(ew,input,eb);
    auto normalized=u;rms(normalized,n1);
    const auto xz=linear<192,48>(ip,normalized);
    std::array<double,96> xc{};
    for(int i=0;i<96;++i) {
        const double xi=xz[i];
        double conv=cb[i]+xi*cw[i*4+3];
        for(int j=0;j<3;++j) conv+=state.conv[i*3+j]*cw[i*4+j];
        xc[i]=silu(conv);
        state.conv[i*3]=state.conv[i*3+1];
        state.conv[i*3+1]=state.conv[i*3+2];
        state.conv[i*3+2]=xi;
    }
    const auto projection=linear<19,96>(xp,xc);
    std::array<double,96> y{};
    for(int i=0;i<96;++i) {
        double delta=db[i];
        for(int j=0;j<3;++j) delta+=dw[i*3+j]*projection[j];
        delta=softplus(delta);
        double total=0;
        for(int j=0;j<8;++j) {
            const int index=i*8+j;
            const double A=-std::exp(alog[index]);
            state.ssm[index]=std::exp(delta*A)*state.ssm[index]+
                delta*projection[3+j]*xc[i];
            total+=state.ssm[index]*projection[11+j];
        }
        y[i]=(total+D[i]*xc[i])*silu(xz[96+i]);
    }
    const auto residual=linear<48,96>(op,y);
    for(int i=0;i<48;++i) u[i]+=residual[i];
    rms(u,n2);
    auto output=linear<4,48>(hw,u,hb);
    if (!std::all_of(output.begin(),output.end(),[](double v){return std::isfinite(v);}))
        throw std::runtime_error("Position model produced nonfinite output");
    return output;
}
