#include "NativeSignalGraph.hpp"
#include "editor/hosted/GraphPluginEndpoint.hpp"
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
  struct Processor {uint64_t id;std::unique_ptr<GraphPluginEndpoint> endpoint;};
  std::vector<Processor> processors;
  struct Auxiliary {
    uint32_t port=0;
    std::vector<float> delay;
    size_t cursor=0;
  };
  std::vector<Auxiliary> auxiliary;
  std::vector<float> dryDelay;
  size_t dryPosition=0;
  std::array<float,8192> dry{};
  size_t bypassStorage() const {size_t result=0;for(const auto &p:processors)result+=p.endpoint->current().plugin->bypassStorageBytes();return result;}
  size_t initialProcessorStorage() const {size_t result=0;for(const auto &p:processors)result+=p.endpoint->initial()->storageBytes();return result;}
  explicit Instance(const SignalDefinition &d,uint8_t role,double sampleRate,bool offline):graph(d.id),role(role),rate(sampleRate) {
    std::vector<SignalProcessorInfo> info;std::vector<SignalParameterInfo> parameterInfo;
    double tail=0;
    for(const auto &n:d.nodes)if(n.kind==SignalNodeKind::Plugin){
      auto state=std::make_shared<GraphPluginState>(d,n,sampleRate,offline);
      info.push_back({n.id,state->latency,state->inputs,state->outputs});
      for(const auto &m:d.modulation)if(m.target==n.id&&m.enabled){const auto p=std::find_if(state->parameters.begin(),state->parameters.end(),[&](const auto &p){return p.id==m.parameter;});if(p==state->parameters.end())throw std::invalid_argument("Graph modulation parameter is unavailable");parameterInfo.push_back({n.id,m.parameter,graphModulationStep(*p,m.quantized)});}
      tail=std::min(mixerMaximumTailSeconds,tail+std::max(0.,state->plugin->tail()));
      processors.push_back({n.id,std::make_unique<GraphPluginEndpoint>(std::move(state),sampleRate)});
    }
    auto plan=compileSignal(d,info);dryDelay.resize(size_t(plan.totalLatency)*2);tailFrames=uint64_t(std::ceil(tail*sampleRate))+plan.totalLatency;initialRuntime=std::make_shared<SignalRuntime>(d,std::move(plan),sampleRate,parameterInfo);runtime=initialRuntime.get();
  }
  uint64_t currentTailFrames() const noexcept {double tail=0;for(const auto &p:processors)tail+=p.endpoint->tail();return std::max(tailFrames,uint64_t(std::ceil(std::min(mixerMaximumTailSeconds,tail)*rate))+runtime->latency());}
  Processor *processor(uint64_t id)noexcept{for(auto &p:processors)if(p.id==id)return &p;return nullptr;}
  bool render(float *buffer,uint32_t frames,uint64_t position,PluginTransport transport,SignalClock clock,std::span<const MixerAudioInput> inputs)noexcept {
    runtime->amount(amount);for(auto &p:processors)p.endpoint->transport(transport,(active||tailRemaining>0)&&wet>0);
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
    for(uint32_t i=0;i<frames*2;++i)buffer[i]=active ? float(buffer[i]*wet+dry[i]*(1-wet)) : dry[i]+(i<uint64_t(tailRemaining)*2 ? float(buffer[i]*wet) : 0.f);return true;
  }
};
struct NativeSignalGraph::Bus {
  uint64_t id=0;
  double rate=48000;
  std::vector<std::unique_ptr<Instance>> instances;
  std::vector<size_t> row,persistent;
  std::array<std::vector<size_t>,2> renderOrder;
  struct NoteWatch {uint16_t index=0;uint64_t generation=0;};
  std::vector<NoteWatch> channels;
  std::array<bool,192> members{};
  uint16_t rawChannels=0;
  const OpenMPT::ModInstrument *instrument=nullptr;
  uint16_t sampleChannel=0;
  std::map<uint16_t,std::vector<SignalCommand>> patterns;
  const std::vector<SignalCommand> *events=nullptr;
  size_t next=0;
  double begin=0,units=0;
  bool expire=false,commands=true;
  PluginTransport transport;
  SignalClock clock;

  uint32_t reserved=0;
  double tailSeconds=0;
  uint64_t inputMask=0;
  std::vector<uint32_t> outputPorts;
  std::vector<std::array<float,8192>> outputBuffers;
  void auxiliary(Instance &instance,uint32_t prefix,uint32_t offset,uint32_t count,uint32_t audible)noexcept {
    for(auto &port:instance.auxiliary){
      const auto *samples=instance.runtime->output(port.port);
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
NativeSignalGraph::NativeSignalGraph(const NativeSong &native,double rate,bool offline,std::span<const SignalSampleSource> sampleSources,size_t storageLimit,size_t processorLimit,ParameterActivity *activity):rate_(rate),offline_(offline),routedMixer_(signalRoutingGraph(native.mixer,native.signal)){
  for(const auto &[index,entity]:native.patterns)patternIDs_.emplace(index,entity.id);
  size_t processors=0,delayBytes=0;
  auto budget=[&](size_t bytes){if(bytes>storageLimit-delayBytes)throw std::invalid_argument("Song graph audio storage exceeds 256 MB");delayBytes+=bytes;};
  for(const auto &bus:native.mixer.buses){std::set<std::pair<uint64_t,uint8_t>> required;
    for(const auto &c:native.signal.commands)if(c.target==bus.id&&(c.kind==SignalCommandKind::Row||c.kind==SignalCommandKind::Start))required.emplace(c.graph,c.kind==SignalCommandKind::Row?0:1);
    for(const auto &a:native.signal.assignments)if(a.target==bus.id)required.emplace(a.graph,2);
    if(required.empty())continue;
    auto prepared=std::make_unique<Bus>();prepared->id=bus.id;prepared->rate=rate;
    for(const auto &[channel,track]:native.tracks){auto id=track.id;for(size_t depth=0;id&&depth<native.mixer.buses.size();++depth){if(id==bus.id){prepared->channels.push_back({channel,0});break;}auto source=std::find_if(native.mixer.buses.begin(),native.mixer.buses.end(),[&](const auto &b){return b.id==id;});id=source==native.mixer.buses.end()?0:source->output;}}

    const bool needsNotes=std::any_of(required.begin(),required.end(),[&](const auto &use){auto d=std::find_if(native.signal.library.begin(),native.signal.library.end(),[&](const auto &d){return d.id==use.first;});return d!=native.signal.library.end()&&std::any_of(d->nodes.begin(),d->nodes.end(),[](const auto &n){return n.kind==SignalNodeKind::NoteEnvelope;});});
    if(!needsNotes)prepared->channels.clear();
    for(const auto &source:sampleSources)if(needsNotes&&source.target==bus.id){prepared->instrument=source.instrument;prepared->sampleChannel=source.channel;prepared->channels.clear();if(source.channel!=UINT16_MAX)prepared->channels.push_back({source.channel,0});for(uint16_t i=source.channels;i<OpenMPT::MAX_CHANNELS;++i)prepared->channels.push_back({i,0});}
    if(needsNotes&&!prepared->instrument){
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
      auto instance=std::make_unique<Instance>(*definition,role,rate,offline);budget(sizeof(Instance)+instance->dryDelay.size()*sizeof(float)+instance->runtime->storageBytes()+instance->bypassStorage());prepared->reserved+=instance->runtime->latency();prepared->tailSeconds=std::min(mixerMaximumTailSeconds,prepared->tailSeconds+instance->tailFrames/rate);
      for(const auto &p:instance->processors)budget(p.endpoint->storageBytes()+p.endpoint->initial()->storageBytes());
      if(role==2){instance->active=true;for(const auto &a:native.signal.assignments)if(a.target==bus.id){instance->amount=a.amount;instance->wet=a.wet;}}
      if(activity)for(auto &p:instance->processors){
        ParameterProcessor observed;observed.graph=id;observed.node=p.id;observed.target=bus.id;observed.role=role;
        observed.name=bus.name+" · "+definition->name+" · ";
        auto n=std::find_if(definition->nodes.begin(),definition->nodes.end(),[&](const auto &n){return n.id==p.id;});observed.name+=n->name;
        for(const auto &source:sampleSources)if(source.target==bus.id){observed.instrument=source.instrumentID;observed.channel=source.channel;observed.target=0;observed.name="Instrument "+std::to_string(source.instrumentID)+(source.channel==UINT16_MAX?" · Inspector":" · Channel "+std::to_string(source.channel+1))+" · "+definition->name+" · "+n->name;}
        observed.key="graph/"+std::to_string(observed.graph)+"/"+std::to_string(observed.node)+"/"+std::to_string(observed.target)+"/"+std::to_string(role)+"/"+std::to_string(observed.instrument)+"/"+std::to_string(observed.channel);
        observed.parameters=p.endpoint->initial()->parameters;
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
    budget(prepared->outputPorts.size()*sizeof(std::array<float,8192>));prepared->outputBuffers.resize(prepared->outputPorts.size());
    const size_t delaySamples=size_t(prepared->reserved)*2+2;
    for(auto &instance:prepared->instances)for(auto port:prepared->outputPorts)if(instance->runtime->output(port)){
      budget(delaySamples*sizeof(float));instance->auxiliary.push_back({port,std::vector<float>(delaySamples),0});
    }
    prepared->row.reserve(required.size());prepared->persistent.reserve(required.size());
    for(const auto &c:native.signal.commands)if(c.target==bus.id){auto p=std::find_if(native.patterns.begin(),native.patterns.end(),[&](const auto &p){return p.second.id==c.pattern;});prepared->patterns[p->first].push_back(c);}
    for(auto &[pattern,events]:prepared->patterns)std::sort(events.begin(),events.end(),[](const auto &a,const auto &b){return std::tie(a.position,a.column)<std::tie(b.position,b.column);});
    buses_.push_back(std::move(prepared));
  }
  storageBytes_=delayBytes;processors_=processors;
}
NativeSignalGraph::~NativeSignalGraph()=default;
std::vector<SignalActivity> NativeSignalGraph::activity() const {
  std::vector<SignalActivity> result;
  for(const auto &bus:buses_)for(const auto &instance:bus->instances){const auto state=instance->published.load(std::memory_order_relaxed);if(state)result.push_back({bus->id,instance->graph,instance->role,uint16_t(state&0xffff),bool(state&0x10000)});}
  return result;
}
void NativeSignalGraph::prepareParameters(const SignalGraph &next,GraphControlPlan &plan,const GraphControlPlan *previous) const {
  auto reserve=[&](size_t bytes){if(bytes>plan.preparationHeadroom)throw std::invalid_argument("Live graph controls exceed the 256 MB prepared storage budget");plan.preparationHeadroom-=bytes;};
  for(const auto &b:buses_)for(const auto &instance:b->instances){
    const auto d=std::find_if(next.library.begin(),next.library.end(),[&](const auto &d){return d.id==instance->graph;});
    if(d==next.library.end())throw std::invalid_argument("Playing subgraph no longer exists");
    const SignalControls *controls=nullptr;
    for(const auto &c:plan.controls)if(c->definition.id==d->id){controls=c.get();break;}
    if(!controls){auto c=std::make_shared<SignalControls>(*d,rate_);controls=c.get();plan.controls.push_back(std::move(c));}
    auto runtime=instance->initialRuntime;
    if(previous)for(const auto &owner:previous->runtimeOwners)if(owner.target==&instance->runtime){runtime=owner.state;break;}
    std::vector<SignalProcessorInfo> processorInfo;std::vector<SignalParameterInfo> parameterInfo;
    const size_t updateStart=plan.updates.size();
    double tail=0;
    for(const auto &p:instance->processors){
      const auto n=std::find_if(d->nodes.begin(),d->nodes.end(),[&](const auto &n){return n.id==p.id;});
      if(n==d->nodes.end())throw std::invalid_argument("Playing graph processor no longer exists");
      auto state=p.endpoint->initial();std::shared_ptr<GraphPluginState> retained;
      if(previous)for(const auto &preset:previous->presets)if(preset.endpoint==p.endpoint.get()){state=preset.state;retained=preset.previous;break;}
      if(state->recipe.state!=n->plugin.state){
        if(!p.endpoint->ready(state.get()))throw std::runtime_error("A graph preset is still fading; retry the edit shortly");
        auto replacement=std::make_shared<GraphPluginState>(*d,*n,rate_,offline_);reserve(replacement->storageBytes());
        const auto sameParameter=[](const PluginParameter &a,const PluginParameter &b){return a.id==b.id&&a.name==b.name&&a.min==b.min&&a.max==b.max&&a.unit==b.unit&&a.unitLabel==b.unitLabel&&a.choices==b.choices&&a.logarithmic==b.logarithmic&&a.step==b.step&&a.writable==b.writable&&a.continuous==b.continuous;};
        const auto &beforeBuses=state->plugin->buses(),&afterBuses=replacement->plugin->buses();
        const bool sameBuses=beforeBuses.size()==afterBuses.size()&&std::equal(beforeBuses.begin(),beforeBuses.end(),afterBuses.begin(),[](const auto &a,const auto &b){return a.index==b.index&&a.channels==b.channels&&a.input==b.input&&a.active==b.active&&a.supported==b.supported;});
        if(!sameBuses||replacement->inputs!=state->inputs||replacement->outputs!=state->outputs||replacement->latency!=state->latency||replacement->parameters.size()!=state->parameters.size()||!std::equal(state->parameters.begin(),state->parameters.end(),replacement->parameters.begin(),sameParameter))
          throw std::invalid_argument("Live graph presets must preserve plugin ports, latency and parameter identities");
        p.endpoint->observePrepared(*replacement);retained=state;state=std::move(replacement);
        for(const auto &parameter:state->parameters)plan.readings.push_back({state->plugin.get(),parameter.id,parameter.value});
      }
      plan.presets.push_back({p.endpoint.get(),state,retained});
      plan.bypasses.emplace_back(p.endpoint.get(),n->plugin.bypass);
      tail=std::min(mixerMaximumTailSeconds,tail+std::max(0.,state->plugin->tail()));
      processorInfo.push_back({p.id,state->latency,state->inputs,state->outputs});
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
        if(previous)for(const auto &range:previous->ranges)if(range.plugin==vendor->plugin.get())ranges.insert(range.parameter);
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
      if(previous)for(const auto &range:previous->ranges)if(range.plugin==state->plugin.get())formerlyModulated.insert(range.parameter);
      std::set<uint32_t> touched=modulated;touched.insert(formerlyModulated.begin(),formerlyModulated.end());
      for(const auto &[id,value]:n->plugin.parameters)touched.insert(id);
      if(previous)for(const auto &v:previous->updates)if(v.endpoint==p.endpoint.get())touched.insert(v.parameter);
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
    if(!runtime->sameLayout(*d)){
      auto compiled=compileSignal(*d,processorInfo);
      const auto inputNode=std::find_if(d->nodes.begin(),d->nodes.end(),[](const auto &n){return n.kind==SignalNodeKind::Input;});
      for(const auto &edge:d->audio)if(edge.source==inputNode->id&&edge.output&&!(b->inputMask&(uint64_t(1)<<edge.output)))throw std::invalid_argument("A live source cannot enable an unprepared external graph input");
      auto replacement=std::make_shared<SignalRuntime>(*d,std::move(compiled),rate_,parameterInfo);reserve(replacement->storageBytes());
      if(!replacement->compatibleHistory(*runtime))throw std::invalid_argument("Live graph source edits must preserve audio latency and compensation");
      runtime=std::move(replacement);
    }
    for(size_t i=updateStart;i<plan.updates.size();++i)if(plan.updates[i].runtime)plan.updates[i].runtime=runtime.get();
    plan.runtimeOwners.push_back({&instance->runtime,instance->initialRuntime,runtime});
    plan.runtimes.emplace_back(runtime.get(),controls);
    plan.tails.emplace_back(&instance->tailFrames,uint64_t(std::ceil(tail*rate_))+runtime->latency());
  }
  for(const auto &bus:buses_){uint64_t frames=0;for(const auto &instance:bus->instances)for(const auto &[target,value]:plan.tails)if(target==&instance->tailFrames){frames+=value;break;}
    plan.graphTails.emplace_back(signalBusIdentity(bus->id),std::min(mixerMaximumTailSeconds,double(frames)/rate_));}
}
uint64_t NativeSignalGraph::tailFrames(size_t index) const noexcept {
  if(index>=buses_.size())return 0;uint64_t total=0;for(const auto &instance:buses_[index]->instances)total+=instance->currentTailFrames();
  return std::min(total,uint64_t(std::ceil(mixerMaximumTailSeconds*rate_)));
}
// Growth is conservative across parallel copies but bounded; it prevents a
// live comb/filter edit from truncating stored energy before a new plan exists.
double NativeSignalGraph::tailGrowth() const noexcept {double growth=0;for(const auto &b:buses_)for(const auto &i:b->instances)for(const auto &p:i->processors)growth+=p.endpoint->tailGrowth();return std::min(60.,growth);}
uint64_t NativeSignalGraph::tailRevision() const noexcept {uint64_t revision=0;for(const auto &b:buses_)for(const auto &i:b->instances)for(const auto &p:i->processors)revision+=p.endpoint->tailRevision();return revision;}
bool NativeSignalGraph::latencyChangePending() const noexcept {
  for (const auto &b : buses_) for (const auto &i : b->instances)
    for (const auto &p : i->processors) if (p.endpoint->latencyChangePending()) return true;
  return false;
}
void NativeSignalGraph::refreshLatencies(std::vector<MixerProcessorInfo> &mixerProcessors) {
  for (auto &b : buses_) {
    const auto previousReserved = b->reserved;
    b->reserved = 0; b->tailSeconds = 0;
    for (auto &i : b->instances) {
      std::vector<SignalProcessorInfo> info;
      double tail = 0;
      const auto oldBypass=i->bypassStorage(),oldInitialProcessors=i->initialProcessorStorage();
      for (auto &p : i->processors) {
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
}
void NativeSignalGraph::compile(MixerGraph &mixer,std::vector<MixerProcessorInfo> &processors){
  mixer=routedMixer_;
  for(const auto &bus:buses_){const auto &b=*bus;uint32_t count=1;uint64_t mask=1;
    for(auto port:b.outputPorts){count=std::max(count,port+1);mask|=uint64_t(1)<<port;}
    processors.push_back({signalBusIdentity(b.id),b.reserved,b.tailSeconds,false,false,count,mask,b.inputMask});
  }
}
bool NativeSignalGraph::sameNoteMembership(const NativeSong &native) const {
  for(const auto &bus:buses_) {
    // Instrument-copy membership follows its original voice/channel, not the
    // destination mixer path. rawChannels is set only for bus note envelopes.
    if(!bus->rawChannels || bus->instrument)continue;
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
  if(index>=buses_.size())return {};return buses_[index]->outputPorts;
}
const float *NativeSignalGraph::output(size_t index,uint32_t port) const noexcept {
  if(index>=buses_.size())return nullptr;const auto &b=*buses_[index];
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
  for(uint32_t cc=0;cc<128;++cc){auto value=controllers_[cc].load(std::memory_order_relaxed);if(value!=appliedControllers_[cc]){appliedControllers_[cc]=value;for(auto &b:buses_)for(auto &i:b->instances)i->runtime->controller(cc,value/127.);}}
  for(auto &b:buses_){bool gate=false,retrigger=false;
    for(auto &watch:b->channels)if(watch.index<state.Chn.size()){
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
    if(entering){auto found=b->patterns.find(uint16_t(pattern_));b->events=found==b->patterns.end()?nullptr:&found->second;b->next=0;
      if(b->events)while(b->next<b->events->size()&&(*b->events)[b->next].position<at-1e-8)++b->next;}
    // Note gates are distinct from amplitude followers and aggregate group members.
  }
}
void NativeSignalGraph::tail()noexcept {for(auto &b:buses_){b->commands=false;b->transport.playing=false;b->clock.playing=false;b->clock.unitsPerFrame=0;for(auto &i:b->instances)i->runtime->note(false);}}
bool NativeSignalGraph::process(size_t index,float *audio,uint32_t frames,uint64_t position,std::span<const MixerAudioInput> inputs)noexcept {return index<buses_.size()&&frames<=4096&&buses_[index]->render(audio,frames,position,inputs);}
} // namespace Tracker
