#include "../Audio/AudioUnitHost.hpp"
#import "../Bridge/TrackerSession.h"
#include "editor/TrackerDocument.hpp"
#include "editor/SongTiming.hpp"
#include <iostream>
using namespace Tracker;
using namespace OpenMPT;
#ifdef TRACKER_SANITIZER
static void tracker_audit_begin() {}
static void tracker_audit_end(uint64_t *a,uint64_t *f,uint64_t *l){*a=*f=*l=0;}
#else
extern "C" void tracker_audit_begin();
extern "C" void tracker_audit_end(uint64_t *,uint64_t *,uint64_t *);
#endif
static void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
static void sessionTest(const PluginDescriptor &descriptor) {
  TrackerSession *session=[TrackerSession new];NSError *error=nil;
  NSDictionary *d=@{@"type":@(descriptor.type),@"subtype":@(descriptor.subtype),@"manufacturer":@(descriptor.manufacturer),
    @"name":@(descriptor.name.c_str()),@"format":@(descriptor.format.c_str()),@"path":@(descriptor.path.c_str()),@"classID":@(descriptor.classID.c_str())};
  check([session addPlugin:d error:&error],"Add command fixture");
  auto call=[&](NSString *method,NSDictionary *params,bool write=false)->NSDictionary * {
    auto request=[params mutableCopy];if(write)request[@"expectedRevision"]=session.automationRevision;
    const auto result=[session automationMethod:method params:request error:&error];
    if(!result)throw std::runtime_error(error.localizedDescription.UTF8String);return result;
  };
  NSString *plugin=[session snapshot:0][@"nativePlugins"][0][@"instanceID"];
  NSDictionary *set=@{@"pattern":@0,@"columns":@[@{@"channel":@0,@"count":@2}],
    @"bindings":@[@{@"id":@1,@"plugin":plugin,@"parameter":@7,@"name":@"Filter motion"}],
    @"commands":@[@{@"channel":@0,@"position":@1234,@"column":@0,@"kind":@"parameter-set",@"binding":@1,@"value":@0.2},
      @{@"channel":@0,@"position":@66000,@"duration":@70000,@"column":@1,@"kind":@"parameter-slide",@"binding":@1,@"value":@0.9}]};
  NSString *revision=session.automationRevision;auto preview=[set mutableCopy];preview[@"dryRun"]=@YES;
  check(![call(@"pattern.performance.set",preview,true)[@"changed"] boolValue]&&[revision isEqual:session.automationRevision],"Preview preserves history and revision");
  check([call(@"pattern.performance.set",set,true)[@"changed"] boolValue],"Commands commit together");
  const auto expected=call(@"pattern.performance.get",@{@"pattern":@0})[@"data"];
  check([expected[@"bindings"][0][@"resolved"] boolValue]&&[expected[@"bindings"][0][@"canSlide"] boolValue],"Binding resolves to a continuous stable parameter");
  revision=session.automationRevision;
  check(![call(@"pattern.performance.set",set,true)[@"changed"] boolValue]&&[revision isEqual:session.automationRevision],"No-op preserves revision");
  auto reject=[&](NSDictionary *params){
    auto request=[params mutableCopy];request[@"expectedRevision"]=session.automationRevision;NSString *before=session.automationRevision;
    NSError *failure=nil;check(![session automationMethod:@"pattern.performance.set" params:request error:&failure],"Malformed command must be rejected");
    check([before isEqual:session.automationRevision]&&[expected isEqual:call(@"pattern.performance.get",@{@"pattern":@0})[@"data"]],"Rejected command leaves document intact");
  };
  reject(@{@"pattern":@0,@"removeBindings":@[@1]});
  reject(@{@"pattern":@0,@"columns":@[@{@"channel":@0,@"count":@1}]});
  reject(@{@"pattern":@0,@"bindings":@[@{@"id":@1,@"plugin":@"missing",@"parameter":@7}]});
  reject(@{@"pattern":@0,@"commands":@[@{@"channel":@0,@"position":@0,@"column":@0,@"kind":@"parameter-slide",@"binding":@1,@"value":@0.5}]});
  for(NSString *method in @[@"automation.pattern.set",@"automation.replaceLane"]) {
    NSDictionary *params=[method isEqual:@"automation.pattern.set"]
      ? @{@"pattern":@0,@"plugin":plugin,@"parameter":@7,@"points":@[@{@"position":@0,@"value":@0.5}],@"expectedRevision":session.automationRevision}
      : @{@"slot":@0,@"id":@7,@"points":@[@{@"frame":@0,@"value":@0.5}],@"expectedRevision":session.automationRevision};
    NSError *failure=nil;check(![session automationMethod:method params:params error:&failure],"Competing automation is rejected before commit");
  }
  call(@"history.undo",@{@"domain":@"document"},true);
  check([call(@"pattern.performance.get",@{@"pattern":@0})[@"data"][@"commands"] count]==0,"API Undo removes full batch");
  call(@"history.redo",@{@"domain":@"document"},true);
  check([expected isEqual:call(@"pattern.performance.get",@{@"pattern":@0})[@"data"]],"API Redo restores stable commands");
  NSString *path=[NSTemporaryDirectory() stringByAppendingPathComponent:[NSUUID.UUID.UUIDString stringByAppendingString:@".resonance"]];
  check([session savePath:path error:&error]&&[session openPath:path error:&error],"Command project saves and reopens");
  check([expected isEqual:call(@"pattern.performance.get",@{@"pattern":@0})[@"data"]],"Native metadata preserves precision and bindings exactly");
  call(@"plugin.remove",@{@"slot":@0},true);
  check(![call(@"pattern.performance.get",@{@"pattern":@0})[@"data"][@"bindings"][0][@"resolved"] boolValue],"Removed plugin leaves retained unresolved binding");
  call(@"history.undo",@{@"domain":@"plugins"},true);
  check([expected isEqual:call(@"pattern.performance.get",@{@"pattern":@0})[@"data"]],"Plugin Undo reconnects only original identity");
  [[NSFileManager defaultManager] removeItemAtPath:path error:nil];
}
int main(int argc,char **argv){@autoreleasepool{try{
  check(argc==2,"Local plugin fixture required");
  PluginState gain{NativePlugin::discoverVST3(argv[1]).at(0)};gain.instanceID="stable-gain";
  Document doc;
  doc.transaction([](CSoundFile &song){check(song.Patterns[0].Resize(4),"Resize pattern");song.Order().assign(3,0);
    song.Order().SetDefaultTempoInt(125);song.Order().SetDefaultSpeed(6);});
  const auto pattern=doc.native().patterns.at(0).id,track=doc.native().tracks.at(0).id;
  doc.annotate([&](NativeSong &native){auto &p=native.performance;p.columns[track]=2;p.bindings[1]={gain.instanceID,7,"Gain"};
    p.commands={{pattern,track,0,0,0,PatternCommandKind::ParameterSet,1,.2},
      {pattern,track,8192,65536,1,PatternCommandKind::ParameterSlide,1,.8},
      {pattern,track,70000,40000,0,PatternCommandKind::ParameterSlide,1,.1},
      {pattern,track,131072,0,0,PatternCommandKind::ParameterSet,1,.7},
      {pattern,track,131072,0,1,PatternCommandKind::ParameterSet,1,.4}};});
  const auto metadata=doc.native();
  check(doc.undoChangesAutomation(),"Native commands invalidate prepared playback on Undo");
  doc.undo();check(doc.native().performance.empty(),"One command edit uses one Undo");doc.redo();
  check(doc.native()==metadata,"Redo preserves stable bindings and subrow positions");
  const auto duplicate=doc.addPattern(4,true,0);
  check(doc.native().performance.commands.size()==10&&doc.native().performance.commands.back().pattern==doc.native().patterns.at(duplicate).id,"Pattern duplication copies native commands");
  doc.undo();
  auto invalid=[&](auto change){const auto revision=doc.revision;const auto before=doc.native();try{doc.annotate(change);check(false,"Invalid command accepted");}catch(const std::invalid_argument &){}
    check(doc.revision==revision&&doc.native()==before,"Invalid command mutation is atomic");};
  invalid([](NativeSong &n){n.performance.commands[0].binding=2;});
  invalid([](NativeSong &n){n.performance.commands[0].value=NAN;});
  invalid([](NativeSong &n){n.performance.commands[1].column=2;});
  invalid([](NativeSong &n){n.performance.commands[1].duration=0;});
  invalid([](NativeSong &n){n.performance.commands.push_back(n.performance.commands[0]);});
  for(uint32_t rate:{44100u,48000u,96000u}){
    auto render=[&](uint32_t block,bool reorder=false,uint32_t order=0){auto unused=gain;unused.instanceID="unrelated";unused.bypass=true;
      PluginChain chain(reorder?std::vector<PluginState>{unused,gain}:std::vector<PluginState>{gain},rate,true);
      Renderer renderer(doc.snapshotData(),rate,order);chain.attachInstruments(renderer);chain.attachMusicalAutomation(renderer,doc.native());
      const auto total=rate*9/10;std::vector<float> output(total*2,.2f);std::array<float,8192> scratch{};
      for(uint32_t at=0;at<total;at+=block){const auto count=std::min(block,total-at);uint64_t a,f,l;tracker_audit_begin();
        renderer.render(scratch.data(),count);const bool ok=chain.process(output.data()+at*2,count);tracker_audit_end(&a,&f,&l);
        check(ok&&!renderer.faulted(),"Native commands render successfully");check(a+f+l==0,"Native command processing must not allocate, free or lock");}
      return output;};
    const auto reference=render(1);const auto rowFrames=rate*12/100;
    for(size_t frame=0;frame<reference.size()/2;++frame){const double p=double(frame%(rowFrames*4))*65536/rowFrames;
      double value=.2;
      if(p>=8192)value=.2+.6*std::min(1.,(p-8192)/65536);
      if(p>=70000){const double interrupted=.2+.6*(70000.-8192)/65536;value=interrupted+(.1-interrupted)*std::min(1.,(p-70000)/40000);}
      if(p>=131072)value=.4;
      if(std::abs(reference[frame*2]-.2*value)>2e-7){std::cerr<<"Rate "<<rate<<" frame "<<frame<<" got "<<reference[frame*2]<<" expected "<<.2*value<<'\n';check(false,"Every sample follows independent command reference");}}
    for(uint32_t block:{17u,128u,4096u}){const auto other=render(block);
      for(size_t i=0;i<reference.size();++i)check(std::abs(reference[i]-other[i])<3e-8,"Command output independent of callback partition");}
    check(render(128)==render(128,true),"Stable binding survives plugin rack reorder");
    check(render(128)==render(128,false,1),"Repeated-pattern seek chases commands consistently");
    doc.transaction([](CSoundFile &song){auto &cell=*song.Patterns[0].GetpModCommand(1,0);cell.command=CMD_TEMPO;cell.param=200;});
    const auto tempo=render(1),tempoOther=render(4096);
    for(size_t i=0;i<tempo.size();++i)check(std::abs(tempo[i]-tempoOther[i])<3e-8,"Tempo changes retain command timing across buffer sizes");doc.undo();
    doc.transaction([](CSoundFile &song){auto timing=songTiming(song);timing.mode=TempoMode::Modern;timing.sequences[0]={1271250,7};timing.rowsPerBeat=4;timing.rowsPerMeasure=12;
      timing.groove=normalizedGroove(std::array{1.5,.5,1.25,.75});applySongTiming(song,timing);});
    const auto groove=render(1),grooveOther=render(4096);
    for(size_t i=0;i<groove.size();++i)check(std::abs(groove[i]-grooveOther[i])<3e-8,"Uneven-row commands retain sample precision");doc.undo();
  }
  sessionTest(gain.descriptor);
  std::cout<<"PASS native command reference audio, exact fractional starts, interruption, column precedence, repeats, seek, rack identity, tempo/groove, callback partition, realtime audit, document history, guarded API, conflict rejection and saved project recall\n";
  return 0;
}catch(const std::exception &e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}}
