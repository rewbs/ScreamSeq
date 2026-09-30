#import "../Bridge/TrackerSession.h"
#include "../Audio/AudioUnitHost.hpp"
#include "../Audio/NativeSignalGraph.hpp"
#include "editor/TrackerDocument.hpp"
#include "GraphRealtimeAudit.hpp"
#include "FixtureTrust.hpp"
#include <iostream>
using namespace Tracker;
static void check(bool b,const char *message){if(!b)throw std::runtime_error(message);}
struct ClockState:OpenMPT::PlayState{using PlayState::m_nBufferCount;};
static void scheduler(const PluginDescriptor &descriptor) {
  for(uint32_t block:{1u,17u,128u,4096u}) {
    PluginState state{descriptor};NativePlugin plugin(state,48000,true);plugin.prepareMusicalAutomation();
    auto activity=std::make_unique<ParameterActivity>(48000);ParameterProcessor target;target.key="rack/test";target.parameters=plugin.parameters();auto processor=activity->add(target);plugin.observe(activity.get(),processor);
    activity->watch(target.key,7,1);activity->begin(0);
    activity->clock(0,2,3,0,.01);activity->clock(100,4,5,0,.02); // Plugin renders after the mixer observer has crossed a pattern boundary.
    plugin.schedule(7,.2f,17,{ParameterOrigin::PatternSet,0,2,111,1,3,2});
    plugin.scheduleRamp(7,.2,.8,65,127,{ParameterOrigin::PatternSlide,0,2,222,1,3,2});
    std::array<float,1024> buffer{};buffer.fill(.25f);
    for(uint32_t at=0;at<512;){const auto count=std::min(block,512-at);tracker_audit_begin();check(plugin.process(buffer.data()+at*2,count,at),"Parameter fixture process failed");uint64_t a,f,l;tracker_audit_end(&a,&f,&l);check(a+f+l==0,"Parameter capture allocates, frees or locks");at+=count;}
    bool sawSet=false,sawSlide=false;for(const auto &p:activity->history()){
      if(p.source.kind==ParameterOrigin::Baseline)continue;
      check(p.frame<512&&std::abs(p.value-buffer[p.frame*2]/.25)<1e-5,"Trace differs from plugin-rendered audio");
      check(p.pattern==(p.frame<100?2u:4u),"Trace uses the wrong pattern across a callback boundary");
      check(std::abs(p.position-(p.frame<100?p.frame*.01:(p.frame-100)*.02))<1e-8,"Trace musical position does not match its sample timestamp");
      check(p.source.channel==1&&p.source.binding==3&&p.source.column==2,"Pattern source loses its editing link");
      sawSet|=p.source.kind==ParameterOrigin::PatternSet;sawSlide|=p.source.kind==ParameterOrigin::PatternSlide;
    }check(sawSet&&sawSlide,"Trace omitted a parameter command source");
  }
}
static void musical(const PluginDescriptor &descriptor) {
  for(bool recorded:{false,true}) {
    Document doc;auto native=doc.native();PluginState state{descriptor};state.instanceID="stable-target";
    if(!recorded)native.automation={{native.makeEntity().id,native.patterns.at(0).id,state.instanceID,7,true,{{0,.2,AutomationCurve::Linear},{256,.8,AutomationCurve::Step}}}};
    PluginChain chain({state},48000,true,recorded?std::vector<ParameterChange>{{0,7,.3f,17},{0,7,.7f,93}}:std::vector<ParameterChange>{});Renderer renderer(doc.snapshotData(),48000);
    chain.attachInstruments(renderer);chain.attachMusicalAutomation(renderer,native);auto &activity=chain.parameterActivity();activity.watch("rack/stable-target",7,1);
    std::array<float,256> audio{},module{};audio.fill(.25f);tracker_audit_begin();chain.beginRenderBlock();renderer.render(module.data(),128);const bool okay=chain.process(audio.data(),128);uint64_t a,f,l;tracker_audit_end(&a,&f,&l);check(okay&&a+f+l==0,"Observed musical render failed realtime audit");
    bool seen=false;for(const auto &p:activity.history())if(p.source.kind==(recorded?ParameterOrigin::Recorded:ParameterOrigin::Envelope)){seen=true;check(std::abs(p.value-audio[p.frame*2]/.25)<1e-5,"Envelope/recording trace does not match audible gain");if(!recorded)check(p.source.id==native.automation[0].id,"Envelope link loses its stable lane ID");}check(seen,"Musical source missing");
    if(!recorded){native.automation.clear();chain.updateMusicalAutomation(native);audio.fill(.25f);chain.beginRenderBlock();renderer.render(module.data(),128);chain.process(audio.data(),128);bool reset=false;for(const auto &p:activity.history())reset|=p.source.kind==ParameterOrigin::Reset;check(reset,"Live envelope removal is absent from audit");}
  }
}
static void graphCopies(const PluginDescriptor &descriptor) {
  NativeSong native;native.patterns[0].id=3;native.tracks[0].id=1;native.tracks[1].id=4;native.mixer.buses={{1,2,MixerBusKind::Track,"One"},{4,2,MixerBusKind::Track,"Two"},{2,0,MixerBusKind::Master,"Main"}};
  SignalDefinition d;d.id=100;d.name="Gain";d.nodes={{101,SignalNodeKind::Input,"In"},{102,SignalNodeKind::Plugin,"Gain"},{103,SignalNodeKind::Output,"Out"},{104,SignalNodeKind::Amount,"Amount"}};
  d.nodes[1].plugin={descriptor.format,descriptor.name,descriptor.path,descriptor.classID,descriptor.type,descriptor.subtype,descriptor.manufacturer};d.audio={{101,102},{102,103}};d.modulation={{104,102,7,0,1,0,true}};native.signal.library={d};native.signal.assignments={{1,100,.2,1},{4,100,.8,1}};
  auto activity=std::make_unique<ParameterActivity>(48000);NativeSignalGraph graph(native,48000,true,{},256*1024*1024,256,activity.get());check(activity->processors.size()==2&&activity->processors[0].key!=activity->processors[1].key,"Graph copies share a monitor identity");
  activity->watch(activity->processors[1].key,7,1);activity->begin(0);activity->clock(0,0,0,0,1);
  ClockState clock;clock.m_nMusicSpeed=1;clock.m_nSamplesPerTick=256;clock.m_nBufferCount=256;clock.m_nPattern=0;clock.m_nRow=0;clock.m_nTickCount=0;clock.m_nCurrentOrder=0;
  std::array<float,256> first{},second{};first.fill(.25f);second.fill(.25f);tracker_audit_begin();graph.begin(clock,128,0,{120,0,0,4,true});bool okay=graph.process(0,first.data(),128,0,{})&&graph.process(1,second.data(),128,0,{});uint64_t a,f,l;tracker_audit_end(&a,&f,&l);check(okay&&a+f+l==0,"Observed graph render fails realtime audit");
  bool final=false,contribution=false;for(const auto &p:activity->history()){if(p.source.kind==ParameterOrigin::Graph){final=true;check(std::abs(p.value-.8)<1e-6&&p.audible,"Graph monitor captured another copy's value");}if(p.source.kind==ParameterOrigin::GraphSource&&p.source.id==1){contribution=true;check(std::abs(p.value-.8)<1e-6,"Incorrect normalized graph source contribution");}}
  check(final&&contribution,"Graph trace omitted output or sources");check(std::abs(second[0]-.2)<1e-6&&std::abs(first[0]-.05)<1e-6,"Monitor changed graph audio");
}
static void bounded() {
  auto a=std::make_unique<ParameterActivity>(48000);ParameterProcessor target;target.key="test";target.parameters={{7,"Gain",0,1,1}};a->add(target);a->watch("test",7,1);a->begin(0);a->clock(0,0,0,0,1);
  tracker_audit_begin();for(uint64_t i=0;i<40000;++i){a->value(1,7,.5,i,{});a->held(1,i);}uint64_t alloc,free,lock;tracker_audit_end(&alloc,&free,&lock);check(alloc+free+lock==0&&a->dropped()>0,"Full capture must drop without blocking or allocation");check(a->history().size()==32768,"Capture history is not bounded");
  a->watch("test",7,1,true);check(a->history().empty(),"New capture retained old-generation events");a->begin(40000);a->value(1,7,.8,40000,{});a->held(1,40000);check(a->history().back().value==.8,"Monitor failed after overflow and clear");
}
static void sessionAPI() {
  TrackerSession *session=[TrackerSession new];NSError *error=nil;
  auto call=[&](NSString *method,NSDictionary *params,bool write=false)->NSDictionary *{auto p=[params mutableCopy];if(write)p[@"expectedRevision"]=session.automationRevision;auto r=[session automationMethod:method params:p error:&error];if(!r)throw std::runtime_error(std::string(method.UTF8String)+": "+error.localizedDescription.UTF8String);return r;};
  call(@"plugin.add",@{@"descriptor":@{@"format":@"Built-in",@"classID":@"resonance.gainer.v1",@"type":@0,@"subtype":@0,@"manufacturer":@0}},true);
  NSDictionary *target=call(@"parameter.activity.targets",@{})[@"data"][@"targets"][0];NSString *key=target[@"key"],*plugin=target[@"plugin"];
  auto revision=session.automationRevision;call(@"parameter.activity.watch",@{@"target":key,@"parameter":@1});check([revision isEqual:session.automationRevision],"Watching creates song history");
  NSDictionary *edit=@{@"plugin":plugin,@"parameter":@1,@"frame":@48000,@"value":@(-6)};
  auto dry=[edit mutableCopy];dry[@"dryRun"]=@YES;call(@"automation.recorded.edit",dry,true);check([revision isEqual:session.automationRevision],"Recorded dry run changes song");call(@"automation.recorded.edit",edit,true);
  auto lane=[&](){return call(@"automation.recorded.get",@{@"plugin":plugin,@"parameter":@1})[@"data"][@"points"];};check([lane() count]==1,"Recorded point is not visible");revision=session.automationRevision;call(@"automation.recorded.edit",edit,true);check([revision isEqual:session.automationRevision],"Identical point edit creates undo");
  call(@"history.undo",@{@"domain":@"plugins"},true);check([lane() count]==0,"Recorded Undo failed");call(@"history.redo",@{@"domain":@"plugins"},true);check([lane() count]==1,"Recorded Redo failed");
  auto invalid=[edit mutableCopy];invalid[@"value"]=@999;invalid[@"expectedRevision"]=session.automationRevision;check(![session automationMethod:@"automation.recorded.edit" params:invalid error:&error]&&[lane() count]==1,"Invalid value partially replaced recording");
  invalid=[edit mutableCopy];invalid[@"expectedRevision"]=@"stale";check(![session automationMethod:@"automation.recorded.edit" params:invalid error:&error],"Stale recorded write accepted");
  NSString *path=[NSTemporaryDirectory() stringByAppendingPathComponent:[NSUUID.UUID.UUIDString stringByAppendingString:@".screamseq"]];check([session savePath:path error:&error]&&[session openPath:path error:&error]&&[lane() count]==1,"Recorded edit failed project roundtrip");[[NSFileManager defaultManager]removeItemAtPath:path error:nil];
}
int main(int argc,char **argv){@autoreleasepool{try{trustFixtureArguments(argc,argv);check(argc==2,"Fixture path required");auto descriptor=NativePlugin::discoverVST3(argv[1]).front();scheduler(descriptor);musical(descriptor);graphCopies(descriptor);bounded();sessionAPI();std::cout<<"Parameter activity: audio values, provenance, copies, timing, bounded realtime capture, API and persistence passed\n";return 0;}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}}
