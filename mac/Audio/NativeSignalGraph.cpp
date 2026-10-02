#include "NativeSignalGraph.hpp"
#include "editor/hosted/GraphPluginEndpoint.hpp"
#include "editor/hosted/GraphSignalObservation.hpp"
#include <cmath>
#include <set>
#include <stdexcept>

namespace Tracker {
struct NativeSignalGraph::Instance {
  uint64_t graph=0;
  uint64_t contributionFrame=UINT64_MAX;
  uint8_t role=0; // row, persistent, ordinary: separate processor histories
  bool active=false;
  std::atomic<uint32_t> published{0};
  double amount=1,wet=1,rate=48000;
  uint64_t tailRemaining=0,tailFrames=0;
  std::shared_ptr<SignalRuntime> initialRuntime;
  SignalRuntime *runtime=nullptr; // Audio owner; snapshots retain every adopted runtime.
  using Processor=GraphProcessorSet::Entry;
  std::shared_ptr<GraphProcessorSet> initialProcessors=std::make_shared<GraphProcessorSet>();
  GraphProcessorSet *processors=initialProcessors.get(); // Audio owner only.
  std::atomic<GraphProcessorSet *> publishedProcessors{processors};
  std::atomic<SignalRuntime *> publishedRuntime{nullptr};
  std::atomic<bool> layoutSettled{true};
  ParameterProcessor identity;
  uint32_t observationDomain=0;
  struct Remembered {
    std::shared_ptr<SignalRuntime> runtime;
    std::shared_ptr<GraphProcessorSet> processors;
    std::shared_ptr<GraphControlPlan> prior=std::make_shared<GraphControlPlan>();
  };
  std::shared_ptr<void> remembered; // Producer-only; immutable after publication.
  SignalCopyIdentity copyIdentity() const noexcept {return {identity.graph,identity.target,identity.instrument?uint8_t(3):identity.role,identity.instrument,identity.channel};}
  static std::vector<SignalObservedBus> observedBuses(const GraphProcessorSet &processors) {std::vector<SignalObservedBus> result;for(const auto &p:processors.entries)for(const auto &bus:p.endpoint->initial()->plugin->buses())if(bus.supported)result.push_back({p.id,!bus.input,bus.index,bus.channels});return result;}
  enum class Transition {Stable,Out,Warm,In};
  Transition transition=Transition::Stable;
  uint32_t fadeFrames=1,phaseFrames=0;
  SignalRuntime *pendingRuntime=nullptr;
  GraphProcessorSet *pendingProcessors=nullptr;
  bool layoutReady(const SignalRuntime *expected) const noexcept {
    return publishedRuntime.load(std::memory_order_acquire)==expected&&layoutSettled.load(std::memory_order_acquire);
  }
  void adopt(SignalRuntime *next,GraphProcessorSet *nextProcessors,bool structural) noexcept {
    if(structural){pendingRuntime=next;pendingProcessors=nextProcessors;phaseFrames=0;transition=Transition::Out;layoutSettled.store(false,std::memory_order_release);return;}
    if(runtime!=next){next->inheritState(*runtime);runtime=next;}
    processors=nextProcessors;publishedProcessors.store(processors,std::memory_order_release);publishedRuntime.store(runtime,std::memory_order_release);
  }
  void switchLayout() noexcept {
    pendingRuntime->inheritState(*runtime);runtime=pendingRuntime;processors=pendingProcessors;
    pendingRuntime=nullptr;pendingProcessors=nullptr;phaseFrames=0;
    publishedProcessors.store(processors,std::memory_order_release);publishedRuntime.store(runtime,std::memory_order_release);
    transition=runtime->latency()?Transition::Warm:Transition::In;
  }
  float transitionWet(uint32_t offset) const noexcept {
    if(transition==Transition::Stable)return 1;
    if(transition==Transition::Warm)return 0;
    const auto t=std::min(1.,double(phaseFrames+offset)/fadeFrames),smooth=t*t*(3-2*t);
    return float(transition==Transition::Out?1-smooth:smooth);
  }
  ParameterProcessor observation(const SignalNode &node,const GraphPluginState &state) const {
    auto result=identity;result.node=node.id;result.name+=node.name;
    result.key="graph/"+std::to_string(result.graph)+"/"+std::to_string(result.node)+"/"+std::to_string(result.target)+"/"+std::to_string(result.role)+"/"+std::to_string(result.instrument)+"/"+std::to_string(result.channel);
    result.parameters=state.parameters;return result;
  }
  struct Auxiliary {
    uint32_t port=0;
    std::vector<float> delay;
    size_t cursor=0;
    std::array<float,8192> samples{};
  };
  std::vector<Auxiliary> auxiliary;
  std::vector<float> dryDelay;
  size_t dryPosition=0;
  std::array<float,8192> dry{};
  size_t bypassStorage() const {size_t result=0;for(const auto &p:initialProcessors->entries)result+=p.endpoint->initial()->plugin->bypassStorageBytes();return result;}
  size_t initialProcessorStorage() const {size_t result=0;for(const auto &p:initialProcessors->entries)result+=p.endpoint->initial()->storageBytes();return result;}
  explicit Instance(const SignalDefinition &d,uint8_t role,double sampleRate,bool offline):graph(d.id),role(role),rate(sampleRate) {
    fadeFrames=std::max(1u,uint32_t(std::ceil(sampleRate*.005)));
    std::vector<SignalProcessorInfo> info;std::vector<SignalParameterInfo> parameterInfo;
    double tail=0;
    for(const auto &n:d.nodes)if(n.kind==SignalNodeKind::Plugin){
      auto state=std::make_shared<GraphPluginState>(d,n,sampleRate,offline);
      info.push_back({n.id,state->latency,state->inputs,state->outputs});
      for(const auto &m:d.modulation)if(m.target==n.id&&m.enabled){const auto p=std::find_if(state->parameters.begin(),state->parameters.end(),[&](const auto &p){return p.id==m.parameter;});if(p==state->parameters.end())throw std::invalid_argument("Graph modulation parameter is unavailable");parameterInfo.push_back({n.id,m.parameter,graphModulationStep(*p,m.quantized)});}
      tail=std::min(mixerMaximumTailSeconds,tail+std::max(0.,state->plugin->tail()));
      processors->entries.push_back({n.id,std::make_shared<GraphPluginEndpoint>(std::move(state),sampleRate)});
    }
    auto plan=compileSignal(d,info);dryDelay.resize(size_t(plan.totalLatency)*2);tailFrames=uint64_t(std::ceil(tail*sampleRate))+plan.totalLatency;initialRuntime=std::make_shared<SignalRuntime>(d,std::move(plan),sampleRate,parameterInfo);runtime=initialRuntime.get();publishedRuntime.store(runtime,std::memory_order_relaxed);
  }
  uint64_t currentTailFrames() const noexcept {double tail=0;for(const auto &p:processors->entries)tail+=p.endpoint->tail();return std::max(tailFrames,uint64_t(std::ceil(std::min(mixerMaximumTailSeconds,tail)*rate))+runtime->latency());}
  Processor *processor(uint64_t id)noexcept{return processors->find(id);}
  bool renderPart(float *buffer,uint32_t frames,uint64_t position,PluginTransport transport,SignalClock clock,std::span<const MixerAudioInput> inputs,uint32_t outputOffset)noexcept {
    runtime->amount(amount);for(auto &p:processors->entries)p.endpoint->transport(transport,(active||tailRemaining>0)&&wet>0&&transition!=Transition::Warm);
    for(uint32_t i=0;i<frames*2;++i){if(dryDelay.empty())dry[i]=buffer[i];else {dry[i]=dryDelay[dryPosition];dryDelay[dryPosition]=buffer[i];if(++dryPosition==dryDelay.size())dryPosition=0;}}
    // Bypass compensation follows the real channel input even while the
    // processor advances on silence. Switching off therefore reveals dry audio
    // at the same fixed time, without replaying an old wet padding buffer.
    if(!active)std::fill_n(buffer,frames*2,0.f);
    SignalCallbacks callbacks;callbacks.context=this;
    callbacks.process=[](void *ctx,uint64_t id,float *buffer,uint32_t frames,uint64_t position,std::span<const MixerAudioInput> inputs)noexcept{auto p=static_cast<Instance *>(ctx)->processor(id);return p&&p->endpoint->process(buffer,frames,position,inputs);};
    callbacks.output=[](void *ctx,uint64_t id,uint32_t bus)noexcept->const float *{auto p=static_cast<Instance *>(ctx)->processor(id);return p?p->endpoint->output(bus):nullptr;};
    callbacks.parameter=[](void *ctx,uint64_t id,uint32_t parameter,double a,double b,uint64_t position,uint32_t duration)noexcept{
      auto p=static_cast<Instance *>(ctx)->processor(id);if(!p)return false;
      const auto &parameters=p->endpoint->current().parameters;auto range=std::find_if(parameters.begin(),parameters.end(),[&](const auto &v){return v.id==parameter;});if(range==parameters.end())return false;
      return p->endpoint->ramp(parameter,range->min+(range->max-range->min)*a,range->min+(range->max-range->min)*b,position,duration,{ParameterOrigin::Graph,id});
    };
    callbacks.parameterSamples=[](void *ctx,uint64_t id,uint32_t parameter,std::span<const double> values,uint64_t)noexcept {
      auto p=static_cast<Instance *>(ctx)->processor(id);if(!p)return false;const auto &parameters=p->endpoint->current().parameters;
      const auto range=std::find_if(parameters.begin(),parameters.end(),[&](const auto &v){return v.id==parameter;});return range!=parameters.end()&&p->endpoint->parameterSamples(parameter,range->min,range->max,values,id);
    };
    callbacks.contribution=[](void *ctx,uint64_t id,uint32_t parameter,uint64_t source,double value,uint64_t frame)noexcept {
      auto instance=static_cast<Instance *>(ctx);
      // Keep exact source contributions at the first graph quantum of each
      // millisecond. Final output also retains min/max between samples.
      if(instance->contributionFrame==UINT64_MAX||frame-instance->contributionFrame>=uint64_t(instance->rate/1000))instance->contributionFrame=frame;
      if(frame!=instance->contributionFrame)return;
      if(auto p=instance->processor(id))p->endpoint->contribution(parameter,source,value,frame);
    };
    if(!runtime->render(buffer,frames,position,clock,callbacks,inputs))return false;
    for(auto &port:auxiliary){const auto *samples=runtime->output(port.port);for(uint32_t f=0;f<frames;++f)for(unsigned c=0;c<2;++c)port.samples[(outputOffset+f)*2+c]=samples?samples[f*2+c]*transitionWet(f):0;}
    for(uint32_t f=0;f<frames;++f){const double mix=wet*transitionWet(f);for(unsigned c=0;c<2;++c){const auto i=f*2+c;buffer[i]=active?float(buffer[i]*mix+dry[i]*(1-mix)):dry[i]+(f+outputOffset<tailRemaining?float(buffer[i]*mix):0.f);}}
    return true;
  }
  bool render(float *buffer,uint32_t frames,uint64_t position,PluginTransport transport,SignalClock clock,std::span<const MixerAudioInput> inputs) noexcept {
    for(uint32_t offset=0;offset<frames;){
      auto count=frames-offset;
      if(transition!=Transition::Stable){const auto length=transition==Transition::Warm?runtime->latency():fadeFrames;count=std::min(count,length-phaseFrames);}
      auto t=transport;if(t.playing)t.beat+=offset*t.tempo/(60*rate);
      auto c=clock;c.beat=t.beat;c.position+=offset*c.unitsPerFrame;
      std::array<MixerAudioInput,64> shifted{};size_t used=0;for(const auto &input:inputs){if(used==shifted.size())return false;shifted[used++]={input.bus,input.samples?input.samples+offset*2:nullptr};}
      if(!renderPart(buffer+offset*2,count,position+offset,t,c,{shifted.data(),used},offset))return false;
      offset+=count;
      if(transition!=Transition::Stable){phaseFrames+=count;const auto length=transition==Transition::Warm?runtime->latency():fadeFrames;
        if(phaseFrames==length){if(transition==Transition::Out)switchLayout();else if(transition==Transition::Warm){transition=Transition::In;phaseFrames=0;}else {transition=Transition::Stable;phaseFrames=0;layoutSettled.store(true,std::memory_order_release);}}
      }
    }
    return true;
  }
};
struct NativeSignalGraph::Bus {
  uint64_t id=0;
  double rate=48000;
  std::vector<std::shared_ptr<Instance>> instances;
  std::vector<size_t> row,persistent;
  std::array<std::vector<size_t>,2> renderOrder;
  struct NoteWatch {uint16_t index=0;uint64_t generation=0;};
  std::vector<NoteWatch> channels;
  std::array<bool,192> members{};
  uint16_t rawChannels=0;
  bool sampleSource=false;
  const OpenMPT::ModInstrument *instrument=nullptr;
  uint16_t sampleChannel=0;
  std::map<uint16_t,std::vector<SignalCommand>> patterns;
  const std::vector<SignalCommand> *events=nullptr;
  size_t next=0;
  double begin=0,units=0;
  bool expire=false,commands=true,seek=false;
  PluginTransport transport;
  SignalClock clock;

  uint32_t reserved=0;
  double tailSeconds=0;
  uint64_t inputMask=0;
  std::vector<uint32_t> outputPorts;
  std::vector<std::array<float,8192>> outputBuffers;
  void auxiliary(Instance &instance,uint32_t prefix,uint32_t offset,uint32_t count,uint32_t audible)noexcept {
    for(auto &port:instance.auxiliary){
      const auto *samples=port.samples.data();
      const auto found=std::find(outputPorts.begin(),outputPorts.end(),port.port);
      if(!samples||found==outputPorts.end())continue;
      auto *target=outputBuffers[size_t(found-outputPorts.begin())].data()+offset*2;
      const size_t delay=size_t(reserved-prefix)*2;
      for(uint32_t f=0;f<count*2;++f){port.delay[port.cursor]=samples[f];
        const auto read=(port.cursor+port.delay.size()-delay)%port.delay.size();
        if(f<audible*2)target[f]+=float(port.delay[read]*instance.wet);
        if(++port.cursor==port.delay.size())port.cursor=0;
      }
    }
  }
  void stop(size_t i,bool tails)noexcept{auto &v=*instances[i];v.active=false;v.tailRemaining=tails?v.currentTailFrames():0;}
  void command(const SignalCommand &c)noexcept {
    if(c.kind==SignalCommandKind::Clear){for(auto i:persistent)stop(i,c.tails);persistent.clear();return;}
    if(c.kind==SignalCommandKind::Stop){for(auto *list:{&row,&persistent}){for(auto i:*list)if(instances[i]->graph==c.graph)stop(i,c.tails);std::erase_if(*list,[&](auto i){return instances[i]->graph==c.graph;});}return;}
    const uint8_t role=c.kind==SignalCommandKind::Row?0:1;
    if(c.kind==SignalCommandKind::Amount||c.kind==SignalCommandKind::Wet){for(auto &p:instances)if(p->graph==c.graph&&p->active&&p->role!=2){if(c.kind==SignalCommandKind::Amount)p->amount=c.amount;else p->wet=c.wet;}return;}
    for(size_t i=0;i<instances.size();++i){auto &p=*instances[i];if(p.graph!=c.graph||p.role!=role)continue;
      if(!p.active){
        auto &order=renderOrder[role];auto current=std::find(order.begin(),order.end(),i);
        auto last=std::find_if(order.rbegin(),order.rend(),[&](size_t v){return instances[v]->active;});
        // Stop leaves the bypass delay in its existing place. Move a copy only
        // when a new start really requests a different audible processor order.
        if(last!=order.rend()&&current<last.base()-1)std::rotate(current,current+1,last.base());
        (role==0?row:persistent).push_back(i);
      }p.active=true;p.tailRemaining=0;p.amount=c.amount;p.wet=c.wet;return;}
  }
  bool render(float *buffer,uint32_t frames,uint64_t position,std::span<const MixerAudioInput> inputs)noexcept {
    for(auto &port:outputBuffers)std::fill_n(port.data(),frames*2,0.f);
    if(expire){for(auto i:row)stop(i,false);row.clear();expire=false;}
    for(uint32_t offset=0;offset<frames;){
      const double now=begin+offset*units;
      if(commands&&events)while(next<events->size()&&(*events)[next].position<=now+1e-8)command((*events)[next++]);
      uint32_t count=frames-offset;
      if(commands&&events&&next<events->size()&&units>0){const double sample=std::ceil(((*events)[next].position-begin)/units-1e-9);if(sample>offset&&sample<frames)count=uint32_t(sample)-offset;}
      auto t=transport;if(t.playing)t.beat+=offset*t.tempo/(60*rate);
      auto c=clock;c.beat=t.beat;c.position+=offset*c.unitsPerFrame;
      std::array<MixerAudioInput,64> shifted{};size_t used=0;for(auto input:inputs){if(used==shifted.size())return false;shifted[used++]={input.bus,input.samples?input.samples+offset*2:nullptr};}
      const std::span<const MixerAudioInput> external(shifted.data(),used);
      uint32_t prefix=0,audibleOrder=0;
      auto apply=[&](size_t i){auto &p=*instances[i];prefix+=p.runtime->latency();
        const auto audible=p.active?count:uint32_t(std::min<uint64_t>(count,p.tailRemaining));
        if(!p.render(buffer+offset*2,count,position+offset,t,c,p.active?external:std::span<const MixerAudioInput>{}))return false;
        auxiliary(p,prefix,offset,count,audible);
        if(!p.active)p.tailRemaining-=audible;
        p.published.store((p.active ? ++audibleOrder : 0) | (p.tailRemaining ? 0x10000u : 0),std::memory_order_relaxed);return true;};
      // Every copy retains its latency and bypass position when switched off.
      // This preserves downstream processors' input histories in a stack.
      for(const auto &order:renderOrder)for(auto i:order)if(!apply(i))return false;
      for(size_t i=0;i<instances.size();++i)if(instances[i]->role==2&&!apply(i))return false;
      if(prefix!=reserved)return false;
      offset+=count;
    }
    if(transport.playing)transport.beat+=frames*transport.tempo/(60*rate);
    return true;
  }
};
struct NativeSignalGraph::CopySet {
  std::vector<std::shared_ptr<Bus>> buses;
  std::shared_ptr<CopySet> predecessor; // Retain the handoff source off the callback.
  std::vector<std::shared_ptr<Instance>> retained;
  MixerGraph routing;
  struct Assignment {Instance *instance=nullptr;double amount=1,wet=1;};
  std::vector<Assignment> assignments;
  struct Buffers {Instance *instance=nullptr;std::vector<float> dry;std::vector<Instance::Auxiliary> auxiliary;};
  std::vector<Buffers> buffers;
  size_t storage=0,tableStorage=0,processors=0;
  uint32_t domainBase=0,domains=0;
};
const std::vector<std::shared_ptr<NativeSignalGraph::Bus>> &NativeSignalGraph::controlBuses() const noexcept {return controlCopies_?controlCopies_->buses:buses_;}
const std::vector<std::shared_ptr<NativeSignalGraph::Bus>> &NativeSignalGraph::audioBuses() const noexcept {return audioCopies_?audioCopies_->buses:buses_;}
const std::vector<std::shared_ptr<NativeSignalGraph::Bus>> &NativeSignalGraph::publishedBuses() const noexcept {const auto *copies=publishedCopies_.load(std::memory_order_acquire);return copies?copies->buses:buses_;}
NativeSignalGraph::NativeSignalGraph(const NativeSong &native,double rate,bool offline,std::span<const SignalSampleSource> sampleSources,size_t storageLimit,size_t processorLimit,ParameterActivity *activity,SignalObservation *observation):rate_(rate),offline_(offline),activity_(activity),observation_(observation),routedMixer_(signalRoutingGraph(native.mixer,native.signal)){
  std::vector<SignalPortIdentity> observedPorts;
  for(const auto &[index,entity]:native.patterns)patternIDs_.emplace(index,entity.id);
  size_t processors=0,delayBytes=0;
  auto budget=[&](size_t bytes){if(bytes>storageLimit-delayBytes)throw std::invalid_argument("Song graph audio storage exceeds 256 MB");delayBytes+=bytes;};
  for(const auto &bus:native.mixer.buses){std::set<std::pair<uint64_t,uint8_t>> required;
    for(const auto &c:native.signal.commands)if(c.target==bus.id&&(c.kind==SignalCommandKind::Row||c.kind==SignalCommandKind::Start))required.emplace(c.graph,c.kind==SignalCommandKind::Row?0:1);
    for(const auto &a:native.signal.assignments)if(a.target==bus.id)required.emplace(a.graph,2);
    const bool sampleSource=std::any_of(sampleSources.begin(),sampleSources.end(),[&](const auto &source){return source.target==bus.id;});
    if(required.empty()&&!sampleSource)continue;
    auto prepared=std::make_shared<Bus>();prepared->sampleSource=sampleSource;prepared->id=bus.id;prepared->rate=rate;
    for(const auto &[channel,track]:native.tracks){auto id=track.id;for(size_t depth=0;id&&depth<native.mixer.buses.size();++depth){if(id==bus.id){prepared->channels.push_back({channel,0});break;}auto source=std::find_if(native.mixer.buses.begin(),native.mixer.buses.end(),[&](const auto &b){return b.id==id;});id=source==native.mixer.buses.end()?0:source->output;}}

    // Prepare membership independently of today's source list. A live-added
    // note envelope can inspect existing held voices without allocating watch
    // storage in the callback. The render path skips this scan when unused.
    for(const auto &source:sampleSources)if(source.target==bus.id){prepared->instrument=source.instrument;prepared->sampleChannel=source.channel;prepared->channels.clear();if(source.channel!=UINT16_MAX)prepared->channels.push_back({source.channel,0});for(uint16_t i=source.channels;i<OpenMPT::MAX_CHANNELS;++i)prepared->channels.push_back({i,0});}
    if(!prepared->sampleSource){
      prepared->rawChannels=uint16_t(native.tracks.size());
      for(const auto &watch:prepared->channels)if(watch.index<prepared->members.size())prepared->members[watch.index]=true;
      for(uint16_t i=prepared->rawChannels;i<OpenMPT::MAX_CHANNELS;++i)prepared->channels.push_back({i,0});
    }
    budget(sizeof(Bus)+prepared->channels.size()*sizeof(Bus::NoteWatch));
    for(auto [id,role]:required){auto definition=std::find_if(native.signal.library.begin(),native.signal.library.end(),[&](const auto &d){return d.id==id;});if(definition==native.signal.library.end())throw std::invalid_argument("Unresolved subgraph assignment");
      processors+=std::count_if(definition->nodes.begin(),definition->nodes.end(),[](const auto &n){return n.kind==SignalNodeKind::Plugin;});if(processors>processorLimit)throw std::invalid_argument("Active song graph exceeds 256 prepared plugin copies");
      for(const auto &edge:definition->audio){
        auto node=std::find_if(definition->nodes.begin(),definition->nodes.end(),[&](const auto &n){return n.id==edge.source;});
        if(node!=definition->nodes.end()&&node->kind==SignalNodeKind::Input&&edge.output)prepared->inputMask|=uint64_t(1)<<edge.output;
      }
      auto instance=std::make_shared<Instance>(*definition,role,rate,offline);budget(sizeof(Instance)+sizeof(GraphProcessorSet)+instance->processors->entries.capacity()*sizeof(GraphProcessorSet::Entry)+instance->dryDelay.size()*sizeof(float)+instance->runtime->storageBytes()+instance->bypassStorage());prepared->reserved+=instance->runtime->latency();prepared->tailSeconds=std::min(mixerMaximumTailSeconds,prepared->tailSeconds+instance->tailFrames/rate);
      for(const auto &p:instance->processors->entries)budget(p.endpoint->storageBytes()+p.endpoint->initial()->storageBytes());
      if(role==2){instance->active=true;for(const auto &a:native.signal.assignments)if(a.target==bus.id){instance->amount=a.amount;instance->wet=a.wet;}}
      instance->identity.graph=id;instance->identity.target=bus.id;instance->identity.role=role;
      instance->identity.name=bus.name+" · "+definition->name+" · ";
      for(const auto &source:sampleSources)if(source.target==bus.id){instance->identity.instrument=source.instrumentID;instance->identity.channel=source.channel;instance->identity.target=0;instance->identity.name="Instrument "+std::to_string(source.instrumentID)+(source.channel==UINT16_MAX?" · Inspector":" · Channel "+std::to_string(source.channel+1))+" · "+definition->name+" · ";}
      if(observation){const auto before=instance->runtime->storageBytes();instance->observationDomain=observation->newDomain();instance->runtime->observer(std::make_shared<GraphSignalObservation>(*observation,instance->observationDomain,instance->copyIdentity(),*definition,instance->runtime->plan(),observedPorts,Instance::observedBuses(*instance->processors)));budget(instance->runtime->storageBytes()-before);}
      if(activity)for(auto &p:instance->processors->entries){
        auto n=std::find_if(definition->nodes.begin(),definition->nodes.end(),[&](const auto &n){return n.id==p.id;});
        auto observed=instance->observation(*n,*p.endpoint->initial());
        for(auto &parameter:observed.parameters){
          if(const auto value=n->plugin.parameters.find(parameter.id);value!=n->plugin.parameters.end())parameter.value=float(value->second);
          for(const auto &edge:definition->modulation)if(edge.enabled&&edge.target==p.id&&edge.parameter==parameter.id){parameter.value=float(parameter.min+(parameter.max-parameter.min)*edge.base);break;}
        }
        p.endpoint->observe(activity,activity->add(std::move(observed)));
      }
      prepared->instances.push_back(std::move(instance));
      if(role<2)prepared->renderOrder[role].push_back(prepared->instances.size()-1);
    }
    if(prepared->reserved>1048576)throw std::invalid_argument("Channel graph compensation exceeds supported delay");
    for(const auto &route:native.signal.outputs)if(route.source==bus.id)prepared->outputPorts.push_back(route.output);
    for(const auto &route:native.signal.stageConnections)if(route.source.stage==bus.id)prepared->outputPorts.push_back(route.output);
    for(const auto &source:native.signal.songSources)if(source.node.kind==SignalNodeKind::Follower&&source.audioStage==bus.id)prepared->outputPorts.push_back(source.output);
    std::sort(prepared->outputPorts.begin(),prepared->outputPorts.end());prepared->outputPorts.erase(std::unique(prepared->outputPorts.begin(),prepared->outputPorts.end()),prepared->outputPorts.end());
    budget(prepared->outputPorts.size()*sizeof(std::array<float,8192>));prepared->outputBuffers.resize(prepared->outputPorts.size());
    const size_t delaySamples=size_t(prepared->reserved)*2+2;
    for(auto &instance:prepared->instances)for(auto port:prepared->outputPorts)if(instance->runtime->output(port)){
      budget(sizeof(Instance::Auxiliary)+delaySamples*sizeof(float));instance->auxiliary.push_back({port,std::vector<float>(delaySamples),0});
    }
    prepared->row.reserve(required.size());prepared->persistent.reserve(required.size());
    for(const auto &c:native.signal.commands)if(c.target==bus.id){auto p=std::find_if(native.patterns.begin(),native.patterns.end(),[&](const auto &p){return p.second.id==c.pattern;});prepared->patterns[p->first].push_back(c);}
    for(auto &[pattern,events]:prepared->patterns)std::sort(events.begin(),events.end(),[](const auto &a,const auto &b){return std::tie(a.position,a.column)<std::tie(b.position,b.column);});
    buses_.push_back(std::move(prepared));
  }
  if(observation){auto batch=observation->preparePorts(std::move(observedPorts));budget(batch.storageBytes());observation->publishPorts(batch);}
  storageBytes_=delayBytes;processors_=processors;
  controlCopies_=std::make_shared<CopySet>();controlCopies_->buses=buses_;controlCopies_->routing=routedMixer_;
  controlCopies_->storage=storageBytes_;controlCopies_->processors=processors_;
  for(const auto &bus:buses_)for(const auto &instance:bus->instances)controlCopies_->retained.push_back(instance);
  initialCopies_=controlCopies_;audioCopies_=controlCopies_.get();publishedCopies_.store(audioCopies_,std::memory_order_relaxed);
}
NativeSignalGraph::~NativeSignalGraph()=default;
std::shared_ptr<NativeSignalGraph::CopySet> NativeSignalGraph::prepareCopies(const NativeSong &native,GraphControlPlan &controls,const GraphControlPlan *previous,std::span<const SignalSampleSource> sampleSources) const {
  // Once the current set is adopted, only its own membership and retained
  // instance cache are needed. Retire the prior handoff's tables on producer.
  if(publishedCopies_.load(std::memory_order_acquire)==controlCopies_.get())controlCopies_->predecessor.reset();
  auto next=std::make_shared<CopySet>();next->routing=signalRoutingGraph(native.mixer,native.signal);next->retained=controlCopies_->retained;next->predecessor=controlCopies_;next->storage=controlCopies_->storage;
  // Stable outer processor slots survive removal and Undo. Empty slots do not
  // enter the compiled mixer but old fading plans can keep their original index.
  for(const auto &old:controlBuses()){auto empty=std::make_shared<Bus>();empty->id=old->id;empty->rate=rate_;next->buses.push_back(std::move(empty));}
  const auto reserve=[&](size_t bytes,bool retained=false){if(bytes>controls.preparationHeadroom)throw std::invalid_argument("Live graph copies exceed the prepared audio storage budget");controls.preparationHeadroom-=bytes;(retained?next->storage:next->tableStorage)+=bytes;};
  const auto sameCopy=[](const SignalCopyIdentity &a,const SignalCopyIdentity &b){return std::tie(a.graph,a.target,a.role,a.instrument,a.channel)==std::tie(b.graph,b.target,b.role,b.instrument,b.channel);};
  for(const auto &bus:native.mixer.buses){
    std::set<std::pair<uint64_t,uint8_t>> required;
    for(const auto &c:native.signal.commands)if(c.target==bus.id&&(c.kind==SignalCommandKind::Row||c.kind==SignalCommandKind::Start))required.emplace(c.graph,c.kind==SignalCommandKind::Row?0:1);
    for(const auto &a:native.signal.assignments)if(a.target==bus.id)required.emplace(a.graph,2);
    const auto sample=std::find_if(sampleSources.begin(),sampleSources.end(),[&](const auto &source){return source.target==bus.id;});
    if(required.empty()&&sample==sampleSources.end())continue;
    auto prepared=std::make_shared<Bus>();prepared->sampleSource=sample!=sampleSources.end();prepared->id=bus.id;prepared->rate=rate_;prepared->seek=true;
    for(const auto &[channel,track]:native.tracks){auto id=track.id;for(size_t depth=0;id&&depth<native.mixer.buses.size();++depth){if(id==bus.id){prepared->channels.push_back({channel,0});break;}const auto source=std::find_if(native.mixer.buses.begin(),native.mixer.buses.end(),[&](const auto &b){return b.id==id;});id=source==native.mixer.buses.end()?0:source->output;}}
    if(sample!=sampleSources.end()){prepared->instrument=sample->instrument;prepared->sampleChannel=sample->channel;prepared->channels.clear();if(sample->channel!=UINT16_MAX)prepared->channels.push_back({sample->channel,0});for(uint16_t i=sample->channels;i<OpenMPT::MAX_CHANNELS;++i)prepared->channels.push_back({i,0});}
    if(!prepared->sampleSource){prepared->rawChannels=uint16_t(native.tracks.size());for(const auto &watch:prepared->channels)if(watch.index<prepared->members.size())prepared->members[watch.index]=true;for(uint16_t i=prepared->rawChannels;i<OpenMPT::MAX_CHANNELS;++i)prepared->channels.push_back({i,0});}
    reserve(sizeof(Bus)+prepared->channels.capacity()*sizeof(Bus::NoteWatch));
    for(auto [id,role]:required){
      const auto definition=std::find_if(native.signal.library.begin(),native.signal.library.end(),[&](const auto &d){return d.id==id;});if(definition==native.signal.library.end())throw std::invalid_argument("Unresolved subgraph assignment");
      SignalCopyIdentity identity{id,bus.id,role,0,UINT16_MAX};if(sample!=sampleSources.end())identity={id,0,3,sample->instrumentID,sample->channel};
      const auto retained=std::find_if(next->retained.begin(),next->retained.end(),[&](const auto &instance){return sameCopy(instance->copyIdentity(),identity);});
      std::shared_ptr<Instance> instance;
      if(retained!=next->retained.end()){
        instance=*retained;const auto saved=std::static_pointer_cast<Instance::Remembered>(instance->remembered);const auto expected=saved?saved->runtime:instance->initialRuntime;
        if(!instance->layoutReady(expected.get()))throw std::runtime_error("A graph copy is still changing layout; retry the assignment shortly");
      }else {
        instance=std::make_shared<Instance>(*definition,role,rate_,offline_);instance->identity.graph=id;instance->identity.target=identity.target;instance->identity.role=role;instance->identity.instrument=identity.instrument;instance->identity.channel=identity.channel;
        instance->identity.name=bus.name+" · "+definition->name+" · ";
        reserve(sizeof(Instance)+sizeof(GraphProcessorSet)+instance->processors->entries.capacity()*sizeof(GraphProcessorSet::Entry)+instance->dryDelay.capacity()*sizeof(float)+instance->runtime->storageBytes()+instance->bypassStorage(),true);
        for(const auto &p:instance->processors->entries)reserve(p.endpoint->storageBytes()+p.endpoint->initial()->storageBytes(),true);
        if(observation_){if(!controls.signalDomains)controls.signalDomainBase=observation_->domainCount();instance->observationDomain=observation_->prepareDomain(controls.signalDomains++);
          const auto before=instance->runtime->storageBytes();instance->runtime->observer(std::make_shared<GraphSignalObservation>(*observation_,instance->observationDomain,identity,*definition,instance->runtime->plan(),controls.signalPortIdentities,Instance::observedBuses(*instance->processors)));reserve(instance->runtime->storageBytes()-before,true);}
        if(activity_)for(auto &p:instance->processors->entries){const auto node=std::find_if(definition->nodes.begin(),definition->nodes.end(),[&](const auto &n){return n.id==p.id;});auto observed=instance->observation(*node,*p.endpoint->initial());
          const auto old=std::find_if(activity_->processors.begin(),activity_->processors.end(),[&](const auto &p){return p.key==observed.key;});uint32_t token=0;
          if(old!=activity_->processors.end())token=uint32_t(old-activity_->processors.begin()+1);
          else {size_t additions=0;for(const auto &pending:controls.observations)if(pending.token>controls.activityBase)++additions;token=uint32_t(controls.activityBase+additions+1);}
          if(token>4096)throw std::invalid_argument("Parameter observation capacity exceeded");controls.observations.push_back({token,std::move(observed)});p.endpoint->observe(activity_,token);
        }
        if(next->retained.size()>=4096)throw std::invalid_argument("Graph copy history exceeds 4096 retained instances");next->retained.push_back(instance);
      }
      for(const auto &edge:definition->audio){const auto node=std::find_if(definition->nodes.begin(),definition->nodes.end(),[&](const auto &n){return n.id==edge.source;});if(node!=definition->nodes.end()&&node->kind==SignalNodeKind::Input&&edge.output)prepared->inputMask|=uint64_t(1)<<edge.output;}
      if(role==2){const auto assignment=std::find_if(native.signal.assignments.begin(),native.signal.assignments.end(),[&](const auto &a){return a.target==bus.id;});next->assignments.push_back({instance.get(),assignment->amount,assignment->wet});}
      prepared->instances.push_back(instance);if(role<2)prepared->renderOrder[role].push_back(prepared->instances.size()-1);
    }
    for(const auto &route:native.signal.outputs)if(route.source==bus.id)prepared->outputPorts.push_back(route.output);
    for(const auto &route:native.signal.stageConnections)if(route.source.stage==bus.id)prepared->outputPorts.push_back(route.output);
    for(const auto &source:native.signal.songSources)if(source.node.kind==SignalNodeKind::Follower&&source.audioStage==bus.id)prepared->outputPorts.push_back(source.output);
    std::sort(prepared->outputPorts.begin(),prepared->outputPorts.end());prepared->outputPorts.erase(std::unique(prepared->outputPorts.begin(),prepared->outputPorts.end()),prepared->outputPorts.end());
    reserve(prepared->outputPorts.size()*sizeof(std::array<float,8192>));prepared->outputBuffers.resize(prepared->outputPorts.size());prepared->row.reserve(required.size());prepared->persistent.reserve(required.size());
    for(const auto &command:native.signal.commands)if(command.target==bus.id){const auto pattern=std::find_if(native.patterns.begin(),native.patterns.end(),[&](const auto &p){return p.second.id==command.pattern;});if(pattern==native.patterns.end())throw std::invalid_argument("Graph command pattern is unavailable");prepared->patterns[pattern->first].push_back(command);}
    for(auto &[pattern,commands]:prepared->patterns)std::sort(commands.begin(),commands.end(),[](const auto &a,const auto &b){return std::tie(a.position,a.column)<std::tie(b.position,b.column);});
    const auto old=std::find_if(next->buses.begin(),next->buses.end(),[&](const auto &b){return b->id==bus.id;});if(old==next->buses.end())next->buses.push_back(std::move(prepared));else *old=std::move(prepared);
  }
  prepareParameters(native.signal,controls,previous,next.get());
  for(auto &bus:next->buses){
    if(!bus->sampleSource&&bus->instances.empty())reserve(sizeof(Bus));
    reserve(bus->instances.capacity()*sizeof(bus->instances[0])+(bus->row.capacity()+bus->persistent.capacity()+bus->renderOrder[0].capacity()+bus->renderOrder[1].capacity())*sizeof(size_t)+bus->outputPorts.capacity()*sizeof(uint32_t));
    for(const auto &[pattern,commands]:bus->patterns)reserve(sizeof(pattern)+sizeof(commands)+4*sizeof(void*)+commands.capacity()*sizeof(SignalCommand));
    for(const auto &instance:bus->instances){const auto owner=std::find_if(controls.runtimeOwners.begin(),controls.runtimeOwners.end(),[&](const auto &owner){return owner.target==&instance->runtime;});if(owner==controls.runtimeOwners.end())throw std::logic_error("Missing prepared copy runtime");bus->reserved+=owner->state->latency();
      for(const auto &[target,tail]:controls.tails)if(target==&instance->tailFrames){bus->tailSeconds=std::min(mixerMaximumTailSeconds,bus->tailSeconds+double(tail)/rate_);break;}
      const auto processor=std::find_if(controls.processorOwners.begin(),controls.processorOwners.end(),[&](const auto &p){return p.target==&instance->processors;});if(processor!=controls.processorOwners.end())next->processors+=processor->state->entries.size();
    }
    if(bus->reserved>1048576)throw std::invalid_argument("Channel graph compensation exceeds supported delay");
    for(const auto &instance:bus->instances){const auto owner=std::find_if(controls.runtimeOwners.begin(),controls.runtimeOwners.end(),[&](const auto &owner){return owner.target==&instance->runtime;});CopySet::Buffers buffers;buffers.instance=instance.get();buffers.dry.resize(size_t(owner->state->latency())*2);reserve(buffers.dry.capacity()*sizeof(float));
      for(const auto port:bus->outputPorts)if(owner->state->output(port)){const auto delay=size_t(bus->reserved)*2+2;reserve(sizeof(Instance::Auxiliary)+delay*sizeof(float));buffers.auxiliary.push_back({port,std::vector<float>(delay),0});}next->buffers.push_back(std::move(buffers));
    }
  }
  reserve(sizeof(CopySet)+next->routing.bytes()+next->retained.capacity()*sizeof(next->retained[0])+next->buses.capacity()*sizeof(next->buses[0])+next->assignments.capacity()*sizeof(CopySet::Assignment)+next->buffers.capacity()*sizeof(CopySet::Buffers));
  return next;
}
size_t NativeSignalGraph::copyIndex(const CopySet &set,uint64_t target) const noexcept {for(size_t i=0;i<set.buses.size();++i)if(set.buses[i]->id==target&&(set.buses[i]->sampleSource||!set.buses[i]->instances.empty()))return i;return SIZE_MAX;}
std::span<const uint32_t> NativeSignalGraph::copyOutputs(const CopySet &set,size_t index) const noexcept {return index<set.buses.size()?std::span<const uint32_t>(set.buses[index]->outputPorts):std::span<const uint32_t>{};}
size_t NativeSignalGraph::copyStorageBytes(const CopySet &set) const noexcept {return set.storage;}
size_t NativeSignalGraph::copyTableStorageBytes(const CopySet &set) const noexcept {return set.tableStorage;}
const NativeSignalGraph::CopySet *NativeSignalGraph::previousCopySet(const CopySet &set) const noexcept {return set.predecessor.get();}
size_t NativeSignalGraph::copyProcessors(const CopySet &set) const noexcept {return set.processors;}
void NativeSignalGraph::compileCopies(const CopySet &set,MixerGraph &mixer,std::vector<MixerProcessorInfo> &processors) const {
  mixer=set.routing;for(const auto &bus:set.buses)if(bus->sampleSource||!bus->instances.empty()){uint32_t count=1;uint64_t mask=1;for(const auto port:bus->outputPorts){count=std::max(count,port+1);mask|=uint64_t(1)<<port;}processors.push_back({signalBusIdentity(bus->id),bus->reserved,bus->tailSeconds,false,false,count,mask,bus->inputMask});}
}
void NativeSignalGraph::acceptCopies(std::shared_ptr<CopySet> copies) noexcept {storageBytes_=copies->storage;processors_=copies->processors;controlCopies_=std::move(copies);}
void NativeSignalGraph::activateCopies(CopySet &next) noexcept {
  const auto &old=audioBuses();
  if(observation_)for(const auto &bus:old)for(const auto &instance:bus->instances){bool retained=false;for(const auto &after:next.buses)if(std::find(after->instances.begin(),after->instances.end(),instance)!=after->instances.end()){retained=true;break;}if(!retained)observation_->activateDomain(instance->observationDomain,{});}
  for(auto &bus:next.buses){const auto prior=std::find_if(old.begin(),old.end(),[&](const auto &p){return p->id==bus->id;});if(prior==old.end())continue;const auto &before=**prior;
    for(auto &watch:bus->channels)for(const auto &v:before.channels)if(watch.index==v.index){watch.generation=v.generation;break;}
    for(uint8_t role=0;role<2;++role){auto &order=bus->renderOrder[role];size_t at=0;for(const auto index:before.renderOrder[role]){const auto &instance=before.instances[index];const auto found=std::find(bus->instances.begin(),bus->instances.end(),instance);if(found==bus->instances.end())continue;const auto nextIndex=size_t(found-bus->instances.begin());const auto oldOrder=std::find(order.begin()+at,order.end(),nextIndex);if(oldOrder!=order.end()){std::rotate(order.begin()+at,oldOrder,oldOrder+1);++at;}if(instance->active)(role?bus->persistent:bus->row).push_back(nextIndex);}}
  }
  for(const auto &assignment:next.assignments){assignment.instance->active=true;assignment.instance->amount=assignment.amount;assignment.instance->wet=assignment.wet;assignment.instance->tailRemaining=0;}
  for(auto &buffers:next.buffers){auto &instance=*buffers.instance;if(instance.dryDelay.size()!=buffers.dry.size()){instance.dryDelay.swap(buffers.dry);instance.dryPosition=0;}instance.auxiliary.swap(buffers.auxiliary);}
  for(const auto &bus:next.buses)for(const auto &instance:bus->instances)for(uint32_t cc=0;cc<128;++cc)instance->runtime->controller(cc,controllers_[cc].load(std::memory_order_relaxed)/127.);
  audioCopies_=&next;publishedCopies_.store(&next,std::memory_order_release);
}

std::vector<SignalActivity> NativeSignalGraph::activity() const {
  std::vector<SignalActivity> result;
  for(const auto &bus:publishedBuses())for(const auto &instance:bus->instances){const auto state=instance->published.load(std::memory_order_relaxed);if(state)result.push_back({bus->id,instance->graph,instance->role,uint16_t(state&0xffff),bool(state&0x10000)});}
  return result;
}
void NativeSignalGraph::prepareParameters(const SignalGraph &next,GraphControlPlan &plan,const GraphControlPlan *previous,const CopySet *copies) const {
  auto reserve=[&](size_t bytes){if(bytes>plan.preparationHeadroom)throw std::invalid_argument("Live graph controls exceed the 256 MB prepared storage budget");plan.preparationHeadroom-=bytes;};
  for(const auto &b:(copies?copies->buses:controlBuses()))for(const auto &instance:b->instances){
    const auto saved=std::static_pointer_cast<Instance::Remembered>(instance->remembered);
    const bool inPrevious=previous&&std::any_of(previous->runtimeOwners.begin(),previous->runtimeOwners.end(),[&](const auto &owner){return owner.target==&instance->runtime;});
    const auto *prior=inPrevious?previous:(saved?saved->prior.get():nullptr);
    const auto d=std::find_if(next.library.begin(),next.library.end(),[&](const auto &d){return d.id==instance->graph;});
    if(d==next.library.end())throw std::invalid_argument("Playing subgraph no longer exists");
    const SignalControls *controls=nullptr;
    for(const auto &c:plan.controls)if(c->definition.id==d->id){controls=c.get();break;}
    if(!controls){auto c=std::make_shared<SignalControls>(*d,rate_);controls=c.get();plan.controls.push_back(std::move(c));}
    auto runtime=saved?saved->runtime:instance->initialRuntime;
    if(prior)for(const auto &owner:prior->runtimeOwners)if(owner.target==&instance->runtime){runtime=owner.state;break;}
    std::vector<SignalProcessorInfo> processorInfo;std::vector<SignalParameterInfo> parameterInfo;
    const size_t updateStart=plan.updates.size();
    double tail=0;bool latencyChanged=false;
    auto processorSet=saved?saved->processors:instance->initialProcessors;
    if(prior)for(const auto &owner:prior->processorOwners)if(owner.target==&instance->processors){processorSet=owner.state;break;}
    if(prior)for(const auto &owner:prior->runtimeOwners)if(owner.target==&instance->runtime&&owner.structural&&!instance->layoutReady(runtime.get()))throw std::runtime_error("This graph is changing its audio layout; retry the edit shortly");
    plan.preparedProcessors+=std::count_if(d->nodes.begin(),d->nodes.end(),[](const auto &n){return n.kind==SignalNodeKind::Plugin;});
    if(plan.preparedProcessors>256)throw std::invalid_argument("Active song graph exceeds 256 prepared plugin copies");
    auto desired=std::make_shared<GraphProcessorSet>();
    const auto sameVendor=[](const GraphPluginState &state,const GraphPluginRecipe &b){const auto &a=state.recipe;return std::tie(a.format,a.path,a.classID,a.type,a.subtype,a.manufacturer)==std::tie(b.format,b.path,b.classID,b.type,b.subtype,b.manufacturer)&&(b.audioLayout.empty()||b.audioLayout==state.plugin->audioLayout());};
    for(const auto &node:d->nodes)if(node.kind==SignalNodeKind::Plugin){
      const auto *existing=processorSet->find(node.id);
      if(existing&&!sameVendor(*existing->endpoint->initial(),node.plugin))existing=nullptr;
      if(!existing)for(const auto &retired:processorSet->retired)if(retired.id==node.id&&sameVendor(*retired.endpoint->initial(),node.plugin)){existing=&retired;break;}
      if(existing)desired->entries.push_back(*existing);
      else {
        auto state=std::make_shared<GraphPluginState>(*d,node,rate_,offline_);reserve(state->storageBytes());
        auto endpoint=std::make_shared<GraphPluginEndpoint>(std::move(state),rate_);reserve(endpoint->storageBytes());
        if(activity_){auto observed=instance->observation(node,*endpoint->initial());
          const auto found=std::find_if(activity_->processors.begin(),activity_->processors.end(),[&](const auto &p){return p.key==observed.key;});
          uint32_t token;
          if(found!=activity_->processors.end())token=uint32_t(found-activity_->processors.begin()+1);
          else {size_t additions=0;for(const auto &pending:plan.observations)if(pending.token>plan.activityBase)++additions;token=uint32_t(plan.activityBase+additions+1);}
          if(token>4096)throw std::invalid_argument("Parameter observation capacity exceeded");
          plan.observations.push_back({token,std::move(observed)});endpoint->observe(activity_,token);
        }
        desired->entries.push_back({node.id,std::move(endpoint)});
      }
    }
    for(const auto *entries:{&processorSet->retired,&processorSet->entries})for(const auto &entry:*entries)
      if(std::none_of(desired->entries.begin(),desired->entries.end(),[&](const auto &active){return active.endpoint==entry.endpoint;}))desired->retired.push_back(entry);
    if(desired->entries.size()+desired->retired.size()>4096)throw std::invalid_argument("Recipe vendor history exceeds 4096 retained processor instances");
    const bool processorsChanged=desired->entries.size()!=processorSet->entries.size()||!std::equal(desired->entries.begin(),desired->entries.end(),processorSet->entries.begin(),[](const auto &a,const auto &b){return a.id==b.id&&a.endpoint==b.endpoint;});
    if(processorsChanged){reserve(sizeof(GraphProcessorSet)+(desired->entries.capacity()+desired->retired.capacity())*sizeof(GraphProcessorSet::Entry));processorSet=std::move(desired);}
    for(const auto &p:processorSet->entries){
      const auto n=std::find_if(d->nodes.begin(),d->nodes.end(),[&](const auto &n){return n.id==p.id;});
      auto state=p.endpoint->initial();std::shared_ptr<GraphPluginState> retained;
      if(prior)for(const auto &preset:prior->presets)if(preset.endpoint==p.endpoint.get()){state=preset.state;retained=preset.previous;break;}
      for(auto port:n->plugin.inputs)if(port>=64||!(state->inputs&(uint64_t(1)<<port)))throw std::invalid_argument("Recipe input is outside the prepared physical channel layout");
      for(auto port:n->plugin.outputs)if(port>=64||!(state->outputs&(uint64_t(1)<<port)))throw std::invalid_argument("Recipe output is outside the prepared physical channel layout");
      if(state->recipe.state!=n->plugin.state){
        if(!p.endpoint->ready(state.get()))throw std::runtime_error("A graph preset is still fading; retry the edit shortly");
        auto replacement=std::make_shared<GraphPluginState>(*d,*n,rate_,offline_);reserve(replacement->storageBytes());
        const auto sameParameter=[](const PluginParameter &a,const PluginParameter &b){return a.id==b.id&&a.name==b.name&&a.min==b.min&&a.max==b.max&&a.unit==b.unit&&a.unitLabel==b.unitLabel&&a.choices==b.choices&&a.logarithmic==b.logarithmic&&a.step==b.step&&a.writable==b.writable&&a.continuous==b.continuous;};
        const auto &beforeBuses=state->plugin->buses(),&afterBuses=replacement->plugin->buses();
        const bool sameBuses=beforeBuses.size()==afterBuses.size()&&std::equal(beforeBuses.begin(),beforeBuses.end(),afterBuses.begin(),[](const auto &a,const auto &b){return a.index==b.index&&a.channels==b.channels&&a.input==b.input&&a.active==b.active&&a.supported==b.supported;});
        if(!sameBuses||replacement->inputs!=state->inputs||replacement->outputs!=state->outputs||replacement->latency!=uint32_t(std::llround(state->plugin->latency()*rate_))||replacement->parameters.size()!=state->parameters.size()||!std::equal(state->parameters.begin(),state->parameters.end(),replacement->parameters.begin(),sameParameter))
          throw std::invalid_argument("Live graph presets must preserve plugin ports, latency and parameter identities");
        p.endpoint->observePrepared(*replacement);retained=state;state=std::move(replacement);
        for(const auto &parameter:state->parameters)plan.readings.push_back({state->plugin.get(),parameter.id,parameter.value});
      }
      plan.presets.push_back({p.endpoint.get(),state,retained});
      plan.bypasses.emplace_back(p.endpoint.get(),n->plugin.bypass);
      auto samples=uint32_t(std::llround(state->plugin->latency()*rate_));auto processorTail=state->plugin->tail();
      if(copies&&state->plugin->latencyChangePending()){
        auto update=state->plugin->prepareLatency();if(!update)throw std::runtime_error("Recipe latency changed while preparing; retry the edit");
        samples=update->snapshot.samples;processorTail=update->snapshot.tail;latencyChanged=true;
        const auto bytes=update->storageBytes();reserve(bytes);plan.latencies.push_back({state->plugin,std::move(update),bytes});
      }
      tail=std::min(mixerMaximumTailSeconds,tail+std::max(0.,processorTail));
      processorInfo.push_back({p.id,samples,state->inputs,state->outputs});
      const auto &parameters=state->parameters;
      std::set<uint32_t> modulated;
      for(const auto &m:d->modulation)if(m.enabled&&m.target==p.id){const auto c=std::find_if(parameters.begin(),parameters.end(),[&](const auto &c){return c.id==m.parameter;});if(c==parameters.end())throw std::invalid_argument("Graph modulation parameter is unavailable");parameterInfo.push_back({p.id,m.parameter,graphModulationStep(*c,m.quantized)});modulated.insert(m.parameter);}
      // Stage first-use queues for both audible preset incarnations. Accepted
      // snapshots retain these queues even if a later edit removes the source.
      for(const auto &vendor:{state,retained})if(vendor){
        if(!modulated.empty()&&!vendor->plugin->initiallyScheduled()){
          auto queue=vendor->scheduling;
          if(!queue){reserve(sizeof(PluginParameterQueue));queue=std::make_shared<PluginParameterQueue>();}
          plan.scheduling.push_back({vendor,std::move(queue)});
        }else if(vendor->scheduling)plan.scheduling.push_back({vendor,vendor->scheduling});
        std::set<uint32_t> ranges=modulated;ranges.insert(vendor->initialModulated.begin(),vendor->initialModulated.end());
        if(prior)for(const auto &range:prior->ranges)if(range.plugin==vendor->plugin.get())ranges.insert(range.parameter);
        // Predict built-in decay from a separate control-thread model, never
        // mutate the live processor's range trackers during preparation.
        std::unique_ptr<NativeEffect> tailModel;
        if(vendor->recipe.format=="Built-in"&&!ranges.empty()){
          tailModel=std::make_unique<NativeEffect>(vendor->recipe.classID,rate_,vendor->recipe.state);
          for(const auto &[id,value]:n->plugin.parameters)tailModel->parameter(id,float(value));
        }
        for(auto id:ranges){const auto c=std::find_if(parameters.begin(),parameters.end(),[&](const auto &c){return c.id==id;});
          if(c!=parameters.end()){plan.ranges.push_back({vendor->plugin.get(),id,c->min,c->max});if(tailModel)tailModel->includeParameterRange(id,c->min,c->max);}
        }
        if(tailModel)tail=std::min(mixerMaximumTailSeconds,tail+std::max(0.,tailModel->tail()-state->plugin->tail()));
      }
      for(const auto &[id,value]:n->plugin.parameters){const auto c=std::find_if(parameters.begin(),parameters.end(),[&](const auto &c){return c.id==id;});
        if(c==parameters.end()||!c->writable||!std::isfinite(value)||value<c->min||value>c->max)throw std::invalid_argument("Graph parameter baseline is unavailable or outside its range");
      }
      // Retain every touched baseline in subsequent snapshots, so skipped
      // publications and Undo restore it. Untouched catalog entries never need
      // an audio command, even for a plugin exposing thousands of parameters.
      std::set<uint32_t> formerlyModulated(state->initialModulated.begin(),state->initialModulated.end());
      if(prior)for(const auto &range:prior->ranges)if(range.plugin==state->plugin.get())formerlyModulated.insert(range.parameter);
      std::set<uint32_t> touched=modulated;touched.insert(formerlyModulated.begin(),formerlyModulated.end());
      for(const auto &[id,value]:n->plugin.parameters)touched.insert(id);
      if(prior)for(const auto &v:prior->updates)if(v.endpoint==p.endpoint.get())touched.insert(v.parameter);
      for(size_t i=0;i<parameters.size();++i)if(state->initialBaselines[i]!=parameters[i].value)touched.insert(parameters[i].id);
      for(const auto &c:parameters)if(c.writable&&touched.contains(c.id)){const auto found=n->plugin.parameters.find(c.id);
        plan.updates.push_back({state->plugin.get(),nullptr,p.id,c.id,found==n->plugin.parameters.end()?c.value:found->second,state->baselines.get()+(&c-parameters.data()),state->initialBaselines[&c-parameters.data()],p.endpoint.get(),formerlyModulated.contains(c.id)&&!modulated.contains(c.id)});
        plan.readings.push_back({state->plugin.get(),c.id,plan.updates.back().value});
      }
      std::set<uint32_t> bases;
      for(const auto &m:d->modulation)if(m.enabled&&m.target==p.id&&bases.insert(m.parameter).second){
        plan.updates.push_back({nullptr,runtime.get(),p.id,m.parameter,m.base});
        const auto c=std::find_if(parameters.begin(),parameters.end(),[&](const auto &c){return c.id==m.parameter;});
        if(c!=parameters.end())plan.readings.push_back({state->plugin.get(),c->id,c->min+(c->max-c->min)*m.base});
      }
      if(plan.updates.size()>8192)throw std::invalid_argument("Live graph parameter update exceeds 8192 prepared controls");
    }
    // A removed endpoint can still point at a previously published opaque
    // incarnation. Carry both owners even after its last audible fade; Undo
    // must not revive a dangling pointer or flatten its vendor state.
    for(const auto &retired:processorSet->retired){
      const auto found=prior?std::find_if(prior->presets.begin(),prior->presets.end(),[&](const auto &p){return p.endpoint==retired.endpoint.get();}):plan.presets.end();
      if(prior&&found!=prior->presets.end())plan.presets.push_back(*found);
      else plan.presets.push_back({retired.endpoint.get(),retired.endpoint->initial(),{}});
    }
    bool structural=processorsChanged;
    auto audioLayout=[](const SignalDefinition &definition){SignalGraph graph;graph.library={definition};graph.assignments={{1,definition.id}};for(auto &node:graph.library[0].nodes)node.plugin.state.clear();return graph;};
    structural|=!sameSignalSourceLayout(audioLayout(runtime->definition()),audioLayout(*d));
    if(structural&&!instance->layoutReady(runtime.get()))throw std::runtime_error("A graph layout publication is pending; retry the edit shortly");
    if(!runtime->sameLayout(*d)||structural||latencyChanged){
      auto compiled=compileSignal(*d,processorInfo);
      const auto inputNode=std::find_if(d->nodes.begin(),d->nodes.end(),[](const auto &n){return n.kind==SignalNodeKind::Input;});
      for(const auto &edge:d->audio)if(edge.source==inputNode->id&&edge.output&&!(b->inputMask&(uint64_t(1)<<edge.output)))throw std::invalid_argument("A live source cannot enable an unprepared external graph input");
      auto replacement=std::make_shared<SignalRuntime>(*d,std::move(compiled),rate_,parameterInfo);
      if(observation_)replacement->observer(std::make_shared<GraphSignalObservation>(*observation_,instance->observationDomain,instance->copyIdentity(),*d,replacement->plan(),plan.signalPortIdentities,Instance::observedBuses(*processorSet)));
      reserve(replacement->storageBytes());
      if(!copies&&replacement->latency()!=runtime->latency())throw std::invalid_argument("This recipe latency change needs a prepared outer-mixer transition");
      if(!copies&&!structural&&!replacement->compatibleHistory(*runtime))throw std::invalid_argument("Live graph source edits must preserve audio latency and compensation");
      for(auto port:b->outputPorts)if(!copies&&replacement->output(port)&&std::none_of(instance->auxiliary.begin(),instance->auxiliary.end(),[&](const auto &p){return p.port==port;}))throw std::invalid_argument("This recipe output activation needs a prepared outer-mixer transition");
      runtime=std::move(replacement);
    }
    for(size_t i=updateStart;i<plan.updates.size();++i)if(plan.updates[i].runtime)plan.updates[i].runtime=runtime.get();
    plan.runtimeOwners.push_back({&instance->runtime,instance->initialRuntime,runtime,{},instance.get(),
      [](void *context,SignalRuntime *runtime,GraphProcessorSet *processors,bool structural) noexcept {static_cast<Instance *>(context)->adopt(runtime,processors,structural);},processorSet.get(),copies?false:structural});
    plan.processorOwners.push_back({&instance->processors,&instance->publishedProcessors,instance->initialProcessors,processorSet});
    plan.runtimes.emplace_back(runtime.get(),controls);
    plan.tails.emplace_back(&instance->tailFrames,uint64_t(std::ceil(tail*rate_))+runtime->latency());
    auto remember=std::make_shared<Instance::Remembered>();remember->runtime=runtime;remember->processors=processorSet;
    const auto ownsEndpoint=[&](const GraphPluginEndpoint *endpoint){for(const auto *entries:{&processorSet->entries,&processorSet->retired})for(const auto &p:*entries)if(p.endpoint.get()==endpoint)return true;return false;};
    auto &memory=*remember->prior;
    for(const auto &preset:plan.presets)if(ownsEndpoint(preset.endpoint))memory.presets.push_back(preset);
    for(const auto &control:plan.controls)if(control->definition.id==instance->graph)memory.controls.push_back(control);
    const auto ownsPlugin=[&](const NativePlugin *plugin){for(const auto &preset:memory.presets)for(const auto &state:{preset.state,preset.previous})if(state&&state->plugin.get()==plugin)return true;return false;};
    for(const auto &range:plan.ranges)if(ownsPlugin(range.plugin))memory.ranges.push_back(range);
    for(const auto &scheduled:plan.scheduling)if(ownsPlugin(scheduled.state->plugin.get()))memory.scheduling.push_back(scheduled);
    for(size_t i=updateStart;i<plan.updates.size();++i)if(ownsEndpoint(plan.updates[i].endpoint))memory.updates.push_back(plan.updates[i]);
    memory.runtimeOwners.push_back(plan.runtimeOwners.back());
    memory.processorOwners.push_back(plan.processorOwners.back());
    const size_t rememberedBytes=sizeof(Instance::Remembered);
    memory.copyOwnerStorage=rememberedBytes;
    reserve(rememberedBytes+sizeof(GraphControlPlan)+memory.presets.capacity()*sizeof(GraphControlPlan::Preset)+memory.ranges.capacity()*sizeof(GraphControlPlan::Range)+memory.updates.capacity()*sizeof(GraphParameterUpdate)+memory.controls.capacity()*sizeof(memory.controls[0])+memory.scheduling.capacity()*sizeof(GraphControlPlan::Scheduling)+memory.runtimeOwners.capacity()*sizeof(GraphControlPlan::Runtime)+memory.processorOwners.capacity()*sizeof(GraphControlPlan::Processors));
    plan.copyOwners.push_back(remember->prior);plan.rememberedCopies.push_back({&instance->remembered,std::move(remember),0});
  }
  // The coordinator retains removed copies for Undo. Their most recent state
  // can outlive every previously active publication, so keep that ownership
  // visible to the same deduplicating budget as active copies.
  for(const auto &instance:(copies?copies->retained:controlCopies_->retained)){
    if(std::any_of(plan.runtimeOwners.begin(),plan.runtimeOwners.end(),[&](const auto &owner){return owner.target==&instance->runtime;}))continue;
    if(const auto memory=std::static_pointer_cast<Instance::Remembered>(instance->remembered))plan.copyOwners.push_back(memory->prior);
  }
  for(const auto &bus:(copies?copies->buses:controlBuses())){uint64_t frames=0;for(const auto &instance:bus->instances)for(const auto &[target,value]:plan.tails)if(target==&instance->tailFrames){frames+=value;break;}
    plan.graphTails.emplace_back(signalBusIdentity(bus->id),std::min(mixerMaximumTailSeconds,double(frames)/rate_));}
}
uint64_t NativeSignalGraph::tailFrames(size_t index) const noexcept {
  if(index>=audioBuses().size())return 0;uint64_t total=0;for(const auto &instance:audioBuses()[index]->instances)total+=instance->currentTailFrames();
  return std::min(total,uint64_t(std::ceil(mixerMaximumTailSeconds*rate_)));
}
// Growth is conservative across parallel copies but bounded; it prevents a
// live comb/filter edit from truncating stored energy before a new plan exists.
double NativeSignalGraph::tailGrowth() const noexcept {double growth=0;for(const auto &b:publishedBuses())for(const auto &i:b->instances)for(const auto &p:i->publishedProcessors.load(std::memory_order_acquire)->entries)growth+=p.endpoint->tailGrowth();return std::min(60.,growth);}
uint64_t NativeSignalGraph::tailRevision() const noexcept {uint64_t revision=0;for(const auto &b:publishedBuses())for(const auto &i:b->instances)for(const auto &p:i->publishedProcessors.load(std::memory_order_acquire)->entries)revision+=p.endpoint->tailRevision();return revision;}
bool NativeSignalGraph::latencyChangePending() const noexcept {
  for (const auto &b : publishedBuses()) for (const auto &i : b->instances)
    for (const auto &p : i->publishedProcessors.load(std::memory_order_acquire)->entries) if (p.endpoint->latencyChangePending()) return true;
  return false;
}
void NativeSignalGraph::refreshLatencies(std::vector<MixerProcessorInfo> &mixerProcessors) {
  for (auto &b : controlBuses()) {
    const auto previousReserved = b->reserved;
    b->reserved = 0; b->tailSeconds = 0;
    for (auto &i : b->instances) {
      std::vector<SignalProcessorInfo> info;
      double tail = 0;
      const auto oldBypass=i->bypassStorage(),oldInitialProcessors=i->initialProcessorStorage();
      for (auto &p : i->processors->entries) {
        p.endpoint->settleStopped();p.endpoint->current().plugin->refreshLatency();
        p.endpoint->current().latency=uint32_t(std::llround(p.endpoint->current().plugin->latency()*rate_));
        uint64_t inputs = 1, outputs = 1;
        for (const auto &bus : p.endpoint->current().plugin->buses()) if (bus.active && bus.supported && bus.index < 64)
          (bus.input ? inputs : outputs) |= uint64_t(1) << bus.index;
        info.push_back({p.id, uint32_t(std::llround(p.endpoint->current().plugin->latency() * rate_)), inputs, outputs});
        tail = std::min(mixerMaximumTailSeconds, tail + std::max(0., p.endpoint->current().plugin->tail()));
      }
      auto plan = compileSignal(i->runtime->definition(), info);
      const auto oldStorage = (i->runtime==i->initialRuntime.get()?i->runtime->storageBytes():0) + i->dryDelay.size() * sizeof(float);
      if (plan.totalLatency != i->runtime->latency()) {
        i->dryDelay.assign(size_t(plan.totalLatency) * 2, 0); i->dryPosition = 0;
      }
      i->runtime->updateLatencyPlan(std::move(plan));
      if(observation_){std::vector<SignalPortIdentity> pending;i->runtime->observer(std::make_shared<GraphSignalObservation>(*observation_,i->observationDomain,i->copyIdentity(),i->runtime->definition(),i->runtime->plan(),pending,Instance::observedBuses(*i->processors)));auto batch=observation_->preparePorts(std::move(pending));observation_->publishPorts(batch);}
      storageBytes_ = storageBytes_ - oldStorage - oldBypass - oldInitialProcessors + i->initialProcessorStorage() + (i->runtime==i->initialRuntime.get()?i->runtime->storageBytes():0) + i->dryDelay.size() * sizeof(float)+i->bypassStorage();
      i->tailFrames = uint64_t(std::ceil(tail * rate_)) + i->runtime->latency();
      b->reserved += i->runtime->latency();
      b->tailSeconds = std::min(mixerMaximumTailSeconds, b->tailSeconds + i->tailFrames / rate_);
    }
    if (b->reserved > 1048576) throw std::invalid_argument("Channel graph compensation exceeds supported delay");
    if (b->reserved != previousReserved) for (auto &i : b->instances) for (auto &port : i->auxiliary) {
      const auto oldBytes = port.delay.size() * sizeof(float);
      port.delay.assign(size_t(b->reserved) * 2 + 2, 0); port.cursor = 0;
      storageBytes_ = storageBytes_ - oldBytes + port.delay.size() * sizeof(float);
    }
    if (storageBytes_ > 256 * 1024 * 1024) throw std::invalid_argument("Song graph audio storage exceeds 256 MB");
    for (auto &p : mixerProcessors) if (p.instance == signalBusIdentity(b->id)) {
      p.latency = b->reserved; p.tail = b->tailSeconds;
    }
  }
  controlCopies_->storage=storageBytes_;
}
void NativeSignalGraph::compile(MixerGraph &mixer,std::vector<MixerProcessorInfo> &processors){compileCopies(*controlCopies_,mixer,processors);}
bool NativeSignalGraph::sameNoteMembership(const NativeSong &native) const {
  for(const auto &bus:controlBuses()) {
    // Instrument-copy membership follows its original voice/channel, not the
    // destination mixer path. rawChannels is set only for bus note envelopes.
    if(!bus->rawChannels || bus->instrument)continue;
    const bool needsNotes=std::any_of(bus->instances.begin(),bus->instances.end(),[&](const auto &instance){
      const auto d=std::find_if(native.signal.library.begin(),native.signal.library.end(),[&](const auto &d){return d.id==instance->graph;});
      return d!=native.signal.library.end()&&std::any_of(d->nodes.begin(),d->nodes.end(),[](const auto &n){return n.kind==SignalNodeKind::NoteEnvelope;});
    });
    if(!needsNotes)continue;
    std::array<bool,192> members{};
    for(const auto &[channel,track]:native.tracks) {
      auto id=track.id;
      for(size_t depth=0;id && depth<native.mixer.buses.size();++depth) {
        if(id==bus->id){if(channel>=members.size())return false;members[channel]=true;break;}
        const auto source=std::find_if(native.mixer.buses.begin(),native.mixer.buses.end(),[&](const auto &b){return b.id==id;});
        id=source==native.mixer.buses.end()?0:source->output;
      }
    }
    if(native.tracks.size()!=bus->rawChannels || members!=bus->members)return false;
  }
  return true;
}
std::span<const uint32_t> NativeSignalGraph::outputs(size_t index) const noexcept {
  if(index>=controlBuses().size())return {};return controlBuses()[index]->outputPorts;
}
const float *NativeSignalGraph::output(size_t index,uint32_t port) const noexcept {
  if(index>=audioBuses().size())return nullptr;const auto &b=*audioBuses()[index];
  auto found=std::find(b.outputPorts.begin(),b.outputPorts.end(),port);
  return found==b.outputPorts.end()?nullptr:b.outputBuffers[size_t(found-b.outputPorts.begin())].data();
}
void NativeSignalGraph::begin(const OpenMPT::PlayState &state,uint32_t,uint64_t,PluginTransport transport,uint32_t patternRows)noexcept {
  const bool valid=!state.m_flags[OpenMPT::SONG_PAUSED|OpenMPT::SONG_FADINGSONG]&&state.m_nSamplesPerTick&&state.TicksOnRow();
  const double units=valid?65536.0/(double(state.TicksOnRow())*state.m_nSamplesPerTick):0;
  const double at=double(state.m_nRow)*65536+(valid?double(state.m_nTickCount)*65536/state.TicksOnRow()+state.SamplesIntoTick()*units:0);
  const bool entering=pattern_!=state.m_nPattern||order_!=state.m_nCurrentOrder||at<=previous_;
  const bool rowChanged=entering||row_!=state.m_nRow;
  pattern_=state.m_nPattern;order_=state.m_nCurrentOrder;row_=state.m_nRow;previous_=at;
  for(uint32_t cc=0;cc<128;++cc){auto value=controllers_[cc].load(std::memory_order_relaxed);if(value!=appliedControllers_[cc]){appliedControllers_[cc]=value;for(auto &b:audioBuses())for(auto &i:b->instances)i->runtime->controller(cc,value/127.);}}
  for(auto &b:audioBuses()){bool gate=false,retrigger=false;
    const bool watching=std::any_of(b->instances.begin(),b->instances.end(),[](const auto &i){return i->runtime->watchesNotes()||(i->pendingRuntime&&i->pendingRuntime->watchesNotes());});
    if(watching)for(auto &watch:b->channels)if(watch.index<state.Chn.size()){
      const auto &channel=state.Chn[watch.index];
      if(!b->instrument&&watch.index>=b->rawChannels&&(!channel.nMasterChn||channel.nMasterChn>b->members.size()||!b->members[channel.nMasterChn-1]))continue;
      if(b->instrument&&(channel.pModInstrument!=b->instrument||(channel.isPreviewNote&&!channel.nMasterChn?UINT16_MAX:channel.nMasterChn?channel.nMasterChn-1:watch.index)!=b->sampleChannel))continue;
      const bool held=channel.nNote>=1&&channel.nNote<=120&&!channel.dwFlags[OpenMPT::CHN_KEYOFF|OpenMPT::CHN_NOTEFADE|OpenMPT::CHN_MUTE|OpenMPT::CHN_SYNCMUTE];
      const bool sounding=held&&(channel.IsSamplePlaying()||channel.HasMIDIOutput());
      gate|=sounding;retrigger|=sounding&&channel.nativeNoteGeneration!=watch.generation;watch.generation=channel.nativeNoteGeneration;
    }
    for(auto &i:b->instances)i->runtime->note(gate,retrigger);
    const auto identity=patternIDs_.find(uint16_t(state.m_nPattern));
    b->clock={transport.beat,transport.tempo,transport.playing,identity==patternIDs_.end()?0:identity->second,at/256,transport.playing?units/256:0,double(patternRows)*256,double(std::max(1u,unsigned(state.m_nCurrentRowsPerBeat)))};
    b->transport=transport;b->begin=at;b->units=units;b->commands=valid;b->expire|=rowChanged;
    if(entering||b->seek){b->seek=false;auto found=b->patterns.find(uint16_t(pattern_));b->events=found==b->patterns.end()?nullptr:&found->second;b->next=0;
      if(b->events)while(b->next<b->events->size()&&(*b->events)[b->next].position<at-1e-8)++b->next;}
    // Note gates are distinct from amplitude followers and aggregate group members.
  }
}
void NativeSignalGraph::tail()noexcept {for(auto &b:audioBuses()){b->commands=false;b->transport.playing=false;b->clock.playing=false;b->clock.unitsPerFrame=0;for(auto &i:b->instances)i->runtime->note(false);}}
bool NativeSignalGraph::process(size_t index,float *audio,uint32_t frames,uint64_t position,std::span<const MixerAudioInput> inputs)noexcept {return index<audioBuses().size()&&frames<=4096&&audioBuses()[index]->render(audio,frames,position,inputs);}
} // namespace Tracker
