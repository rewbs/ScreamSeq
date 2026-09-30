#include "MixerTransition.hpp"
#include <algorithm>
#include <cmath>
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
  require(storage(plan)<=storageLimit_,"Live mixer exceeds the audio storage budget");
  stable_=&plan;through_=plan.runtime->through();plan.owner=owner_;
}
size_t MixerTransition::storage(const Plan &plan) const noexcept {
  // Conservative double-counting of shared PDC/DSP ownership bounds the whole
  // transition, rather than accepting two individually legal oversized plans.
  const auto runtime=sizeof(Plan)+plan.runtime->storageBytes();
  return plan.processorStorage>SIZE_MAX-runtime?SIZE_MAX:runtime+plan.processorStorage;
}
void MixerTransition::settle() noexcept {
  if(submitted_ && settled_.load(std::memory_order_acquire)==serial_) {
    if(failedRevision_.load(std::memory_order_acquire)!=serial_)stable_=submitted_;
    submitted_=nullptr;
  }
}
std::unique_ptr<MixerTransition::Plan> MixerTransition::prepare(MixerGraph graph,
    std::vector<MixerProcessorInfo> catalog,const std::vector<std::string> &reset) {
  require(ready(),"A routing transition is still preparing");
  require(catalog.size()==stable_->catalog.size(),"Prepare source adapters before changing the processor catalog");
  for(size_t i=0;i<catalog.size();++i)require(catalog[i].instance==stable_->catalog[i].instance &&
    catalog[i].instrument==stable_->catalog[i].instrument,"A routing transition must retain processor identities and source kinds");
  auto compiled=compileMixer(graph,tracks_,catalog,rate_);
  require(compiled.latency==stable_->runtime->plan().latency,"A latency change requires a latency-aligned host transition");
  auto result=std::make_unique<Plan>();result->owner=owner_;result->sourceRevision=plans_.renderedRevision();
  result->reuse=mixerTransitionReuse(stable_->runtime->graph(),stable_->runtime->plan(),stable_->catalog,
    graph,compiled,catalog,reset);
  result->catalog=std::move(catalog);
  result->runtime=std::make_unique<MixerRuntime>(std::move(graph),std::move(compiled),rate_);
  result->runtime->retainHistory(*stable_->runtime,result->reuse);
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
  plans_.cancel();failedRevision_.store(revision,std::memory_order_release);
  status_.store(Status::Failed,std::memory_order_release);settled_.store(revision,std::memory_order_release);
}
bool MixerTransition::controls(const std::vector<MixerControls> &values) noexcept {
  settle();
  if(!stable_->runtime->canQueueControls() || (submitted_ && !submitted_->runtime->canQueueControls()))return false;
  // The host may use this fast path only when stable bus order is retained.
  // Validate both targets before publishing either queue; the single consumer
  // can only make more room after the preflight, never consume that room.
  if(submitted_) {
    const auto &before=stable_->runtime->graph().buses,&after=submitted_->runtime->graph().buses;
    if(before.size()!=after.size())return false;
    for(size_t i=0;i<before.size();++i)if(before[i].id!=after[i].id)return false;
  }
  if(!stable_->runtime->controls(values))return false;
  return !submitted_ || submitted_->runtime->controls(values);
}
bool MixerTransition::commitStopped() noexcept {
  if(open_)return false;
  const bool next=plans_.begin();
  if(next && !plans_.current().runtime->activateHistory(true)){reject();settle();return false;}
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
  frames_=frames;position_=position;open_=true;
  const bool next=plans_.begin();
  if(auto *previous=plans_.previous()) {
    previous->runtime->begin(frames,position);
    if(next) {
      if(!plans_.current().runtime->activateHistory()){reject();return true;}
      // Feed the new plan actual ongoing sources until its compensation/DSP
      // latency has filled. The old plan remains fully audible during warmup.
      warmup_=plans_.current().runtime->plan().latency;fade_=0;
    }
  }
  plans_.current().runtime->begin(frames,position);return true;
}
void MixerTransition::instrument(size_t processor,uint32_t port,const float *samples) noexcept {
  if(!open_){failed_=true;return;}
  plans_.current().runtime->instrument(processor,port,samples);
  if(auto *previous=plans_.previous())previous->runtime->instrument(processor,port,samples);
}
const float *MixerTransition::evaluate(Plan &plan,std::span<const DirectInput> sources) noexcept {
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
    if(bus==plan.runtime->plan().master)output=samples;
  }
  plan.runtime->complete();return plan.runtime->failed()?nullptr:output;
}
const float *MixerTransition::render(std::span<const DirectInput> sources) noexcept {
  if(!open_ || sources.size()!=sources_.size()){failed_=true;return nullptr;}
  auto *previous=plans_.previous();
  const float *old=previous?evaluate(*previous,sources):nullptr;
  const float *current=evaluate(plans_.current(),sources);
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
