#include "CabinetSimulator.hpp"
#include "Oversampler.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <numbers>

namespace Tracker {
namespace {
using Frame = std::array<double, 2>;
constexpr FilterShape eqShapes[]{FilterShape::Bell, FilterShape::LowShelf, FilterShape::HighShelf, FilterShape::LowPass, FilterShape::HighPass, FilterShape::Notch};
constexpr float centers[]{80,250,1000,4000,12000};
// T = preamp, C = cabinet, E = equalizer. Every path has exactly one preamp
// resampler and therefore the same latency. Keeping all six histories warm
// permits continuous, interrupted route crossfades without resetting a filter.
constexpr std::array<std::array<int,3>,6> routes{{{0,1,2},{0,2,1},{1,0,2},{1,2,0},{2,0,1},{2,1,0}}};
uint32_t factorFor(double rate) {
  if (!std::isfinite(rate) || rate < 8000 || rate > 384000) throw std::invalid_argument("Unsupported cabinet sample rate");
  return rate <= 48000 ? 16 : rate <= 96000 ? 8 : rate <= 192000 ? 4 : 2;
}
uint32_t latencyFor(uint32_t factor) noexcept { return factor==16?90:factor==8?88:factor==4?84:72; }
class Preamp {
  double pole_, bias_ = std::tanh(.2), normalization_ = 1/(1-bias_*bias_);
  uint32_t factor_;
  Oversampler oversampler_;
  EffectRamp gain_, enabled_;
  EffectDelay<double,720> gainDelay_;
  EffectDelay<double,90> enabledDelay_;
  EffectDelay<Frame,90> dryDelay_;
  Frame previous_{}, memory_{};
public:
  Preamp(double rate,uint32_t factor) : pole_(std::exp(-2*std::numbers::pi*5/rate)),factor_(factor),oversampler_(factor),
    gainDelay_(latencyFor(factor)*factor/2),enabledDelay_(latencyFor(factor)),dryDelay_(latencyFor(factor)) {
    if (oversampler_.latencyFrames()!=latencyFor(factor)) throw std::logic_error("Cabinet preamp delay mismatch");
    gain(6,0); enabled(1,0);
  }
  void gain(double value,uint32_t frames) noexcept {
    const auto g=std::pow(10.,value/20); gain_.set(g,frames*factor_); if (!frames) gainDelay_.fill(g);
  }
  void enabled(double value,uint32_t frames) noexcept { enabled_.set(value,frames); if (!frames) enabledDelay_.fill(value); }
  Frame process(Frame input) noexcept {
    const auto dry=dryDelay_.process(input);
    auto wet=oversampler_.process(input,[&](Frame &audio) noexcept {
      const auto drive=gainDelay_.process(gain_.next());
      for (auto &x:audio) x=x==0?0:(std::tanh(x*drive+.2)-bias_)*normalization_;
    });
    const auto amount=enabledDelay_.process(enabled_.next());
    for (size_t c=0;c<2;++c) {
      auto high=(wet[c]-previous_[c])*(1+pole_)*.5+pole_*memory_[c];
      if (std::abs(high)<1e-30) high=0;
      previous_[c]=wet[c];memory_[c]=high;
      wet[c]=amount==0?dry[c]:amount==1?high:dry[c]+amount*(high-dry[c]);
    }
    return wet;
  }
};
}
struct CabinetSimulator::Path {
  struct Updates { uint32_t mask=0; std::array<float,30> values{}; };
  double rate;
  uint32_t latency, route;
  Preamp preamp;
  CabinetVoicing cabinet;
  std::array<StateVariableFilter,5> eq;
  std::array<float,20> eqValues{};
  EffectRamp cabinetEnabled;
  // At most one final value per parameter per frame. Coalescing prevents an
  // arbitrarily large same-frame event batch from overflowing a render queue.
  std::array<Updates,91> deferred{};
  size_t cursor=0;
  Path(double rate,uint32_t factor,uint32_t route) : rate(rate),latency(latencyFor(factor)),route(route),preamp(rate,factor),cabinet(rate) {
    cabinetEnabled.set(1,0);
    for (size_t b=0;b<5;++b) { eqValues[b*4]=centers[b];eqValues[b*4+2]=.70710678f; configureEQ(b,0); }
  }
  void configureEQ(size_t band,uint32_t frames) noexcept {
    const auto at=band*4;
    eq[band].configure(eqShapes[uint32_t(eqValues[at+3])],eqValues[at],eqValues[at+2],eqValues[at+1],rate,frames);
  }
  void apply(uint32_t id,float value,uint32_t frames) noexcept {
    if (id==1) cabinet.model(uint32_t(value),frames);
    else if (id==3) preamp.gain(value,frames);
    else if (id==8) preamp.enabled(value,frames);
    else if (id==9) cabinetEnabled.set(value,frames);
    else if (id>=10 && id<30) { eqValues[id-10]=value; configureEQ((id-10)/4,frames); }
  }
  void parameter(uint32_t id,float value,bool rendered) noexcept {
    if (id!=1 && id!=3 && id!=8 && id!=9 && !(id>=10 && id<30)) return;
    const int stage=(id==1 || id==9)?1:(id>=10?2:0);
    bool afterPreamp=false;
    for (int s:routes[route]) { if (s==stage) break; if (s==0) afterPreamp=true; }
    if (rendered && afterPreamp) {
      auto &pending=deferred[(cursor+latency)%deferred.size()]; pending.mask|=1u<<id;pending.values[id]=value;
    } else apply(id,value,rendered?uint32_t(std::ceil(rate*.005)):0);
  }
  Frame process(Frame audio) noexcept {
    auto &pending=deferred[cursor];
    while (pending.mask) { const auto id=uint32_t(std::countr_zero(pending.mask));pending.mask&=pending.mask-1;apply(id,pending.values[id],uint32_t(std::ceil(rate*.005))); }
    if (++cursor==deferred.size()) cursor=0;
    for (int stage:routes[route]) {
      if (stage==0) audio=preamp.process(audio);
      else if (stage==1) {
        const auto filtered=cabinet.process(audio);const auto amount=cabinetEnabled.next();
        for (size_t c=0;c<2;++c) if (amount==1) audio[c]=filtered[c]; else if (amount!=0) audio[c]+=amount*(filtered[c]-audio[c]);
      } else for (auto &band:eq) band.process(audio[0],audio[1]);
    }
    return audio;
  }
};
CabinetSimulator::CabinetSimulator(double rate) : rate_(rate),factor_(factorFor(rate)),latency_(latencyFor(factor_)),cabinetTail_(CabinetVoicing::decaySeconds(rate)),controlDelay_(latency_),dryDelay_(latency_) {
  for (uint32_t r=0;r<6;++r) paths_[r]=std::make_unique<Path>(rate,factor_,r);
}
CabinetSimulator::~CabinetSimulator()=default;
void CabinetSimulator::parameter(uint32_t id,float value,bool rendered) noexcept {
  const auto frames=rendered?uint32_t(std::ceil(rate_*.005)):0;
  auto output=[&](size_t i,double target) { controls_[i].set(target,frames); };
  if (id==0) output(0,value);
  else if (id==2) {
    // A route is one vector-valued control: restart every weight together so
    // their sum stays one when another switch interrupts an existing fade.
    for (uint32_t r=0;r<6;++r) controls_[5+r].set(uint32_t(value)==r?1:0,frames,true);
  }
  else if (id==4) { inputMono_.set(value,frames);output(1,value); }
  else if (id==5 || id==6) output(id-3,value/100.);
  else if (id==7) output(4,std::pow(10.,value/20));
  else for (auto &path:paths_) path->parameter(id,value,rendered);
  if (!rendered) {
    std::array<double,11> values;for (size_t i=0;i<values.size();++i) values[i]=controls_[i].current;
    controlDelay_.fill(values);
  }
}
std::array<double,2> CabinetSimulator::process(Frame input) noexcept {
  const auto dry=dryDelay_.process(input);
  const auto mono=inputMono_.next(),mid=(input[0]+input[1])*.5;
  for (auto &x:input) x+=mono*(mid-x);
  std::array<double,11> values;for (size_t i=0;i<values.size();++i) values[i]=controls_[i].next();
  const auto controls=controlDelay_.process(values);
  Frame wet{};
  for (size_t r=0;r<paths_.size();++r) {
    const auto output=paths_[r]->process(input);
    for (size_t c=0;c<2;++c) wet[c]+=controls[5+r]*output[c];
  }
  const auto wetMid=(wet[0]+wet[1])*.5;
  for (size_t c=0;c<2;++c) {
    wet[c]=controls[1]==1?wetMid:wet[c]+controls[1]*(wetMid-wet[c]);
    const auto mixed=controls[4]*(controls[2]*dry[c]+controls[3]*wet[c]);
    wet[c]=controls[0]==0?dry[c]:controls[0]==1?mixed:dry[c]+controls[0]*(mixed-dry[c]);
  }
  return wet;
}
}
