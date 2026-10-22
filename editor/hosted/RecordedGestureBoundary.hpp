#pragma once
#include "RecordedGestureTake.hpp"
#include "PluginEditQueue.hpp"
#include <map>

namespace Tracker {
// Pure control-owner validation of an already captured, grouped native cut.
// Native adapters allocate/capture the cuts on the provider owner and recheck
// document/host context around vendor calls. This policy never reads a queue,
// pumps native input, acquires a lease or changes manual plugin state.
struct RecordedGestureBoundary {
  using Cuts=std::map<std::string,PluginEditBoundary>;

  static void requireProviders(const RecordedGestureTake &take,const Cuts &cuts,bool fresh) {
    if(cuts.size()!=take.providerCount())
      throw std::invalid_argument("Recording needs one native boundary for every armed provider");
    for(const auto &provider:take.providers()) {
      const auto found=cuts.find(provider.plugin);
      if(found==cuts.end())throw std::invalid_argument("Recording boundary belongs to another provider");
      const auto &cut=found->second;
      if(cut.source.incarnation!=provider.source.incarnation||
         cut.source.dropped<provider.source.dropped||
         (fresh&&cut.source.dropped!=provider.source.dropped))
        throw std::invalid_argument("Recording notification provider changed; input retained");
      if(!cut.fence.hostTime||cut.fence.hostTime<take.startTime()||
         cut.fence.pending>PluginEditQueue::capacity)
        throw std::invalid_argument("Recording needs a current bounded native input boundary");
      if(fresh&&cut.fence.pending)
        throw std::invalid_argument("Flush earlier manual plugin input before recording; queued input retained");
    }
  }

  static void requireFresh(const RecordedGestureTake &take,const Cuts &cuts) {
    if(take.completed()||take.endTime()||take.eventCount()||take.failure()!=RecordedGestureTake::Failure::None)
      throw std::invalid_argument("Begin recording with a fresh take");
    requireProviders(take,cuts,true);
  }

  static uint64_t stopTime(const RecordedGestureTake &take,const Cuts &cuts) {
    if(take.completed()||take.endTime())
      throw std::invalid_argument("Take already has a release boundary or is complete");
    requireProviders(take,cuts,false);
    uint64_t end=0;
    for(const auto &[identity,cut]:cuts)end=std::max(end,cut.fence.hostTime);
    if(end==UINT64_MAX)throw std::invalid_argument("Recording stop time exceeds the native clock range");
    // Timestamp intervals are half-open. Include the last clock quantum;
    // queue positions, not timestamp guesses, exclude later notifications.
    return end+1;
  }

  static uint64_t seal(RecordedGestureTake &take,const Cuts &cuts) {
    const auto end=stopTime(take,cuts);
    // The take preflights every retained event before mutating any target.
    take.seal(end);
    return end;
  }
};
}
