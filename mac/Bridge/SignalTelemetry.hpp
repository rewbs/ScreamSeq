#pragma once
#import <Foundation/Foundation.h>
#include "editor/hosted/SignalObservation.hpp"

namespace Tracker {
// UI projection only. Keep the full serializer identical for API/copy readers.
// The existing session owner must keep observation alive throughout this call.
// Root views discard copy ports, so skip their atomic read and Foundation data
// construction before bridging; root/aggregate identities retain their meaning.
inline NSArray<NSDictionary *> *encodeSignalTelemetryPorts(const SignalObservation *observation,bool songOnly) {
  const auto nativeID=[](uint64_t identifier) {return [NSString stringWithFormat:@"n%llu",(unsigned long long)identifier];};
  auto ports=[NSMutableArray array];
  if(observation)for(size_t i=0;i<observation->ports.size();++i) {
    const auto &port=observation->ports[i];
    if(songOnly && port.copy)continue;
    const auto value=observation->read(uint32_t(i+1));if(!value.available)continue;
    auto entry=[@{@"key":@(port.key.c_str()),@"node":@(port.node.c_str()),@"name":@(port.name.c_str()),
      @"direction":port.output?@"output":@"input",@"port":@(port.port),@"channels":@(port.channels),
      @"processorLatency":value.processorLatency<0?(id)NSNull.null:@(value.processorLatency),
      @"compensation":value.compensation<0?(id)NSNull.null:@(value.compensation),
      @"available":@(value.available),@"fresh":@(value.fresh),@"measured":@(value.measured),@"peak":@[@(value.peakLeft),@(value.peakRight)],
      @"rms":@[@(value.rmsLeft),@(value.rmsRight)],@"through":@(value.through),@"lastSignal":@(value.lastSignal),
      @"clipped":@(value.clipped),@"nonFinite":@(value.nonFinite)} mutableCopy];
    entry[@"kind"]=@(port.kind.c_str());
    if(port.kind=="control"){entry[@"value"]=@(value.value);entry[@"first"]=@(value.first);entry[@"minimum"]=@(std::min(value.first,value.value));entry[@"maximum"]=@(std::max(value.first,value.value));}
    if(value.noteGate){const auto &g=*value.noteGate;entry[@"noteGate"]=@{@"held":@(g.held),@"on":@(g.on),@"off":@(g.off),@"retrigger":@(g.retrigger),@"lastFrame":g.hasEvent?(id)@(g.lastFrame):(id)NSNull.null,@"scope":@"aggregate-envelope-gate"};}
    if(port.copy){const auto &c=*port.copy;entry[@"copy"]=@{@"graph":nativeID(c.graph),@"target":c.target?(id)nativeID(c.target):NSNull.null,@"role":@[@"row",@"persistent",@"ordinary",@"instrument"][c.role],@"instrument":c.instrument?(id)nativeID(c.instrument):NSNull.null,@"channel":c.channel==UINT16_MAX?(id)NSNull.null:@(c.channel)};}
    if(port.route) {
      const auto &route=*port.route;
      entry[@"route"]=@{@"kind":@(route.kind.c_str()),@"source":@(route.source.c_str()),@"target":@(route.target.c_str()),
        @"plugin":@(route.plugin.c_str()),@"input":@(route.input),@"output":@(route.output),@"tap":@(route.tap.c_str()),
        @"gainDB":std::isfinite(value.routeGain)&&value.routeGain>0?(id)@(20*std::log10(value.routeGain)):NSNull.null,@"preFader":@(value.preFader)};
    }
    [ports addObject:entry];
  }
  return ports;
}
} // namespace Tracker
