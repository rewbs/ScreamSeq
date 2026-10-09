#include "../Audio/AudioUnitHost.hpp"
#include "FixtureTrust.hpp"
#include "editor/TrackerDocument.hpp"
#include "soundlib/ModInstrument.h"
#import "../Bridge/TrackerSession.h"
#include <dlfcn.h>
#include <cmath>
#include <iostream>
using namespace Tracker;using namespace OpenMPT;
std::vector<PluginDescriptor> registerFixtureAUs();
void setFixtureAUChannelWeights(bool);
#ifdef TRACKER_SANITIZER
static void tracker_audit_begin(){}
static void tracker_audit_end(uint64_t *a,uint64_t *f,uint64_t *l){*a=*f=*l=0;}
#else
extern "C" void tracker_audit_begin();
extern "C" void tracker_audit_end(uint64_t *,uint64_t *,uint64_t *);
#endif
static void check(bool ok,const char *why){if(!ok)throw std::runtime_error(why);}
static std::unique_ptr<Document> fixture(){auto doc=std::make_unique<Document>();doc->transaction([](CSoundFile &s){
  s.m_nInstruments=2;s.Instruments[1]=new ModInstrument(0);s.Instruments[2]=new ModInstrument(0);
  s.Order().SetDefaultTempoInt(125);s.Order().SetDefaultSpeed(6);
  for(auto row:{0u,2u,4u}){auto &c=*s.Patterns[0].GetpModCommand(row,0);c.note=61;c.instr=1;}
  s.Patterns[0].GetpModCommand(5,0)->note=NOTE_KEYOFF;
});return doc;}
#include "NoteActivityHostChecks.inc"
#include "NoteRoutingScopeChecks.inc"
static NSDictionary *descriptorDictionary(const PluginDescriptor &d){return @{@"type":@(d.type),@"subtype":@(d.subtype),@"manufacturer":@(d.manufacturer),@"name":@(d.name.c_str()),@"format":@(d.format.c_str()),@"path":@(d.path.c_str()),@"classID":@(d.classID.c_str()),@"isInstrument":@(d.instrument)};}
static void sessionChecks(const PluginDescriptor &descriptor){
  TrackerSession *session=[TrackerSession new];NSError *error=nil;check([session addInstrument:0 error:&error]>0,"Create note instrument");
  check([session addPlugin:descriptorDictionary(descriptor) error:&error],"Add fixture VST instrument");
  auto call=[&](NSString *method,NSDictionary *values,bool write=false){auto params=[values mutableCopy];if(write)params[@"expectedRevision"]=session.automationRevision;auto reply=[session automationMethod:method params:params error:&error];if(!reply)throw std::runtime_error(error.localizedDescription.UTF8String);return reply;};
  auto graph=[&](){return call(@"graph.get",@{@"includeImplicitMixer":@YES})[@"data"];};
  NSString *plugin=graph()[@"plugins"][0][@"id"];
  call(@"instrument.plugin.set",@{@"instrument":@1,@"plugin":plugin,@"channel":@1},true);
  NSString *instrument=graph()[@"instruments"][0][@"id"],*track=graph()[@"mixer"][@"buses"][0][@"id"];
  const auto before=graph()[@"noteRouting"];NSString *revision=session.automationRevision;
  NSDictionary *connect=@{@"sourceKind":@"channel",@"source":track,@"plugin":plugin,@"midiChannel":@4};auto dry=[connect mutableCopy];dry[@"dryRun"]=@YES;
  check([call(@"graph.note.connect",dry,true)[@"data"][@"wouldChange"] boolValue]&&[revision isEqual:session.automationRevision],"Note dry run must not mutate history");
  NSString *route=call(@"graph.note.connect",connect,true)[@"data"][@"route"];check(route.length>0,"Connect returns stable cable ID");const auto connected=graph()[@"noteRouting"];
  call(@"history.undo",@{},true);check([graph()[@"noteRouting"] isEqual:before],"Note routing joins unified Undo");call(@"history.redo",@{},true);check([graph()[@"noteRouting"] isEqual:connected],"Redo restores cable identity");
  revision=session.automationRevision;call(@"graph.note.update",@{@"id":route,@"midiChannel":@4},true);check([revision isEqual:session.automationRevision],"Identical note edit is a no-op");
  auto invalid=[connect mutableCopy];invalid[@"plugin"]=@"missing";invalid[@"expectedRevision"]=revision;check(![session automationMethod:@"graph.note.connect" params:invalid error:&error]&&[revision isEqual:session.automationRevision]&&[graph()[@"noteRouting"] isEqual:connected],"Invalid destination fails before model/history mutation");
  call(@"graph.note.disconnect",@{@"instrument":instrument},true);check([graph()[@"noteRouting"][@"suppressedAssignments"] count]==1,"Disconnect remembers implicit assignment suppression");
  call(@"graph.note.update",@{@"id":route,@"enabled":@NO},true);
  NSString *path=[NSTemporaryDirectory() stringByAppendingPathComponent:[NSUUID.UUID.UUIDString stringByAppendingString:@".screamseq"]];
  check([[session serializedData] writeToFile:path atomically:YES],"Save route fixture");TrackerSession *reopened=[TrackerSession new];check([reopened openPath:path error:&error],"Reopen note routing project");
  auto saved=[reopened automationMethod:@"graph.get" params:@{} error:&error][@"data"][@"noteRouting"];check([saved isEqual:graph()[@"noteRouting"]],"Routes, mapping, mute and implicit suppression persist");[NSFileManager.defaultManager removeItemAtPath:path error:nil];
  call(@"graph.note.restoreAssignment",@{@"instrument":instrument},true);check([graph()[@"noteRouting"][@"suppressedAssignments"] count]==0,"Explicit restore reveals default cable");
  call(@"graph.note.disconnect",@{@"id":route},true);check([graph()[@"noteRouting"][@"routes"] count]==0,"Explicit disconnect removes cable");
  // Removing the assigned synth must not turn its still-routed trigger into
  // sample playback. The source kind/channel belong to portable song data.
  call(@"instrument.plugin.set",@{@"instrument":@1,@"plugin":plugin,@"channel":@5},true);
  check([session addPlugin:descriptorDictionary(descriptor) error:&error],"Add unassigned note destination");
  NSString *destination=graph()[@"plugins"][1][@"id"];
  NSString *retained=call(@"graph.note.connect",@{@"sourceKind":@"instrument",@"source":instrument,@"plugin":destination},true)[@"data"][@"route"];
  call(@"plugin.remove",@{@"plugins":@[plugin]},true);
  auto triggers=graph()[@"noteRouting"][@"triggerSources"];
  check([triggers count]==1&&[triggers[0][@"instrument"] isEqual:instrument]&&[triggers[0][@"midiChannel"] intValue]==5,"Removing original plugin erased a routed note source");
  call(@"history.undo",@{},true);check([graph()[@"plugins"] count]==2&&[graph()[@"noteRouting"][@"triggerSources"] count]==0,"Undo source migration and rack removal atomically");
  call(@"history.redo",@{},true);check([graph()[@"plugins"] count]==1&&[graph()[@"noteRouting"][@"triggerSources"] count]==1,"Redo source migration and rack removal atomically");
  call(@"graph.note.update",@{@"id":retained,@"midiChannel":@7},true);
  check([[session serializedData] writeToFile:path atomically:YES],"Save retained plugin trigger");
  check([reopened openPath:path error:&error],"Reopen retained plugin trigger");
  check([[reopened automationMethod:@"graph.get" params:@{} error:&error][@"data"][@"noteRouting"] isEqual:graph()[@"noteRouting"]],"Retained trigger/cable mapping must persist");
  [NSFileManager.defaultManager removeItemAtPath:path error:nil];
  call(@"instrument.plugin.set",@{@"instrument":@1,@"plugin":@""},true);
  check([graph()[@"noteRouting"][@"triggerSources"] count]==0&&[graph()[@"noteRouting"][@"routes"] count]==1,"Explicit unassign restores sample mode without deleting unrelated cable data");
  call(@"history.undo",@{},true);check([graph()[@"noteRouting"][@"triggerSources"] count]==1,"Undo explicit source-mode reset");
}
int main(int argc,char **argv){trustFixtureArguments(argc,argv);@autoreleasepool{try{
  check(argc==2,"Fixture bundle required");void *handle=dlopen((std::string(argv[1])+"/Contents/MacOS/ResonanceFixture").c_str(),RTLD_NOW|RTLD_LOCAL);check(handle,"Load fixture");auto weighted=reinterpret_cast<void(*)(bool)>(dlsym(handle,"ResonanceFixtureChannelWeights"));check(weighted,"Channel-weight fixture hook");weighted(true);setFixtureAUChannelWeights(true);
  const auto vst=NativePlugin::discoverVST3(argv[1]),au=registerFixtureAUs();
  for(const auto &descriptor:{vst[1],au[1]}){
    noteRoutingScopeChecks(descriptor);
    noteActivityHostChecks(descriptor);
    PluginState a{descriptor},b{descriptor};a.instanceID="note-a";b.instanceID="note-b";a.instrument=1;b.instrument=2;
    auto render=[&](uint32_t block){auto doc=fixture();auto native=doc->native();Renderer renderer(doc->snapshotData(),48000);PluginChain chain({a,b},48000,true);chain.attachInstruments(renderer,&native);chain.attachMusicalAutomation(renderer,native);
      const auto activity=chain.noteActivity();check(activity.available&&activity.fresh&&activity.requestedGeneration==activity.adoptedGeneration,"Initial note telemetry is not adopted");
      const auto instrument=native.instruments.at(1).id,track=native.tracks.at(0).id;const auto route=native.makeEntity().id;
      std::vector<float> out(32000*2);const std::array<uint32_t,5> boundaries{750,1000,2000,14000,15000};size_t next=0;
      for(uint32_t pos=0;pos<32000;){
        if(next<boundaries.size()&&pos==boundaries[next]){
          if(next==0){auto invalid=native;invalid.signal.noteRouting.routes={{route,NoteSourceKind::Channel,track,"missing",0,true}};bool rejected=false;try{chain.prepareGraphControls(invalid);}catch(const std::invalid_argument &){rejected=true;}check(rejected&&!chain.failed(),"Failed note preparation must preserve the old audible plan");}
          else {
            if(next==1)native.signal.noteRouting.routes={{route,NoteSourceKind::Channel,track,"note-b",3,true}};
            if(next==2)native.signal.noteRouting.suppressedAssignments={instrument};
            if(next==3)native.signal.noteRouting.routes.clear();
            if(next==4)native.signal.noteRouting.routes={{route,NoteSourceKind::Channel,track,"note-b",3,true}};
            auto plan=chain.prepareGraphControls(native);check(bool(plan)&&chain.publishGraphControls(std::move(plan)),"Prepare/publish live note cable edit");
            const auto pending=chain.noteActivity();check(pending.requestedGeneration>pending.adoptedGeneration,"Published note telemetry did not expose pending adoption");
          }++next;
        }
        auto frames=std::min(block,32000-pos);if(next<boundaries.size())frames=std::min(frames,boundaries[next]-pos);
        uint64_t alloc,freed,locks;tracker_audit_begin();chain.beginRenderBlock();renderer.render(out.data()+pos*2,frames);const bool ok=chain.process(out.data()+pos*2,frames);tracker_audit_end(&alloc,&freed,&locks);
        check(ok&&!renderer.faulted()&&alloc+freed+locks==0,"Live note render must allocate/free/lock nothing");const auto adopted=chain.noteActivity();check(adopted.fresh&&adopted.requestedGeneration==adopted.adoptedGeneration,"Rendered note telemetry did not adopt the accepted generation");pos+=frames;
      }return out;
    };
    const auto reference=render(17);for(auto block:{128u,512u,4096u})check(render(block)==reference,"Live route PCM depends on callback partition");
    auto value=[&](uint32_t frame){return reference[frame*2];};
    check(std::abs(value(500)-.1/16)<1e-7,"Default instrument destination audible");
    check(value(1500)==value(500),"Added cable must not replay held note");
    check(value(3000)==0,"Suppressing last original owner releases held note");
    check(std::abs(value(12000)-.3/16)<1e-7,"Next note enters newly routed plugin/MIDI channel");
    check(value(14500)==0&&value(18000)==0,"Disconnect and Undo release without resurrection");
    check(std::abs(value(24000)-.3/16)<1e-7&&value(31000)==0,"Future note and note-off after Undo route correctly");
  }
  sessionChecks(vst[1]);std::cout<<"PASS native AU/VST3 live note routing, held ownership, channel remap, Undo wait semantics, callback partitions, realtime audit, API/history/persistence\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}return 0;}}
