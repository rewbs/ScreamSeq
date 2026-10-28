#import "../Bridge/TrackerSession.h"
#include <iostream>
static void check(bool okay,const char *why){if(!okay)throw std::runtime_error(why);}
static id call(TrackerSession *s,NSString *method,NSDictionary *params,bool write=false){auto p=[params mutableCopy];if(write)p[@"expectedRevision"]=s.automationRevision;NSError *error=nil;auto result=[s automationMethod:method params:p error:&error];if(!result)throw std::runtime_error(error.localizedDescription.UTF8String);return result[@"data"];}
int main(){@autoreleasepool{try{
  auto s=[TrackerSession new];NSError *error=nil;
  call(s,@"plugin.add",@{@"descriptor":@{@"format":@"Built-in",@"classID":@"resonance.gainer.v1",@"name":@"Gainer",@"type":@0,@"subtype":@0,@"manufacturer":@0}},true);
  NSString *plugin=[s snapshot:0][@"nativePlugins"][0][@"instanceID"];
  auto binding=@{@"id":@7,@"plugin":plugin,@"parameter":@1,@"name":@"Gate gain"};
  auto fields=@{@"target":@"parameter",@"floor":@0.125,@"ceiling":@0.987654321987,@"period":@0.375,@"phase":@(-0.123456789123),@"width":@0.3,@"rise":@1.75,@"fall":@0.0625,@"riseShape":@"ease-in",@"fallShape":@"ease-out",@"linked":@NO,@"binding":@7,@"pitchRange":@12};
  auto gate=@{@"kind":@"native",@"native":@"gate",@"parameters":fields,@"channel":@0,@"column":@1,@"position":@12345};
  auto pan=@{@"kind":@"native",@"native":@"gate",@"parameters":@{@"target":@"pan",@"floor":@(-1),@"ceiling":@1},@"channel":@0,@"column":@0,@"position":@0};
  auto stop=@{@"kind":@"native",@"native":@"gate-stop",@"parameters":@{@"target":@"parameter",@"binding":@7},@"channel":@0,@"column":@1,@"position":@196731};
  auto request=@{@"pattern":@0,@"columns":@[@{@"channel":@0,@"count":@2}],@"bindings":@[binding],@"commands":@[pan,gate,stop]};
  auto before=call(s,@"pattern.effects.get",@{@"pattern":@0});auto revision=s.automationRevision;
  auto dry=[request mutableCopy];dry[@"dryRun"]=@YES;call(s,@"pattern.effects.set",dry,true);check([revision isEqual:s.automationRevision],"Gate dry run is nonmutating");
  call(s,@"pattern.effects.set",request,true);auto saved=call(s,@"pattern.effects.get",@{@"pattern":@0});revision=s.automationRevision;
  call(s,@"pattern.effects.set",request,true);check([revision isEqual:s.automationRevision],"Identical gate write creates no Undo");
  call(s,@"history.undo",@{},true);check([before isEqual:call(s,@"pattern.effects.get",@{@"pattern":@0})],"Gate, binding and columns use one Undo");call(s,@"history.redo",@{},true);check([saved isEqual:call(s,@"pattern.effects.get",@{@"pattern":@0})],"Redo restores all gate data");
  for(NSDictionary *bad in @[@{@"period":@0},@{@"period":@YES},@{@"width":@1.1},@{@"floor":@1.1},@{@"binding":@0},@{@"linked":@YES},@{@"riseShape":@"unknown"},@{@"surprise":@1}]){
    auto p=[fields mutableCopy];[p addEntriesFromDictionary:bad];auto c=[gate mutableCopy];c[@"parameters"]=p;revision=s.automationRevision;NSError *rejected=nil;
    check(![s automationMethod:@"pattern.effects.set" params:@{@"pattern":@0,@"commands":@[c],@"expectedRevision":revision} error:&rejected],"Invalid gate rejects");
    check([revision isEqual:s.automationRevision]&&[saved isEqual:call(s,@"pattern.effects.get",@{@"pattern":@0})],"Invalid gate preserves unrelated effects and history");
  }
  NSError *stale=nil;check(![s automationMethod:@"pattern.effects.set" params:@{@"pattern":@0,@"commands":@[],@"expectedRevision":@"stale"} error:&stale],"Stale gate write rejects");
  auto path=[NSTemporaryDirectory() stringByAppendingPathComponent:[NSUUID.UUID.UUIDString stringByAppendingString:@".screamseq"]];check([s savePath:path error:&error],"Gate project saves");auto reopened=[TrackerSession new];check([reopened openPath:path error:&error],"Gate project reopens");check([saved isEqual:call(reopened,@"pattern.effects.get",@{@"pattern":@0})],"All 13 parameters, stable binding, stop and fractional timing persist exactly");
  for(NSDictionary *target in call(reopened,@"parameter.activity.targets",@{})[@"targets"])if([target[@"plugin"] isEqual:plugin]){auto sources=call(reopened,@"parameter.activity.sources",@{@"target":target[@"key"],@"parameter":@1});bool gateSource=false;for(NSDictionary *source in sources[@"sources"])gateSource|=[source[@"kind"] isEqual:@"pattern-gate"];check(gateSource,"Parameter activity links gate definitions");}
  [[NSFileManager defaultManager] removeItemAtPath:path error:nil];[s shutdown];[reopened shutdown];
  std::cout<<"PASS gate API, atomic validation, stale/no-op/dry-run, combined Undo/Redo and exact native persistence\n";return 0;
}catch(const std::exception &e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}}
