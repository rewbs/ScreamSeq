#include "HostedAudio.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
namespace Tracker {
size_t RecordedAutomationTimeline::storageBytes() const noexcept {
  size_t result=sizeof(*this)+points.capacity()*sizeof(ParameterChange)+parameters.capacity()*sizeof(Parameter);
  for(const auto &p:parameters)result+=p.points.capacity()*sizeof(ParameterChange);
  return result;
}
bool NativePlugin::adoptRecordedAutomation(const RecordedAutomationTimeline &timeline,uint64_t position) noexcept {
  // Every processor, including a removed processor's fading old route, adopts
  // a pointer into this complete snapshot before any plugin processes audio.
  activeAutomation_=&timeline.points;
  automationPosition_=size_t(std::upper_bound(timeline.points.begin(),timeline.points.end(),position,
    [](uint64_t frame,const auto &p){return frame<p.frame;})-timeline.points.begin());
  for(const auto &parameter:timeline.parameters) {
    includeParameterRange(parameter.id,parameter.minimum,parameter.maximum);
    const auto at=std::upper_bound(parameter.points.begin(),parameter.points.end(),position,
      [](uint64_t frame,const auto &p){return frame<p.frame;});
    const bool recorded=at!=parameter.points.begin();
    const auto value=recorded?std::prev(at)->value:parameter.baseline;
    cancelScheduledParameter(parameter.id);
    if(!appliedParameter(parameter.id,value,position,{recorded?ParameterOrigin::Recorded:ParameterOrigin::Baseline}))return false;
  }
  return true;
}
std::unique_ptr<RecordedAutomationPlan> PluginChain::prepareRecordedAutomation(const std::vector<ParameterChange> &points) {
  if(points.size()>100000)throw std::invalid_argument("Automation exceeds 100000 points");
  if(!recordedPlans_.available())throw std::runtime_error("Recorded automation publication is busy; retry the edit");
  auto plan=std::make_unique<RecordedAutomationPlan>();plan->sourceRevision=recordedRevision_;plan->active=!points.empty();
  std::vector<std::vector<ParameterChange>> bySlot(rack_.size());
  std::set<std::tuple<uint32_t,uint32_t,uint64_t>> unique;
  for(auto point:points) {
    if(point.slot>=rack_.size()||!std::isfinite(point.value)||point.frame>uint64_t(48000)*604800||!unique.emplace(point.slot,point.id,point.frame).second)
      throw std::invalid_argument("Invalid or duplicate recorded automation point");
    const auto &catalog=rack_[point.slot]->parameters;
    const auto parameter=std::find_if(catalog.begin(),catalog.end(),[&](const auto &p){return p.id==point.id;});
    if(parameter==catalog.end()||!parameter->writable||point.value<parameter->min||point.value>parameter->max)
      throw std::invalid_argument("Recorded automation parameter is unavailable or outside its range");
    point.frame=uint64_t(double(point.frame)*sampleRate_/48000);bySlot[point.slot].push_back(point);
  }
  for(auto &lane:bySlot)std::stable_sort(lane.begin(),lane.end(),[](const auto &a,const auto &b){return a.frame<b.frame;});
  for(const auto &entry:rack_)plan->rack.push_back(entry->plugin.get());
  size_t bytes=sizeof(*plan);recordedPlans_.forEachRetained([&](const auto &old){for(const auto &lane:old.lanes)bytes+=lane.timeline.storageBytes();});
  for(const auto &entry:retainedRack_) {
    const auto old=lastRecordedPlan_?std::find_if(lastRecordedPlan_->lanes.begin(),lastRecordedPlan_->lanes.end(),[&](const auto &p){return p.plugin==entry->plugin;}):std::vector<RecordedAutomationPlan::Lane>::const_iterator{};
    const bool hadPlan=lastRecordedPlan_&&old!=lastRecordedPlan_->lanes.end();
    const auto &previous=hadPlan?old->timeline.points:entry->plugin->initialAutomation();
    const auto current=std::find(rack_.begin(),rack_.end(),entry);
    RecordedAutomationPlan::Lane lane;lane.plugin=entry->plugin;
    lane.timeline.points=current==rack_.end()?previous:bySlot[size_t(current-rack_.begin())];
    std::set<uint32_t> targets;for(const auto &p:previous)targets.insert(p.id);for(const auto &p:lane.timeline.points)targets.insert(p.id);
    if(hadPlan)for(const auto &p:old->timeline.parameters)targets.insert(p.id);
    if(targets.size()>1024)throw std::invalid_argument("Recorded automation exceeds 1024 parameters per processor");
    for(auto id:targets) {
      const auto parameter=std::find_if(entry->parameters.begin(),entry->parameters.end(),[&](const auto &p){return p.id==id;});
      if(parameter==entry->parameters.end()||!parameter->writable)throw std::invalid_argument("Recorded automation parameter is unavailable");
      RecordedAutomationTimeline::Parameter p;p.id=id;p.baseline=parameter->value;p.minimum=p.maximum=p.baseline;
      for(const auto &point:lane.timeline.points)if(point.id==id){p.points.push_back(point);p.minimum=std::min(p.minimum,point.value);p.maximum=std::max(p.maximum,point.value);}
      lane.timeline.parameters.push_back(std::move(p));
    }
    bytes+=lane.timeline.storageBytes();
    if(bytes>64u*1024u*1024u)throw std::invalid_argument("Recorded automation snapshots exceed the 64 MB prepared storage budget");
    plan->lanes.push_back(std::move(lane));
  }
  return plan;
}
bool PluginChain::publishRecordedAutomation(std::unique_ptr<RecordedAutomationPlan> plan) {
  if(!plan||plan->sourceRevision!=recordedRevision_||plan->rack.size()!=rack_.size())return false;
  for(size_t i=0;i<rack_.size();++i)if(plan->rack[i]!=rack_[i]->plugin.get())return false;
  plan->parameterFence=write_.load(std::memory_order_relaxed);
  const auto *published=plan.get();if(!recordedPlans_.publish(std::move(plan)))return false;
  lastRecordedPlan_=published;++recordedRevision_;recordedActive_.store(published->active,std::memory_order_release);return true;
}
} // namespace Tracker
