#import "../Bridge/TrackerSession.h"
#include "editor/TrackerDocument.hpp"
#include "../Audio/AudioUnitHost.hpp"
#include "soundlib/ModInstrument.h"
#include <cmath>
#include <iostream>
#include <chrono>
#include <thread>
static void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
static void mixerControlHistory() {
  TrackerSession *session=[TrackerSession new];[session newSong:YES];
  auto call=[&](NSString *method,NSDictionary *values,bool write=false)->NSDictionary * {
    NSError *error=nil;auto params=[values mutableCopy];
    if(write)params[@"expectedRevision"]=session.automationRevision;
    auto response=[session automationMethod:method params:params error:&error];
    if(!response)throw std::runtime_error(std::string(method.UTF8String)+": "+error.localizedDescription.UTF8String);
    return response[@"data"];
  };
  call(@"mixer.enable",@{},true);
  NSDictionary *baseline=call(@"mixer.get",@{});
  NSString *bus=baseline[@"buses"][0][@"id"];
  NSString *revision=session.automationRevision;
  for(NSNumber *value in @[@-3,@-12,@-9])call(@"mixer.bus.set",@{@"bus":bus,@"gainDB":value,@"preview":@YES},true);
  check([session.automationRevision isEqual:revision] &&
    [call(@"mixer.get",@{})[@"buses"] isEqual:baseline[@"buses"]],"Mixer previews retain saved data and revision");
  call(@"mixer.bus.set",@{@"bus":bus,@"gainDB":@0},true);
  check([session.automationRevision isEqual:revision],"Saved-frame reset after previews creates no history");
  NSError *error=nil;
  check(![session automationMethod:@"mixer.bus.set" params:@{@"bus":bus,@"gainDB":@-6,@"pan":@2,@"expectedRevision":revision} error:&error],
    "Invalid simultaneous control patch is rejected");
  check([session.automationRevision isEqual:revision] &&
    [call(@"mixer.get",@{})[@"buses"] isEqual:baseline[@"buses"]],"Invalid control patch cannot partly commit");
  NSDictionary *values=@{@"bus":bus,@"gainDB":@-6.123456789,@"preGainDB":@-3,@"prePan":@-.5,
    @"pan":@.25,@"width":@1.5,@"mute":@YES,@"solo":@YES};
  auto dry=[values mutableCopy];dry[@"dryRun"]=@YES;call(@"mixer.bus.set",dry,true);
  check([session.automationRevision isEqual:revision],"Dry control patch creates no history");
  call(@"mixer.bus.set",values,true);
  NSDictionary *changed=call(@"mixer.get",@{});
  NSDictionary *edited=changed[@"buses"][0];
  for(NSString *key in @[@"gainDB",@"preGainDB",@"prePan",@"pan",@"width",@"mute",@"solo"])
    check([edited[key] isEqual:values[key]],"Mixer candidate preserves the exact supplied control values");
  revision=session.automationRevision;call(@"mixer.bus.set",values,true);
  check([session.automationRevision isEqual:revision],"Repeated exact control values create no history");
  call(@"history.undo",@{},true);
  check([call(@"mixer.get",@{})[@"buses"] isEqual:baseline[@"buses"]],"One Undo reverses the complete control patch");
  revision=session.automationRevision;
  call(@"mixer.bus.set",@{@"bus":bus,@"gainDB":@-24,@"preview":@YES},true);
  call(@"mixer.bus.set",@{@"bus":bus,@"gainDB":@0},true);
  check(session.canRedo && [session.automationRevision isEqual:revision],"Preview and saved-frame reset preserve Redo");
  call(@"history.redo",@{},true);
  check([call(@"mixer.get",@{})[@"buses"] isEqual:changed[@"buses"]],"Redo restores every control exactly");
  call(@"history.undo",@{},true);call(@"history.undo",@{},true);
  check(![call(@"mixer.get",@{})[@"active"] boolValue],"Previews, invalid edits and no-ops leave no phantom Undo entries");
  [session shutdown];
}
static void liveHistory() {
  auto doc=Tracker::Document::demo();doc->transaction([](OpenMPT::CSoundFile &song){song.m_nInstruments=4;for(OpenMPT::INSTRUMENTINDEX i=1;i<=4;++i)song.Instruments[i]=new OpenMPT::ModInstrument(i);});doc->song().m_nDefaultGlobalVolume=0;
  NSString *path=[NSTemporaryDirectory() stringByAppendingPathComponent:[NSUUID.UUID.UUIDString stringByAppendingString:@".mptm"]];doc->save(path.UTF8String);
  TrackerSession *session=[TrackerSession new];NSError *error=nil;
  check([session openPath:path error:&error],"Open silent live-history fixture");[NSFileManager.defaultManager removeItemAtPath:path error:nil];
  auto call=[&](NSString *method,NSDictionary *values,bool write=true)->NSDictionary *{
    auto params=[values mutableCopy];if(write)params[@"expectedRevision"]=session.automationRevision;
    auto r=[session automationMethod:method params:params error:&error];if(!r)throw std::runtime_error(std::string(method.UTF8String)+": "+error.localizedDescription.UTF8String);return r;
  };
  NSDictionary *descriptor=nil;for(NSDictionary *p in session.builtInPlugins)if([p[@"classID"] isEqual:@"resonance.gainer.v1"])descriptor=p;
  const auto target=call(@"mixer.get",@{@"includeImplicit":@YES},false)[@"data"][@"buses"][2][@"id"];
  NSString *recipe=call(@"graph.create",@{@"name":@"Live recipe bank"})[@"data"][@"graph"];
  NSString *recipeCurve=call(@"graph.node.add",@{@"graph":recipe,@"kind":@"automation"})[@"data"][@"node"];
  check([session playOrder:0 error:&error],"Start actual silent CoreAudio transport with implicit mixer");
  uint64_t frames=0;
  auto settled=[&] {
    for(unsigned i=0;i<200;++i){std::this_thread::sleep_for(std::chrono::milliseconds(5));
      const auto routing=session.routingTelemetry;
      check(session.playing&&![@"failed" isEqual:routing[@"state"]],"Topology edit retains active successful transport");
      const auto now=[session.telemetry[@"frames"] unsignedLongLongValue];
      if([@"stable" isEqual:routing[@"state"]]&&now>frames){frames=now;return;}
    }
    throw std::runtime_error("Live topology did not settle while advancing transport");
  };
  settled();
  const auto orderNoOpRevision=session.automationRevision;
  call(@"order.edit",@{@"order":@0,@"operation":@"move",@"destination":@0});
  check([session.automationRevision isEqual:orderNoOpRevision],"Live order no-op preserves revision");
  settled(); // Requires the same running transport to keep advancing frames.
  const auto source=call(@"graph.song.source.add",@{@"source":@{@"kind":@"amount",@"name":@"Live implicit source",@"amount":@.2}})[@"data"][@"node"];settled();
  check([call(@"graph.get",@{},false)[@"data"][@"mixer"][@"buses"] count]==0,"Adding live song controls preserves implicit document routing");
  call(@"graph.song.source.update",@{@"node":source,@"source":@{@"amount":@.6}});settled();
  call(@"history.undo",@{});settled();call(@"history.redo",@{});settled();
  call(@"graph.song.source.remove",@{@"nodes":@[source]});settled();
  call(@"plugin.add",@{@"descriptor":descriptor,@"target":target});settled();
  check([[session snapshot:0][@"nativePlugins"] count]==1,"First effect added during running implicit routing");
  NSString *routeKey=nil;for(NSDictionary *port in call(@"graph.signal.get",@{},false)[@"data"][@"ports"]){NSDictionary *route=port[@"route"];if([route[@"kind"] isEqual:@"insert"]&&[route[@"source"] isEqual:target]){
    check([route[@"tap"] isEqual:@"main-path"]&&[route[@"gainDB"] doubleValue]==0&&[port[@"available"] boolValue]&&[port[@"fresh"] boolValue],"Actual route API distinguishes exact serial main path and adopted metadata");routeKey=port[@"key"];break;}}
  check(routeKey!=nil,"Actual playback exposes a stable route observation key");
  call(@"graph.scope.watch",@{@"port":routeKey});call(@"graph.listen.set",@{@"port":routeKey});settled();
  check([call(@"graph.scope.get",@{},false)[@"data"][@"frames"] unsignedIntegerValue]>0,"Actual route scope captures current full-rate PCM");
  call(@"history.undo",@{});settled();check([[session snapshot:0][@"nativePlugins"] count]==0,"Grouped live Undo removes first effect and explicit routing");
  check(![call(@"graph.scope.get",@{},false)[@"data"][@"available"] boolValue]&&![call(@"graph.listen.get",@{},false)[@"data"][@"available"] boolValue],"Removed route scope/listen selections report unavailable rather than stale audio");
  call(@"history.redo",@{});settled();check([[session snapshot:0][@"nativePlugins"] count]==1,"Grouped live Redo restores first effect and route");
  check([call(@"graph.scope.get",@{},false)[@"data"][@"fresh"] boolValue]&&[call(@"graph.listen.get",@{},false)[@"data"][@"available"] boolValue],"Redo restores the same selected route with fresh capture");
  call(@"graph.scope.watch",@{@"port":NSNull.null});call(@"graph.listen.set",@{@"port":NSNull.null});settled();
  const auto first=[session snapshot:0][@"nativePlugins"][0][@"instanceID"];
  call(@"plugin.parameters.set",@{@"plugin":first,@"values":@[@{@"id":@1,@"value":@-9}]});
  call(@"plugin.add",@{@"descriptor":descriptor,@"target":target});settled();
  const auto second=[session snapshot:0][@"nativePlugins"][1][@"instanceID"];
  call(@"plugin.move",@{@"slot":@0,@"position":@1});settled();
  bool same=false;for(NSDictionary *p in call(@"plugin.parameters.get",@{@"plugin":first},false)[@"data"])if([p[@"id"] intValue]==1)same=std::abs([p[@"value"] doubleValue]+9)<1e-5;
  check(same,"Live rack reorder preserves authoritative manually edited processor state");
  const auto revision=session.automationRevision;
  check(![session automationMethod:@"mixer.bus.set" params:@{@"bus":target,@"inserts":@[second,first],@"expectedRevision":revision} error:&error] && [revision isEqual:session.automationRevision],"Cyclic union order change rejects before model/history changes");settled();
  call(@"plugin.remove",@{@"plugins":@[first]});settled();
  call(@"history.undo",@{});settled();check([[session snapshot:0][@"nativePlugins"] count]==2,"Grouped live remove Undo restores retained processor and channel route");
  call(@"history.redo",@{});settled();
  // One linked bank template can update a root source and a normal parameter
  // lane together, including history while a real callback owns both snapshots.
  NSString *curve=call(@"graph.song.source.add",@{@"source":@{@"kind":@"automation",@"name":@"Live bank"}})[@"data"][@"node"];settled();
  NSDictionary *curveTarget=@{@"kind":@"graph",@"graph":NSNull.null,@"node":curve,@"pattern":@0};
  NSDictionary *parameterTarget=@{@"kind":@"parameter",@"pattern":@0,@"plugin":second,@"parameter":@1};
  call(@"graph.automation.set",@{@"graph":NSNull.null,@"node":curve,@"pattern":@0,@"points":@[@{@"position":@0,@"value":@.1},@{@"position":@256,@"value":@.2}]});settled();
  NSString *bank=call(@"envelope.bank.save",@{@"target":curveTarget,@"name":@"Live shared shape"})[@"data"][@"id"];
  call(@"envelope.bank.apply",@{@"template":bank,@"target":curveTarget,@"linked":@YES});settled();
  call(@"envelope.bank.apply",@{@"template":bank,@"target":parameterTarget,@"linked":@YES});settled();
  NSDictionary *oldCurve=call(@"envelope.bank.list",@{@"target":curveTarget},false)[@"data"],*oldParameter=call(@"envelope.bank.list",@{@"target":parameterTarget},false)[@"data"];
  auto shape=[oldCurve[@"shape"] mutableCopy];auto points=[shape[@"points"] mutableCopy];auto point=[points[0] mutableCopy];point[@"value"]=@.6;points[0]=point;shape[@"points"]=points;
  call(@"envelope.bank.save",@{@"id":bank,@"name":@"Updated shared shape",@"shape":shape});settled();
  NSDictionary *newCurve=call(@"envelope.bank.list",@{@"target":curveTarget},false)[@"data"],*newParameter=call(@"envelope.bank.list",@{@"target":parameterTarget},false)[@"data"];
  check([newCurve[@"shape"][@"points"][0][@"value"] doubleValue]==.6 && [newParameter[@"shape"][@"points"][0][@"value"] doubleValue]==.6,"Linked source and lane commit in one playing document edit");
  call(@"history.undo",@{});settled();check([oldCurve isEqual:call(@"envelope.bank.list",@{@"target":curveTarget},false)[@"data"]] && [oldParameter isEqual:call(@"envelope.bank.list",@{@"target":parameterTarget},false)[@"data"]],"Live combined Undo restores complete template and both uses");
  call(@"history.redo",@{});settled();check([newCurve isEqual:call(@"envelope.bank.list",@{@"target":curveTarget},false)[@"data"]] && [newParameter isEqual:call(@"envelope.bank.list",@{@"target":parameterTarget},false)[@"data"]],"Live combined Redo restores complete template and both uses");
  NSDictionary *recipeTarget=@{@"kind":@"graph",@"graph":recipe,@"node":recipeCurve,@"pattern":@0};
  call(@"envelope.bank.unlink",@{@"target":curveTarget});settled();
  call(@"envelope.bank.apply",@{@"template":bank,@"target":recipeTarget,@"linked":@YES});settled();
  auto recipeShape=[call(@"envelope.bank.list",@{@"target":recipeTarget},false)[@"data"][@"shape"] mutableCopy];auto recipePoints=[recipeShape[@"points"] mutableCopy];auto recipePoint=[recipePoints[0] mutableCopy];recipePoint[@"value"]=@.35;recipePoints[0]=recipePoint;recipeShape[@"points"]=recipePoints;
  call(@"envelope.bank.save",@{@"id":bank,@"name":@"Recipe and rack template",@"shape":recipeShape});settled();
  check([call(@"envelope.bank.list",@{@"target":recipeTarget},false)[@"data"][@"shape"][@"points"][0][@"value"] doubleValue]==.35 && [call(@"envelope.bank.list",@{@"target":parameterTarget},false)[@"data"][@"shape"][@"points"][0][@"value"] doubleValue]==.35,"Live bank save updates reusable recipe curve and rack lane together");
  call(@"history.undo",@{});settled();call(@"history.redo",@{});settled();
  call(@"plugin.add",@{@"descriptor":descriptor,@"detached":@YES});settled();
  NSString *loose=[session snapshot:0][@"nativePlugins"][1][@"instanceID"];
  check([call(@"mixer.get",@{},false)[@"data"][@"detached"] containsObject:loose],"Live no-selection Add stays unconnected");
  call(@"mixer.inserts.move",@{@"plugins":@[loose],@"target":target});settled();
  call(@"history.undo",@{});settled();check([call(@"mixer.get",@{},false)[@"data"][@"detached"] containsObject:loose],"Undo live insertion restores silent processing slot");
  call(@"history.redo",@{});settled();
  call(@"mixer.inserts.detach",@{@"plugins":@[loose]});settled();
  check([call(@"mixer.get",@{},false)[@"data"][@"detached"] containsObject:loose],"Live detach pulls existing processor out and heals main path");
  call(@"history.undo",@{});settled();check(![call(@"mixer.get",@{},false)[@"data"][@"detached"] containsObject:loose],"Live Undo restores detached processor to original channel");
  call(@"history.redo",@{});settled();call(@"history.undo",@{});settled();
  // Install the graph while stopped, then keep the real callback running for
  // full opaque recipe replacement and both directions of unified history.
  [session stop];
  NSString *recipePlugin=call(@"graph.node.add",@{@"graph":recipe,@"kind":@"plugin",@"plugin":@{@"format":@"Built-in",@"classID":@"resonance.gainer.v1"},@"insertEdge":@0})[@"data"][@"node"];
  call(@"graph.assign",@{@"target":target,@"graph":recipe});
  call(@"graph.instrument.assign",@{@"instrument":@1,@"graph":recipe});
  check([session playOrder:0 error:&error],"Start assigned recipe preset fixture");frames=0;settled();
  call(@"graph.song.source.add",@{@"source":@{@"kind":@"lfo",@"name":@"Source added after engine construction"}});settled();
  call(@"graph.plugin.set",@{@"graph":recipe,@"node":recipePlugin,@"parameters":@[@{@"id":@1,@"value":@-3}]});settled();
  call(@"graph.plugin.set",@{@"graph":recipe,@"node":recipePlugin,@"parameters":@[@{@"id":@1,@"value":@-6}]});settled();
  call(@"history.undo",@{});settled();call(@"history.redo",@{});settled();
  const auto recipeBeforeBypass=call(@"graph.plugin.get",@{@"graph":recipe,@"node":recipePlugin},false)[@"data"];
  const auto bypassRevision=session.automationRevision;
  call(@"graph.plugin.bypass",@{@"graph":recipe,@"node":recipePlugin,@"bypass":@YES,@"dryRun":@YES});
  check([session.automationRevision isEqual:bypassRevision]&&![call(@"graph.plugin.get",@{@"graph":recipe,@"node":recipePlugin},false)[@"data"][@"bypass"] boolValue],"Bypass preview cannot change the live recipe or history");
  call(@"graph.plugin.bypass",@{@"graph":recipe,@"node":recipePlugin,@"bypass":@YES});settled();
  auto recipeBypassed=call(@"graph.plugin.get",@{@"graph":recipe,@"node":recipePlugin},false)[@"data"];
  check([recipeBypassed[@"bypass"] boolValue]&&[recipeBypassed[@"parameters"] isEqual:recipeBeforeBypass[@"parameters"]],"Live recipe bypass preserves its manual parameter state");
  call(@"history.undo",@{});settled();
  check(![call(@"graph.plugin.get",@{@"graph":recipe,@"node":recipePlugin},false)[@"data"][@"bypass"] boolValue],"Live recipe bypass Undo restores the enabled state");
  call(@"history.redo",@{});settled();
  check([call(@"graph.plugin.get",@{@"graph":recipe,@"node":recipePlugin},false)[@"data"][@"bypass"] boolValue],"Live recipe bypass Redo restores the requested bypass");
  call(@"graph.plugin.bypass",@{@"graph":recipe,@"node":recipePlugin,@"bypass":@NO});settled();
  auto currentRecipe=[&]()->NSDictionary *{for(NSDictionary *entry in call(@"graph.get",@{},false)[@"data"][@"library"])if([entry[@"id"] isEqual:recipe])return entry;return nil;};
  NSDictionary *recipeBeforeSource=currentRecipe();NSString *recipeInput=nil;for(NSDictionary *entry in recipeBeforeSource[@"nodes"])if([entry[@"kind"] isEqual:@"input"])recipeInput=entry[@"id"];
  check(recipeInput!=nil,"Find the actual recipe input socket");
  NSString *follower=call(@"graph.node.add",@{@"graph":recipe,@"kind":@"follower",@"audioInput":@{@"node":recipeInput,@"port":@0},@"connect":@{@"node":recipePlugin,@"port":@1,@"output":@NO,@"modulation":@YES,@"base":@.5}})[@"data"][@"node"];settled();
  NSDictionary *recipeWithFollower=currentRecipe();check([recipeWithFollower[@"modulation"] count]==1,"Audio-to-parameter gesture installs a source and both wires during real playback");
  NSString *copyKey=nil;for(NSDictionary *copy in call(@"parameter.activity.targets",@{},false)[@"data"][@"targets"])if([copy[@"graph"] isEqual:recipe]&&[copy[@"node"] isEqual:recipePlugin]){copyKey=copy[@"key"];break;}
  check(copyKey!=nil,"Live source edit retains its processor observation identity");bool stableSource=false;
  for(NSDictionary *source in call(@"parameter.activity.sources",@{@"target":copyKey,@"parameter":@1},false)[@"data"][@"sources"])if([source[@"kind"] isEqual:@"graph-source"]&&[source[@"node"] isEqual:follower])stableSource=[source[@"id"] isEqual:follower];
  check(stableSource,"Recipe source API uses the same stable node identity as rendered provenance");
  auto cut=[recipeWithFollower mutableCopy];cut[@"modulation"]=@[];call(@"graph.update",@{@"definition":cut});settled();
  check([currentRecipe()[@"modulation"] count]==0,"A recipe cable cut applies while vendors keep processing");
  call(@"history.undo",@{});settled();check([recipeWithFollower isEqual:currentRecipe()],"Undo live control-cable cut restores stable source identities");
  call(@"history.redo",@{});settled();check([currentRecipe()[@"modulation"] count]==0,"Redo live control-cable cut preserves transport");
  call(@"graph.node.remove",@{@"graph":recipe,@"node":follower});settled();call(@"history.undo",@{});settled();call(@"history.redo",@{});settled();
  call(@"graph.node.add",@{@"graph":recipe,@"kind":@"note-envelope"});settled();call(@"history.undo",@{});settled();call(@"history.redo",@{});settled();
  call(@"graph.update",@{@"definition":recipeBeforeSource});settled();
  Tracker::PluginState presetState;for(const auto &d:Tracker::NativePlugin::builtins())if(d.classID=="resonance.gainer.v1")presetState.descriptor=d;
  Tracker::NativePlugin presetPlugin(presetState,48000,true);check(presetPlugin.parameter(1,-12),"Prepare full builtin vendor state");const auto saved=presetPlugin.state().state;
  auto definition=[call(@"graph.get",@{},false)[@"data"][@"library"][0] mutableCopy];auto nodes=[definition[@"nodes"] mutableCopy];
  for(NSUInteger i=0;i<[nodes count];++i)if([nodes[i][@"id"] isEqual:recipePlugin]){auto node=[nodes[i] mutableCopy];auto plugin=[node[@"plugin"] mutableCopy];plugin[@"state"]=[[NSData dataWithBytes:saved.data() length:saved.size()] base64EncodedStringWithOptions:0];plugin[@"parameters"]=@[];node[@"plugin"]=plugin;nodes[i]=node;}definition[@"nodes"]=nodes;
  auto presetSettled=[&]{const auto before=[session.telemetry[@"frames"] unsignedLongLongValue];do{settled();}while([session.telemetry[@"frames"] unsignedLongLongValue]<before+2048);};
  call(@"graph.update",@{@"definition":definition});presetSettled();call(@"history.undo",@{});presetSettled();call(@"history.redo",@{});presetSettled();
  NSDictionary *compressor=nil;for(NSDictionary *p in session.builtInPlugins)if([p[@"classID"] isEqual:@"resonance.compressor.v1"])compressor=p;
  call(@"plugin.add",@{@"descriptor":compressor,@"target":target});settled();
  NSString *detectorPlugin=[[session snapshot:0][@"nativePlugins"] lastObject][@"instanceID"];
  NSString *detectorSource=call(@"mixer.get",@{},false)[@"data"][@"buses"][0][@"id"];
  call(@"mixer.sidechains.set",@{@"plugin":detectorPlugin,@"input":@1,@"sources":@[@{@"source":detectorSource,@"gainDB":@0,@"preFader":@NO,@"enabled":@YES}]});settled();
  call(@"history.undo",@{});settled();call(@"history.redo",@{});settled();
  // Graph inspector parameter history is a scalar publication, including when
  // root modulation runs and autosave has folded the current manual baseline.
  NSString *motion=call(@"graph.song.source.add",@{@"source":@{@"kind":@"lfo",@"name":@"History LFO"}})[@"data"][@"node"];settled();
  call(@"graph.song.modulation.set",@{@"source":motion,@"plugin":second,@"parameter":@1,@"minimum":@0,@"maximum":@.1});settled();
  auto manual=[&](){for(NSDictionary *p in call(@"plugin.parameters.get",@{@"plugin":second},false)[@"data"])if([p[@"id"] intValue]==1)return [p[@"manualValue"] doubleValue];return 999.0;};
  const auto priorManual=manual();const auto beforeGraph=call(@"graph.get",@{},false)[@"data"];
  call(@"plugin.parameters.set",@{@"plugin":second,@"values":@[@{@"id":@1,@"value":@-10}]});settled();
  NSString *savedPath=[NSTemporaryDirectory() stringByAppendingPathComponent:[NSUUID.UUID.UUIDString stringByAppendingString:@".screamseq"]];
  check([session savePath:savedPath error:&error],"Autosave-equivalent state fold during live parameter history");settled();
  call(@"history.undo",@{});settled();check(std::abs(manual()-priorManual)<1e-5&&session.canRedo,"Live parameter Undo restores manual base with an active root LFO after save");
  call(@"history.redo",@{});settled();check(std::abs(manual()+10)<1e-5,"Live parameter Redo restores manual base without replacing the processor");
  const auto afterGraph=call(@"graph.get",@{},false)[@"data"];
  check([beforeGraph[@"songSources"] isEqual:afterGraph[@"songSources"]]&&[beforeGraph[@"songModulation"] isEqual:afterGraph[@"songModulation"]],"Numeric history preserves root modulation definitions and routes");
  [NSFileManager.defaultManager removeItemAtPath:savedPath error:nil];
  check([session.telemetry[@"callbacks"] unsignedLongLongValue]>0,"Real device callbacks serviced every publication");
  [session shutdown];std::cout<<"PASS live CoreAudio rack add/remove/reorder and grouped Undo/Redo, implicit mixer, retained manual state, unsafe-order rejection, root/recipe linked bank plus lane atomic publication, detached add/insert/history, opaque recipe preset/bypass history, live sample recipe knob after root-source edits, detector activation/history\n";
}
int main(int argc,char **argv){@autoreleasepool {try {
  if(argc>1&&std::string(argv[1])=="--device"){liveHistory();return 0;}
  mixerControlHistory();
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

  auto routes=gestureDocument.native();routes.ensureMixer();
  const auto targetBus=routes.mixer.buses.front().id;
  routes.mixer.buses.front().inserts={"removed","retained"};
  routes.mixer.sidechains={{targetBus,"removed",1},{targetBus,"retained",1}};
  routes.mixer.instruments={{"removed",targetBus,1},{"retained",targetBus,1}};
  const auto parent=routes.makeEntity().id,child=routes.makeEntity().id;
  routes.signal.groups={{parent,0,"Parent",0,0,{}},{child,parent,"Child",0,0,{"plugin:removed"}}};
  routes.signal.layout["plugin:removed"]={1,2};routes.signal.layout["plugin:retained"]={3,4};
  routes.removePluginRoutes("removed");
  check(routes.mixer.buses.front().inserts==std::vector<std::string>{"retained"} && routes.mixer.sidechains.size()==1 && routes.mixer.sidechains.front().plugin=="retained" && routes.mixer.instruments.size()==1 && routes.mixer.instruments.front().plugin=="retained","Removal prunes only that plugin's inserts, detector and auxiliary routes");
  check(routes.signal.groups.empty() && !routes.signal.layout.contains("plugin:removed") && routes.signal.layout.contains("plugin:retained"),"Removal prunes empty ancestors without touching another processor's placement");

  TrackerSession *session=[TrackerSession new];[session newSong:YES];NSError *error=nil;
  auto call=[&](NSString *method,NSDictionary *values,bool write=false)->NSDictionary *{
    auto params=[values mutableCopy];if(write)params[@"expectedRevision"]=session.automationRevision;
    auto r=[session automationMethod:method params:params error:&error];if(!r)throw std::runtime_error(std::string(method.UTF8String)+": "+error.localizedDescription.UTF8String);return r;
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
  const auto orderNoOpRevision=session.automationRevision;
  call(@"order.edit",@{@"order":@0,@"operation":@"move",@"destination":@0},true);
  check([session.automationRevision isEqual:orderNoOpRevision]&&session.canRedo,
    "Order API move-to-self preserves revision and the interleaved Redo branch");
  check([session editOrder:0 pattern:0 operation:@"assign" error:&error],"Unchanged order assignment succeeds");
  check([session.automationRevision isEqual:orderNoOpRevision]&&session.canRedo,
    "Native unchanged assignment preserves revision and the interleaved Redo branch");
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
  NSString *group=call(@"graph.song.group.create",@{@"nodes":@[[ @"plugin:" stringByAppendingString:firstPlugin]],@"name":@"Channel tools"},true)[@"data"][@"group"];
  call(@"plugin.add",@{@"descriptor":descriptor,@"target":target,@"parent":group,@"position":@{@"x":@600,@"y":@240}},true);
  auto groupedPlugin=[session snapshot:0][@"nativePlugins"][1][@"instanceID"];
  auto groupedGraph=call(@"graph.get",@{})[@"data"];
  check([groupedGraph[@"groups"][0][@"nodes"] containsObject:[@"plugin:" stringByAppendingString:groupedPlugin]],"Adding inside a song group preserves membership");
  call(@"history.undo",@{},true);
  check([[session snapshot:0][@"nativePlugins"] count]==1 && [call(@"graph.get",@{})[@"data"][@"groups"][0][@"nodes"] count]==1,"One Undo removes group addition and its route");
  call(@"history.redo",@{},true);
  auto beforeRemoveGraph=call(@"graph.get",@{})[@"data"],beforeRemoveMixer=call(@"mixer.get",@{})[@"data"];
  auto removalRevision=session.automationRevision;
  check(![session automationMethod:@"plugin.remove" params:@{@"plugins":@[firstPlugin,@"missing"],@"expectedRevision":removalRevision} error:&error] && [removalRevision isEqual:session.automationRevision],"Invalid batch removal changes nothing");
  call(@"plugin.remove",@{@"plugins":@[firstPlugin,groupedPlugin]},true);
  check([[session snapshot:0][@"nativePlugins"] count]==0 && [call(@"mixer.get",@{})[@"data"][@"buses"][2][@"inserts"] count]==0,"Batch removes processors and insert references");
  check([call(@"graph.get",@{})[@"data"][@"groups"] count]==0 && [call(@"graph.get",@{})[@"data"][@"layout"] count]==0,"Removal prunes empty boundaries and layout");
  call(@"history.undo",@{},true);
  check([beforeRemoveGraph isEqual:call(@"graph.get",@{})[@"data"]] && [beforeRemoveMixer isEqual:call(@"mixer.get",@{})[@"data"]],"One Undo restores exact rack, group, placement and routing");
  call(@"history.redo",@{},true);call(@"history.undo",@{},true);
  check([session savePath:path error:&error] && [session openPath:path error:&error],"Group add/delete history survives a project round trip");
  check([beforeRemoveMixer isEqual:call(@"mixer.get",@{})[@"data"]] && [call(@"graph.get",@{})[@"data"][@"groups"][0][@"nodes"] count]==2,"Restored routing and membership persist");
  [[NSFileManager defaultManager] removeItemAtPath:path error:nil];
  call(@"plugin.remove",@{@"plugins":@[groupedPlugin]},true);
  call(@"graph.song.group.remove",@{@"group":group},true);
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
  call(@"plugin.parameters.set",@{@"plugin":stable,@"values":@[@{@"id":@1,@"value":@-19}]},true);
  NSString *foldedPath=[NSTemporaryDirectory() stringByAppendingPathComponent:[NSUUID.UUID.UUIDString stringByAppendingString:@".screamseq"]];
  check([session savePath:foldedPath error:&error],"Save folds queued manual values into opaque baseline");
  call(@"history.undo",@{},true);
  auto stableGain=[&](){for(NSDictionary *p in call(@"plugin.parameters.get",@{@"plugin":stable})[@"data"])if([p[@"id"] intValue]==1)return [p[@"manualValue"] doubleValue];return 999.0;};
  check(std::abs(stableGain()+8)<1e-5&&session.canRedo,"Manual scalar history survives autosave folding and stable rack reorder");
  call(@"history.redo",@{},true);check(std::abs(stableGain()+19)<1e-5,"Redo remains a manual scalar after autosave");
  [NSFileManager.defaultManager removeItemAtPath:foldedPath error:nil];
  [session shutdown];std::cout<<"PASS unified chronological history, live parameter gestures, cross-domain branches and persistence\n";return 0;
}catch(const std::exception &e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}}
