#include "SignalRuntime.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace Tracker {
SignalRuntime::Port *SignalRuntime::lookup(const std::vector<std::unique_ptr<Port>> &ports,uint32_t index) noexcept {
  for(const auto &p:ports)if(p->index==index)return p.get();return nullptr;
}
SignalRuntime::Port *SignalRuntime::port(std::vector<std::unique_ptr<Port>> &ports,uint32_t index) {
  if(auto p=lookup(ports,index))return p;
  ports.push_back(std::make_unique<Port>());ports.back()->index=index;return ports.back().get();
}
void SignalRuntime::Edge::add(uint32_t count) noexcept {
  auto *from=source->samples.data(),*to=target->samples.data();const float gain=float(spec->gain);
  if(delay.empty()){for(size_t i=0;i<count*2;++i)to[i]+=from[i]*gain;return;}
  for(size_t i=0;i<count*2;++i){to[i]+=delay[cursor]*gain;delay[cursor]=from[i];if(++cursor==delay.size())cursor=0;}
}
SignalRuntime::SignalRuntime(SignalDefinition d,SignalPlan p,double rate):definition_(std::move(d)),plan_(std::move(p)),sampleRate_(rate) {
  if(!std::isfinite(rate)||rate<8000||rate>384000)throw std::invalid_argument("Invalid signal graph sample rate");
  for(auto &node:definition_.nodes)std::sort(node.envelopes.begin(),node.envelopes.end(),[](const auto &a,const auto &b){return a.pattern<b.pattern;});
  nodes_.resize(definition_.nodes.size());
  for(size_t i=0;i<nodes_.size();++i){port(nodes_[i].inputs,0);port(nodes_[i].outputs,0);nodes_[i].attackCoefficient=std::exp(-1/(rate*definition_.nodes[i].attack));nodes_[i].releaseCoefficient=std::exp(-1/(rate*definition_.nodes[i].release));}
  for(size_t i=0;i<definition_.audio.size();++i){const auto &e=plan_.edges[i];auto &spec=definition_.audio[i];
    edges_.push_back({&spec,port(nodes_[e.source].outputs,spec.output),port(nodes_[e.target].inputs,spec.input),std::vector<float>(size_t(e.delay)*2),0});}
  for(auto &n:nodes_)for(auto &p:n.inputs)if(p->index)n.auxiliary.push_back({p->index,p->samples.data()});
  for(const auto &p:nodes_[plan_.output].inputs)port(result_,p->index);
  for(size_t i=0;i<definition_.modulation.size();++i){const auto &m=definition_.modulation[i];if(!m.enabled)continue;
    auto index=[&](uint64_t id){return size_t(std::find_if(definition_.nodes.begin(),definition_.nodes.end(),[&](const auto &n){return n.id==id;})-definition_.nodes.begin());};
    const auto to=index(m.target),from=index(m.source);
    auto target=std::find_if(targets_.begin(),targets_.end(),[&](const auto &v){return v.node==to&&v.parameter==m.parameter;});
    if(target==targets_.end()){targets_.push_back({to,m.parameter,m.base,{}});target=targets_.end()-1;}
    target->sources.emplace_back(from,i);
  }
}
void SignalRuntime::updateLatencyPlan(SignalPlan plan) {
  if (plan.order != plan_.order || plan.edges.size() != edges_.size())
    throw std::invalid_argument("Latency update changed signal topology");
  for (size_t i = 0; i < edges_.size(); ++i) if (plan.edges[i].delay != plan_.edges[i].delay) {
    edges_[i].delay.assign(size_t(plan.edges[i].delay) * 2, 0); edges_[i].cursor = 0;
  }
  plan_ = std::move(plan);
}
void SignalRuntime::note(bool gate,bool retrigger) noexcept {
  gate_=gate;
  if(retrigger)for(size_t i=0;i<nodes_.size();++i)if(definition_.nodes[i].kind==SignalNodeKind::NoteEnvelope)nodes_[i].envelope=0;
}
const SignalPatternEnvelope *SignalRuntime::envelope(size_t index,uint64_t pattern) const noexcept {
  const auto &lanes=definition_.nodes[index].envelopes;
  auto found=std::lower_bound(lanes.begin(),lanes.end(),pattern,[](const auto &lane,uint64_t p){return lane.pattern<p;});
  return found!=lanes.end()&&found->pattern==pattern&&found->enabled ? &*found : nullptr;
}
double SignalRuntime::source(size_t index,double beat,double position,const SignalClock &clock) const noexcept {
  const auto &n=definition_.nodes[index];
  switch(n.kind){
  case SignalNodeKind::LFO:return .5+.5*std::sin(2*std::numbers::pi*(beat*n.rate+n.phase));
  case SignalNodeKind::Random:{// Repeatable at a song position; independent instance history is unnecessary.
    uint64_t x=uint64_t(int64_t(std::floor(beat*n.rate+n.phase))) ^ (n.id*0x9e3779b97f4a7c15ULL);
    x=(x^(x>>30))*0xbf58476d1ce4e5b9ULL;x=(x^(x>>27))*0x94d049bb133111ebULL;x^=x>>31;
    return double(x>>11)*(1.0/9007199254740991.0);}
  case SignalNodeKind::MIDI:return midi_[n.controller];
  case SignalNodeKind::Amount:return amount_;
  case SignalNodeKind::Automation:{auto lane=envelope(index,clock.pattern);return lane?automationValue(lane->points,position,clock.endPosition,clock.rowsPerBeat):0;}
  default:return nodes_[index].envelope;
  }
}
double SignalRuntime::sampledSource(size_t index,uint64_t frame,double beat,double position,double beatsPerFrame,const SignalClock &clock) const noexcept {
  const auto kind=definition_.nodes[index].kind;
  if(kind!=SignalNodeKind::Automation&&kind!=SignalNodeKind::LFO)return source(index,beat,position,clock);
  // The approximation grid belongs to the song sample clock, not the caller's
  // buffer. A callback ending partway through a quantum samples the same line
  // that a larger callback would use; it must not fit a new, shorter segment.
  double first=-double(frame%quantum),last=first+quantum-1;
  if(kind==SignalNodeKind::Automation&&clock.playing&&clock.unitsPerFrame>0){
    const auto *lane=envelope(index,clock.pattern);if(!lane)return 0;
    const auto sampleAt=[&](double p){return std::ceil((p-position)/clock.unitsPerFrame-1e-9);};
    first=std::max(first,sampleAt(0));if(clock.endPosition>position)last=std::min(last,sampleAt(clock.endPosition)-1);
    auto right=std::upper_bound(lane->points.begin(),lane->points.end(),position,[](double p,const auto &point){return p<point.position;});
    if(right!=lane->points.end())last=std::min(last,sampleAt(right->position)-1);
    if(right!=lane->points.begin()){
      const auto &left=*(right-1);double boundary=sampleAt(left.position);
      // Step-at-start has a distinct value at its exact point and changes on
      // the following sample. Never interpolate across either side of it.
      if(left.curve==AutomationCurve::StepNext&&std::abs(position+boundary*clock.unitsPerFrame-left.position)<1e-8){
        if(boundary==0)last=std::min(last,0.0);else boundary+=1;
      }
      first=std::max(first,boundary);
    }
  }
  if(first>=last)return source(index,beat,position,clock);
  const auto a=source(index,beat+first*beatsPerFrame,position+first*clock.unitsPerFrame,clock);
  const auto b=source(index,beat+last*beatsPerFrame,position+last*clock.unitsPerFrame,clock);
  return a+(b-a)*(-first)/(last-first);
}
bool SignalRuntime::render(float *main,uint32_t frames,uint64_t position,SignalClock clock,const SignalCallbacks &cb,std::span<const MixerAudioInput> inputs) noexcept {
  if(!main||frames>maximumFrames||!std::isfinite(clock.beat)||!std::isfinite(clock.tempo)||clock.tempo<=0||!std::isfinite(clock.position)||!std::isfinite(clock.unitsPerFrame)||clock.unitsPerFrame<0||!std::isfinite(clock.endPosition)||!std::isfinite(clock.rowsPerBeat)||clock.rowsPerBeat<1)return false;
  const double beatsPerFrame=clock.playing?clock.tempo/(60*sampleRate_):0;
  for(uint32_t offset=0;offset<frames;){auto count=std::min<uint32_t>(targets_.empty()?maximumFrames:uint32_t(quantum-(position+offset)%quantum),frames-offset);
    // Stop at point boundaries so step curves never become short ramps.
    if(clock.playing&&clock.unitsPerFrame>0)for(size_t i=0;i<nodes_.size();++i)if(definition_.nodes[i].kind==SignalNodeKind::Automation){
      if(const auto *lane=envelope(i,clock.pattern)){
        const double now=clock.position+offset*clock.unitsPerFrame;
        auto next=std::upper_bound(lane->points.begin(),lane->points.end(),now,[](double p,const auto &point){return p<point.position;});
        if(next!=lane->points.end()){const double framesTo=std::ceil((next->position-clock.position)/clock.unitsPerFrame-1e-9)-offset;if(framesTo>=1&&framesTo<count)count=uint32_t(framesTo);}
        if(next!=lane->points.begin()&&(next-1)->curve==AutomationCurve::StepNext&&std::abs(now-(next-1)->position)<1e-8)count=1;
      }
    }
    for(auto &n:nodes_)for(auto &p:n.inputs)std::fill_n(p->samples.data(),count*2,0.f);
    for(size_t index:plan_.order){auto &n=nodes_[index];const auto &spec=definition_.nodes[index];
      // Every edge is evaluated exactly once, when its destination is ready.
      for(size_t e=0;e<edges_.size();++e)if(plan_.edges[e].target==index)edges_[e].add(count);
      auto *in=lookup(n.inputs,0)->samples.data();auto *out=lookup(n.outputs,0)->samples.data();
      if(spec.kind==SignalNodeKind::Input){for(auto &p:n.outputs){const float *from=nullptr;if(!p->index)from=main;else for(const auto &external:inputs)if(external.bus==p->index)from=external.samples;
        if(from)std::copy_n(from+offset*2,count*2,p->samples.data());else std::fill_n(p->samples.data(),count*2,0.f);}}
      else if(spec.kind==SignalNodeKind::Output){for(auto &p:n.inputs)std::copy_n(p->samples.data(),count*2,lookup(result_,p->index)->samples.data()+offset*2);}
      else if(spec.kind==SignalNodeKind::Plugin){
        for(const auto &t:targets_)if(t.node==index){double first=t.base,last=t.base;
          for(auto [sourceIndex,edge]:t.sources){const auto &m=definition_.modulation[edge];first+=m.minimum+(m.maximum-m.minimum)*nodes_[sourceIndex].first;last+=m.minimum+(m.maximum-m.minimum)*nodes_[sourceIndex].last;}
          if(!cb.parameter||!cb.parameter(cb.context,spec.id,t.parameter,std::clamp(first,0.,1.),std::clamp(last,0.,1.),position+offset,count-1))return false;
        }
        std::copy_n(in,count*2,out);
        if(!cb.process||!cb.process(cb.context,spec.id,out,count,position+offset,n.auxiliary))return false;
        for(auto &p:n.outputs)if(p->index){const auto *from=cb.output?cb.output(cb.context,spec.id,p->index):nullptr;if(!from)return false;std::copy_n(from,count*2,p->samples.data());}
      } else if(spec.kind==SignalNodeKind::Follower||spec.kind==SignalNodeKind::NoteEnvelope){
        for(uint32_t f=0;f<count;++f){const double target=spec.kind==SignalNodeKind::Follower?std::min(1.f,std::max(std::abs(in[f*2]),std::abs(in[f*2+1]))):double(gate_);
          const double coefficient=(target>n.envelope?n.attackCoefficient:n.releaseCoefficient);
          n.envelope=target+coefficient*(n.envelope-target);if(f==0)n.first=n.envelope;}
        n.last=n.envelope;
      } else {n.first=sampledSource(index,position+offset,clock.beat+offset*beatsPerFrame,clock.position+offset*clock.unitsPerFrame,beatsPerFrame,clock);n.last=sampledSource(index,position+offset+count-1,clock.beat+(offset+count-1)*beatsPerFrame,clock.position+(offset+count-1)*clock.unitsPerFrame,beatsPerFrame,clock);}
    }
    offset+=count;
  }
  std::copy_n(lookup(result_,0)->samples.data(),frames*2,main);return true;
}
size_t SignalRuntime::storageBytes() const noexcept {
  size_t bytes=definition_.bytes()+result_.size()*sizeof(Port);for(const auto &n:nodes_)bytes+=(n.inputs.size()+n.outputs.size())*sizeof(Port);for(const auto &e:edges_)bytes+=e.delay.size()*sizeof(float);return bytes;
}
const float *SignalRuntime::output(uint32_t bus) const noexcept {auto p=lookup(result_,bus);return p?p->samples.data():nullptr;}
} // namespace Tracker
