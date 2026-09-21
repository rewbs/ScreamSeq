#include "NativeSignalGraph.hpp"
#include <cmath>
#include <set>
#include <stdexcept>

namespace Tracker {
struct NativeSignalGraph::Instance {
  uint64_t graph=0;
  uint8_t role=0; // row, persistent, ordinary: separate processor histories
  bool active=false;
  std::atomic<uint32_t> published{0};
  double amount=1,wet=1,rate=48000;
  uint64_t tailRemaining=0,tailFrames=0;
  std::unique_ptr<SignalRuntime> runtime;
  struct Processor {uint64_t id;std::shared_ptr<NativePlugin> plugin;std::vector<PluginParameter> parameters;};
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
  explicit Instance(const SignalDefinition &d,uint8_t role,double sampleRate,bool offline):graph(d.id),role(role),rate(sampleRate) {
    std::vector<SignalProcessorInfo> info;
    double tail=0;
    for(const auto &n:d.nodes)if(n.kind==SignalNodeKind::Plugin){const auto &r=n.plugin;
      PluginState state;state.descriptor={r.type,r.subtype,r.manufacturer,r.name,r.format,r.path,r.classID,false};state.state=r.state;state.instanceID="graph-"+std::to_string(d.id)+"-"+std::to_string(n.id);state.auxiliaryInputs=r.inputs;state.auxiliaryOutputs=r.outputs;
      auto plugin=std::make_shared<NativePlugin>(state,sampleRate,offline);
      if(plugin->isInstrument())throw std::invalid_argument("Subgraphs accept effect plugins; route instrument outputs into a mixer bus");
      uint64_t inputs=1,outputs=1;for(const auto &b:plugin->buses())if(b.active&&b.supported&&b.index<64)(b.input?inputs:outputs)|=uint64_t(1)<<b.index;
      info.push_back({n.id,uint32_t(std::llround(plugin->latency()*sampleRate)),inputs,outputs});
      auto parameters=plugin->parameters();
      for(const auto &m:d.modulation)if(m.target==n.id&&m.enabled){auto p=std::find_if(parameters.begin(),parameters.end(),[&](const auto &p){return p.id==m.parameter;});
        if(p==parameters.end()||!p->writable||!p->continuous||p->max<=p->min)throw std::invalid_argument("Modulation requires a writable continuous plugin parameter");
        plugin->prepareMusicalAutomation();plugin->includeParameterRange(m.parameter,p->min,p->max);
      }
      tail=std::min(120.,tail+std::max(0.,plugin->tail()));processors.push_back({n.id,std::move(plugin),std::move(parameters)});
    }
    auto plan=compileSignal(d,info);dryDelay.resize(size_t(plan.totalLatency)*2);tailFrames=uint64_t(std::ceil(tail*sampleRate))+plan.totalLatency;runtime=std::make_unique<SignalRuntime>(d,std::move(plan),sampleRate);
  }
  Processor *processor(uint64_t id)noexcept{for(auto &p:processors)if(p.id==id)return &p;return nullptr;}
  bool render(float *buffer,uint32_t frames,uint64_t position,PluginTransport transport,SignalClock clock,std::span<const MixerAudioInput> inputs)noexcept {
    runtime->amount(amount);for(auto &p:processors)p.plugin->transport(transport);
    for(uint32_t i=0;i<frames*2;++i){if(dryDelay.empty())dry[i]=buffer[i];else {dry[i]=dryDelay[dryPosition];dryDelay[dryPosition]=buffer[i];if(++dryPosition==dryDelay.size())dryPosition=0;}}
    // Bypass compensation follows the real channel input even while the
    // processor advances on silence. Switching off therefore reveals dry audio
    // at the same fixed time, without replaying an old wet padding buffer.
    if(!active)std::fill_n(buffer,frames*2,0.f);
    SignalCallbacks callbacks;callbacks.context=this;
    callbacks.process=[](void *ctx,uint64_t id,float *buffer,uint32_t frames,uint64_t position,std::span<const MixerAudioInput> inputs)noexcept{auto p=static_cast<Instance *>(ctx)->processor(id);return p&&p->plugin->process(buffer,frames,position,inputs);};
    callbacks.output=[](void *ctx,uint64_t id,uint32_t bus)noexcept->const float *{auto p=static_cast<Instance *>(ctx)->processor(id);return p?p->plugin->auxiliaryOutput(bus):nullptr;};
    callbacks.parameter=[](void *ctx,uint64_t id,uint32_t parameter,double a,double b,uint64_t position,uint32_t duration)noexcept{
      auto p=static_cast<Instance *>(ctx)->processor(id);if(!p)return false;
      auto range=std::find_if(p->parameters.begin(),p->parameters.end(),[&](const auto &v){return v.id==parameter;});if(range==p->parameters.end())return false;
      return p->plugin->scheduleRamp(parameter,range->min+(range->max-range->min)*a,range->min+(range->max-range->min)*b,position,duration);
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
  void stop(size_t i,bool tails)noexcept{auto &v=*instances[i];v.active=false;v.tailRemaining=tails?v.tailFrames:0;}
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
NativeSignalGraph::NativeSignalGraph(const NativeSong &native,double rate,bool offline,std::span<const SignalSampleSource> sampleSources,size_t storageLimit,size_t processorLimit):rate_(rate),routedMixer_(signalRoutingGraph(native.mixer,native.signal)){
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
    for(const auto &source:sampleSources)if(needsNotes&&source.target==bus.id){prepared->instrument=source.instrument;prepared->sampleChannel=source.channel;prepared->channels.clear();prepared->channels.push_back({source.channel,0});for(uint16_t i=source.channels;i<OpenMPT::MAX_CHANNELS;++i)prepared->channels.push_back({i,0});}
    budget(sizeof(Bus)+prepared->channels.size()*sizeof(Bus::NoteWatch));
    for(auto [id,role]:required){auto definition=std::find_if(native.signal.library.begin(),native.signal.library.end(),[&](const auto &d){return d.id==id;});if(definition==native.signal.library.end())throw std::invalid_argument("Unresolved subgraph assignment");
      processors+=std::count_if(definition->nodes.begin(),definition->nodes.end(),[](const auto &n){return n.kind==SignalNodeKind::Plugin;});if(processors>processorLimit)throw std::invalid_argument("Active song graph exceeds 256 prepared plugin copies");
      for(const auto &edge:definition->audio){
        auto node=std::find_if(definition->nodes.begin(),definition->nodes.end(),[&](const auto &n){return n.id==edge.source;});
        if(node!=definition->nodes.end()&&node->kind==SignalNodeKind::Input&&edge.output)prepared->inputMask|=uint64_t(1)<<edge.output;
      }
      auto instance=std::make_unique<Instance>(*definition,role,rate,offline);budget(sizeof(Instance)+instance->dryDelay.size()*sizeof(float)+instance->runtime->storageBytes());prepared->reserved+=instance->runtime->latency();prepared->tailSeconds=std::min(120.,prepared->tailSeconds+instance->tailFrames/rate);
      if(role==2){instance->active=true;for(const auto &a:native.signal.assignments)if(a.target==bus.id){instance->amount=a.amount;instance->wet=a.wet;}}
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
bool NativeSignalGraph::latencyChangePending() const noexcept {
  for (const auto &b : buses_) for (const auto &i : b->instances)
    for (const auto &p : i->processors) if (p.plugin->latencyChangePending()) return true;
  return false;
}
void NativeSignalGraph::refreshLatencies(std::vector<MixerProcessorInfo> &mixerProcessors) {
  for (auto &b : buses_) {
    const auto previousReserved = b->reserved;
    b->reserved = 0; b->tailSeconds = 0;
    for (auto &i : b->instances) {
      std::vector<SignalProcessorInfo> info;
      double tail = 0;
      for (auto &p : i->processors) {
        p.plugin->refreshLatency();
        uint64_t inputs = 1, outputs = 1;
        for (const auto &bus : p.plugin->buses()) if (bus.active && bus.supported && bus.index < 64)
          (bus.input ? inputs : outputs) |= uint64_t(1) << bus.index;
        info.push_back({p.id, uint32_t(std::llround(p.plugin->latency() * rate_)), inputs, outputs});
        tail = std::min(120., tail + std::max(0., p.plugin->tail()));
      }
      auto plan = compileSignal(i->runtime->definition(), info);
      const auto oldStorage = i->runtime->storageBytes() + i->dryDelay.size() * sizeof(float);
      if (plan.totalLatency != i->runtime->latency()) {
        i->dryDelay.assign(size_t(plan.totalLatency) * 2, 0); i->dryPosition = 0;
      }
      i->runtime->updateLatencyPlan(std::move(plan));
      storageBytes_ = storageBytes_ - oldStorage + i->runtime->storageBytes() + i->dryDelay.size() * sizeof(float);
      i->tailFrames = uint64_t(std::ceil(tail * rate_)) + i->runtime->latency();
      b->reserved += i->runtime->latency();
      b->tailSeconds = std::min(120., b->tailSeconds + i->tailFrames / rate_);
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
      if(b->instrument&&(channel.pModInstrument!=b->instrument||(channel.nMasterChn?channel.nMasterChn-1:watch.index)!=b->sampleChannel))continue;
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
