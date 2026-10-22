#pragma once
#include "RecordedGestureTake.hpp"
#include <map>

namespace Tracker {
// Serialized control-owner policy, not an audio callback. Native adapters keep
// provider lookup, grouped producer cuts, input leases and baseline restoration.
// Neither callback may switch to a manual-state consumer while this take owns
// its providers. Read supplies the paired source even for an empty result.
struct RecordedGestureDrain {
  using Catalogs=std::map<std::string,std::vector<PluginParameter>>;
  using Cuts=std::map<std::string,PluginEditBoundary>;
  enum class Result { Complete, Pending, ContextOrProvider, InvalidBoundary, FailedTake };
  static constexpr unsigned eventsPerProvider=128;

  template<class Observe,class Read>
  static Result poll(RecordedGestureTake &take,const std::string &revision,uint64_t epoch,
      const NativeSong &native,const Catalogs &catalogs,const Cuts &cuts,bool acknowledgeStop,
      Observe &&observe,Read &&read) {
    if(take.completed())return Result::Complete;
    if(catalogs.size()!=take.providerCount())return Result::ContextOrProvider;
    if((acknowledgeStop&&(!take.endTime()||cuts.size()!=catalogs.size()))||
       (!cuts.empty()&&cuts.size()!=catalogs.size()))return Result::InvalidBoundary;
    for(const auto &[identity,catalog]:catalogs) {
      if(!take.owns(identity))return Result::ContextOrProvider;
      if(!cuts.empty()&&!cuts.contains(identity))return Result::InvalidBoundary;
    }
    // Validate every provider before consuming any of them. A later provider
    // failure must not remove an earlier provider's still-reviewable input.
    for(const auto &[identity,catalog]:catalogs) {
      const auto source=observe(identity);
      if(!source||!take.observeProvider(identity,*source,revision,epoch))return Result::ContextOrProvider;
    }
    bool empty=true;
    for(const auto &[identity,catalog]:catalogs) {
      bool drained=false;PluginEdit event{};PluginEditSource source{};
      const auto cut=cuts.find(identity);
      const auto boundary=cut==cuts.end()?nullptr:&cut->second;
      for(unsigned n=0;n<eventsPerProvider;++n) {
        const auto result=read(identity,boundary,event,source);
        if(result!=PluginEditRead::Event&&result!=PluginEditRead::Complete)return Result::InvalidBoundary;
        if(result==PluginEditRead::Complete){drained=true;break;}
        (void)take.append(identity,event,source,revision,epoch,native,catalog);
      }
      // Exactly 128 events needs another poll: only a real paired empty read
      // can acknowledge a stopped provider, including its loss counter.
      empty=empty&&drained;
      if(drained&&acknowledgeStop)(void)take.drained(identity,source,revision,epoch);
      else (void)take.observeProvider(identity,source,revision,epoch);
    }
    const auto failure=take.failure();
    if(failure==RecordedGestureTake::Failure::Context||failure==RecordedGestureTake::Failure::Provider||
       failure==RecordedGestureTake::Failure::Target)return Result::FailedTake;
    return empty?Result::Complete:Result::Pending;
  }

  template<class Visit>
  static void latest(const RecordedGestureTake &take,Visit &&visit) {
    for(size_t i=0;i<take.targetCount();++i) {
      const auto &target=take.target(i);const auto events=target.events();
      if(events.empty())continue;
      // Native controls and vendor notifications can arrive out of order.
      // Latest original time wins; equal timestamps use last delivery order.
      const auto *last=&events.front();
      for(const auto &event:events)if(event.hostTime>=last->hostTime)last=&event;
      visit(target.key(),*last);
    }
  }
};
}
