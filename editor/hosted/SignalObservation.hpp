#pragma once
#include "SignalScope.hpp"
#include "SignalListen.hpp"
#include "editor/SignalRouteIdentity.hpp"
#include "editor/SignalRuntimeObserver.hpp"
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
struct SignalCopyIdentity {uint64_t graph=0,target=0;uint8_t role=0;uint64_t instrument=0;uint16_t channel=UINT16_MAX;};
struct SignalPortIdentity {
  std::string key, node, name;
  bool output = false;
  uint32_t port = 0, channels = 2;
  int64_t processorLatency = -1, compensation = -1;
  std::optional<SignalRouteIdentity> route;
  std::optional<SignalCopyIdentity> copy;
  std::string kind="audio";
};
struct SignalPortConfiguration {uint32_t token=0;int64_t processorLatency=-1,compensation=-1;double routeGain=1;bool preFader=false;};
struct SignalPortReading {
  float peakLeft = 0, peakRight = 0, rmsLeft = 0, rmsRight = 0;
  uint64_t through = 0, lastSignal = 0, generation = 0;
  bool available=false,fresh=false,measured = false, clipped = false, nonFinite = false;
  int64_t processorLatency=-1,compensation=-1;
  double routeGain=1;bool preFader=false;
  double value=0,first=0;
  std::optional<SignalNoteGate> noteGate;
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
    std::atomic<double> value{0},first{0};
    std::atomic<uint64_t> noteVersion{0},noteGeneration{0},noteOn{0},noteOff{0},noteRetrigger{0},noteFrame{0};
    std::atomic<bool> noteHeld{false},noteHasEvent{false};
  };
  // The audio owner resets history before the first write in an adopted
  // generation. Explicit Clear only resets diagnostic latches, as before.
  static void beginMeasurement(Meter &m,uint64_t generation) noexcept {
    if(m.generation.load(std::memory_order_relaxed)!=generation) {
      m.measured.store(false,std::memory_order_release);
      m.left.store(0,std::memory_order_relaxed);m.right.store(0,std::memory_order_relaxed);
      m.rmsLeft.store(0,std::memory_order_relaxed);m.rmsRight.store(0,std::memory_order_relaxed);
      m.through.store(0,std::memory_order_relaxed);m.lastSignal.store(0,std::memory_order_relaxed);
      m.first.store(0,std::memory_order_relaxed);m.value.store(0,std::memory_order_relaxed);
      m.clip.store(false,std::memory_order_relaxed);m.nonFinite.store(false,std::memory_order_relaxed);
    }
    const auto clear=m.clear.load(std::memory_order_relaxed);
    if(clear!=m.cleared){m.clip.store(false,std::memory_order_relaxed);m.nonFinite.store(false,std::memory_order_relaxed);m.cleared=clear;}
  }
  struct Configuration {
    std::atomic<uint64_t> generation{0};
    std::atomic<int64_t> processorLatency{-1},compensation{-1};
    std::atomic<double> routeGain{1};
    std::atomic<bool> preFader{false};
    std::atomic<uint32_t> domain{0};
  };
  // Independent of meter publication: a queued audio plan can adopt before
  // the producer finishes appending newly allocated catalogue slots.
  std::unique_ptr<std::array<Configuration,maximumPorts>> configuration_=std::make_unique<std::array<Configuration,maximumPorts>>();
  std::atomic<uint64_t> generation_{0},through_{0};
  uint64_t nextGeneration_=0,audioThrough_=0; // Audio owner; quiescent preparation may initialize.
  static constexpr size_t maximumDomains=4096;
  std::array<std::atomic<uint64_t>,maximumDomains> domains_{};
  uint32_t nextDomain_=0; // Single control owner, prepared before publication.
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
  uint32_t domainCount() const noexcept {return nextDomain_;} // Control owner only.
  uint32_t prepareDomain(uint32_t offset) const {
    if(offset>=maximumDomains-nextDomain_-1)throw std::invalid_argument("Signal copy observation capacity exceeded");
    return nextDomain_+offset+1;
  }
  bool canPublishDomains(uint32_t base,uint32_t count) const noexcept {return base==nextDomain_&&count<maximumDomains-base;}
  void publishDomains(uint32_t base,uint32_t count) noexcept {nextDomain_=base+count;}
  uint32_t newDomain() {if(nextDomain_+1>=maximumDomains)throw std::invalid_argument("Signal copy observation capacity exceeded");return ++nextDomain_;}
  uint64_t domainGeneration(uint32_t domain) const noexcept {return domain<maximumDomains?domains_[domain].load(std::memory_order_acquire):0;}
  void activate(std::span<const SignalPortConfiguration> ports) noexcept {activateDomain(0,ports);}
  void activateDomain(uint32_t domain,std::span<const SignalPortConfiguration> ports) noexcept {
    if(domain>=maximumDomains)return;
    const auto generation=++nextGeneration_;
    for(const auto &p:ports)if(p.token && p.token<=maximumPorts){auto &c=(*configuration_)[p.token-1];
      // Invalidate before changing metadata; acquiring a new field also observes
      // this invalidation, so a concurrent reader cannot report mixed plans.
      c.generation.store(0,std::memory_order_release);
      c.domain.store(domain,std::memory_order_release);
      c.processorLatency.store(p.processorLatency,std::memory_order_release);c.compensation.store(p.compensation,std::memory_order_release);
      c.routeGain.store(p.routeGain,std::memory_order_release);c.preFader.store(p.preFader,std::memory_order_release);
      c.generation.store(generation,std::memory_order_release);
    }
    domains_[domain].store(generation,std::memory_order_release);
    const auto watched=scope.watchedToken();
    if(watched&&watched<=maximumPorts&&(*configuration_)[watched-1].domain.load(std::memory_order_acquire)==domain)scope.route(generation);
    if(!domain)generation_.store(generation,std::memory_order_release);
  }
  bool available(uint32_t token) const noexcept {
    if(!token||token>ports.size())return false;
    const auto domain=(*configuration_)[token-1].domain.load(std::memory_order_acquire);
    const auto generation=domains_[domain].load(std::memory_order_acquire);
    return (generation||!ports[token-1].copy)&&(!generation||(*configuration_)[token-1].generation.load(std::memory_order_acquire)==generation);
  }
  void observe(uint32_t token,const float *samples,uint32_t frames,uint64_t position) noexcept {
    if(!available(token) || !frames || frames>4096 || position>UINT64_MAX-frames)return;
    const auto domain=(*configuration_)[token-1].domain.load(std::memory_order_acquire);
    const auto generation=domains_[domain].load(std::memory_order_acquire);
    auto &meter=*meters_[token-1];beginMeasurement(meter,generation);
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
    if(scope.watchedToken()==token)scope.route(generation);
    scope.capture(token,samples,frames,position);
    listen.capture(token,samples,frames,position);
  }
  void routeGain(uint32_t token,double gain) noexcept {if(token&&token<=maximumPorts)(*configuration_)[token-1].routeGain.store(gain,std::memory_order_release);}
  void observeControl(uint32_t token,double first,double last,uint32_t frames,uint64_t position) noexcept {
    if(!available(token)||!frames||frames>4096||position>UINT64_MAX-frames)return;
    const auto domain=(*configuration_)[token-1].domain.load(std::memory_order_acquire);
    const auto generation=domains_[domain].load(std::memory_order_acquire);
    auto &m=*meters_[token-1];beginMeasurement(m,generation);
    const bool finite=std::isfinite(first)&&std::isfinite(last);
    if(!finite)m.nonFinite.store(true,std::memory_order_relaxed);
    m.first.store(finite?first:0,std::memory_order_relaxed);m.value.store(finite?last:0,std::memory_order_relaxed);
    m.through.store(position+frames,std::memory_order_release);m.generation.store(generation,std::memory_order_release);m.measured.store(true,std::memory_order_release);
    audioThrough_=std::max(audioThrough_,position+frames);through_.store(audioThrough_,std::memory_order_release);
  }
  void observeNoteGate(uint32_t token,const SignalNoteGate &gate) noexcept {
    if(!available(token))return;
    const auto domain=(*configuration_)[token-1].domain.load(std::memory_order_acquire);
    const auto generation=domains_[domain].load(std::memory_order_acquire);
    auto &m=*meters_[token-1];m.noteVersion.fetch_add(1,std::memory_order_acq_rel);
    m.noteHeld.store(gate.held,std::memory_order_release);m.noteHasEvent.store(gate.hasEvent,std::memory_order_release);
    m.noteOn.store(gate.on,std::memory_order_release);m.noteOff.store(gate.off,std::memory_order_release);
    m.noteRetrigger.store(gate.retrigger,std::memory_order_release);m.noteFrame.store(gate.lastFrame,std::memory_order_release);
    m.noteGeneration.store(generation,std::memory_order_release);m.noteVersion.fetch_add(1,std::memory_order_release);
  }
  SignalPortReading read(uint32_t token) const noexcept {
    if(!token || token>ports.size())return {};
    const auto &config=(*configuration_)[token-1];const auto domain=config.domain.load(std::memory_order_acquire);
    const auto generation=domains_[domain].load(std::memory_order_acquire);
    SignalPortReading result;result.generation=generation;result.available=(generation||!ports[token-1].copy)&&(!generation||config.generation.load(std::memory_order_acquire)==generation);if(!result.available)return result;
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
    result.value=m.value.load(std::memory_order_relaxed);result.first=m.first.load(std::memory_order_relaxed);
    // Bounded read: a simultaneous render can omit this one diagnostic sample,
    // but cannot expose counters from different events or a retired generation.
    const auto noteVersion=m.noteVersion.load(std::memory_order_acquire);
    if(noteVersion&&!(noteVersion&1)&&m.noteGeneration.load(std::memory_order_acquire)==generation){
      SignalNoteGate gate;gate.held=m.noteHeld.load(std::memory_order_acquire);gate.hasEvent=m.noteHasEvent.load(std::memory_order_acquire);
      gate.on=m.noteOn.load(std::memory_order_acquire);gate.off=m.noteOff.load(std::memory_order_acquire);
      gate.retrigger=m.noteRetrigger.load(std::memory_order_acquire);gate.lastFrame=m.noteFrame.load(std::memory_order_acquire);
      if(m.noteVersion.load(std::memory_order_acquire)==noteVersion)result.noteGate=gate;
    }
    // An adopted but not-yet-rendered generation has no measurements. Retained
    // slots must not expose its predecessor's overload or last-signal history.
    if(!measured||measuredGeneration!=generation){
      result.peakLeft=result.peakRight=result.rmsLeft=result.rmsRight=0;
      result.through=result.lastSignal=0;result.clipped=result.nonFinite=false;
      result.value=result.first=0;
      result.noteGate.reset();
    }
    if(domains_[domain].load(std::memory_order_acquire)!=generation || config.domain.load(std::memory_order_acquire)!=domain || (generation&&config.generation.load(std::memory_order_acquire)!=generation))return {};
    return result;
  }
  void clear(uint32_t token) noexcept {
    if(token && token<=ports.size())meters_[token-1]->clear.fetch_add(1,std::memory_order_relaxed);
  }
};
}
