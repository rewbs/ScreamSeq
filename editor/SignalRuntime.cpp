#include "SignalRuntime.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace Tracker {
SignalControls::SignalControls(SignalDefinition d,double rate):definition(std::move(d)) {
  for(auto &node:definition.nodes){
    std::sort(node.envelopes.begin(),node.envelopes.end(),[](const auto &a,const auto &b){return a.pattern<b.pattern;});
    envelopeCoefficients.push_back({std::exp(-1/(rate*node.attack)),std::exp(-1/(rate*node.release))});
  }
}
void SignalRuntime::controls(const SignalControls &next) noexcept {
  controls_=&next.definition;
  for(size_t i=0;i<nodes_.size();++i){nodes_[i].attackCoefficient=next.envelopeCoefficients[i][0];nodes_[i].releaseCoefficient=next.envelopeCoefficients[i][1];}
  for(size_t i=0;i<edges_.size();++i)edges_[i].spec=&controls_->audio[i];
  for(auto &t:targets_)if(!t.sources.empty())t.base=controls_->modulation[t.sources.front().second].base;
}
SignalRuntime::Port *SignalRuntime::lookup(const std::vector<std::unique_ptr<Port>> &ports,uint32_t index) noexcept {
  for(const auto &p:ports)if(p->index==index)return p.get();return nullptr;
}
SignalRuntime::Port *SignalRuntime::port(std::vector<std::unique_ptr<Port>> &ports,uint32_t index) {
  if(auto p=lookup(ports,index))return p;
  ports.push_back(std::make_unique<Port>());ports.back()->index=index;return ports.back().get();
}
void SignalRuntime::Edge::add(uint32_t count,float *observed,const SignalGroupRuntime *groups,size_t edge) noexcept {
  auto *from=source->samples.data(),*to=target->samples.data();const float gain=float(spec->gain);
  if(delay.empty()){for(uint32_t i=0;i<count*2;++i){const auto value=(groups?groups->audio(edge,from[i],i):from[i])*gain;to[i]+=value;if(observed)observed[i]=value;}return;}
  for(uint32_t i=0;i<count*2;++i){const auto value=delay[cursor]*gain;to[i]+=value;if(observed)observed[i]=value;delay[cursor]=groups?groups->audio(edge,from[i],i):from[i];if(++cursor==delay.size())cursor=0;}
}
SignalRuntime::SignalRuntime(SignalDefinition d,SignalPlan p,double rate,std::span<const SignalParameterInfo> parameters):definition_(std::move(d)),plan_(std::move(p)),sampleRate_(rate) {
  if(!std::isfinite(rate)||rate<8000||rate>384000)throw std::invalid_argument("Invalid signal graph sample rate");
  for(auto &node:definition_.nodes)std::sort(node.envelopes.begin(),node.envelopes.end(),[](const auto &a,const auto &b){return a.pattern<b.pattern;});
  watchesNotes_=std::any_of(definition_.nodes.begin(),definition_.nodes.end(),[](const auto &n){return n.kind==SignalNodeKind::NoteEnvelope;});
  nodes_.resize(definition_.nodes.size());
  for(size_t i=0;i<nodes_.size();++i){port(nodes_[i].inputs,0);port(nodes_[i].outputs,0);nodes_[i].attackCoefficient=std::exp(-1/(rate*definition_.nodes[i].attack));nodes_[i].releaseCoefficient=std::exp(-1/(rate*definition_.nodes[i].release));}
  for(size_t i=0;i<nodes_.size();++i)if(definition_.nodes[i].kind==SignalNodeKind::Plugin){for(auto p:definition_.nodes[i].plugin.inputs)port(nodes_[i].inputs,p);for(auto p:definition_.nodes[i].plugin.outputs)port(nodes_[i].outputs,p);}
  for(size_t i=0;i<definition_.audio.size();++i){const auto &e=plan_.edges[i];auto &spec=definition_.audio[i];
    edges_.push_back({&spec,port(nodes_[e.source].outputs,spec.output),port(nodes_[e.target].inputs,spec.input),std::vector<float>(size_t(e.delay)*2),0});}
  for(size_t i=0;i<nodes_.size();++i){auto &n=nodes_[i];std::vector<std::string> keys;for(const auto &p:n.inputs)keys.push_back(audioTrimPort(false,p->index));for(const auto &p:n.outputs)keys.push_back(audioTrimPort(true,p->index));n.trims=AudioTrimRuntime(std::move(keys),definition_.nodes[i].trims,rate);}
  for(auto &n:nodes_)for(auto &p:n.inputs)if(p->index)n.auxiliary.push_back({p->index,p->samples.data()});
  for(const auto &p:nodes_[plan_.output].inputs)port(result_,p->index);
  if(!definition_.groups.empty())groups_=std::make_unique<SignalGroupRuntime>(definition_,plan_,rate);
  for(size_t i=0;i<definition_.modulation.size();++i){const auto &m=definition_.modulation[i];if(!m.enabled)continue;
    auto index=[&](uint64_t id){return size_t(std::find_if(definition_.nodes.begin(),definition_.nodes.end(),[&](const auto &n){return n.id==id;})-definition_.nodes.begin());};
    const auto to=index(m.target),from=index(m.source);
    auto target=std::find_if(targets_.begin(),targets_.end(),[&](const auto &v){return v.node==to&&v.parameter==m.parameter;});
    if(target==targets_.end()){targets_.push_back({to,m.parameter,m.base,{}});target=targets_.end()-1;}
    const auto metadata=std::find_if(parameters.begin(),parameters.end(),[&](const auto &v){return v.node==m.target&&v.parameter==m.parameter;});
    if(metadata!=parameters.end()){if(!std::isfinite(metadata->normalizedStep)||metadata->normalizedStep<0||metadata->normalizedStep>1)throw std::invalid_argument("Invalid graph parameter quantization step");target->step=metadata->normalizedStep;}
    if(m.quantized&&target->step<=0)throw std::invalid_argument("Quantized graph modulation requires prepared parameter steps");
    target->sources.emplace_back(from,i);
  }
  for(size_t i=0;i<definition_.nodes.size();++i)nodeIndex_.emplace_back(definition_.nodes[i].id,i);
  for(size_t i=0;i<definition_.audio.size();++i){const auto &e=definition_.audio[i];edgeIndex_.emplace_back(EdgeIdentity{e.source,e.output,e.target,e.input},i);}
  std::sort(nodeIndex_.begin(),nodeIndex_.end());std::sort(edgeIndex_.begin(),edgeIndex_.end());for(auto &n:nodes_)n.trims.sourceReader(readTrimSource,this);if(groups_)groups_->trimSourceReader(readTrimSource,this);
  hasTrimModulation_=std::any_of(definition_.nodes.begin(),definition_.nodes.end(),[](const auto &n){return !n.trims.modulation.empty();})||std::any_of(definition_.groups.begin(),definition_.groups.end(),[](const auto &g){return !g.trims.modulation.empty();});preparedBytes_=measureStorage();
}
bool SignalRuntime::sameLayout(const SignalDefinition &next) const noexcept {
  if(next.nodes.size()!=definition_.nodes.size()||next.audio.size()!=definition_.audio.size()||next.modulation.size()!=definition_.modulation.size())return false;
  if(next.groups.size()!=definition_.groups.size())return false;
  for(size_t i=0;i<next.groups.size();++i){const auto &a=next.groups[i],&b=definition_.groups[i];if(a.id!=b.id||a.parent!=b.parent||a.nodes!=b.nodes||a.dryRoutes!=b.dryRoutes||a.trims.modulation!=b.trims.modulation)return false;}
  for(size_t i=0;i<next.nodes.size();++i)if(next.nodes[i].id!=definition_.nodes[i].id||next.nodes[i].kind!=definition_.nodes[i].kind||next.nodes[i].trims.modulation!=definition_.nodes[i].trims.modulation)return false;
  for(size_t i=0;i<next.audio.size();++i){const auto &a=next.audio[i],&b=definition_.audio[i];if(std::tie(a.source,a.output,a.target,a.input)!=std::tie(b.source,b.output,b.target,b.input))return false;}
  for(size_t i=0;i<next.modulation.size();++i){const auto &a=next.modulation[i],&b=definition_.modulation[i];if(std::tie(a.source,a.target,a.parameter,a.enabled)!=std::tie(b.source,b.target,b.parameter,b.enabled))return false;}
  return true;
}
bool SignalRuntime::compatibleHistory(const SignalRuntime &previous) const noexcept {
  if(plan_.totalLatency!=previous.plan_.totalLatency)return false;
  for(const auto &[identity,index]:edgeIndex_){const auto found=std::lower_bound(previous.edgeIndex_.begin(),previous.edgeIndex_.end(),std::pair{identity,size_t(0)});
    if(found!=previous.edgeIndex_.end()&&found->first==identity&&plan_.edges[index].delay!=previous.plan_.edges[found->second].delay)return false;
  }
  return true;
}
void SignalRuntime::inheritState(SignalRuntime &previous) noexcept {
  if(&previous==this)return;
  midi_=previous.midi_;amount_=previous.amount_;gate_=previous.gate_;
  if(groups_&&previous.groups_)groups_->inheritState(*previous.groups_);
  for(const auto &[id,index]:nodeIndex_){const auto found=std::lower_bound(previous.nodeIndex_.begin(),previous.nodeIndex_.end(),std::pair{id,size_t(0)});
    if(found!=previous.nodeIndex_.end()&&found->first==id&&definition_.nodes[index].kind==previous.definition_.nodes[found->second].kind){
      auto &node=nodes_[index];const auto &old=previous.nodes_[found->second];
      node.trims.inherit(old.trims);node.envelope=old.envelope;node.noteGate=old.noteGate;node.pendingNoteEvent=old.pendingNoteEvent;
    }
  }
  for(const auto &[identity,index]:edgeIndex_){const auto found=std::lower_bound(previous.edgeIndex_.begin(),previous.edgeIndex_.end(),std::pair{identity,size_t(0)});
    if(found!=previous.edgeIndex_.end()&&found->first==identity){auto &a=edges_[index],&b=previous.edges_[found->second];
      if(a.delay.size()==b.delay.size()){a.delay.swap(b.delay);std::swap(a.cursor,b.cursor);}
    }
  }
}
void SignalRuntime::updateLatencyPlan(SignalPlan plan) {
  if (plan.order != plan_.order || plan.edges.size() != edges_.size())
    throw std::invalid_argument("Latency update changed signal topology");
  for (size_t i = 0; i < edges_.size(); ++i) if (plan.edges[i].delay != plan_.edges[i].delay) {
    std::vector<float>(size_t(plan.edges[i].delay) * 2,0).swap(edges_[i].delay); edges_[i].cursor = 0;
  }
  plan_ = std::move(plan);
  if(groups_){auto next=std::make_unique<SignalGroupRuntime>(definition_,plan_,sampleRate_);next->inheritState(*groups_);groups_=std::move(next);groups_->trimSourceReader(readTrimSource,this);}
  preparedBytes_=measureStorage();
}
void SignalRuntime::parameterBase(uint64_t node,uint32_t parameter,double value) noexcept {
  for(auto &target:targets_)if(definition_.nodes[target.node].id==node&&target.parameter==parameter)target.base=value;
}
void SignalRuntime::note(bool gate,bool retrigger) noexcept {
  for(size_t i=0;i<nodes_.size();++i)if(controls_->nodes[i].kind==SignalNodeKind::NoteEnvelope){
    auto &node=nodes_[i];auto &events=node.noteGate;
    const auto increment=[](uint64_t &value)noexcept{if(value<UINT64_MAX)++value;};
    if(gate!=gate_){if(gate)increment(events.on);else increment(events.off);node.pendingNoteEvent=true;}
    if(retrigger){increment(events.retrigger);node.pendingNoteEvent=true;node.envelope=0;}
  }
  gate_=gate;
}
const SignalPatternEnvelope *SignalRuntime::envelope(size_t index,uint64_t pattern) const noexcept {
  const auto &lanes=controls_->nodes[index].envelopes;
  auto found=std::lower_bound(lanes.begin(),lanes.end(),pattern,[](const auto &lane,uint64_t p){return lane.pattern<p;});
  return found!=lanes.end()&&found->pattern==pattern&&found->enabled ? &*found : nullptr;
}
double SignalRuntime::source(size_t index,double beat,double position,const SignalClock &clock) const noexcept {
  const auto &n=controls_->nodes[index];
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
  const auto kind=controls_->nodes[index].kind;
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
  if(observer_)observer_->activate();
  const double beatsPerFrame=clock.playing?clock.tempo/(60*sampleRate_):0;
  // Dense source values are only consumed by parameter targets. Without any
  // targets the audio block can be larger than the fixed 32-sample scratch.
  const bool discrete=hasTrimModulation_||(!targets_.empty()&&bool(groups_))||std::any_of(targets_.begin(),targets_.end(),[&](const auto &t){return controls_->modulation[t.sources.front().second].quantized;});
  for(uint32_t offset=0;offset<frames;){auto count=std::min<uint32_t>((targets_.empty()&&!hasTrimModulation_)?maximumFrames:uint32_t(quantum-(position+offset)%quantum),frames-offset);
    // Stop at point boundaries so step curves never become short ramps.
    if(clock.playing&&clock.unitsPerFrame>0)for(size_t i=0;i<nodes_.size();++i)if(controls_->nodes[i].kind==SignalNodeKind::Automation){
      if(const auto *lane=envelope(i,clock.pattern)){
        const double now=clock.position+offset*clock.unitsPerFrame;
        auto next=std::upper_bound(lane->points.begin(),lane->points.end(),now,[](double p,const auto &point){return p<point.position;});
        if(next!=lane->points.end()){const double framesTo=std::ceil((next->position-clock.position)/clock.unitsPerFrame-1e-9)-offset;if(framesTo>=1&&framesTo<count)count=uint32_t(framesTo);}
        if(next!=lane->points.begin()&&(next-1)->curve==AutomationCurve::StepNext&&std::abs(now-(next-1)->position)<1e-8)count=1;
      }
    }
    for(auto &n:nodes_)for(auto &p:n.inputs)std::fill_n(p->samples.data(),count*2,0.f);
    trimPosition_=position+offset;trimFrames_=count;
    if(groups_)groups_->begin(*controls_,count,position+offset);
    for(size_t index:plan_.order){auto &n=nodes_[index];const auto &spec=controls_->nodes[index];
      // Every edge is evaluated exactly once, when its destination is ready.
      for(size_t e=0;e<edges_.size();++e)if(plan_.edges[e].target==index){auto *capture=observer_?observationScratch_->data():nullptr;edges_[e].add(count,capture,groups_.get(),e);if(observer_)observer_->route(uint32_t(e),capture,count,position+offset,edges_[e].spec->gain);}
      n.trims.begin(spec.trims,position+offset);
      for(size_t p=0;p<n.inputs.size();++p)n.trims.apply(p,n.inputs[p]->samples.data(),count,position+offset);
      auto *in=lookup(n.inputs,0)->samples.data();auto *out=lookup(n.outputs,0)->samples.data();
      if(observer_&&(spec.kind==SignalNodeKind::Plugin||spec.kind==SignalNodeKind::Output||spec.kind==SignalNodeKind::Follower))for(const auto &p:n.inputs)observer_->audio(spec.id,false,p->index,p->samples.data(),count,position+offset);
      if(spec.kind==SignalNodeKind::Input){for(auto &p:n.outputs){const float *from=nullptr;if(!p->index)from=main;else for(const auto &external:inputs)if(external.bus==p->index)from=external.samples;
        if(from)std::copy_n(from+offset*2,count*2,p->samples.data());else std::fill_n(p->samples.data(),count*2,0.f);}}
      else if(spec.kind==SignalNodeKind::Output){for(auto &p:n.inputs)std::copy_n(p->samples.data(),count*2,lookup(result_,p->index)->samples.data()+offset*2);}
      else if(spec.kind==SignalNodeKind::Plugin){
        for(auto &t:targets_)if(t.node==index){
          const bool quantized=controls_->modulation[t.sources.front().second].quantized;
          const bool grouped=groups_&&std::any_of(t.sources.begin(),t.sources.end(),[&](const auto &source){return groups_->affectsModulation(source.second);});
          if(quantized||grouped){
            if((quantized&&!(t.step>0))||count>quantum)return false;
            for(uint32_t f=0;f<count;++f){double value=t.base;for(auto [sourceIndex,edge]:t.sources){const auto &m=controls_->modulation[edge];value+=controls_->nodes[sourceIndex].muted?0:(m.minimum+(m.maximum-m.minimum)*nodes_[sourceIndex].sampled[f])*(groups_?groups_->modulation(edge,f):1);}value=std::clamp(value,0.,1.);t.sampled[f]=quantized?std::clamp(std::round(value/t.step)*t.step,0.,1.):value;}
            if(cb.contribution){const auto at=position+offset+count-1;cb.contribution(cb.context,spec.id,t.parameter,0,t.base,at);for(auto [sourceIndex,edge]:t.sources){const auto &m=controls_->modulation[edge];cb.contribution(cb.context,spec.id,t.parameter,m.source,controls_->nodes[sourceIndex].muted?0:(m.minimum+(m.maximum-m.minimum)*nodes_[sourceIndex].sampled[count-1])*(groups_?groups_->modulation(edge,count-1):1),at);}}
            if(!cb.parameterSamples||!cb.parameterSamples(cb.context,spec.id,t.parameter,{t.sampled.data(),count},position+offset))return false;
            continue;
          }
          double first=t.base,last=t.base;
          if(cb.contribution)cb.contribution(cb.context,spec.id,t.parameter,0,t.base,position+offset);
          for(auto [sourceIndex,edge]:t.sources){const auto &m=controls_->modulation[edge];first+=controls_->nodes[sourceIndex].muted?0:(m.minimum+(m.maximum-m.minimum)*nodes_[sourceIndex].first)*(groups_?groups_->modulation(edge,0):1);last+=controls_->nodes[sourceIndex].muted?0:(m.minimum+(m.maximum-m.minimum)*nodes_[sourceIndex].last)*(groups_?groups_->modulation(edge,count-1):1);
            if(cb.contribution)cb.contribution(cb.context,spec.id,t.parameter,m.source,controls_->nodes[sourceIndex].muted?0:(m.minimum+(m.maximum-m.minimum)*nodes_[sourceIndex].first)*(groups_?groups_->modulation(edge,0):1),position+offset);
          }
          if(!cb.parameter||!cb.parameter(cb.context,spec.id,t.parameter,std::clamp(first,0.,1.),std::clamp(last,0.,1.),position+offset,count-1))return false;
        }
        std::copy_n(in,count*2,out);
        if(!cb.process||!cb.process(cb.context,spec.id,out,count,position+offset,n.auxiliary))return false;
        for(auto &p:n.outputs)if(p->index){const auto *from=cb.output?cb.output(cb.context,spec.id,p->index):nullptr;if(!from)return false;std::copy_n(from,count*2,p->samples.data());}
      } else if(spec.kind==SignalNodeKind::Follower||spec.kind==SignalNodeKind::NoteEnvelope){
        if(spec.kind==SignalNodeKind::NoteEnvelope){n.noteGate.held=gate_;if(n.pendingNoteEvent){n.noteGate.lastFrame=position+offset;n.noteGate.hasEvent=true;n.pendingNoteEvent=false;}}
        for(uint32_t f=0;f<count;++f){const double target=spec.kind==SignalNodeKind::Follower?std::min(1.f,std::max(std::abs(in[f*2]),std::abs(in[f*2+1]))):double(gate_);
          const double coefficient=(target>n.envelope?n.attackCoefficient:n.releaseCoefficient);
          n.envelope=target+coefficient*(n.envelope-target);if(f==0)n.first=n.envelope;if(discrete)n.sampled[f]=n.envelope;}
        n.last=n.envelope;
      } else {n.first=sampledSource(index,position+offset,clock.beat+offset*beatsPerFrame,clock.position+offset*clock.unitsPerFrame,beatsPerFrame,clock);n.last=sampledSource(index,position+offset+count-1,clock.beat+(offset+count-1)*beatsPerFrame,clock.position+(offset+count-1)*clock.unitsPerFrame,beatsPerFrame,clock);if(discrete)for(uint32_t f=0;f<count;++f)n.sampled[f]=sampledSource(index,position+offset+f,clock.beat+(offset+f)*beatsPerFrame,clock.position+(offset+f)*clock.unitsPerFrame,beatsPerFrame,clock);}
      for(size_t p=0;p<n.outputs.size();++p)n.trims.apply(n.inputs.size()+p,n.outputs[p]->samples.data(),count,position+offset);
      if(groups_)groups_->capture(index,count,this,[](void *context,size_t node,uint32_t port)noexcept->const float *{auto *runtime=static_cast<SignalRuntime *>(context);const auto *p=lookup(runtime->nodes_[node].outputs,port);return p?p->samples.data():nullptr;});
      if(observer_){
        if(spec.kind==SignalNodeKind::Input||spec.kind==SignalNodeKind::Plugin)for(const auto &p:n.outputs)observer_->audio(spec.id,true,p->index,p->samples.data(),count,position+offset);
        else if(spec.kind!=SignalNodeKind::Output){observer_->control(spec.id,n.first,n.last,count,position+offset);
          if(spec.kind==SignalNodeKind::NoteEnvelope)observer_->noteGate(spec.id,n.noteGate);
          for(size_t m=0;m<controls_->modulation.size();++m){const auto &edge=controls_->modulation[m];if(edge.source==spec.id){const bool audible=edge.enabled&&!spec.muted;observer_->contribution(uint32_t(m),audible?(edge.minimum+(edge.maximum-edge.minimum)*n.first)*(groups_?groups_->modulation(m,0):1):0,audible?(edge.minimum+(edge.maximum-edge.minimum)*n.last)*(groups_?groups_->modulation(m,count-1):1):0,count,position+offset);}}
        }
      }
    }
    for(const auto &n:nodes_)if(!n.trims.valid())return false;if(groups_&&!groups_->trimsValid())return false;
    offset+=count;
  }
  std::copy_n(lookup(result_,0)->samples.data(),frames*2,main);return true;
}
bool SignalRuntime::readTrimSource(void *context,uint64_t id,uint64_t frame,double &value,bool &enabled) noexcept {
  auto &r=*static_cast<SignalRuntime *>(context);if(frame<r.trimPosition_||frame-r.trimPosition_>=r.trimFrames_)return false;for(size_t i=0;i<r.controls_->nodes.size();++i)if(r.controls_->nodes[i].id==id){enabled=!r.controls_->nodes[i].muted;value=r.nodes_[i].sampled[size_t(frame-r.trimPosition_)];return true;}return false;
}
size_t SignalRuntime::measureStorage() const noexcept {
  size_t bytes=definition_.bytes()+result_.size()*sizeof(Port)+nodes_.capacity()*sizeof(Node)+targets_.capacity()*sizeof(ModulationTarget)+nodeIndex_.capacity()*sizeof(nodeIndex_[0])+edgeIndex_.capacity()*sizeof(edgeIndex_[0]);for(const auto &t:targets_)bytes+=t.sources.capacity()*sizeof(t.sources[0]);for(const auto &n:nodes_)bytes+=n.trims.bytes()+(n.inputs.size()+n.outputs.size())*sizeof(Port);for(const auto &e:edges_)bytes+=e.delay.capacity()*sizeof(float);if(observer_)bytes+=observer_->storageBytes();if(observationScratch_)bytes+=sizeof(*observationScratch_);if(groups_)bytes+=groups_->storageBytes();return bytes;
}
const float *SignalRuntime::output(uint32_t bus) const noexcept {auto p=lookup(result_,bus);return p?p->samples.data():nullptr;}
} // namespace Tracker
