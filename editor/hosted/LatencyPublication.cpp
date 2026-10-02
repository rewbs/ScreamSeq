#include "HostedAudio.hpp"
#include <algorithm>
#include <stdexcept>

namespace Tracker {
bool PluginChain::prepareRoutingLatencies(std::vector<MixerProcessorInfo> &catalog,HostedMixerPlan &hosted,size_t &bytes) {
  hosted.latencies.clear();
  for(const auto &entry:hosted.rack){
    auto &plugin=*entry->plugin;if(!plugin.latencyChangePending())continue;
    auto update=plugin.prepareLatency();
    if(!update)throw std::runtime_error("Plugin latency changed during preparation; retry the update");
    const auto processor=std::find_if(catalog.begin(),catalog.end(),[&](const auto &p){return p.instance==entry->baseline.instanceID;});
    if(processor==catalog.end())throw std::logic_error("Latency endpoint is absent from the prepared rack");
    processor->latency=update->snapshot.samples;
    processor->tail=plugin.isInstrument()?std::max(2.,update->snapshot.tail):update->snapshot.tail;
    bytes+=update->storageBytes();hosted.latencies.push_back({entry->plugin,std::move(update)});
  }
  bytes+=hosted.latencies.capacity()*sizeof(HostedMixerPlan::Latency);
  return !hosted.latencies.empty();
}
void PluginChain::adoptRoutingLatencies(HostedMixerPlan &hosted) noexcept {
  for(const auto &latency:hosted.latencies)latency.plugin->adoptLatency(*latency.state);
  latency_.store(hosted.preparedLatency,std::memory_order_release);
  liveTail_.store(hosted.preparedTail,std::memory_order_release);liveTailRevision_.fetch_add(1,std::memory_order_release);
}
bool PluginChain::refreshLatencies(const NativeSong &native) {
  if(!latencyChangePending())return true;
  if(!mixerTransition_||!mixerRoutingReady())return false;
  auto plan=prepareMixerRouting(native);
  return plan&&publishMixerRouting(plan);
}
}
