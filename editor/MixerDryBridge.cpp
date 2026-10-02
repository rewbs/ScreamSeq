#include "MixerTransition.hpp"
#include <algorithm>
#include <stdexcept>

namespace Tracker {
size_t MixerTransition::DryBridge::storageBytes() const noexcept {
  size_t bytes=sizeof(*this);
  for(const auto *s:{&before,&after}){
    bytes+=sizeof(Plan)+s->plan->runtime->storageBytes()+s->delays.capacity()*sizeof(Delay)+s->sources.capacity()*sizeof(Shadow::SourceDelay);
    bytes+=s->plan->catalog.capacity()*sizeof(MixerProcessorInfo)+s->plan->directSources.capacity()*sizeof(size_t)+s->plan->dependencies.capacity()*sizeof(Dependency);
    for(const auto &processor:s->plan->catalog)bytes+=processor.instance.capacity();
    if(s->plan->execution)bytes+=s->plan->execution->storageBytes();
    for(const auto &d:s->delays)bytes+=d.samples.capacity()*sizeof(float);
    for(const auto &source:s->sources)bytes+=source.delay.samples.capacity()*sizeof(float);
  }
  return bytes;
}
void MixerTransition::prepareBridge(Plan &plan) {
  auto bridge=std::make_unique<DryBridge>();
  auto shadow=[&](DryBridge::Shadow &result,const Plan &source){
    result.plan=std::make_unique<Plan>();auto &p=*result.plan;
    p.catalog=source.catalog;p.directSources=source.directSources;p.dependencies=source.dependencies;
    p.runtime=std::make_unique<MixerRuntime>(source.runtime->graph(),source.runtime->plan(),rate_);
    // Dry and wet expressions differ. Only bus control ramps may be inherited;
    // every dry delay line owns independent history and warms before use.
    MixerTransitionReuse controls;
    const auto &mix=source.runtime->plan();
    controls.controls.resize(mix.nodes.size());for(size_t i=0;i<controls.controls.size();++i)controls.controls[i]=i;
    controls.direct.assign(mix.nodes.size(),SIZE_MAX);controls.connections.assign(mix.connections.size(),SIZE_MAX);
    controls.instruments.assign(mix.instruments.size(),SIZE_MAX);controls.sidechains.assign(mix.sidechains.size(),SIZE_MAX);
    p.runtime->retainHistory(*source.runtime,controls);
    result.delays.resize(p.catalog.size());
    for(size_t i=0;i<p.catalog.size();++i)if(!p.catalog[i].instrument)result.delays[i].samples.assign(size_t(p.catalog[i].latency)*2,0);
    for(size_t i=0;i<p.catalog.size();++i)if(p.catalog[i].instrument)
      for(uint32_t port=0;port<std::min(64u,p.catalog[i].outputBuses);++port)if(p.catalog[i].activeOutputs&(uint64_t(1)<<port))
        result.sources.push_back({i,port,{std::vector<float>(size_t(p.catalog[i].latency)*2),0}});
    p.processors=std::shared_ptr<void>(&result,[](void *){});
    p.process=[](void *opaque,MixerRuntime &,size_t index,float *samples,uint32_t count,uint64_t)noexcept{
      auto &s=*static_cast<DryBridge::Shadow *>(opaque);if(index>=s.delays.size())return false;
      auto &delay=s.delays[index];if(delay.samples.empty())return true;
      for(uint32_t i=0;i<count*2;++i){std::swap(samples[i],delay.samples[delay.cursor]);if(++delay.cursor==delay.samples.size())delay.cursor=0;}return true;
    };
    // Auxiliary outputs of an effect have no defined main-input dry mapping.
    // Their transition substitute is silence; source instruments stay actual.
    if(!p.dependencies.empty()||!mix.detached.empty()||mix.segmented)p.execution=prepareMorph(p,p,true);
  };
  shadow(bridge->before,*stable_);shadow(bridge->after,plan);
  bridge->remaining=std::max(stable_->runtime->plan().latency,plan.runtime->plan().latency);
  plan.morph.reset();plan.requiresAudioHandoff=true;plan.bridge=std::move(bridge);
}
bool MixerTransition::latch(uint64_t position) noexcept {
  if(open_||position!=through_){failed_=true;return false;}
  if(!adoptionPending_&&plans_.begin())adoptionPending_=true;
  bridgeBoundary();return true;
}
void MixerTransition::bridgeBoundary() noexcept {
  auto *previous=plans_.previous();auto &current=plans_.current();
  if(!previous||!current.bridge)return;
  auto &b=*current.bridge;
  while(!b.remaining&&b.phase!=DryBridge::Phase::Done){
    b.elapsed=0;
    switch(b.phase){
    case DryBridge::Phase::WarmDry:b.phase=DryBridge::Phase::FadeDry;b.remaining=fadeFrames_;break;
    case DryBridge::Phase::FadeDry:
      b.phase=DryBridge::Phase::WarmNew;b.remaining=std::max<uint64_t>(current.runtime->plan().latency,fadeFrames_);
      if(current.activateAudio)current.activateAudio(current.processors.get(),previous->processors.get(),through_);
      b.activated=true;break;
    case DryBridge::Phase::WarmNew:b.phase=DryBridge::Phase::FadeWet;b.remaining=fadeFrames_;break;
    case DryBridge::Phase::FadeWet:b.phase=DryBridge::Phase::Done;break;
    case DryBridge::Phase::Done:break;
    }
  }
}
uint32_t MixerTransition::limitFrames(uint32_t frames,uint64_t position) noexcept {
  if(!frames||!latch(position))return frames;
  if(auto *previous=plans_.previous();previous&&plans_.current().bridge)
    return uint32_t(std::min<uint64_t>(frames,plans_.current().bridge->remaining));
  return frames;
}
const float *MixerTransition::renderBridge(std::span<const DirectInput> sources) noexcept {
  auto &current=plans_.current();auto &previous=*plans_.previous();auto &b=*current.bridge;
  const auto *dryBefore=evaluate(*b.before.plan,sources),*dryAfter=evaluate(*b.after.plan,sources);
  const auto *wet=evaluate(b.activated?current:previous,sources);
  open_=false;through_+=frames_;
  if(!dryBefore||!dryAfter||!wet){failed_=true;return nullptr;}
  for(uint32_t frame=0;frame<frames_;++frame){
    const auto x=std::min(1.,double(b.elapsed+frame)/fadeFrames_);const auto mix=float(x*x*(3-2*x));
    for(uint32_t channel=0;channel<2;++channel){const auto i=frame*2+channel;
      switch(b.phase){
      case DryBridge::Phase::WarmDry:output_[i]=wet[i];break;
      case DryBridge::Phase::FadeDry:output_[i]=wet[i]+(dryBefore[i]-wet[i])*mix;break;
      case DryBridge::Phase::WarmNew:output_[i]=dryBefore[i]+(dryAfter[i]-dryBefore[i])*mix;break;
      case DryBridge::Phase::FadeWet:output_[i]=dryAfter[i]+(wet[i]-dryAfter[i])*mix;break;
      case DryBridge::Phase::Done:output_[i]=wet[i];break;
      }
    }
  }
  b.elapsed+=frames_;b.remaining-=frames_;
  if(!b.remaining&&b.phase==DryBridge::Phase::FadeWet){
    current.renderInput=true;current.dryInput=nullptr;previous.dryInput=nullptr;current.drySource=previous.drySource=nullptr;current.dryContext=previous.dryContext=nullptr;b.phase=DryBridge::Phase::Done;
    const auto revision=plans_.currentRevision();plans_.finish();status_.store(Status::Stable,std::memory_order_release);settled_.store(revision,std::memory_order_release);
  }
  return output_.data();
}
}
