#import "../Bridge/TrackerSession.h"
#include "../Audio/AudioUnitHost.hpp"
#include "editor/TrackerDocument.hpp"
#include <iostream>
using namespace Tracker;
static void check(bool b,const char *message){if(!b)throw std::runtime_error(message);}
int main(){@autoreleasepool{try{
  TrackerSession *session=[TrackerSession new];NSError *error=nil;
  auto call=[&](NSString *method,NSDictionary *parameters,bool write=false)->NSDictionary *{auto p=[parameters mutableCopy];if(write)p[@"expectedRevision"]=session.automationRevision;auto response=[session automationMethod:method params:p error:&error];if(!response)throw std::runtime_error(error.localizedDescription.UTF8String);return response;};
  auto data=[&](){return call(@"graph.get",@{})[@"data"];};
  auto empty=data();NSString *revision=session.automationRevision;
  call(@"graph.create",@{@"name":@"Echo",@"dryRun":@YES},true);check([revision isEqual:session.automationRevision]&&[empty isEqual:data()],"Graph preview mutates document");
  NSString *graph=call(@"graph.create",@{@"name":@"Echo"},true)[@"data"][@"graph"];
  NSString *node=call(@"graph.node.add",@{@"graph":graph,@"kind":@"plugin",@"plugin":@{@"format":@"Built-in",@"classID":@"resonance.gainer.v1"}},true)[@"data"][@"node"];
  NSMutableDictionary *definition=[data()[@"library"][0] mutableCopy];NSArray *nodes=definition[@"nodes"];
  definition[@"audio"]=@[@{@"source":nodes[0][@"id"],@"target":node},@{@"source":node,@"target":nodes[1][@"id"]}];
  call(@"graph.update",@{@"definition":definition},true);
  auto metadata=call(@"graph.plugin.get",@{@"graph":graph,@"node":node})[@"data"];
  check([metadata[@"parameters"] count]>=2&&[metadata[@"buses"] count]>=2,"Graph recipe exposes parameters and ports");
  revision=session.automationRevision;
  call(@"graph.plugin.set",@{@"graph":graph,@"node":node,@"parameters":@[@{@"id":@1,@"value":@(-6)}],@"dryRun":@YES},true);
  check([revision isEqual:session.automationRevision],"Plugin recipe preview committed");
  call(@"graph.plugin.set",@{@"graph":graph,@"node":node,@"parameters":@[@{@"id":@1,@"value":@(-6)}]},true);
  auto parameters=call(@"graph.plugin.get",@{@"graph":graph,@"node":node})[@"data"][@"parameters"];
  check([parameters[1][@"value"] doubleValue]==-6,"Graph plugin parameter did not persist in recipe");
  call(@"graph.layout.set",@{@"positions":@[@{@"node":@"test-node",@"x":@125,@"y":@250}]},true);
  check([data()[@"layout"] count]==1,"Graph layout missing from API");
  call(@"mixer.enable",@{},true);NSString *target=data()[@"mixer"][@"buses"][0][@"id"];
  call(@"graph.assign",@{@"target":target,@"graph":graph,@"amount":@.4,@"wet":@.7},true);
  definition=[data()[@"library"][0] mutableCopy];NSMutableArray *audio=[definition[@"audio"] mutableCopy];[audio addObject:@{@"source":nodes[0][@"id"],@"target":nodes[1][@"id"],@"output":@1,@"input":@1}];definition[@"audio"]=audio;call(@"graph.update",@{@"definition":definition},true);
  NSString *side=data()[@"mixer"][@"buses"][1][@"id"],*master=[data()[@"mixer"][@"buses"] lastObject][@"id"];
  call(@"graph.routes.set",@{@"inputs":@[@{@"source":side,@"target":target,@"input":@1}],@"outputs":@[@{@"source":target,@"target":master,@"output":@1}]},true);
  call(@"graph.commands.set",@{@"pattern":@0,@"lanes":@[@{@"target":target,@"count":@2}],@"commands":@[@{@"target":target,@"graph":graph,@"kind":@"start",@"position":@32768,@"amount":@.2},@{@"target":target,@"kind":@"clear",@"position":@65536}]},true);
  auto expected=data();revision=session.automationRevision;
  auto reject=[&](NSString *method,NSDictionary *parameters){auto p=[parameters mutableCopy];p[@"expectedRevision"]=session.automationRevision;NSError *failure=nil;check(![session automationMethod:method params:p error:&failure],"Invalid graph edit accepted");check([revision isEqual:session.automationRevision]&&[expected isEqual:data()],"Rejected graph edit partially committed");};
  reject(@"graph.routes.set",@{@"inputs":@[@{@"source":master,@"target":target,@"input":@1}]});
  reject(@"graph.plugin.set",@{@"graph":graph,@"node":node,@"parameters":@[@{@"id":@1,@"value":@999}]});
  reject(@"graph.layout.set",@{@"positions":@[@{@"node":@"bad",@"x":@(-1),@"y":@0}]});
  reject(@"graph.remove",@{@"graph":graph});
  reject(@"graph.commands.set",@{@"pattern":@0,@"lanes":@[@{@"target":target,@"count":@0}]});
  definition=[expected[@"library"][0] mutableCopy];definition[@"audio"]=@[@{@"source":node,@"target":node}];reject(@"graph.update",@{@"definition":definition});
  call(@"history.undo",@{@"domain":@"document"},true);check([data()[@"commands"] count]==0,"Undo failed to remove command transaction");call(@"history.redo",@{@"domain":@"document"},true);check([expected isEqual:data()],"Redo lost graph identities or values");
  NSString *path=[NSTemporaryDirectory() stringByAppendingPathComponent:[NSUUID.UUID.UUIDString stringByAppendingString:@".resonance"]];
  check([session savePath:path error:&error]&&[session openPath:path error:&error],"Graph project save/reload failed");check([expected isEqual:data()],"Graph project roundtrip lost data");
  call(@"graph.clone",@{@"graph":graph,@"name":@"Echo copy"},true);auto copied=data()[@"library"][1];check(![copied[@"id"] isEqual:graph]&&![copied[@"nodes"][0][@"id"] isEqual:expected[@"library"][0][@"nodes"][0][@"id"]],"Cloned graph reused identities");
  [[NSFileManager defaultManager] removeItemAtPath:path error:nil];
  std::cout<<"PASS graph API: preview, identity allocation, guarded atomic edits, assignment and group-capable lanes, Undo/Redo, recipe/state project roundtrip, independent clone identities\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}}
