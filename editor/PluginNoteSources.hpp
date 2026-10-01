#pragma once
#include "NativeSong.hpp"
#include "hosted/PluginTypes.hpp"
#include <span>
namespace Tracker {
// Control-thread reconciliation shared by both transaction adapters. A rack
// removal preserves a plugin-note generator only when another explicit route
// still uses it. Explicit unassignment restores normal sample-instrument mode.
inline void reconcilePluginNoteSources(NativeSong &native,std::span<const PluginState> before,
                                      std::span<const PluginState> after,uint64_t explicitlyUnassigned=0) {
  auto &sources=native.signal.noteRouting.triggerSources;
  if(explicitlyUnassigned)std::erase_if(sources,[&](const auto &s){return s.instrument==explicitlyUnassigned;});
  auto assigned=[&](uint32_t slot){return std::any_of(after.begin(),after.end(),[&](const auto &p){const auto a=pluginAssignments(p);return std::any_of(a.begin(),a.end(),[&](const auto &i){return i.instrument==slot;});});};
  for(const auto &plugin:before)for(const auto &assignment:pluginAssignments(plugin)){
    const auto found=native.instruments.find(uint16_t(assignment.instrument));if(found==native.instruments.end())continue;const auto instrument=found->second.id;
    const bool remains=std::any_of(after.begin(),after.end(),[&](const auto &p){return p.instanceID==plugin.instanceID;});
    if(assigned(assignment.instrument)||remains||instrument==explicitlyUnassigned){std::erase_if(sources,[&](const auto &s){return s.instrument==instrument;});continue;}
    const bool routed=std::any_of(native.signal.noteRouting.routes.begin(),native.signal.noteRouting.routes.end(),[&](const auto &r){
      return (r.sourceKind==NoteSourceKind::Channel||r.source==instrument)&&std::any_of(after.begin(),after.end(),[&](const auto &p){return p.instanceID==r.plugin;});
    });
    if(routed&&std::none_of(sources.begin(),sources.end(),[&](const auto &s){return s.instrument==instrument;}))sources.push_back({instrument,uint8_t(assignment.channel)});
  }
  // Assigning a formerly orphaned source gives it an ordinary implicit cable.
  for(const auto &plugin:after)for(const auto &assignment:pluginAssignments(plugin)){
    const auto found=native.instruments.find(uint16_t(assignment.instrument));if(found!=native.instruments.end())std::erase_if(sources,[&](const auto &s){return s.instrument==found->second.id;});
  }
}
}
