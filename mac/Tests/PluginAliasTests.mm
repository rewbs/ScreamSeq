#import "../Bridge/TrackerSession.h"
#include "../Audio/AudioUnitHost.hpp"
#include "editor/TrackerDocument.hpp"
#include "soundlib/ModInstrument.h"
#include <dlfcn.h>
#include <iostream>
using namespace Tracker;
using namespace OpenMPT;
std::vector<PluginDescriptor> registerFixtureAUs();
void setFixtureAUChannelWeights(bool);
#ifdef TRACKER_SANITIZER
static void tracker_audit_begin() {}
static void tracker_audit_end(uint64_t *a,uint64_t *f,uint64_t *l){*a=*f=*l=0;}
#else
extern "C" void tracker_audit_begin();
extern "C" void tracker_audit_end(uint64_t *,uint64_t *,uint64_t *);
#endif
static void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
template<class F>static void rejects(F f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}check(caught,"Invalid assignment must reject");}
static NSDictionary *dictionary(const PluginDescriptor &d){return @{@"type":@(d.type),@"subtype":@(d.subtype),@"manufacturer":@(d.manufacturer),@"name":@(d.name.c_str()),@"format":@(d.format.c_str()),@"path":@(d.path.c_str()),@"classID":@(d.classID.c_str()),@"isInstrument":@(d.instrument)};}
static std::unique_ptr<Document> song(){
  auto doc=std::make_unique<Document>();doc->transaction([](CSoundFile &s){
    s.m_nInstruments=3;s.Order().SetDefaultTempoInt(125);s.Order().SetDefaultSpeed(6);
    for(int i=1;i<=3;++i){s.Instruments[i]=new ModInstrument(0);auto &on=*s.Patterns[0].GetpModCommand(0,i-1);on.note=61;on.instr=uint8_t(i);
      auto &off=*s.Patterns[0].GetpModCommand(i,i-1);off.note=NOTE_KEYOFF;}
  });return doc;
}
int main(int argc,char **argv){@autoreleasepool{try{
  check(argc==2,"Fixture path required");NSString *path=@(argv[1]);
  void *handle=dlopen([path stringByAppendingPathComponent:@"Contents/MacOS/ResonanceFixture"].UTF8String,RTLD_NOW|RTLD_LOCAL);
  check(handle,"Open private VST3 fixture");auto weighted=reinterpret_cast<void(*)(bool)>(dlsym(handle,"ResonanceFixtureChannelWeights"));check(weighted,"Weighted channel test hook");weighted(true);setFixtureAUChannelWeights(true);
  auto vst=NativePlugin::discoverVST3(argv[1]);auto au=registerFixtureAUs();
  const std::vector<PluginInstrumentAlias> bindings{{1,2},{2,7},{3,16}};
  for(const auto &descriptor:{vst[1],au[1]}) {
    PluginState synth{descriptor};synth.instanceID="shared";setPluginAssignments(synth,bindings);
    validatePluginAssignments({&synth,1});validatePluginCapacity({synth},249);
    rejects([&]{validatePluginCapacity({synth},250);});
    auto duplicate=synth;duplicate.instanceID="duplicate";rejects([&]{validatePluginAssignments(std::vector{synth,duplicate});});
    for(auto invalid:std::vector<std::vector<PluginInstrumentAlias>>{{{1,0}},{{1,17}},{{0,1}},{{256,1}},{{1,1},{1,2}}}){auto copy=synth;rejects([&]{setPluginAssignments(copy,invalid);validatePluginAssignments({&copy,1});});}
    NativePlugin probe(synth,48000);check(pluginAssignments(probe.state())==bindings,"Opaque state capture retains full assignment list");
    for(uint32_t rate:{44100u,48000u,96000u}) {
      auto doc=song();
      auto render=[&](uint32_t block,bool mute=false,bool sameChannel=false,bool mixer=false) {
        auto state=synth;if(sameChannel)setPluginAssignments(state,{{1,2},{2,2},{3,16}});
        auto local=song();
        if(mixer)local->annotate([](NativeSong &n){auto master=n.makeEntity().id;for(const auto &[channel,track]:n.tracks)n.mixer.buses.push_back({track.id,master,MixerBusKind::Track,"Track"});n.mixer.buses.push_back({master,0,MixerBusKind::Master,"Master"});n.mixer.instruments={{"shared",master,0}};});
        Renderer renderer(local->snapshotData(),rate);PluginChain chain({state},rate,true);chain.attachInstruments(renderer,mixer?&local->native():nullptr);
        check(renderer.song().Instruments[1]->nMixPlug==renderer.song().Instruments[2]->nMixPlug&&renderer.song().Instruments[2]->nMixPlug==renderer.song().Instruments[3]->nMixPlug,"All aliases use one core adapter");
        check(renderer.song().Instruments[2]->nMidiChannel==(sameChannel?2:7),"Core instrument has explicit MIDI channel");
        const uint32_t total=rate/2,muting=rate/20;std::vector<float> out(total*2);
        for(uint32_t pos=0;pos<total;) {
          if(mute&&pos==muting)renderer.mute(0,true);
          auto count=std::min(block,total-pos);if(mute&&pos<muting)count=std::min(count,muting-pos);
          uint64_t a,f,l;tracker_audit_begin();renderer.render(out.data()+pos*2,count);bool ok=chain.process(out.data()+pos*2,count);tracker_audit_end(&a,&f,&l);
          check(ok&&!renderer.faulted()&&a+f+l==0,"Alias render is callback-safe");pos+=count;
        }return out;
      };
      auto reference=render(1);
      for(auto block:{17u,128u,4096u})check(render(block)==reference,"AU/VST3 alias output is sample-exact across callback sizes");
      for(uint32_t row=0;row<4;++row){const auto frame=uint32_t((row*.12+.01)*rate);const double expected=std::array{25.,23.,16.,0.}[row]*.1/16;
        if(std::abs(reference[frame*2]-expected)>1e-7)std::cerr<<descriptor.format<<" row "<<row<<" actual "<<reference[frame*2]<<" expected "<<expected<<'\n';
        check(std::abs(reference[frame*2]-expected)<1e-7,"Independent channel-weighted levels prove routing and per-alias note-off");}
      auto same=render(17,false,true);check(std::abs(same[uint32_t(.14*rate)*2]-18*.1/16)<1e-7,"Shared-channel same-pitch alias survives neighboring note-off");
      auto muted=render(17,true);check(std::abs(muted[uint32_t(.06*rate)*2]-23*.1/16)<1e-7,"Column mute releases only its alias notes");
      auto mixed=render(128,false,false,true);check(mixed==reference,"Shared plugin output reaches mixer once regardless of alias count");
    }
    TrackerSession *session=[TrackerSession new];NSError *error=nil;
    for(int i=0;i<3;++i)check([session addInstrument:0 error:&error]>0,"Create tracker aliases");
    if(descriptor.format=="AU") {
      // The AU exists only in this process, so load it through project recall;
      // the separate scanner intentionally cannot see process-local registrations.
      auto root=[[NSPropertyListSerialization propertyListWithData:[session serializedData] options:0 format:nil error:nil] mutableCopy];
      auto item=[dictionary(descriptor) mutableCopy];const auto captured=probe.state();item[@"state"]=[NSData dataWithBytes:captured.state.data() length:captured.state.size()];
      item[@"instrument"]=@0;item[@"bypass"]=@NO;item[@"instanceID"]=@"fixture-au";root[@"plugins"]=@[item];
      NSString *initial=[NSTemporaryDirectory() stringByAppendingPathComponent:[NSUUID.UUID.UUIDString stringByAppendingString:@".resonance"]];
      [[NSPropertyListSerialization dataWithPropertyList:root format:NSPropertyListBinaryFormat_v1_0 options:0 error:nil] writeToFile:initial atomically:YES];
      check([session openPath:initial error:&error],"Recall process-local AU fixture");[NSFileManager.defaultManager removeItemAtPath:initial error:nil];
      check([[session snapshot:0][@"pluginError"] length]==0,"AU fixture is resolved in its owning process");
    } else if(![session addPlugin:dictionary(descriptor) error:&error]) throw std::runtime_error(error.localizedDescription.UTF8String);
    NSString *identity=[session snapshot:0][@"nativePlugins"][0][@"instanceID"];
    auto call=[&](NSString *method,NSDictionary *values,bool mutation=false){auto p=[values mutableCopy];if(mutation)p[@"expectedRevision"]=session.automationRevision;
      auto result=[session automationMethod:method params:p error:&error];if(!result)throw std::runtime_error(error.localizedDescription.UTF8String);return result;};
    NSArray *assignments=@[@{@"instrument":@1,@"channel":@2},@{@"instrument":@2,@"channel":@7},@{@"instrument":@3,@"channel":@16}];
    NSString *revision=session.automationRevision;
    auto preview=call(@"plugin.instruments.set",@{@"plugin":identity,@"assignments":assignments,@"dryRun":@YES},true);
    check(![preview[@"changed"] boolValue]&&[preview[@"data"][@"wouldChange"] boolValue]&&[revision isEqual:session.automationRevision],"Alias preview leaves document intact");
    call(@"plugin.instruments.set",@{@"plugin":identity,@"assignments":assignments},true);
    auto routing=call(@"plugin.instruments.get",@{@"plugin":identity})[@"data"];
    check(![call(@"plugin.instruments.set",@{@"plugin":identity,@"assignments":assignments},true)[@"changed"] boolValue],"Identical routing is a no-op");
    call(@"history.undo",@{@"domain":@"plugins"},true);check([call(@"plugin.instruments.get",@{@"plugin":identity})[@"data"][@"assignments"] count]==0,"Plugin Undo removes aliases in one step");
    call(@"history.redo",@{@"domain":@"plugins"},true);check([call(@"plugin.instruments.get",@{@"plugin":identity})[@"data"] isEqual:routing],"Redo restores MIDI channels and stable owner");
    call(@"pattern.transform",@{@"operation":@"clear",@"scope":@"pattern",@"pattern":@0},true);
    NSMutableArray *cells=[NSMutableArray array];
    for(int i=1;i<=3;++i){
      [cells addObject:@{@"pattern":@0,@"row":@0,@"channel":@(i-1),@"note":@61,@"instrument":@(i)}];
      [cells addObject:@{@"pattern":@0,@"row":@(i),@"channel":@(i-1),@"note":@(NOTE_KEYOFF)}];
    }
    call(@"pattern.apply",@{@"cells":cells},true);
    call(@"document.timing.set",@{@"mode":@"classic",@"tempo":@125,@"speed":@6},true);
    check([session pluginParameter:0 identifier:7 value:.3 record:NO error:&error],"Native parameter edit with aliases");
    NSData *saved=[session serializedData];NSDictionary *root=[NSPropertyListSerialization propertyListWithData:saved options:0 format:nil error:nil];
    check([root[@"version"] intValue]==5&&[root[@"plugins"][0][@"instrumentAssignments"] isEqual:assignments],"Project v5 retains aliases through manual state capture");
    NSString *file=[NSTemporaryDirectory() stringByAppendingPathComponent:[NSUUID.UUID.UUIDString stringByAppendingString:@".resonance"]];
    [saved writeToFile:file atomically:YES];check([session openPath:file error:&error],"Alias project reopens");
    check([call(@"plugin.instruments.get",@{@"plugin":identity})[@"data"] isEqual:routing],"Alias names and owner persist");
    NSString *wav=[file stringByAppendingString:@".wav"],*reopened=[file stringByAppendingString:@".reopened.wav"];
    check([TrackerSession exportData:saved path:wav error:&error]&&[TrackerSession exportData:[session serializedData] path:reopened error:&error],"Saved aliases export without a device");
    NSData *audio=[NSData dataWithContentsOfFile:wav];
    check([audio isEqual:[NSData dataWithContentsOfFile:reopened]],"Native alias reopen preserves complete WAV bytes");
    for(int row=0;row<4;++row){const size_t frame=size_t((row*.12+.01)*48000),offset=44+frame*2*sizeof(float);float sample=0;
      check(audio.length>=offset+sizeof(float),"Export contains independent alias note-offs");std::memcpy(&sample,static_cast<const char *>(audio.bytes)+offset,sizeof(float));
      check(std::abs(sample-std::array{25.,23.,16.,0.}[row]*.2*.3/16)<1e-6,"Actual WAV preserves each channel and saved plugin parameter");}
    [NSFileManager.defaultManager removeItemAtPath:wav error:nil];[NSFileManager.defaultManager removeItemAtPath:reopened error:nil];
    auto broken=[root mutableCopy];broken[@"version"]=@4;[[NSPropertyListSerialization dataWithPropertyList:broken format:NSPropertyListBinaryFormat_v1_0 options:0 error:nil] writeToFile:file atomically:YES];
    revision=session.automationRevision;check(![session openPath:file error:&error]&&[revision isEqual:session.automationRevision],"Legacy version cannot silently ignore aliases");
    for(int failure=0;failure<4;++failure){
      auto malformed=[root mutableCopy];auto plugin=[root[@"plugins"][0] mutableCopy];
      if(failure==0)[plugin removeObjectForKey:@"instrumentAssignments"];
      if(failure==1)plugin[@"instrument"]=@2;
      if(failure==2)plugin[@"instrumentAssignments"]=@[@{@"instrument":@1,@"channel":@17}];
      if(failure==3)plugin[@"instrumentAssignments"]=@[@{@"instrument":@1,@"channel":@2},@{@"instrument":@1,@"channel":@7}];
      malformed[@"plugins"]=@[plugin];[[NSPropertyListSerialization dataWithPropertyList:malformed format:NSPropertyListBinaryFormat_v1_0 options:0 error:nil] writeToFile:file atomically:YES];
      check(![session openPath:file error:&error]&&[revision isEqual:session.automationRevision],"Malformed v5 assignments reject atomically");
    }
    [NSFileManager.defaultManager removeItemAtPath:file error:nil];
    call(@"plugin.instruments.set",@{@"plugin":identity,@"assignments":@[@{@"instrument":@1,@"channel":@1}]},true);
    root=[NSPropertyListSerialization propertyListWithData:[session serializedData] options:0 format:nil error:nil];check([root[@"version"] intValue]==4,"Ordinary single-channel assignment keeps older project compatibility");
  }
  weighted(false);setFixtureAUChannelWeights(false);dlclose(handle);
  std::cout<<"PASS shared instrument aliases: AU/VST3 channel-weighted audio, one processor/adapter, note-off and mute ownership, same-channel overlap, buffer/rate invariance, realtime audit, state persistence, previews/history and project version guards\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}}
