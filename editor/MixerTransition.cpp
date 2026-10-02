#include "MixerTransition.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>

namespace Tracker {
namespace {
std::atomic<uint64_t> nextOwner{1};
void require(bool okay,const char *message){if(!okay)throw std::invalid_argument(message);}
}
MixerTransition::MixerTransition(std::unique_ptr<Plan> initial,std::vector<uint64_t> sources,
    std::vector<uint64_t> tracks,uint32_t rate,size_t limit)
  : sources_(std::move(sources)),tracks_(std::move(tracks)),rate_(rate),
    fadeFrames_(std::max(1u,uint32_t(std::llround(rate*.01)))),owner_(nextOwner.fetch_add(1)),
    storageLimit_(limit),plans_(std::move(initial)) {
  require(rate>=8000 && rate<=384000,"Invalid live mixer sample rate");
  auto &plan=plans_.current();
  require(plan.runtime && plan.process && plan.processors,"Live mixer requires prepared processors");
  require(sources_.size()<=MixerRuntime::maximumBuses,"Too many live mixer sources");
  auto unique=sources_;std::sort(unique.begin(),unique.end());
  require(std::adjacent_find(unique.begin(),unique.end())==unique.end(),"Duplicate live mixer source");
  plan.directSources.clear();
  for(const auto &bus:plan.runtime->graph().buses) {
    const auto found=std::find(sources_.begin(),sources_.end(),bus.id);
    plan.directSources.push_back(found==sources_.end()?SIZE_MAX:size_t(found-sources_.begin()));
  }
  sourceSlots_=plan.catalog.size();
  for(size_t i=0;i<plan.catalog.size();++i)if(plan.catalog[i].instrument)
    instrumentSources_.emplace_back(i,plan.catalog[i]);
  prepareSources(plan);
  if(!plan.dependencies.empty() || !plan.runtime->plan().detached.empty() || plan.runtime->plan().segmented)plan.execution=prepareMorph(plan,plan,true);
  require(storage(plan)<=storageLimit_,"Live mixer exceeds the audio storage budget");
  stable_=&plan;through_=plan.runtime->through();plan.owner=owner_;
}
size_t MixerTransition::storage(const Plan &plan) const noexcept {
  // Conservative double-counting of shared PDC/DSP ownership bounds the whole
  // transition, rather than accepting two individually legal oversized plans.
  const auto runtime=sizeof(Plan)+plan.runtime->storageBytes()+plan.sourceProcessors.capacity()*sizeof(size_t)+(plan.morph?plan.morph->storageBytes():0)+(plan.execution?plan.execution->storageBytes():0)+(plan.bridge?plan.bridge->storageBytes():0)+plan.dependencies.capacity()*sizeof(Dependency);
  return plan.processorStorage>SIZE_MAX-runtime?SIZE_MAX:runtime+plan.processorStorage;
}
size_t MixerTransition::InputMorph::storageBytes() const noexcept {
  return sizeof(*this)+steps.capacity()*sizeof(Step)+inputs.capacity()*sizeof(MixerAudioInput)+
    auxiliary.capacity()*sizeof(auxiliary[0])+auxiliary.size()*sizeof(*auxiliary[0])+
    autoFallback.capacity()*sizeof(autoFallback[0])+std::count_if(autoFallback.begin(),autoFallback.end(),[](const auto &p){return bool(p);})*sizeof(*auxiliary[0]);
}
std::unique_ptr<MixerTransition::InputMorph> MixerTransition::prepareMorph(const Plan &before,const Plan &after,bool single) {
  using Location=InputMorph::Location;using Step=InputMorph::Step;
  std::vector<Step> nodes;
  std::vector<std::set<size_t>> edges;
  std::map<std::string,size_t> processors;
  std::array<std::vector<size_t>,2> starts,ends,processorSteps;
  auto add=[&](Step step){const auto id=nodes.size();nodes.push_back(step);edges.emplace_back();return id;};
  for(unsigned side=0;side<(single?1u:2u);++side){
    const auto &p=side?after:before;const auto &plan=p.runtime->plan();
    processorSteps[side].assign(p.catalog.size(),SIZE_MAX);
    for(size_t bus=0;bus<plan.nodes.size();++bus){
      const auto input=add({0,side?Location{}:Location{bus},side?Location{bus}:Location{}});
      starts[side].push_back(input);size_t previous=input;
      for(auto processor:plan.nodes[bus].processors){
        const auto &info=p.catalog[processor];auto found=processors.find(info.instance);size_t step;
        if(found==processors.end()) {step=add({1});processors.emplace(info.instance,step);}
        else step=found->second;
        auto &location=side?nodes[step].after:nodes[step].before;
        require(location.processor==SIZE_MAX,"A morph processor has multiple owners");location={bus,processor};
        processorSteps[side][processor]=step;
        if(!plan.segmented||std::find(plan.disconnectedMainInputs.begin(),plan.disconnectedMainInputs.end(),processor)==plan.disconnectedMainInputs.end())edges[previous].insert(step);
        previous=step;
      }
      const auto output=add({2,side?Location{}:Location{bus},side?Location{bus}:Location{}});
      ends[side].push_back(output);edges[previous].insert(output);
      if(plan.segmented){edges[input].insert(output);for(auto processor:plan.nodes[bus].processors)edges[processorSteps[side][processor]].insert(output);}
    }
    for(auto processor:plan.detached){
      const auto &info=p.catalog[processor];auto found=processors.find(info.instance);size_t step;
      if(found==processors.end()){step=add({1});processors.emplace(info.instance,step);}else step=found->second;
      auto &location=side?nodes[step].after:nodes[step].before;
      require(location.processor==SIZE_MAX,"A morph processor has multiple owners");
      location={SIZE_MAX,processor};processorSteps[side][processor]=step;
    }
    for(const auto &edge:plan.connections)edges[ends[side][edge.source]].insert(starts[side][edge.target]);
    for(const auto &edge:plan.sidechains)edges[ends[side][edge.source]].insert(processorSteps[side][edge.processor]);
    for(const auto &edge:plan.instruments)if(edge.owner!=SIZE_MAX)
      edges[processorSteps[side][edge.processor]].insert(starts[side][edge.target]);
    for(const auto &edge:plan.pluginConnections)if(!p.catalog[edge.source].instrument)edges[processorSteps[side][edge.source]].insert(processorSteps[side][edge.target]);
    for(const auto &edge:p.dependencies){
      const size_t target=edge.targetBus!=SIZE_MAX?(edge.targetBus<ends[side].size()?ends[side][edge.targetBus]:SIZE_MAX):(edge.target<processorSteps[side].size()?processorSteps[side][edge.target]:SIZE_MAX);
      require(target!=SIZE_MAX,"Group/modulation target is not a scheduled audio endpoint");
      size_t source=SIZE_MAX;
      if(edge.processor!=SIZE_MAX){require(edge.processor<processorSteps[side].size(),"Invalid follower processor dependency");source=processorSteps[side][edge.processor];}
      else {require(edge.bus<ends[side].size(),"Invalid follower bus dependency");source=ends[side][edge.bus];}
      require(source!=SIZE_MAX,"Follower source is not a scheduled effect");
      // A self-dependency is real feedback (for example a follower targeting
      // the processor whose output it reads), not a redundant ordering edge.
      edges[source].insert(target);
    }
  }
  auto prefix=[](const Plan &p,Location location){
    if(location.bus==SIZE_MAX)return uint64_t(0);
    if(p.runtime->plan().segmented)return uint64_t(p.runtime->plan().processors[location.processor].inputLatency);
    uint64_t frames=p.runtime->plan().nodes[location.bus].inputLatency;
    for(auto processor:p.runtime->plan().nodes[location.bus].processors){if(processor==location.processor)return frames;frames+=p.catalog[processor].latency;}
    return UINT64_MAX;
  };
  uint64_t ports=0,fallbackPorts=0;
  for(const auto &node:nodes)if(node.kind==1 && node.before.processor!=SIZE_MAX && node.after.processor!=SIZE_MAX){
    const auto &a=before.catalog[node.before.processor],&b=after.catalog[node.after.processor];
    require(a.instrument==b.instrument && a.latency==b.latency && a.activeInputs==b.activeInputs && a.activeOutputs==b.activeOutputs && a.outputBuses==b.outputBuses && a.mainInputFallback==b.mainInputFallback,
      "Live routing must retain each processor's latency and active ports");
    require(prefix(before,node.before)==prefix(after,node.after),"Live routing changes a retained processor's input latency");
    ports|=a.activeInputs;fallbackPorts|=a.mainInputFallback;
  }
  auto result=std::make_unique<InputMorph>();
  std::vector<size_t> pending(nodes.size());for(const auto &targets:edges)for(auto target:targets){require(target<nodes.size(),"Invalid morph dependency");++pending[target];}
  std::set<size_t> ready;for(size_t i=0;i<nodes.size();++i)if(!pending[i])ready.insert(i);
  while(!ready.empty()){auto i=*ready.begin();ready.erase(ready.begin());result->steps.push_back(nodes[i]);for(auto target:edges[i])if(!--pending[target])ready.insert(target);}
  require(result->steps.size()==nodes.size(),"The old and new routing orders form a live transition cycle");
  for(uint32_t port=1;port<64;++port)if(ports&(uint64_t(1)<<port)){
    result->auxiliary.push_back(std::make_unique<std::array<float,MixerRuntime::maximumFrames*2>>());
    result->autoFallback.push_back(fallbackPorts&(uint64_t(1)<<port)?std::make_unique<std::array<float,MixerRuntime::maximumFrames*2>>():nullptr);
    result->inputs.push_back({port,result->auxiliary.back()->data()});
  }
  return result;
}
void MixerTransition::prepareDependencies(Plan &plan) {
  plan.execution=plan.dependencies.empty() && plan.runtime->plan().detached.empty() && !plan.runtime->plan().segmented?nullptr:prepareMorph(plan,plan,true);
  if(plan.requiresAudioHandoff||plan.bridge)prepareBridge(plan);
  else if(plan.morph)try {plan.morph=prepareMorph(*stable_,plan);}catch(const std::invalid_argument &){prepareBridge(plan);}
}
std::unique_ptr<MixerTransition::Plan> MixerTransition::prepareRetained(MixerGraph graph,std::vector<MixerProcessorInfo> catalog) {
  auto result=prepare(std::move(graph),std::move(catalog),{},true);
  if(result->runtime->plan().latency!=stable_->runtime->plan().latency||result->runtime->plan().segmented||stable_->runtime->plan().segmented)prepareBridge(*result);
  else try {result->morph=prepareMorph(*stable_,*result);}catch(const std::invalid_argument &){prepareBridge(*result);}
  require(storage(*result)<=storageLimit_-storage(*stable_),"Combined routing morph exceeds the audio storage budget");
  return result;
}
float MixerTransition::amount(uint32_t frame) const noexcept {
  if(frame<warmup_)return 0;
  return float(std::min<uint64_t>(fade_+frame-warmup_,fadeFrames_))/fadeFrames_;
}
void MixerTransition::prepareSources(Plan &plan,bool allowChanges) const {
  plan.sourceProcessors.assign(sourceSlots_,SIZE_MAX);
  if(!allowChanges)for(const auto &p:plan.catalog)if(p.instrument)
    require(std::any_of(instrumentSources_.begin(),instrumentSources_.end(),[&](const auto &source){return source.second.instance==p.instance;}),"A new source requires an explicit audio handoff");
  for(const auto &[slot,source]:instrumentSources_) {
    const auto found=std::find_if(plan.catalog.begin(),plan.catalog.end(),[&](const auto &p){return p.instance==source.instance;});
    require(allowChanges||found!=plan.catalog.end(),"Removing a source requires an explicit audio handoff");
    if(found==plan.catalog.end())continue; // Retired plugin endpoints use the per-plan source renderer.
    require(found->instrument,"A routing transition must retain source kind");
    require(allowChanges||(found->latency==source.latency&&found->outputBuses==source.outputBuses&&found->activeInputs==source.activeInputs&&found->activeOutputs==source.activeOutputs),"Source replacement requires an explicit audio handoff");
    plan.sourceProcessors[slot]=size_t(found-plan.catalog.begin());
  }
}
void MixerTransition::settle() noexcept {
  if(submitted_ && settled_.load(std::memory_order_acquire)==serial_) {
    if(failedRevision_.load(std::memory_order_acquire)!=serial_)stable_=submitted_;
    submitted_=nullptr;
  }
}
std::unique_ptr<MixerTransition::Plan> MixerTransition::prepare(MixerGraph graph,
    std::vector<MixerProcessorInfo> catalog,const std::vector<std::string> &reset,bool allowHandoff) {
  require(ready(),"A routing transition is still preparing");
  for(const auto &[slot,source]:instrumentSources_)
    require(std::find(reset.begin(),reset.end(),source.instance)==reset.end(),"A source reset requires a prepared adapter handoff");
  auto compiled=compileMixer(graph,tracks_,catalog,rate_);
  require(allowHandoff||compiled.latency==stable_->runtime->plan().latency,"A latency change requires a latency-aligned host transition");
  auto result=std::make_unique<Plan>();result->owner=owner_;result->sourceRevision=plans_.renderedRevision();
  result->reuse=mixerTransitionReuse(stable_->runtime->graph(),stable_->runtime->plan(),stable_->catalog,
    graph,compiled,catalog,reset);
  result->catalog=std::move(catalog);
  prepareSources(*result,allowHandoff);
  result->runtime=std::make_unique<MixerRuntime>(std::move(graph),std::move(compiled),rate_);
  result->runtime->retainHistory(*stable_->runtime,result->reuse);
  if(!result->runtime->plan().detached.empty()||result->runtime->plan().segmented)result->execution=prepareMorph(*result,*result,true);
  for(const auto &bus:result->runtime->graph().buses) {
    const auto found=std::find(sources_.begin(),sources_.end(),bus.id);
    result->directSources.push_back(found==sources_.end()?SIZE_MAX:size_t(found-sources_.begin()));
  }
  require(withinBudget() && storage(*result)<=storageLimit_-storage(*stable_),"Combined routing plans exceed the audio storage budget");
  return result;
}
bool MixerTransition::accepts(const Plan &plan) noexcept {
  return ready() && plan.owner==owner_ && plan.sourceRevision==plans_.renderedRevision() &&
    plan.runtime && plan.processors && plan.process && serial_!=UINT64_MAX && withinBudget() &&
    storage(plan)<=storageLimit_-storage(*stable_);
}
bool MixerTransition::publish(std::unique_ptr<Plan> &plan) noexcept {
  if(!plan || !accepts(*plan))return false;
  auto *candidate=plan.get();
  const auto previousStatus=status_.load(std::memory_order_acquire);
  status_.store(Status::Preparing,std::memory_order_release);
  if(!plans_.publish(plan,serial_+1)){status_.store(previousStatus,std::memory_order_release);return false;}
  submitted_=candidate;++serial_;return true;
}
void MixerTransition::reject() noexcept {
  const auto revision=plans_.currentRevision();
  // The outgoing controls already advanced alongside the candidate. Restoring
  // them must not overwrite their envelopes with a failed candidate's state.
  if(auto *previous=plans_.previous();previous && previous->adopt)previous->adopt(previous->processors.get(),nullptr);
  plans_.cancel();failedRevision_.store(revision,std::memory_order_release);
  status_.store(Status::Failed,std::memory_order_release);settled_.store(revision,std::memory_order_release);
}
bool MixerTransition::controls(const std::vector<MixerControls> &values) noexcept {
  settle();
  std::array<MixerRuntime *,4> targets{stable_->runtime.get(),nullptr,nullptr,nullptr};size_t count=1;
  if(submitted_) {
    targets[count++]=submitted_->runtime.get();
    if(submitted_->bridge){targets[count++]=submitted_->bridge->before.plan->runtime.get();targets[count++]=submitted_->bridge->after.plan->runtime.get();}
  }
  // Shadows receive the same accepted gesture as the wet plans. Otherwise a
  // gain/mute gesture during a dry bridge could jump back to its old value.
  const auto &before=stable_->runtime->graph().buses;
  for(size_t n=0;n<count;++n){const auto &after=targets[n]->graph().buses;
    if(!targets[n]->canQueueControls()||before.size()!=after.size())return false;
    for(size_t i=0;i<before.size();++i)if(before[i].id!=after[i].id)return false;
  }
  // The first queue validates all values; equal cardinality and preflighted
  // capacity make every subsequent single-producer publication infallible.
  if(!targets[0]->controls(values))return false;
  for(size_t n=1;n<count;++n)if(!targets[n]->controls(values))return false;
  return true;
}
void MixerTransition::refreshStopped(std::vector<MixerProcessorInfo> catalog) {
  require(commitStopped(),"Cannot refresh latency while a routing publication is pending");
  require(catalog.size()==stable_->catalog.size(),"Latency refresh cannot change processor identity");
  for(size_t i=0;i<catalog.size();++i)require(catalog[i].instance==stable_->catalog[i].instance,"Latency refresh cannot reorder processors");
  auto plan=compileMixer(stable_->runtime->graph(),tracks_,catalog,rate_);
  stable_->runtime->updateLatencyPlan(std::move(plan));stable_->catalog=std::move(catalog);
  for(auto &[slot,info]:instrumentSources_)for(const auto &current:stable_->catalog)if(current.instance==info.instance){info=current;break;}
  stable_->execution=stable_->dependencies.empty()&&stable_->runtime->plan().detached.empty()?nullptr:prepareMorph(*stable_,*stable_,true);
  require(withinBudget(),"Updated mixer latency exceeds the combined audio storage budget");
}
bool MixerTransition::commitStopped() noexcept {
  if(open_)return false;
  const bool next=plans_.begin();
  if(next && !plans_.current().runtime->activateHistory(true)){reject();settle();return false;}
  if(next && plans_.current().adopt)plans_.current().adopt(plans_.current().processors.get(),plans_.previous()?plans_.previous()->processors.get():nullptr);
  if(next && plans_.current().activateAudio)plans_.current().activateAudio(plans_.current().processors.get(),plans_.previous()?plans_.previous()->processors.get():nullptr,through_);
  if(auto &current=plans_.current();current.bridge&&plans_.previous()){
    if(!next&&!current.bridge->activated&&current.activateAudio)current.activateAudio(current.processors.get(),plans_.previous()->processors.get(),through_);
    current.bridge->activated=true;current.bridge->phase=DryBridge::Phase::Done;current.renderInput=true;current.dryInput=nullptr;current.drySource=nullptr;current.dryContext=nullptr;
  }
  if(plans_.previous()) {
    const auto revision=plans_.currentRevision();plans_.finish();
    status_.store(Status::Stable,std::memory_order_release);settled_.store(revision,std::memory_order_release);
  }
  collect();return true;
}
bool MixerTransition::begin(uint32_t frames,uint64_t position) noexcept {
  if(open_ || !frames || frames>MixerRuntime::maximumFrames || position!=through_ || position>UINT64_MAX-frames) {
    failed_=true;return false;
  }
  if(!latch(position))return false;
  auto &current=plans_.current();
  if(current.bridge&&plans_.previous()&&frames>current.bridge->remaining){failed_=true;return false;}
  frames_=frames;position_=position;open_=true;
  const bool next=std::exchange(adoptionPending_,false);
  if(auto *previous=plans_.previous()) {
    previous->runtime->begin(frames,position);
    if(next) {
      if(!current.runtime->activateHistory()){reject();return true;}
      if(current.adopt)current.adopt(current.processors.get(),previous->processors.get());
      if(!current.bridge&&current.activateAudio)current.activateAudio(current.processors.get(),previous->processors.get(),position);
      // Feed the new plan actual ongoing sources until its compensation/DSP
      // latency has filled. The old plan remains fully audible during warmup.
      warmup_=current.runtime->plan().latency;fade_=0;
    }
  }
  current.runtime->begin(frames,position);
  if(current.bridge&&plans_.previous()){
    auto &bridge=*current.bridge;auto &previous=*plans_.previous();
    if(next){if(!bridge.before.plan->runtime->activateHistory()||!bridge.after.plan->runtime->activateHistory()){reject();return true;}}
    bridge.before.plan->runtime->begin(frames,position);bridge.after.plan->runtime->begin(frames,position);
    previous.renderInput=!bridge.activated;current.renderInput=bridge.activated;
    previous.dryInput=bridge.before.plan->runtime.get();current.dryInput=bridge.after.plan->runtime.get();
    bridge.before.frames=bridge.after.frames=frames;
    auto connectDry=[](Plan &p,DryBridge::Shadow &shadow)noexcept{
      p.dryContext=&shadow;p.drySource=[](void *opaque,size_t processor,uint32_t port,const float *samples)noexcept{
        auto &shadow=*static_cast<DryBridge::Shadow *>(opaque);
        for(auto &source:shadow.sources)if(source.processor==processor&&source.port==port){
          auto &d=source.delay;
          for(uint32_t i=0;i<shadow.frames*2;++i){auto value=samples?samples[i]:0.f;if(!d.samples.empty()){std::swap(value,d.samples[d.cursor]);if(++d.cursor==d.samples.size())d.cursor=0;}shadow.sourceScratch[i]=value;}
          shadow.plan->runtime->instrument(processor,port,shadow.sourceScratch.data());return;
        }
      };
    };connectDry(previous,bridge.before);connectDry(current,bridge.after);
  }
  if(auto *previous=plans_.previous();previous && previous->begin)previous->begin(previous->processors.get(),frames,position,false);
  if(plans_.current().begin)plans_.current().begin(plans_.current().processors.get(),frames,position,true);
  return true;
}
bool MixerTransition::renderSources(uint32_t frames,uint64_t position) noexcept {
  if(!open_ || frames!=frames_ || position!=position_){failed_=true;return false;}
  auto &current=plans_.current();
  // Retained RenderOnce wrappers share vendor output, but each plan has its
  // own route sums and follower inputs. The accepted controls render first.
  if(current.renderSources&&!current.renderSources(current.processors.get(),current,frames,position,true)){failed_=true;return false;}
  if(auto *previous=plans_.previous();previous&&previous->renderSources)
    if(!previous->renderSources(previous->processors.get(),*previous,frames,position,false)){failed_=true;return false;}
  return true;
}
void MixerTransition::instrument(size_t processor,uint32_t port,const float *samples) noexcept {
  auto &current=plans_.current();
  if(!open_ || processor>=current.sourceProcessors.size() || current.sourceProcessors[processor]==SIZE_MAX){failed_=true;return;}
  current.instrument(current.sourceProcessors[processor],port,samples);
  if(current.source)current.source(current.processors.get(),current.sourceProcessors[processor],port,samples,frames_,position_);
  if(auto *previous=plans_.previous();previous&&processor<previous->sourceProcessors.size()&&previous->sourceProcessors[processor]!=SIZE_MAX){previous->instrument(previous->sourceProcessors[processor],port,samples);if(previous->source)previous->source(previous->processors.get(),previous->sourceProcessors[processor],port,samples,frames_,position_);}
}
void MixerTransition::instrument(std::string_view identity,uint32_t port,const float *wet,const float *rawDry) noexcept {
  if(!open_){failed_=true;return;}
  auto feed=[&](Plan &plan)noexcept{
    const auto p=std::find_if(plan.catalog.begin(),plan.catalog.end(),[&](const auto &p){return p.instrument&&p.instance==identity;});
    if(p==plan.catalog.end())return;
    const auto index=size_t(p-plan.catalog.begin());plan.instrument(index,port,wet,rawDry);
    if(plan.source)plan.source(plan.processors.get(),index,port,wet,frames_,position_);
  };
  feed(plans_.current());if(auto *previous=plans_.previous())feed(*previous);
}
const float *MixerTransition::evaluate(Plan &plan,std::span<const DirectInput> sources) noexcept {
  if(plan.execution){
    for(const auto &step:plan.execution->steps){const auto location=step.before;
      if(step.kind==0){const auto index=plan.directSources[location.bus];const auto input=index<sources.size()?sources[index]:DirectInput{};if(!plan.runtime->beginBus(location.bus,input.left,input.right))return nullptr;}
      else if(step.kind==2){if(!plan.runtime->finishBus(location.bus))return nullptr;}
      else {
        float *buffer;
        if(location.bus==SIZE_MAX){buffer=plan.execution->silentBefore.data();std::fill_n(buffer,frames_*2,0.f);}
        else buffer=plan.runtime->processorInput(location.bus,location.processor);
        if(!buffer || !plan.process(plan.processors.get(),*plan.runtime,location.processor,buffer,frames_,position_) ||
           (location.bus!=SIZE_MAX && !plan.runtime->finishProcessor(location.bus,location.processor)))return nullptr;
      }
    }
    plan.runtime->complete();return plan.runtime->failed()?nullptr:plan.runtime->masterOutput();
  }
  struct Context {Plan &plan;};Context context{plan};
  const auto callback=[](void *opaque,size_t processor,float *samples,uint32_t frames,uint64_t position) noexcept {
    auto &plan=static_cast<Context *>(opaque)->plan;
    return plan.process(plan.processors.get(),*plan.runtime,processor,samples,frames,position);
  };
  const float *output=nullptr;
  for(auto bus:plan.runtime->plan().order) {
    const auto source=plan.directSources[bus];
    const auto input=source<sources.size()?sources[source]:DirectInput{};
    const auto *samples=plan.runtime->process(bus,input.left,input.right,callback,&context);
    if(bus==plan.runtime->plan().master)output=plan.runtime->masterOutput();
  }
  plan.runtime->complete();return plan.runtime->failed()?nullptr:output;
}
bool MixerTransition::evaluateMorph(Plan &before,Plan &after,std::span<const DirectInput> sources,bool &candidateFailed) noexcept {
  auto &morph=*after.morph;
  auto source=[&](const Plan &p,size_t bus){const auto index=p.directSources[bus];return index<sources.size()?sources[index]:DirectInput{};};
  auto auxiliary=[](std::span<const MixerAudioInput> inputs,uint32_t port)->const float *{
    for(const auto &input:inputs)if(input.bus==port)return input.samples;return nullptr;
  };
  for(const auto &step:morph.steps){
    const auto a=step.before,b=step.after;
    if(step.kind!=1){
      auto &p=a.bus!=SIZE_MAX?before:after;const auto bus=a.bus!=SIZE_MAX?a.bus:b.bus;
      if(step.kind==0){const auto input=source(p,bus);if(!p.runtime->beginBus(bus,input.left,input.right))return false;}
      else if(!p.runtime->finishBus(bus))return false;
      continue;
    }
    auto input=[&](Plan &p,InputMorph::Location location,auto &silence)->float * {
      if(location.processor==SIZE_MAX)return nullptr;
      if(location.bus!=SIZE_MAX)return p.runtime->processorInput(location.bus,location.processor);
      std::fill_n(silence.data(),frames_*2,0.f);return silence.data();
    };
    auto *old=input(before,a,morph.silentBefore),*next=input(after,b,morph.silentAfter);
    if((a.processor!=SIZE_MAX&&!old)||(b.processor!=SIZE_MAX&&!next))return false;
    if(old&&next){
      // Moving between a silent slot and an audible chain already fades at
      // the graph output. Feed the audible side fully during that handoff;
      // fading the same input again creates a dip even for a unity processor.
      const bool leaving=a.bus!=SIZE_MAX && b.bus==SIZE_MAX;
      const bool entering=a.bus==SIZE_MAX && b.bus!=SIZE_MAX;
      const auto oldInputs=before.runtime->inputs(a.processor),newInputs=after.runtime->inputs(b.processor);
      const auto fallback=after.catalog[b.processor].mainInputFallback;
      std::array<MixerAudioInput,64> mixed{};size_t count=0;
      for(size_t port=0;port<morph.inputs.size();++port){
        const auto index=morph.inputs[port].bus;if(!(after.catalog[b.processor].activeInputs&(uint64_t(1)<<index)))continue;
        const auto *left=auxiliary(oldInputs,index),*right=auxiliary(newInputs,index);auto *output=morph.auxiliary[port]->data();
        float *automatic=nullptr;
        if(fallback&(uint64_t(1)<<index)){
          if(!left&&!right)continue; // Keep ordinary self-detection on the blended main input.
          automatic=morph.autoFallback[port]->data();
        }
        for(uint32_t frame=0;frame<frames_;++frame){const float t=leaving?0:(entering?1:(candidateFailed?0:amount(frame)));for(unsigned channel=0;channel<2;++channel){const auto i=frame*2+channel;const float x=left?left[i]:0,y=right?right[i]:0;output[i]=x+(y-x)*t;if(automatic)automatic[i]=(left?0:old[i]*(1-t))+(right?0:next[i]*t);}}
        // The native host adds this only in Auto mode, after applying any
        // sample-offset detector-mode event inside the processing buffer.
        mixed[count++]={index,output,automatic};
      }
      // Detector fallback above needs each side's unmodified main input.
      if(leaving)std::copy_n(old,frames_*2,next);
      else if(!entering)for(uint32_t frame=0;frame<frames_;++frame){const float t=candidateFailed?0:amount(frame);for(unsigned channel=0;channel<2;++channel){const auto i=frame*2+channel;next[i]=old[i]+(next[i]-old[i])*t;}}
      after.runtime->overrideInputs(b.processor,{mixed.data(),count});
      const bool okay=after.process(after.processors.get(),*after.runtime,b.processor,next,frames_,position_);
      after.runtime->clearInputOverride();
      if(!okay)return false; // A retained vendor failed, not merely a candidate.
      std::copy_n(next,frames_*2,old);
      if(before.source)before.source(before.processors.get(),a.processor,0,old,frames_,position_);
      const auto &info=before.catalog[a.processor];
      for(uint32_t port=1;port<info.outputBuses;++port)if(info.activeOutputs&(uint64_t(1)<<port)){
        if(!after.output)return false;
        before.runtime->instrument(a.processor,port,after.output(after.processors.get(),b.processor,port));
        if(before.source)before.source(before.processors.get(),a.processor,port,after.output(after.processors.get(),b.processor,port),frames_,position_);
      }
    } else {
      auto &p=old?before:after;const auto location=old?a:b;auto *buffer=old?old:next;
      if(!p.process(p.processors.get(),*p.runtime,location.processor,buffer,frames_,position_)){
        if(old)return false;
        candidateFailed=true;std::fill_n(buffer,frames_*2,0.f);
      }
    }
    if(old && a.bus!=SIZE_MAX && !before.runtime->finishProcessor(a.bus,a.processor))return false;
    if(next && b.bus!=SIZE_MAX && !after.runtime->finishProcessor(b.bus,b.processor))return false;
  }
  before.runtime->complete();after.runtime->complete();
  candidateFailed|=after.runtime->failed();return !before.runtime->failed();
}
const float *MixerTransition::render(std::span<const DirectInput> sources) noexcept {
  if(!open_ || sources.size()!=sources_.size()){failed_=true;return nullptr;}
  auto *previous=plans_.previous();
  if(previous&&plans_.current().bridge)return renderBridge(sources);
  const float *old=nullptr,*current=nullptr;
  if(previous && plans_.current().morph){
    auto &candidate=plans_.current();bool candidateFailed=false;
    if(evaluateMorph(*previous,candidate,sources,candidateFailed)){
      old=previous->runtime->masterOutput();
      if(!candidateFailed)current=candidate.runtime->masterOutput();
    }
  } else {
    old=previous?evaluate(*previous,sources):nullptr;
    current=evaluate(plans_.current(),sources);
  }
  open_=false;through_+=frames_;
  if(previous && !old){failed_=true;return nullptr;}
  if(!current) {
    if(previous){std::copy_n(old,frames_*2,output_.data());reject();return output_.data();}
    failed_=true;return nullptr;
  }
  if(!previous)return current;
  for(uint32_t frame=0;frame<frames_;++frame) {
    float amount=0;
    if(warmup_)--warmup_;
    else {amount=float(std::min<uint64_t>(fade_,fadeFrames_))/fadeFrames_;++fade_;}
    for(size_t channel=0;channel<2;++channel) {
      const auto sample=frame*2+channel;
      // A linear crossfade retains unity for correlated, unchanged paths.
      output_[sample]=old[sample]+(current[sample]-old[sample])*amount;
    }
  }
  if(!warmup_ && fade_>fadeFrames_) {
    const auto revision=plans_.currentRevision();plans_.finish();
    status_.store(Status::Stable,std::memory_order_release);settled_.store(revision,std::memory_order_release);
  }
  return output_.data();
}
} // namespace Tracker
