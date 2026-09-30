#import "../Bridge/TrackerSession.h"
#include "editor/TrackerDocument.hpp"
#include <cmath>
#include <iostream>
static void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
int main(){@autoreleasepool {try {
  Tracker::Document document;auto cell=document.cell(0,1,0);cell.note=49;
  document.edit({{0,1,0,{},cell}});auto first=document.historyHead(false);
  auto external=document.externalHistoryEdit();cell.note=51;document.edit({{0,2,0,{},cell}});
  check(first<external && external<document.historyHead(false),"Document and host edits share chronological ordering");
  auto last=document.historyHead(false);document.undo();check(document.historyHead(true)==last,"Undo preserves entry identity");
  document.externalHistoryEdit();check(!document.canRedo(),"A host edit forks document redo");

  Tracker::Document gestureDocument;
  gestureDocument.annotate([](auto &n){n.signal.layout["gesture"]={1,2};});
  const auto sequence=gestureDocument.historySequence();auto gestureBefore=gestureDocument.native();
  try {gestureDocument.annotate([](auto &n){n.signal.layout["gesture"]={3,4};},[]{throw std::runtime_error("Publication refused");},sequence);check(false,"Rejected publication must throw");}catch(const std::runtime_error &){}
  check(gestureDocument.native()==gestureBefore && gestureDocument.historyHead(false)==sequence,"Failed coalesced publication preserves model and history");
  gestureDocument.undo();check(gestureDocument.native().signal.layout.empty(),"Failed coalesced publication retains original before state");

  TrackerSession *session=[TrackerSession new];[session newSong:YES];NSError *error=nil;
  auto call=[&](NSString *method,NSDictionary *values,bool write=false)->NSDictionary *{
    auto params=[values mutableCopy];if(write)params[@"expectedRevision"]=session.automationRevision;
    auto r=[session automationMethod:method params:params error:&error];if(!r)throw std::runtime_error(error.localizedDescription.UTF8String);return r;
  };
  NSDictionary *descriptor=nil;for(NSDictionary *p in session.builtInPlugins)if([p[@"classID"] isEqual:@"resonance.gainer.v1"])descriptor=p;
  check(descriptor!=nil,"Gainer fixture exists");check([session addPlugin:descriptor error:&error],"Add plugin");
  auto gain=[&](){for(NSDictionary *p in [session pluginParameters:0])if([p[@"id"] integerValue]==1)return [p[@"value"] doubleValue];return 999.0;};
  auto notes=[&](){return [session snapshot:0][@"cells"];};
  id initialNotes=notes();
  check([session editPattern:0 row:1 channel:0 values:@[@52,@1,@0,@0,@0,@0] error:&error],"Write a note after adding plugin");
  id changedNotes=notes();check(![initialNotes isEqual:changedNotes],"Pattern fixture changed");
  check([session pluginParameter:0 identifier:1 value:-12 record:NO error:&error],"Live parameter edit");
  call(@"history.undo",@{},true);check(std::abs(gain())<1e-6,"Global Undo restores the latest live parameter");check([notes() isEqual:changedNotes],"Parameter Undo preserves pattern edit");
  call(@"history.undo",@{},true);check([notes() isEqual:initialNotes],"Next Undo restores pattern");
  call(@"history.undo",@{},true);check([[session snapshot:0][@"nativePlugins"] count]==0,"Next Undo removes added plugin");
  call(@"history.redo",@{},true);call(@"history.redo",@{},true);call(@"history.redo",@{},true);
  check(std::abs(gain()+12)<1e-6 && [notes() isEqual:changedNotes],"Redo replays interleaved domains in order");
  [session parameterGesture:YES];check([session pluginParameter:0 identifier:1 value:-6 record:NO error:&error],"Begin slider gesture");check([session pluginParameter:0 identifier:1 value:-18 record:NO error:&error],"Continue gesture");[session parameterGesture:NO];
  call(@"history.undo",@{},true);check(std::abs(gain()+12)<1e-6,"A complete slider drag is one Undo");
  check([session editPattern:0 row:2 channel:0 values:@[@55,@1,@0,@0,@0,@0] error:&error],"Branch with a pattern edit");
  check(!session.canRedo,"A document edit discards plugin redo");
  call(@"history.undo",@{},true);check(session.canRedo,"Document branch has redo");
  check([session pluginParameter:0 identifier:1 value:-3 record:NO error:&error],"Branch with a plugin edit");
  check(!session.canRedo,"A plugin edit discards document redo");
  NSString *path=[NSTemporaryDirectory() stringByAppendingPathComponent:[NSUUID.UUID.UUIDString stringByAppendingString:@".screamseq"]];
  check([session savePath:path error:&error],"Save mixed edits");
  check([session openPath:path error:&error],"Reopen mixed edits");check(std::abs(gain()+3)<1e-6,"Saved manual parameter survives reopen");
  check(!session.canUndo && !session.canRedo,"New document cannot inherit old session history");[[NSFileManager defaultManager] removeItemAtPath:path error:nil];
  [session newSong:YES];
  auto clean=session.automationRevision;
  auto routing=call(@"mixer.get",@{@"includeImplicit":@YES})[@"data"];
  check([routing[@"implicit"] boolValue] && [routing[@"buses"] count]==9,"Implicit channel/master paths are visible before any routing edit");
  check([session.automationRevision isEqual:clean]&&!session.canUndo,"Inspecting default routing does not dirty the song");
  auto target=routing[@"buses"][2][@"id"];
  call(@"plugin.add",@{@"descriptor":descriptor,@"target":target},true);
  check([call(@"mixer.get",@{})[@"data"][@"active"] boolValue],"Adding a channel effect materializes routing");
  check([call(@"mixer.get",@{})[@"data"][@"buses"][2][@"inserts"] count]==1,"Effect added directly to channel 3");
  call(@"history.undo",@{},true);
  check([[session snapshot:0][@"nativePlugins"] count]==0 && ![call(@"mixer.get",@{})[@"data"][@"active"] boolValue],"One Undo removes effect and initial routing together");
  call(@"history.redo",@{},true);
  check([[session snapshot:0][@"nativePlugins"] count]==1 && [call(@"mixer.get",@{})[@"data"][@"buses"][2][@"inserts"] count]==1,"One Redo restores effect and route");
  auto firstPlugin=[session snapshot:0][@"nativePlugins"][0][@"instanceID"];
  call(@"plugin.add",@{@"descriptor":descriptor,@"target":target,@"before":firstPlugin,@"position":@{@"x":@440,@"y":@220}},true);
  auto insertedPlugin=[session snapshot:0][@"nativePlugins"][1][@"instanceID"];
  auto insertedRouting=call(@"mixer.get",@{})[@"data"];
  check([insertedRouting[@"buses"][2][@"inserts"][0] isEqual:insertedPlugin] && [insertedRouting[@"buses"][2][@"inserts"][1] isEqual:firstPlugin],"Add at cable inserts before the exact stable processor");
  check([call(@"graph.get",@{})[@"data"][@"layout"] count]==1,"Add at cable saves placement with the insertion");
  call(@"history.undo",@{},true);
  check([[session snapshot:0][@"nativePlugins"] count]==1 && [call(@"graph.get",@{})[@"data"][@"layout"] count]==0,"One Undo removes inserted plugin, route and position");
  auto rejectedRevision=session.automationRevision;NSError *insertionError=nil;
  check(![session addPlugin:descriptor target:target before:@"missing" position:nil error:&insertionError] && [rejectedRevision isEqual:session.automationRevision],"Missing insertion anchor changes neither song nor plugin list");
  auto beforeFailure=session.automationRevision;NSError *invalid=nil;
  check(![session addPlugin:descriptor target:@"n9999999" error:&invalid] && [session.automationRevision isEqual:beforeFailure],"Invalid destination leaves plugin, route and history unchanged");
  call(@"plugin.add",@{@"descriptor":descriptor},true);call(@"plugin.add",@{@"descriptor":descriptor},true);
  auto original=[[session snapshot:0][@"nativePlugins"] valueForKey:@"instanceID"];
  call(@"plugin.move",@{@"slot":@0,@"position":@2},true);
  auto moved=[[session snapshot:0][@"nativePlugins"] valueForKey:@"instanceID"];
  check([moved[2] isEqual:original[0]]&&[moved[0] isEqual:original[1]]&&[moved[1] isEqual:original[2]],"Rack moves insert in order, rather than swapping endpoints");
  call(@"history.undo",@{},true);check([[[session snapshot:0][@"nativePlugins"] valueForKey:@"instanceID"] isEqual:original],"Rack drag is one undoable edit");
  auto stable=[session snapshot:0][@"nativePlugins"][0][@"instanceID"];
  call(@"plugin.parameters.set",@{@"plugin":stable,@"values":@[@{@"id":@1,@"value":@-8},@{@"id":@3,@"value":@1}]},true);
  check(std::abs(gain()+8)<1e-6,"API batch targets the stable processor and updates without rebuilding it");
  auto noOpRevision=session.automationRevision;
  call(@"plugin.parameters.set",@{@"plugin":stable,@"values":@[@{@"id":@1,@"value":@-8},@{@"id":@3,@"value":@1}]},true);
  check([noOpRevision isEqual:session.automationRevision],"Unchanged parameter batches do not create history");
  auto invalidBatch=[@{@"plugin":stable,@"values":@[@{@"id":@1,@"value":@-2},@{@"id":@999999,@"value":@1}],@"expectedRevision":session.automationRevision} mutableCopy];
  check(![session automationMethod:@"plugin.parameters.set" params:invalidBatch error:&error] && std::abs(gain()+8)<1e-6 && [noOpRevision isEqual:session.automationRevision],"One invalid parameter rejects the entire batch");
  call(@"history.undo",@{},true);check(std::abs(gain())<1e-6,"API batch is one chronological Undo");
  call(@"history.redo",@{},true);check(std::abs(gain()+8)<1e-6,"API batch Redo restores manual state");
  [session parameterGesture:YES];
  call(@"plugin.parameters.set",@{@"plugin":stable,@"values":@[@{@"id":@1,@"value":@-10}]},true);
  call(@"plugin.parameters.set",@{@"plugin":stable,@"values":@[@{@"id":@1,@"value":@-14}]},true);
  [session parameterGesture:NO];call(@"history.undo",@{},true);check(std::abs(gain()+8)<1e-6,"Graph sidebar slider changes share one Undo gesture");
  NSDictionary *conflict=@{@"plugin":stable,@"slot":@0};
  check(![session automationMethod:@"plugin.parameters.get" params:conflict error:&error],"Ambiguous plugin identity and slot rejected");
  call(@"plugin.move",@{@"slot":@0,@"position":@2},true);
  auto identityValues=call(@"plugin.parameters.get",@{@"plugin":stable})[@"data"];
  bool correct=false;for(NSDictionary *value in identityValues)if([value[@"id"] intValue]==1)correct=std::abs([value[@"value"] doubleValue]+8)<1e-6;
  check(correct,"Stable parameter target follows its plugin after rack reordering");
  [session shutdown];std::cout<<"PASS unified chronological history, live parameter gestures, cross-domain branches and persistence\n";return 0;
}catch(const std::exception &e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}}
