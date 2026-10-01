#pragma once
#include "SignalScope.hpp"
#include "SignalListen.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <span>
#include <vector>

namespace Tracker {
struct SignalRouteIdentity {
  std::string kind,source,target,plugin,tap="post-gain";
  uint32_t input=0,output=0;
};
struct SignalPortIdentity {
  std::string key, node, name;
  bool output = false;
  uint32_t port = 0, channels = 2;
  int64_t processorLatency = -1, compensation = -1;
  std::optional<SignalRouteIdentity> route;
};
struct SignalPortConfiguration {uint32_t token=0;int64_t processorLatency=-1,compensation=-1;double routeGain=1;bool preFader=false;};
struct SignalPortReading {
  float peakLeft = 0, peakRight = 0, rmsLeft = 0, rmsRight = 0;
  uint64_t through = 0, lastSignal = 0, generation = 0;
  bool available=false,fresh=false,measured = false, clipped = false, nonFinite = false;
  int64_t processorLatency=-1,compensation=-1;
  double routeGain=1;bool preFader=false;
};
// Immutable identities are prepared by the control owner. Each meter has one
// audio writer; readers touch atomics only. No vendor callbacks or allocations
// are needed to observe a port. Values describe host ports, not plugin internals.
class SignalObservation {
public:
  static constexpr size_t maximumPorts = 8192;
  // One control producer appends; UI/API readers may enumerate concurrently.
  // Published identities never move or change. Publish the identity and its
  // meter together, so a newly visible token is immediately safe to observe.
  class PortCatalogue {
    friend class SignalObservation;
    std::unique_ptr<SignalPortIdentity[]> entries_{std::make_unique<SignalPortIdentity[]>(maximumPorts)};
    std::atomic<size_t> count_{0};
  public:
    size_t size() const noexcept { return count_.load(std::memory_order_acquire); }
    const SignalPortIdentity *begin() const noexcept { return entries_.get(); }
    const SignalPortIdentity *end() const noexcept { return begin()+size(); }
    const SignalPortIdentity &operator[](size_t index) const noexcept { return entries_[index]; }
  };
private:
  struct Meter {
    std::atomic<float> left{0}, right{0}, rmsLeft{0}, rmsRight{0};
    std::atomic<uint64_t> through{0}, lastSignal{0},generation{0};
    std::atomic<bool> measured{false}, clip{false}, nonFinite{false};
    std::atomic<uint32_t> clear{0};
    uint32_t cleared = 0;
  };
  struct Configuration {
    std::atomic<uint64_t> generation{0};
    std::atomic<int64_t> processorLatency{-1},compensation{-1};
    std::atomic<double> routeGain{1};
    std::atomic<bool> preFader{false};
  };
  // Independent of meter publication: a queued audio plan can adopt before
  // the producer finishes appending newly allocated catalogue slots.
  std::unique_ptr<std::array<Configuration,maximumPorts>> configuration_=std::make_unique<std::array<Configuration,maximumPorts>>();
  std::atomic<uint64_t> generation_{0},through_{0};
  uint64_t nextGeneration_=0,audioThrough_=0; // Audio owner; quiescent preparation may initialize.
  double rate_;
  // Stable slots permit new bus observations to be prepared while old ports
  // are being rendered. Publishing a slot never relocates an audio-reader's
  // storage or races a vector size/capacity change.
  std::array<std::unique_ptr<Meter>,maximumPorts> meters_;
public:
  SignalScope scope;
  SignalListen listen;
  PortCatalogue ports;
  explicit SignalObservation(double rate):rate_(rate),listen(rate) {
    if(!std::isfinite(rate)||rate<8000||rate>384000)throw std::invalid_argument("Invalid signal observation rate");
  }
  struct PreparedPorts {
    size_t base=0;
    std::vector<SignalPortIdentity> identities;
    std::vector<std::unique_ptr<Meter>> meters;
    size_t storageBytes() const noexcept {
      size_t result=sizeof(*this)+identities.capacity()*sizeof(SignalPortIdentity)+meters.capacity()*sizeof(meters[0])+meters.size()*sizeof(Meter);
      for(const auto &identity:identities){result+=identity.key.capacity()+identity.node.capacity()+identity.name.capacity();if(identity.route){const auto &r=*identity.route;result+=r.kind.capacity()+r.source.capacity()+r.target.capacity()+r.plugin.capacity()+r.tap.capacity();}}
      return result;
    }
  };
  PreparedPorts preparePorts(std::vector<SignalPortIdentity> identities) const {
    PreparedPorts result;result.base=ports.size();
    if(identities.size()>maximumPorts-result.base)throw std::invalid_argument("Signal observation port capacity exceeded");
    for(size_t i=0;i<identities.size();++i){const auto &p=identities[i];
      if(p.key.empty()||p.channels<1||p.channels>2||std::any_of(ports.begin(),ports.end(),[&](const auto &v){return v.key==p.key;})||
         std::any_of(identities.begin(),identities.begin()+i,[&](const auto &v){return v.key==p.key;}))
        throw std::invalid_argument("Invalid or duplicate prepared signal port");
      result.meters.push_back(std::make_unique<Meter>());
    }
    result.identities=std::move(identities);return result;
  }
  bool canPublishPorts(const PreparedPorts &batch) const noexcept {return batch.base==ports.size()&&batch.identities.size()==batch.meters.size()&&batch.identities.size()<=maximumPorts-batch.base;}
  // Single producer: after canPublishPorts succeeds, no other catalog append
  // may intervene. All strings/meters are already allocated by preparePorts.
  void publishPorts(PreparedPorts &batch) noexcept {
    for(size_t i=0;i<batch.identities.size();++i){ports.entries_[batch.base+i]=std::move(batch.identities[i]);meters_[batch.base+i]=std::move(batch.meters[i]);}
    ports.count_.store(batch.base+batch.identities.size(),std::memory_order_release);
  }
  uint32_t add(SignalPortIdentity port) {
    if(ports.size()>=maximumPorts || port.key.empty() || port.channels<1 || port.channels>2 ||
       std::any_of(ports.begin(),ports.end(),[&](const auto &p){return p.key==port.key;}))
      throw std::invalid_argument("Invalid, duplicate or excessive signal observation port");
    auto meter=std::make_unique<Meter>();
    const auto index=ports.size();
    ports.entries_[index]=std::move(port);meters_[index]=std::move(meter);
    ports.count_.store(index+1,std::memory_order_release);return uint32_t(index+1);
  }
  // Prepared numeric metadata only. Called at actual plan adoption, including
  // rollback, or while stopped; neither allocations nor catalogue mutation.
  void activate(std::span<const SignalPortConfiguration> ports) noexcept {
    const auto generation=++nextGeneration_;
    for(const auto &p:ports)if(p.token && p.token<=maximumPorts){auto &c=(*configuration_)[p.token-1];
      // Invalidate before changing metadata; acquiring a new field also observes
      // this invalidation, so a concurrent reader cannot report mixed plans.
      c.generation.store(0,std::memory_order_release);
      c.processorLatency.store(p.processorLatency,std::memory_order_release);c.compensation.store(p.compensation,std::memory_order_release);
      c.routeGain.store(p.routeGain,std::memory_order_release);c.preFader.store(p.preFader,std::memory_order_release);
      c.generation.store(generation,std::memory_order_release);
    }
    scope.route(generation);generation_.store(generation,std::memory_order_release);
  }
  bool available(uint32_t token) const noexcept {
    if(!token||token>ports.size())return false;
    const auto generation=generation_.load(std::memory_order_acquire);
    return !generation||(*configuration_)[token-1].generation.load(std::memory_order_acquire)==generation;
  }
  void observe(uint32_t token,const float *samples,uint32_t frames,uint64_t position) noexcept {
    if(!available(token) || !frames || frames>4096 || position>UINT64_MAX-frames)return;
    const auto generation=generation_.load(std::memory_order_relaxed);
    auto &meter=*meters_[token-1];const auto clear=meter.clear.load(std::memory_order_relaxed);
    if(clear!=meter.cleared){meter.clip.store(false,std::memory_order_relaxed);meter.nonFinite.store(false,std::memory_order_relaxed);meter.cleared=clear;}
    float peaks[2]{};double sum[2]{};bool invalid=false;uint64_t signal=0;
    for(uint32_t i=0;i<frames;++i)for(size_t c=0;c<2;++c) {
      const float sample=samples?samples[i*2+c]:0;
      if(!std::isfinite(sample)){invalid=true;continue;}
      const float absolute=std::abs(sample);peaks[c]=std::max(peaks[c],absolute);sum[c]+=double(sample)*sample;
      if(absolute>1e-7f)signal=position+i+1;
    }
    const float decay=float(std::exp(-double(frames)/(rate_*.2)));
    meter.left.store(std::max(peaks[0],meter.left.load(std::memory_order_relaxed)*decay),std::memory_order_relaxed);
    meter.right.store(std::max(peaks[1],meter.right.load(std::memory_order_relaxed)*decay),std::memory_order_relaxed);
    meter.rmsLeft.store(float(std::sqrt(sum[0]/frames)),std::memory_order_relaxed);
    meter.rmsRight.store(float(std::sqrt(sum[1]/frames)),std::memory_order_relaxed);
    if(peaks[0]>=1 || peaks[1]>=1)meter.clip.store(true,std::memory_order_relaxed);
    if(invalid)meter.nonFinite.store(true,std::memory_order_relaxed);
    if(signal)meter.lastSignal.store(signal,std::memory_order_relaxed);
    meter.through.store(position+frames,std::memory_order_release);meter.generation.store(generation,std::memory_order_release);meter.measured.store(true,std::memory_order_release);
    audioThrough_=std::max(audioThrough_,position+frames);through_.store(audioThrough_,std::memory_order_release);
    scope.capture(token,samples,frames,position);
    listen.capture(token,samples,frames,position);
  }
  SignalPortReading read(uint32_t token) const noexcept {
    if(!token || token>ports.size())return {};
    const auto generation=generation_.load(std::memory_order_acquire);const auto &config=(*configuration_)[token-1];
    SignalPortReading result;result.generation=generation;result.available=!generation||config.generation.load(std::memory_order_acquire)==generation;if(!result.available)return result;
    result.processorLatency=generation?config.processorLatency.load(std::memory_order_acquire):ports[token-1].processorLatency;
    result.compensation=generation?config.compensation.load(std::memory_order_acquire):ports[token-1].compensation;
    if(generation){result.routeGain=config.routeGain.load(std::memory_order_acquire);result.preFader=config.preFader.load(std::memory_order_acquire);}
    const auto &m=*meters_[token-1];const bool measured=m.measured.load(std::memory_order_acquire);
    const auto measuredGeneration=m.generation.load(std::memory_order_acquire);result.through=m.through.load(std::memory_order_acquire);
    const auto through=through_.load(std::memory_order_acquire);
    result.fresh=measured&&measuredGeneration==generation&&(result.through>=through||through-result.through<=4096);
    result.measured=result.fresh;
    result.peakLeft=m.left.load(std::memory_order_relaxed);result.peakRight=m.right.load(std::memory_order_relaxed);
    result.rmsLeft=m.rmsLeft.load(std::memory_order_relaxed);result.rmsRight=m.rmsRight.load(std::memory_order_relaxed);
    result.lastSignal=m.lastSignal.load(std::memory_order_relaxed);result.clipped=m.clip.load(std::memory_order_relaxed);result.nonFinite=m.nonFinite.load(std::memory_order_relaxed);
    if(generation_.load(std::memory_order_acquire)!=generation || (generation&&config.generation.load(std::memory_order_acquire)!=generation))return {};
    return result;
  }
  void clear(uint32_t token) noexcept {
    if(token && token<=ports.size())meters_[token-1]->clear.fetch_add(1,std::memory_order_relaxed);
  }
};
}
