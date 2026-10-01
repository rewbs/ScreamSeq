#include "GraphPluginEndpoint.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace Tracker {
double graphModulationStep(const PluginParameter &parameter,bool quantized) {
  const double range=double(parameter.max)-double(parameter.min),step=parameter.step;
  if(!parameter.writable||!std::isfinite(range)||range<=0||!std::isfinite(step)||step<0||step>range)throw std::invalid_argument("Modulation requires a writable parameter with a finite supported range");
  if((!parameter.continuous&&!quantized)||(quantized&&step<=0))throw std::invalid_argument("Stepped recipe parameters require explicit discrete modulation and a supported step");
  return step/range;
}
GraphPluginState::GraphPluginState(const SignalDefinition &definition,const SignalNode &node,double rate,bool offline):recipe(node.plugin) {
  const auto &r=recipe;
  PluginState state;state.descriptor={r.type,r.subtype,r.manufacturer,r.name,r.format,r.path,r.classID,false};state.state=r.state;state.audioLayout=r.audioLayout;
  state.instanceID="graph-"+std::to_string(definition.id)+"-"+std::to_string(node.id);state.auxiliaryInputs=r.inputs;state.auxiliaryOutputs=r.outputs;state.bypass=r.bypass;
  plugin=std::make_shared<NativePlugin>(state,rate,offline);
  if(plugin->isInstrument())throw std::invalid_argument("Subgraphs accept effect plugins; route instrument outputs into a mixer bus");
  inputs|=plugin->preparedAuxiliaryInputs();outputs|=plugin->preparedAuxiliaryOutputs();
  for(const auto &bus:plugin->buses())if(bus.active&&bus.supported&&bus.index<64)(bus.input?inputs:outputs)|=uint64_t(1)<<bus.index;
  latency=uint32_t(std::llround(plugin->latency()*rate));parameters=plugin->parameters();
  for(const auto &[id,value]:r.parameters){const auto p=std::find_if(parameters.begin(),parameters.end(),[&](const auto &p){return p.id==id;});
    if(p==parameters.end()||!p->writable||!std::isfinite(value)||value<p->min||value>p->max||!plugin->parameter(id,float(value)))throw std::invalid_argument("Graph parameter baseline is unavailable or outside its range");
  }
  for(const auto &m:definition.modulation)if(m.target==node.id&&m.enabled){const auto p=std::find_if(parameters.begin(),parameters.end(),[&](const auto &p){return p.id==m.parameter;});
    if(p==parameters.end())throw std::invalid_argument("Graph modulation parameter is unavailable");
    (void)graphModulationStep(*p,m.quantized);
    plugin->prepareMusicalAutomation();plugin->includeParameterRange(m.parameter,p->min,p->max);
    if(std::find(initialModulated.begin(),initialModulated.end(),m.parameter)==initialModulated.end())initialModulated.push_back(m.parameter);
  }
  preparedTail=plugin->tail();
  baselines=std::make_unique<double[]>(parameters.size());initialBaselines.reserve(parameters.size());
  for(size_t i=0;i<parameters.size();++i){const auto found=r.parameters.find(parameters[i].id);baselines[i]=found==r.parameters.end()?parameters[i].value:found->second;initialBaselines.push_back(baselines[i]);}
}
size_t GraphPluginState::storageBytes() const noexcept {return sizeof(*this)+plugin->preparedStorageBytes()+parameters.capacity()*sizeof(PluginParameter)+initialBaselines.capacity()*sizeof(double)+parameters.size()*sizeof(double)+recipe.state.capacity()+initialModulated.capacity()*sizeof(uint32_t);}
GraphPluginEndpoint::GraphPluginEndpoint(std::shared_ptr<GraphPluginState> state,double rate):initial_(std::move(state)),current_(initial_.get()),published_(current_),fadeFrames_(std::max(1u,uint32_t(std::ceil(rate*.01)))) {
  for(unsigned i=1;i<64;++i)if(initial_->outputs&(uint64_t(1)<<i))outputs_[i]=std::make_unique<std::array<float,8192>>();
}
size_t GraphPluginEndpoint::storageBytes() const noexcept {size_t bytes=sizeof(*this);for(const auto &p:outputs_)if(p)bytes+=sizeof(*p);return bytes;}
bool GraphPluginEndpoint::ready(const GraphPluginState *expected) const noexcept {return published_.load(std::memory_order_acquire)==expected&&settled_.load(std::memory_order_acquire);}
void GraphPluginEndpoint::observe(ParameterActivity *activity,uint32_t token) noexcept {activity_=activity;activityToken_=token;current_->plugin->observe(activity,token);}
void GraphPluginEndpoint::observePrepared(GraphPluginState &state) const noexcept {state.plugin->observe(activity_,activityToken_);}
void GraphPluginEndpoint::adopt(GraphPluginState &state) noexcept {
  if(&state==current_)return;
  previous_=current_;current_=&state;fade_=0;settled_.store(false,std::memory_order_relaxed);
  observedTailRevision_=state.plugin->tailRevision();tailEpoch_.fetch_add(1,std::memory_order_relaxed);
  previous_->plugin->observe(nullptr,0);current_->plugin->observe(activity_,activityToken_);
  published_.store(current_,std::memory_order_release);
}
void GraphPluginEndpoint::bypass(bool value) noexcept {
  current_->plugin->bypass(value);
  if(previous_)previous_->plugin->bypass(value);
}
double GraphPluginEndpoint::tailGrowth() const noexcept {const auto *state=published_.load(std::memory_order_acquire);return std::max(0.,state->plugin->tail()-state->preparedTail);}
void GraphPluginEndpoint::observeTail() noexcept {const auto revision=current_->plugin->tailRevision();if(revision!=observedTailRevision_){observedTailRevision_=revision;tailEpoch_.fetch_add(1,std::memory_order_relaxed);}}
void GraphPluginEndpoint::transport(PluginTransport transport,bool audible) noexcept {current_->plugin->transport(transport);current_->plugin->audible(audible);if(previous_){previous_->plugin->transport(transport);previous_->plugin->audible(false);}}
bool GraphPluginEndpoint::process(float *audio,uint32_t frames,uint64_t position,std::span<const MixerAudioInput> inputs) noexcept {
  struct Clear{size_t &count;~Clear(){count=0;}} clear{sampledCount_};
  const std::span<const PluginParameterSamples> values(sampled_.data(),sampledCount_);
  blended_=previous_!=nullptr;
  if(!previous_){const bool okay=current_->plugin->process(audio,frames,position,inputs,nullptr,values);observeTail();return okay;}
  if(frames>4096)return false;
  std::copy_n(audio,frames*2,oldAudio_.data());
  const bool oldOkay=previous_->plugin->process(oldAudio_.data(),frames,position,inputs,nullptr,values);
  const bool newOkay=current_->plugin->process(audio,frames,position,inputs,nullptr,values);
  // A newly prepared vendor (including its host dry delay) starts empty.
  // Advance it on the real input for its declared latency before blending;
  // otherwise even a fully bypassed replacement dips toward cold silence.
  // Both incarnations still process each ordinary block exactly once.
  auto mixAt=[&](uint32_t frame)noexcept{const auto elapsed=fade_+frame;return elapsed<current_->latency?0.f:std::min(1.f,float(elapsed-current_->latency+1)/fadeFrames_);};
  for(uint32_t f=0;f<frames;++f){const float mix=mixAt(f);for(unsigned c=0;c<2;++c)audio[f*2+c]=oldAudio_[f*2+c]*(1-mix)+audio[f*2+c]*mix;}
  for(unsigned port=1;port<64;++port)if(outputs_[port]){
    const auto *before=previous_->plugin->auxiliaryOutput(port),*after=current_->plugin->auxiliaryOutput(port);auto *out=outputs_[port]->data();
    for(uint32_t f=0;f<frames;++f){const float mix=mixAt(f);for(unsigned c=0;c<2;++c)out[f*2+c]=(before?before[f*2+c]:0)*(1-mix)+(after?after[f*2+c]:0)*mix;}
  }
  observeTail();fade_+=frames;if(fade_>=current_->latency+fadeFrames_){previous_=nullptr;settled_.store(true,std::memory_order_release);}return oldOkay&&newOkay;
}
const float *GraphPluginEndpoint::output(uint32_t port) const noexcept {return blended_&&port<outputs_.size()&&outputs_[port]?outputs_[port]->data():current_->plugin->auxiliaryOutput(port);}
bool GraphPluginEndpoint::ramp(uint32_t id,double a,double b,uint64_t frame,uint32_t duration,ParameterSource source) noexcept {
  const bool now=current_->plugin->scheduleRamp(id,a,b,frame,duration,source);
  return (!previous_||previous_->plugin->scheduleRamp(id,a,b,frame,duration,source))&&now;
}
bool GraphPluginEndpoint::parameter(uint32_t id,double value,uint64_t frame) noexcept {
  current_->plugin->cancelScheduledParameter(id);const bool now=current_->plugin->appliedParameter(id,value,frame,{});
  if(previous_){previous_->plugin->cancelScheduledParameter(id);return previous_->plugin->appliedParameter(id,value,frame,{})&&now;}return now;
}
bool GraphPluginEndpoint::parameterSamples(uint32_t id,double minimum,double maximum,std::span<const double> values,uint64_t node) noexcept {
  if(sampledCount_==sampled_.size()||values.empty()||values.size()>32)return false;
  for(size_t i=0;i<sampledCount_;++i)if(sampled_[i].parameter==id)return false;
  current_->plugin->cancelScheduledParameter(id);if(previous_)previous_->plugin->cancelScheduledParameter(id);
  sampled_[sampledCount_++]={id,minimum,maximum,values,{ParameterOrigin::Graph,node}};return true;
}
void GraphPluginEndpoint::contribution(uint32_t id,uint64_t source,double value,uint64_t frame) noexcept {current_->plugin->contribution(id,source,value,frame);}
}
