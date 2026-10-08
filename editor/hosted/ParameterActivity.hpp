#pragma once
#include "PluginTypes.hpp"
#include "editor/RealtimePlan.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <deque>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace Tracker {
// Host-visible values only: a plugin's internal modulators are not observable.
enum class ParameterOrigin : uint8_t { Baseline, Manual, PluginEditor, Recorded, Envelope, PatternSet, PatternSlide, Graph, Reset, GraphSource };
struct ParameterSource {
  ParameterOrigin kind = ParameterOrigin::Manual;
  uint64_t id = 0;
  uint32_t pattern = UINT32_MAX, position = 0;
  uint16_t channel = UINT16_MAX, binding = 0;
  uint8_t column = 0;
  bool operator==(const ParameterSource &) const = default;
};
struct ParameterTracePoint {
  uint64_t sequence=0, generation=0, frame=0;
  double value=0, minimum=0, maximum=0, position=0;
  uint32_t pattern=UINT32_MAX, order=UINT32_MAX;
  ParameterSource source;
  bool audible=true;
};
struct ParameterProcessor {
  std::string key, name, plugin;
  uint64_t graph=0,node=0,target=0,instrument=0;
  uint16_t channel=UINT16_MAX;
  uint8_t role=2;
  bool bypass=false;
  std::vector<PluginParameter> parameters;
};
// A single selected parameter per playback engine. The audio producer never
// allocates, locks, discovers metadata, or overwrites unread consumer storage.
// Metadata, selection publication, draining and retained history are control-only.
class ParameterActivity {
  static constexpr uint32_t capacity=32768;
  struct Selection {uint64_t generation=0;uint32_t processor=0,parameter=0;double baseline=0;};
  RealtimePlan<Selection> selections_;
  Selection selected_,requested_;
  std::array<ParameterTracePoint,capacity> queue_{};
  alignas(64) std::atomic<uint32_t> write_{0};
  alignas(64) std::atomic<uint32_t> read_{0};
  std::atomic<uint64_t> dropped_{0};
  struct Clock {uint64_t frame=0;uint32_t pattern=UINT32_MAX,order=UINT32_MAX;double position=0,units=0;};
  std::array<Clock,4097> clocks_{};
  size_t clockCount_=1;
  uint64_t bucketFrame_=0;
  std::optional<ParameterTracePoint> pending_;
  uint32_t quantum_=48;
  bool audible_=true;
  double lastValue_=0;
  ParameterSource lastSource_{ParameterOrigin::Baseline};
  uint64_t sequence_=0;
  std::deque<ParameterTracePoint> history_;
  inline static std::atomic<uint64_t> nextIdentity_{0};
  void push(ParameterTracePoint point) noexcept {
    auto w=write_.load(std::memory_order_relaxed),r=read_.load(std::memory_order_acquire);
    if(w-r==capacity){dropped_.fetch_add(1,std::memory_order_relaxed);return;}
    queue_[w%capacity]=point;write_.store(w+1,std::memory_order_release);
  }
  ParameterTracePoint point(double value,uint64_t frame,ParameterSource source) const noexcept {
    ParameterTracePoint p;p.generation=selected_.generation;p.frame=frame;p.value=p.minimum=p.maximum=value;p.source=source;
    auto next=std::upper_bound(clocks_.begin(),clocks_.begin()+clockCount_,frame,[](uint64_t frame,const Clock &c){return frame<c.frame;});
    const auto &c=next==clocks_.begin()?clocks_[0]:*(next-1);
    p.pattern=c.pattern;p.order=c.order;p.position=c.position+(frame>=c.frame?double(frame-c.frame):-double(c.frame-frame))*c.units;p.audible=audible_;return p;
  }
public:
  const uint64_t identity=++nextIdentity_;
  std::vector<ParameterProcessor> processors;
  explicit ParameterActivity(double rate):quantum_(std::max(1u,uint32_t(rate/1000))){}
  uint32_t add(ParameterProcessor p){processors.push_back(std::move(p));return uint32_t(processors.size());}
  const ParameterProcessor *processor() const {return requested_.processor && requested_.processor<=processors.size()?&processors[requested_.processor-1]:nullptr;}
  uint32_t parameter() const {return requested_.parameter;}
  uint64_t generation() const {return requested_.generation;}
  uint64_t dropped() const {return dropped_.load(std::memory_order_relaxed);}
  bool watch(const std::string &key,uint32_t parameter,double baseline,bool clear=false) {
    auto p=std::find_if(processors.begin(),processors.end(),[&](const auto &p){return p.key==key;});
    if(p==processors.end())throw std::invalid_argument("Processor copy is unavailable; start playback to prepare graph copies");
    if(std::none_of(p->parameters.begin(),p->parameters.end(),[&](const auto &v){return v.id==parameter;}))throw std::invalid_argument("Parameter no longer exists");
    uint32_t index=uint32_t(p-processors.begin())+1;
    if(!clear&&requested_.processor==index&&requested_.parameter==parameter)return false;
    auto next=std::make_unique<Selection>(Selection{requested_.generation+1,index,parameter,baseline});
    if(!selections_.publish(std::move(next)))throw std::runtime_error("Parameter monitor is busy; retry shortly");
    requested_={requested_.generation+1,index,parameter,baseline};history_.clear();return true;
  }
  // Audio owner, at a host buffer boundary (also valid while fully stopped).
  void begin(uint64_t frame) noexcept {
    clocks_[0]=clocks_[clockCount_-1];clockCount_=1;
    if(auto s=selections_.consume()){pending_.reset();selected_=*s;lastValue_=s->baseline;lastSource_={ParameterOrigin::Baseline};}
  }
  void clock(uint64_t frame,uint32_t pattern,uint32_t order,double position,double units) noexcept {
    if(clockCount_&&clocks_[clockCount_-1].frame==frame)clocks_[clockCount_-1]={frame,pattern,order,position,units};
    else if(clockCount_<clocks_.size())clocks_[clockCount_++]={frame,pattern,order,position,units};
  }
  bool watching(uint32_t processor,uint32_t parameter) const noexcept {return selected_.processor==processor&&selected_.parameter==parameter&&processor;}
  void value(uint32_t processor,uint32_t parameter,double value,uint64_t frame,ParameterSource source,bool audible=true) noexcept {
    if(!watching(processor,parameter)||!std::isfinite(value))return;
    audible_=audible;
    auto p=point(value,frame,source);
    if(pending_&&(pending_->source!=source||pending_->pattern!=p.pattern||pending_->order!=p.order||frame<pending_->frame||frame-bucketFrame_>=quantum_||pending_->audible!=audible))flush();
    if(pending_){p.minimum=std::min(pending_->minimum,value);p.maximum=std::max(pending_->maximum,value);}
    // Keep the bucket's start for bounded sampling, and its last value/time for drawing.
    if(pending_) {
      pending_->minimum=p.minimum;pending_->maximum=p.maximum;pending_->value=value;pending_->frame=frame;pending_->position=p.position;
    } else {pending_=p;bucketFrame_=frame;}
    lastValue_=value;lastSource_=source;
  }
  void contribution(uint32_t processor,uint32_t parameter,uint64_t source,double value,uint64_t frame) noexcept {
    if(!watching(processor,parameter)||!std::isfinite(value))return;
    // Control source contributions are normalized before the graph's final clamp.
    push(point(value,frame,{ParameterOrigin::GraphSource,source}));
  }
  void held(uint32_t processor,uint64_t frame,bool audible=true) noexcept {
    if(selected_.processor!=processor||!processor)return;
    flush();audible_=audible;push(point(lastValue_,frame,lastSource_));
  }
  void flush() noexcept {if(pending_){push(*pending_);pending_.reset();}}
  const std::deque<ParameterTracePoint> &history() {
    auto r=read_.load(std::memory_order_relaxed),w=write_.load(std::memory_order_acquire);
    for(;r!=w;++r){auto p=queue_[r%capacity];if(p.generation!=requested_.generation)continue;p.sequence=++sequence_;history_.push_back(p);if(history_.size()>capacity)history_.pop_front();}
    read_.store(r,std::memory_order_release);return history_;
  }
};
}
