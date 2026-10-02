#include "MixerRuntime.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Tracker {
bool MixerRuntime::Delay::prepare(const float *interleaved,const float *left,const float *right,uint32_t frames,uint64_t position) noexcept {
  auto &state=*state_;
  if(state.frames && position==state.position)return frames==state.frames;
  if(state.frames && position<state.position+state.frames)return false;
  state.position=position;state.frames=frames;
  for(uint32_t i=0;i<frames*2;++i) {
    state.output[i]=state.buffer[state.cursor];
    state.buffer[state.cursor]=interleaved?interleaved[i]:(i%2?(right?right[i/2]:0):(left?left[i/2]:0));
    if(++state.cursor==state.buffer.size())state.cursor=0;
  }
  return true;
}
bool MixerRuntime::Delay::add(const float *source,float *destination,uint32_t frames,uint64_t position,float gain,float *capture) noexcept {
  if(state_) {
    if(!prepare(source,nullptr,nullptr,frames,position))return false;
    source=state_->output.data();
  }
  if(source)for(size_t i=0;i<size_t(frames)*2;++i){destination[i]+=source[i]*gain;if(capture)capture[i]=source[i]*gain;}
  else if(capture)std::fill_n(capture,size_t(frames)*2,0.f);
  return true;
}
bool MixerRuntime::Delay::addPlanar(const float *left,const float *right,float *destination,uint32_t frames,uint64_t position) noexcept {
  if(state_) {
    if(!prepare(nullptr,left,right,frames,position))return false;
    for(size_t i=0;i<size_t(frames)*2;++i)destination[i]+=state_->output[i];
  } else for(size_t i=0;i<frames;++i) {
    destination[i*2]+=left?left[i]:0;destination[i*2+1]+=right?right[i]:0;
  }
  return true;
}
MixerRuntime::Values MixerRuntime::values(const MixerControls &control) noexcept {
  return {float(std::pow(10.0, control.preGainDB / 20)), float(std::pow(10.0, control.gainDB / 20)),
          float(control.pan), float(control.width), control.audible ? 1.f : 0.f, float(control.prePan)};
}
MixerRuntime::MixerRuntime(MixerGraph graph, MixerPlan plan, double rate, uint64_t start)
    : graph_(projectMixerDetachedChains(graph)), plan_(std::move(plan)), rate_(rate), position_(start), through_(start) {
  if (!std::isfinite(rate) || rate < 8000 || rate > 384000 || plan_.nodes.empty() ||
      plan_.nodes.size() > maximumBuses || plan_.nodes.size() != graph_.buses.size() ||
      plan_.order.size() != plan_.nodes.size() || plan_.master >= plan_.nodes.size())
    throw std::invalid_argument("Invalid compiled mixer plan");
  rampFrames_ = uint32_t(std::max(1.0, std::round(rate_ * .005)));
  for (size_t i = 0; i < plan_.nodes.size(); ++i) {
    nodes_.push_back(std::make_unique<Node>(plan_.nodes[i].directDelay));
    const auto &bus = graph_.buses[i];
    nodes_.back()->current = nodes_.back()->target = values({bus.preGainDB, bus.gainDB, bus.pan, bus.width, plan_.nodes[i].audible, bus.prePan});
  }
  for (const auto &edge : plan_.connections) edges_.emplace_back(edge.delay);
  for (const auto &instrument : plan_.instruments) instruments_.emplace_back(instrument.delay);
  auto auxiliaryTarget=[&](size_t processor,uint32_t input){
    auto target=std::find_if(auxiliaries_.begin(),auxiliaries_.end(),[&](const auto &a){return a.processor==processor;});
    if(target==auxiliaries_.end()){auxiliaries_.push_back({processor});target=auxiliaries_.end()-1;}
    if(!input){if(!target->main)target->main=std::make_unique<std::array<float,maximumFrames*2>>();return target->main->data();}
    auto port=std::find_if(target->inputs.begin(),target->inputs.end(),[&](const auto &i){return i.bus==input;});
    if(port==target->inputs.end()){target->buffers.push_back(std::make_unique<std::array<float,maximumFrames*2>>());target->inputs.push_back({input,target->buffers.back()->data()});port=target->inputs.end()-1;}
    return target->buffers[size_t(port-target->inputs.begin())]->data();
  };
  for(const auto &side:plan_.sidechains){sideTargets_.push_back(auxiliaryTarget(side.processor,side.input));sideDelays_.emplace_back(side.delay);}
  for(const auto &route:plan_.pluginConnections){pluginTargets_.push_back(auxiliaryTarget(route.target,route.input));pluginDelays_.emplace_back(route.delay);}
  if(plan_.segmented){processorStages_.resize(plan_.processors.size());for(const auto &node:plan_.nodes)for(auto processor:node.processors)processorStages_[processor]=std::make_unique<ProcessorStage>(plan_.processors[processor].mainDelay);}
  meters_ = std::make_unique<std::atomic<float>[]>(nodes_.size() * 2);
  for (size_t i = 0; i < nodes_.size() * 2; ++i) meters_[i].store(0, std::memory_order_relaxed);
}
void MixerRuntime::updateLatencyPlan(MixerPlan plan) {
  if (plan.order != plan_.order || plan.nodes.size() != nodes_.size() ||
      plan.connections.size() != edges_.size() || plan.instruments.size() != instruments_.size() ||
      plan.sidechains.size() != sideDelays_.size() || plan.pluginConnections.size()!=pluginDelays_.size() || plan.segmented!=plan_.segmented)
    throw std::invalid_argument("Latency update changed mixer topology");
  for (size_t i = 0; i < nodes_.size(); ++i)
    if (plan.nodes[i].directDelay != plan_.nodes[i].directDelay) nodes_[i]->direct = Delay(plan.nodes[i].directDelay);
  for (size_t i = 0; i < edges_.size(); ++i)
    if (plan.connections[i].delay != plan_.connections[i].delay) edges_[i] = Delay(plan.connections[i].delay);
  for (size_t i = 0; i < instruments_.size(); ++i)
    if (plan.instruments[i].delay != plan_.instruments[i].delay) instruments_[i] = Delay(plan.instruments[i].delay);
  for (size_t i = 0; i < sideDelays_.size(); ++i)
    if (plan.sidechains[i].delay != plan_.sidechains[i].delay) sideDelays_[i] = Delay(plan.sidechains[i].delay);
  for(size_t i=0;i<pluginDelays_.size();++i)if(plan.pluginConnections[i].delay!=plan_.pluginConnections[i].delay)pluginDelays_[i]=Delay(plan.pluginConnections[i].delay);
  for(size_t i=0;i<processorStages_.size();++i)if(processorStages_[i]&&plan.processors[i].mainDelay!=plan_.processors[i].mainDelay)processorStages_[i]->main=Delay(plan.processors[i].mainDelay);
  plan_ = std::move(plan);
}
void MixerRuntime::retainHistory(const MixerRuntime &source,const MixerTransitionReuse &reuse) {
  if(started_ || &source==this || rate_!=source.rate_)
    throw std::invalid_argument("Prepare mixer history before rendering at the same sample rate");
  auto validate=[](const std::vector<size_t> &mapping,size_t destination,size_t source) {
    if(mapping.size()!=destination)throw std::invalid_argument("Mixer history mapping has the wrong size");
    for(auto index:mapping)if(index!=SIZE_MAX && index>=source)throw std::invalid_argument("Mixer history mapping is out of range");
  };
  validate(reuse.direct,nodes_.size(),source.nodes_.size());validate(reuse.controls,nodes_.size(),source.nodes_.size());
  validate(reuse.connections,edges_.size(),source.edges_.size());validate(reuse.instruments,instruments_.size(),source.instruments_.size());
  validate(reuse.sidechains,sideDelays_.size(),source.sideDelays_.size());
  auto length=[](const Delay &a,const Delay &b) {
    if(a.frames()!=b.frames())throw std::invalid_argument("Retained mixer delay lengths do not match");
  };
  for(size_t i=0;i<nodes_.size();++i) {
    if(reuse.direct[i]!=SIZE_MAX)length(nodes_[i]->direct,source.nodes_[reuse.direct[i]]->direct);
    if(reuse.controls[i]!=SIZE_MAX && graph_.buses[i].id!=source.graph_.buses[reuse.controls[i]].id)
      throw std::invalid_argument("Retained mixer controls belong to another bus");
  }
  auto checkDelays=[&](const auto &destination,const auto &previous,const auto &mapping) {
    for(size_t i=0;i<destination.size();++i)if(mapping[i]!=SIZE_MAX)length(destination[i],previous[mapping[i]]);
  };
  checkDelays(edges_,source.edges_,reuse.connections);checkDelays(instruments_,source.instruments_,reuse.instruments);
  checkDelays(sideDelays_,source.sideDelays_,reuse.sidechains);
  // Validate and allocate before replacing any ownership. Only immutable delay
  // lengths/bindings are read here; their ring buffers belong to the audio thread.
  controlHistory_=reuse.controls;
  for(size_t i=0;i<nodes_.size();++i)if(reuse.direct[i]!=SIZE_MAX)nodes_[i]->direct=source.nodes_[reuse.direct[i]]->direct;
  auto retain=[](auto &destination,const auto &previous,const auto &mapping) {
    for(size_t i=0;i<destination.size();++i)if(mapping[i]!=SIZE_MAX)destination[i]=previous[mapping[i]];
  };
  retain(edges_,source.edges_,reuse.connections);retain(instruments_,source.instruments_,reuse.instruments);
  retain(sideDelays_,source.sideDelays_,reuse.sidechains);historySource_=&source;
}
bool MixerRuntime::activateHistory(bool audioStopped) noexcept {
  if(started_ || !historySource_ || (!audioStopped && (!historySource_->frames_ || historySource_->next_)))return false;
  through_=historySource_->through_;
  for(size_t i=0;i<nodes_.size();++i)if(controlHistory_[i]!=SIZE_MAX) {
    const auto &previous=*historySource_->nodes_[controlHistory_[i]];auto &next=*nodes_[i];
    next.current=previous.current;next.target=previous.target;next.rampStart=previous.rampStart;next.ramp=previous.ramp;
  }
  historySource_=nullptr;return true;
}
size_t MixerRuntime::historyStorageBytes() const noexcept {
  size_t result=0;for(const auto &node:nodes_)result+=node->direct.bytes();
  for(const auto &edge:edges_)result+=edge.bytes();for(const auto &source:instruments_)result+=source.bytes();
  for(const auto &side:sideDelays_)result+=side.bytes();for(const auto &route:pluginDelays_)result+=route.bytes();for(const auto &processor:processorStages_)if(processor)result+=processor->main.bytes();return result;
}
size_t MixerRuntime::storageBytes() const noexcept {
  size_t result=sizeof(*this)+historyStorageBytes()+nodes_.size()*sizeof(Node)+
    nodes_.capacity()*sizeof(nodes_[0])+(edges_.capacity()+instruments_.capacity()+sideDelays_.capacity())*sizeof(Delay)+
    auxiliaries_.capacity()*sizeof(Auxiliary)+sideTargets_.capacity()*sizeof(float *)+
    nodes_.size()*2*sizeof(std::atomic<float>)+controlHistory_.capacity()*sizeof(size_t);
  result+=plan_.processors.capacity()*sizeof(MixerProcessorPlan)+plan_.execution.capacity()*sizeof(MixerExecutionStep)+plan_.pluginConnections.capacity()*sizeof(MixerPluginConnectionPlan);
  result+=processorStages_.capacity()*sizeof(processorStages_[0])+pluginDelays_.capacity()*sizeof(Delay)+pluginTargets_.capacity()*sizeof(float *);for(const auto &p:processorStages_)if(p)result+=sizeof(ProcessorStage);
  for(const auto &aux:auxiliaries_)result+=aux.inputs.capacity()*sizeof(MixerAudioInput)+
    aux.buffers.capacity()*sizeof(aux.buffers[0])+(aux.buffers.size()+bool(aux.main))*sizeof(auxiliaryScratch_);
  return result;
}
bool MixerRuntime::controls(const std::vector<MixerControls> &controls) noexcept {
  if (controls.size() != nodes_.size()) return false;
  auto valid = [](double value, double low, double high) { return std::isfinite(value) && value >= low && value <= high; };
  for (const auto &c : controls)
    if (!valid(c.preGainDB, -96, 24) || !valid(c.prePan, -1, 1) || !valid(c.gainDB, -96, 24) || !valid(c.pan, -1, 1) || !valid(c.width, 0, 2)) return false;
  const auto write = write_.load(std::memory_order_relaxed);
  if (write - read_.load(std::memory_order_acquire) == controls_.size()) return false;
  std::copy(controls.begin(), controls.end(), controls_[write % controls_.size()].values.begin());
  write_.store(write + 1, std::memory_order_release);
  return true;
}
void MixerRuntime::pending() noexcept {
  const auto write = write_.load(std::memory_order_acquire);
  if (write == read_.load(std::memory_order_relaxed)) return;
  // The producer cannot recycle any consumed slot until this complete snapshot
  // has been copied, so a solo gesture is applied atomically across all buses.
  const auto &latest = controls_[(write - 1) % controls_.size()].values;
  for (size_t i = 0; i < nodes_.size(); ++i) {
    auto &node = *nodes_[i]; const auto target = values(latest[i]);
    if (target.pre != node.target.pre || target.gain != node.target.gain || target.pan != node.target.pan ||
        target.width != node.target.width || target.audible != node.target.audible || target.prePan != node.target.prePan) {
      node.rampStart = node.current; node.target = target; node.ramp = rampFrames_;
    }
  }
  read_.store(write, std::memory_order_release);
}
void MixerRuntime::begin(uint32_t frames, uint64_t position) noexcept {
  if (historySource_ || !frames || frames > maximumFrames || position < through_ || position > UINT64_MAX - frames) {
    failed_ = true; frames_ = 0; return;
  }
  frames_ = frames; position_ = position; next_ = 0;
  started_ = true;
  pending();
  for (auto &node : nodes_) { std::fill_n(node->input.data(), frames * 2, 0); node->stage=0; node->processor=0; }
  processingBus_=SIZE_MAX;for(auto &processor:processorStages_)if(processor)processor->complete=false;
  for (auto &aux : auxiliaries_) { if(aux.main)std::fill_n(aux.main->data(),frames*2,0); for (auto &buffer : aux.buffers) std::fill_n(buffer->data(), frames * 2, 0); }
}
std::span<const MixerAudioInput> MixerRuntime::inputs(size_t processor) const noexcept {
  if(processor==overrideProcessor_)return overrideInputs_;
  for (const auto &aux : auxiliaries_) if (aux.processor == processor) return aux.inputs;
  return {};
}
bool MixerRuntime::addRoute(Delay &delay,const float *source,float *destination,float gain,RouteKind kind,size_t index) noexcept {
  if(routeTransform_){
    std::fill_n(routeScratch_.data(),frames_*2,0.f);
    if(!delay.add(source,routeScratch_.data(),frames_,position_,gain))return false;
    routeTransform_(routeTransformContext_,kind,index,routeScratch_.data(),frames_,position_);
    for(uint32_t i=0;i<frames_*2;++i)destination[i]+=routeScratch_[i];
  }else if(!delay.add(source,destination,frames_,position_,gain,routeObserver_?routeScratch_.data():nullptr))return false;
  if(routeObserver_)routeObserver_(routeObserverContext_,kind,index,routeScratch_.data(),frames_,position_);return true;
}
void MixerRuntime::instrument(size_t processor, uint32_t output, const float *buffer) noexcept {
  if (!frames_) return;
  for(size_t i=0;i<plan_.pluginConnections.size();++i){const auto &route=plan_.pluginConnections[i];if(route.source!=processor||route.output!=output)continue;
    const auto owner=plan_.processors[processor].owner;const float *source=buffer;
    if(owner!=SIZE_MAX){if(processingBus_!=owner){failed_=true;return;}const auto &node=*nodes_[owner];for(uint32_t frame=0;frame<frames_;++frame)for(unsigned c=0;c<2;++c)auxiliaryScratch_[frame*2+c]=(buffer?buffer[frame*2+c]:0)*at(node,frame).audible;source=auxiliaryScratch_.data();}
    if(!addRoute(pluginDelays_[i],source,pluginTargets_[i],float(route.gain),RouteKind::PluginConnection,i))failed_=true;
  }
  // Native sources run before graph nodes, once per core mix chunk.

  for (size_t i = 0; i < plan_.instruments.size(); ++i) {
    const auto &source = plan_.instruments[i];
    if (source.processor == processor && source.output == output) {
      if(source.owner==SIZE_MAX){if(next_){failed_=true;return;}if(!addRoute(instruments_[i],buffer,nodes_[source.target]->input.data(),1,RouteKind::Instrument,i))failed_=true;}
      else {
        if(processingBus_!=source.owner){failed_=true;return;}
        const auto &owner=*nodes_[source.owner];
        for(uint32_t frame=0;frame<frames_;++frame){float audible=owner.target.audible;if(owner.ramp&&frame<owner.ramp){const float t=float(rampFrames_-owner.ramp+frame+1)/rampFrames_;audible=owner.rampStart.audible+(owner.target.audible-owner.rampStart.audible)*t;}for(int ch=0;ch<2;++ch)auxiliaryScratch_[frame*2+ch]=(buffer?buffer[frame*2+ch]:0)*audible;}
        if(!addRoute(instruments_[i],auxiliaryScratch_.data(),nodes_[source.target]->input.data(),1,RouteKind::Instrument,i))failed_=true;
      }

    }
  }
}
MixerRuntime::Values MixerRuntime::at(const Node &node,uint32_t sample) const noexcept {
  if(!node.ramp || sample>=node.ramp)return node.target;
  const float t=float(rampFrames_-node.ramp+sample+1)/rampFrames_;
  auto interpolate=[t](float a,float b){return a+(b-a)*t;};
  return {interpolate(node.rampStart.pre,node.target.pre),interpolate(node.rampStart.gain,node.target.gain),
    interpolate(node.rampStart.pan,node.target.pan),interpolate(node.rampStart.width,node.target.width),
    interpolate(node.rampStart.audible,node.target.audible),interpolate(node.rampStart.prePan,node.target.prePan)};
}
bool MixerRuntime::beginBus(size_t bus,const float *directLeft,const float *directRight) noexcept {
  if(!frames_ || bus>=nodes_.size() || nodes_[bus]->stage){failed_=true;return false;}
  auto &node=*nodes_[bus];node.stage=1;
  if(!node.direct.addPlanar(directLeft,directRight,node.input.data(),frames_,position_)){failed_=true;return false;}
  if(observer_)observer_(observerContext_,bus,false,node.input.data(),frames_,position_);
  for(uint32_t i=0;i<frames_;++i){
    const auto v=at(node,i);
    node.work[i*2]=node.input[i*2]*v.pre;
    node.work[i*2+1]=node.input[i*2+1]*v.pre;
    if(v.prePan!=0){node.work[i*2]*=1-std::max(0.f,v.prePan);node.work[i*2+1]*=1+std::min(0.f,v.prePan);}
  }
  return true;
}
float *MixerRuntime::processorInput(size_t bus,size_t processor) noexcept {
  if(!frames_ || bus>=nodes_.size()){failed_=true;return nullptr;}
  auto &node=*nodes_[bus];const auto &plan=plan_.nodes[bus];
  if(plan_.segmented){
    if(processor>=processorStages_.size()||!processorStages_[processor]||processorStages_[processor]->complete||plan_.processors[processor].owner!=bus){failed_=true;return nullptr;}
    auto &stage=*processorStages_[processor];const auto &prepared=plan_.processors[processor];processingBus_=bus;
    std::fill_n(stage.work.data(),frames_*2,0.f);
    const bool disconnected=std::find(plan_.disconnectedMainInputs.begin(),plan_.disconnectedMainInputs.end(),processor)!=plan_.disconnectedMainInputs.end();
    if(!disconnected){const float *input=node.work.data();if(prepared.previous!=SIZE_MAX){if(!processorStages_[prepared.previous]->complete){failed_=true;return nullptr;}input=processorStages_[prepared.previous]->work.data();}else if(node.stage!=1){failed_=true;return nullptr;}
      if(!stage.main.add(input,stage.work.data(),frames_,position_)){failed_=true;return nullptr;}
      if(routeTransform_)routeTransform_(routeTransformContext_,RouteKind::Insert,processor,stage.work.data(),frames_,position_);
      if(routeObserver_)routeObserver_(routeObserverContext_,RouteKind::Insert,processor,stage.work.data(),frames_,position_);
    }
    for(const auto &aux:auxiliaries_)if(aux.processor==processor&&aux.main)for(uint32_t i=0;i<frames_*2;++i)stage.work[i]+=(*aux.main)[i];return stage.work.data();
  }
  if(node.stage!=1 || node.processor>=plan.processors.size() || plan.processors[node.processor]!=processor){failed_=true;return nullptr;}
  processingBus_=bus;node.stage=2;
  const bool disconnected=std::find(plan_.disconnectedMainInputs.begin(),plan_.disconnectedMainInputs.end(),processor)!=plan_.disconnectedMainInputs.end();
  if(disconnected)std::fill_n(node.work.data(),frames_*2,0.f);
  else {if(routeTransform_)routeTransform_(routeTransformContext_,RouteKind::Insert,processor,node.work.data(),frames_,position_);
    if(routeObserver_)routeObserver_(routeObserverContext_,RouteKind::Insert,processor,node.work.data(),frames_,position_);}
  for(const auto &aux:auxiliaries_)if(aux.processor==processor && aux.main)
    for(uint32_t i=0;i<frames_*2;++i)node.work[i]+=(*aux.main)[i];
  return node.work.data();
}
bool MixerRuntime::finishProcessor(size_t bus,size_t processor) noexcept {
  if(plan_.segmented){if(bus>=nodes_.size()||processor>=processorStages_.size()||!processorStages_[processor]||processorStages_[processor]->complete||processingBus_!=bus){failed_=true;return false;}auto &stage=*processorStages_[processor];instrument(processor,0,stage.work.data());stage.complete=true;++nodes_[bus]->processor;return !failed_;}
  if(bus>=nodes_.size() || nodes_[bus]->stage!=2 || processingBus_!=bus){failed_=true;return false;}
  auto &node=*nodes_[bus];instrument(processor,0,node.work.data());++node.processor;node.stage=1;
  return !failed_;
}
const float *MixerRuntime::process(size_t bus,const float *directLeft,const float *directRight,Process callback,void *context) noexcept {
  if(!frames_ || next_>=plan_.order.size() || plan_.order[next_]!=bus || !beginBus(bus,directLeft,directRight)){failed_=true;return nullptr;}
  for(auto processor:plan_.nodes[bus].processors){
    auto *buffer=processorInput(bus,processor);
    if(!buffer || !callback || !callback(context,processor,buffer,frames_,position_)){failed_=true;return nullptr;}
    if(!finishProcessor(bus,processor))return nullptr;
  }
  return finishBus(bus);
}
const float *MixerRuntime::finishBus(size_t bus) noexcept {
  if(!frames_ || bus>=nodes_.size()){failed_=true;return nullptr;}
  auto &node=*nodes_[bus];const auto &plan=plan_.nodes[bus];
  if(node.stage!=1 || node.processor!=plan.processors.size()){failed_=true;return nullptr;}
  if(plan_.segmented&&!plan.processors.empty())std::copy_n(processorStages_[plan.processors.back()]->work.data(),frames_*2,node.work.data());
  node.stage=3;++next_;
  if(bus==plan_.master && !graph_.masterOutputDisconnected){
    if(routeTransform_)routeTransform_(routeTransformContext_,RouteKind::MasterInput,bus,node.work.data(),frames_,position_);
    if(routeObserver_)routeObserver_(routeObserverContext_,RouteKind::MasterInput,bus,node.work.data(),frames_,position_);}
  float peakL = 0, peakR = 0;
  for (uint32_t i = 0; i < frames_; ++i) {
    auto v = at(node,i);
    float left = node.work[i * 2], right = node.work[i * 2 + 1];
    if (!std::isfinite(left) || !std::isfinite(right)) { failed_ = true; left = right = 0; }
    node.input[i * 2] = left * v.audible; node.input[i * 2 + 1] = right * v.audible;
    // Preserve the exact unity path; M/S conversion is only needed for width.
    if (v.width != 1) { const float mid = (left + right) * .5f, side = (left - right) * .5f * v.width; left = mid + side; right = mid - side; }
    left *= v.gain * v.audible * (1 - std::max(0.f, v.pan));
    right *= v.gain * v.audible * (1 + std::min(0.f, v.pan));
    if (!std::isfinite(left) || !std::isfinite(right)) { failed_ = true; left = right = 0; }
    node.work[i * 2] = left; node.work[i * 2 + 1] = right;
    peakL = std::max(peakL, std::abs(left)); peakR = std::max(peakR, std::abs(right));
  }
  for (auto index : plan.outputs) {
    const auto &edge = plan_.connections[index];
    if(!addRoute(edges_[index],edge.preFader?node.input.data():node.work.data(),nodes_[edge.target]->input.data(),float(edge.gain),RouteKind::Connection,index))failed_=true;

  }
  for (auto index : plan.sidechains) {
    const auto &side = plan_.sidechains[index];
    if(!addRoute(sideDelays_[index],side.preFader?node.input.data():node.work.data(),sideTargets_[index],float(side.gain),RouteKind::Sidechain,index))failed_=true;

  }
  const float decay = float(std::exp(-double(frames_) / (rate_ * .2)));
  if(observer_)observer_(observerContext_,bus,true,node.work.data(),frames_,position_);
  meters_[bus * 2].store(std::max(peakL, meters_[bus * 2].load(std::memory_order_relaxed) * decay), std::memory_order_relaxed);
  meters_[bus * 2 + 1].store(std::max(peakR, meters_[bus * 2 + 1].load(std::memory_order_relaxed) * decay), std::memory_order_relaxed);
  node.current = at(node,frames_ - 1); node.ramp = frames_ >= node.ramp ? 0 : node.ramp - frames_;
  return bus==plan_.master?masterOutput():node.work.data();
}
void MixerRuntime::dryBusGain(size_t bus,bool pre,uint32_t frame,float &left,float &right) const noexcept {
  if(bus>=nodes_.size())return;const auto value=at(*nodes_[bus],frame);
  if(pre){left*=value.audible;right*=value.audible;return;}
  if(value.width!=1){const float mid=(left+right)*.5f,side=(left-right)*.5f*value.width;left=mid+side;right=mid-side;}
  left*=value.gain*value.audible*(1-std::max(0.f,value.pan));right*=value.gain*value.audible*(1+std::min(0.f,value.pan));
}
void MixerRuntime::dryRouteGain(RouteKind kind,size_t index,uint32_t frame,float &left,float &right) const noexcept {
  if(kind==RouteKind::Connection&&index<plan_.connections.size()){const auto &edge=plan_.connections[index];dryBusGain(edge.source,edge.preFader,frame,left,right);}
  else if(kind==RouteKind::Sidechain&&index<plan_.sidechains.size()){const auto &edge=plan_.sidechains[index];dryBusGain(edge.source,edge.preFader,frame,left,right);}
  else if(kind==RouteKind::PluginConnection&&index<plan_.pluginConnections.size()){const auto owner=plan_.processors[plan_.pluginConnections[index].source].owner;if(owner!=SIZE_MAX)dryBusGain(owner,true,frame,left,right);}
  else if(kind==RouteKind::Instrument&&index<plan_.instruments.size()){const auto owner=plan_.instruments[index].owner;if(owner!=SIZE_MAX)dryBusGain(owner,true,frame,left,right);}
}
void MixerRuntime::complete() noexcept {
  if (!frames_ || next_ != nodes_.size()) failed_ = true;
  else through_ = position_ + frames_;
  frames_ = 0;
}
std::vector<MixerMeter> MixerRuntime::meters() const {
  std::vector<MixerMeter> result(nodes_.size());
  for (size_t i = 0; i < result.size(); ++i) result[i] = {meters_[i * 2].load(std::memory_order_relaxed), meters_[i * 2 + 1].load(std::memory_order_relaxed)};
  return result;
}
} // namespace Tracker
