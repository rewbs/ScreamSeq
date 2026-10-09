#include "editor/hosted/SelectionRender.hpp"
#include "../Audio/AudioUnitHost.hpp"
#import "../Bridge/TrackerSession.h"
#include <cmath>
#include <iostream>
#include "soundlib/ModInstrument.h"
std::vector<Tracker::PluginDescriptor> registerFixtureAUs();
using namespace Tracker; using namespace OpenMPT;
static void check(bool ok,const char *why){if(!ok)throw std::runtime_error(why);}
static std::unique_ptr<Document> fixture(MODTYPE type=MOD_TYPE_MPT) {
  auto d=std::make_unique<Document>(type);
  d->transaction([](CSoundFile &s){
    for(auto &p:s.Patterns)if(p.IsValid())for(auto &c:p)c.Clear();
    s.Patterns[0].Resize(8);s.Order().assign(1,0);s.Order().SetDefaultTempoInt(125);s.Order().SetDefaultSpeed(6);
    s.m_nSamples=1;auto &sample=s.GetSample(1);sample.Initialize();sample.nLength=48000;sample.nC5Speed=48000;sample.uFlags.set(CHN_16BIT);
    check(sample.AllocateSample()!=0,"Allocate resampling fixture");for(uint32_t i=0;i<sample.nLength;++i)sample.sample16()[i]=int16_t(3000*std::sin(i*.031));sample.PrecomputeLoops(s,false);
    for(int channel=0;channel<2;++channel){s.ChnSettings[channel].nPan=channel?256:0;auto &note=*s.Patterns[0].GetpModCommand(0,CHANNELINDEX(channel));note.note=61;note.instr=1;}
  });return d;
}
static double energy(const std::vector<float> &pcm,size_t side){double value=0;for(size_t i=side;i<pcm.size();i+=2)value+=pcm[i]*pcm[i];return value;}
static void apiChecks() {
  TrackerSession *session=[TrackerSession new];NSError *error=nil;
  auto call=[&](NSString *method,NSDictionary *values,bool write=false){auto params=[values mutableCopy];if(write)params[@"expectedRevision"]=session.automationRevision;error=nil;auto reply=[session automationMethod:method params:params error:&error];if(!reply)throw std::runtime_error(error.localizedDescription.UTF8String);return reply;};
  const auto before=call(@"document.get",@{})[@"data"];
  NSString *revision=session.automationRevision;
  NSDictionary *selection=@{@"pattern":@0,@"firstRow":@0,@"lastRow":@1,@"firstChannel":@0,@"lastChannel":@0,@"createInstrument":@YES,@"name":@"Offline phrase"};
  auto dry=[selection mutableCopy];dry[@"dryRun"]=@YES;
  const auto preview=call(@"sample.renderSelection",dry,true);
  check(![preview[@"changed"] boolValue]&&[revision isEqual:session.automationRevision],"Selection dry-run preserves revision/history");
  for(NSDictionary *invalid in @[@{@"firstRow":@YES},@{@"lastRow":@65535},@{@"lastChannel":@65535},@{@"tailSeconds":@61},@{@"unknown":@1}]) {
    auto values=[selection mutableCopy];[values addEntriesFromDictionary:invalid];values[@"expectedRevision"]=revision;
    check(![session automationMethod:@"sample.renderSelection" params:values error:&error]&&error.code==-32602&&[revision isEqual:session.automationRevision],"Selection API rejects invalid input atomically");
  }
  auto stale=[selection mutableCopy];stale[@"expectedRevision"]=@"stale";
  check(![session automationMethod:@"sample.renderSelection" params:stale error:&error]&&error.code==-32001,"Selection render guards stale document revision");
  // The packaged application dispatches document edits onto a small-stack GCD
  // worker. A main-thread-only fixture misses large render-state stack overflow.
  auto worker=dispatch_queue_create("org.screamseq.sampling-regression",DISPATCH_QUEUE_SERIAL);
  dispatch_semaphore_t finished=dispatch_semaphore_create(0);
  __block NSDictionary *workerReply=nil;__block NSString *workerFailure=nil;
  dispatch_async(worker, ^{ @autoreleasepool {
    try { workerReply=call(@"sample.renderSelection",selection,true); }
    catch(const std::exception &e){workerFailure=@(e.what());}
    dispatch_semaphore_signal(finished);
  }});
  check(dispatch_semaphore_wait(finished,dispatch_time(DISPATCH_TIME_NOW,30*NSEC_PER_SEC))==0,"Selection render worker completed");
  if(workerFailure)throw std::runtime_error(workerFailure.UTF8String);
  const auto added=workerReply[@"data"];
  check([added[@"frames"] integerValue]>0&&[added[@"instrument"] integerValue]>0,"API renders and creates sample/instrument atomically");
  const auto pcm=call(@"sample.pcm.get",@{@"sample":added[@"sample"],@"frames":@64})[@"data"];
  call(@"history.undo",@{},true);
  const auto undone=call(@"document.get",@{})[@"data"];
  check([undone[@"samples"] isEqual:before[@"samples"]]&&[undone[@"instruments"] isEqual:before[@"instruments"]],"API Undo removes both created assets");
  call(@"history.redo",@{},true);
  check([call(@"sample.pcm.get",@{@"sample":added[@"sample"],@"frames":@64})[@"data"] isEqual:pcm],"API Redo restores PCM");
  NSString *path=[NSTemporaryDirectory() stringByAppendingPathComponent:[NSUUID.UUID.UUIDString stringByAppendingString:@".screamseq"]];
  check([[session serializedData] writeToFile:path atomically:YES],"Persist rendered sample fixture");
  TrackerSession *reopen=[TrackerSession new];check([reopen openPath:path error:&error],"Reload rendered sample/instrument project");
  const auto restored=[reopen automationMethod:@"sample.pcm.get" params:@{@"sample":added[@"sample"],@"frames":@64} error:&error];
  check([restored[@"data"] isEqual:pcm],"Native project retains rendered PCM");
  [reopen shutdown];[session shutdown];[NSFileManager.defaultManager removeItemAtPath:path error:nil];
}
int main(){@autoreleasepool{try{
  auto d=fixture();auto before=d->snapshotData();auto meta=d->native();
  auto all=renderPatternSelection(prepareSamplingSelection(*d,{0,0,7,0,1}),{},{});
  check(all.selectedFrames==46080&&all.preRollFrames==0&&all.pcm.size()==92160,"Eight125BPM rows have exact frame count");
  auto left=renderPatternSelection(prepareSamplingSelection(*d,{0,2,4,0,0}),{},{});
  auto right=renderPatternSelection(prepareSamplingSelection(*d,{0,2,4,1,1}),{},{});
  check(left.selectedFrames==17280&&left.preRollFrames==11520,"Exact inclusive selected range with row-zero warm-up");
  check(energy(left.pcm,0)>.01&&energy(left.pcm,1)<1e-10&&energy(right.pcm,1)>.01&&energy(right.pcm,0)<1e-10,"Source-channel isolation and prior held notes");
  for(size_t i=0;i<left.pcm.size();++i)check(std::abs(left.pcm[i]+right.pcm[i]-all.pcm[i+2*11520])<1e-6,"Selected audio equals independently cropped full mix");
  for(uint32_t block:{17u,128u,4096u}) {auto partition=renderPatternSelection(prepareSamplingSelection(*d,{0,2,4,0,0}),{},{},0,block);check(partition.pcm.size()==left.pcm.size(),"Block-independent range");for(size_t i=0;i<left.pcm.size();++i)check(std::abs(partition.pcm[i]-left.pcm[i])<1e-6,"Partition-independent exact crop");}
  auto tail=renderPatternSelection(prepareSamplingSelection(*d,{0,2,4,0,0}),{},{},.05);
  check(tail.selectedFrames==17280&&tail.tailFrames==2400&&tail.pcm.size()==39360,"Explicit tail is appended after selected duration");
  check(d->snapshotData()==before&&d->native()==meta,"Offline rendering does not mutate source");
  d->annotate([](NativeSong &n){PatternCommand c;c.pattern=n.patterns.at(0).id;c.track=n.tracks.at(0).id;c.kind=PatternCommandKind::Native;c.native=NativePatternOp::GainSet;c.arguments=nativePatternDefaults(c.native);c.arguments[0]=.25;n.performance.commands.push_back(c);});
  auto gained=renderPatternSelection(prepareSamplingSelection(*d,{0,2,4,0,0}),{},{});
  for(size_t i=0;i<left.pcm.size();++i)check(std::abs(gained.pcm[i]-.25*left.pcm[i])<2e-6,"Native fine-resolution effects survive warm-up and selection");
  d->annotate([](NativeSong &n){n.performance.commands.clear();PatternCommand c;c.pattern=n.patterns.at(0).id;c.track=n.tracks.at(0).id;c.position=2*65536;c.kind=PatternCommandKind::Native;c.native=NativePatternOp::RowLength;c.arguments=nativePatternDefaults(c.native);c.arguments[0]=.5;n.performance.commands.push_back(c);c.position=3*65536;c.native=NativePatternOp::TempoSet;c.arguments=nativePatternDefaults(c.native);c.arguments[0]=250;n.performance.commands.push_back(c);});
  auto timed=renderPatternSelection(prepareSamplingSelection(*d,{0,2,4,0,0}),{},{});
  check(timed.preRollFrames==11520&&timed.selectedFrames==17280,"Native RL and tempo determine boundaries from actual mixed frames");
  d->annotate([](NativeSong &n){n.performance.commands.clear();});
  d->transaction([](CSoundFile &s){auto &c=*s.Patterns[0].GetpModCommand(3,0);c.command=CMD_POSITIONJUMP;c.param=0;});
  auto bounded=renderPatternSelection(prepareSamplingSelection(*d,{0,2,4,0,0}),{},{});
  check(bounded.selectedFrames==17280,"A pattern jump cannot repeat or escape the selected time");
  PluginState gain;gain.descriptor.format="Built-in";gain.descriptor.classID="resonance.gainer.v1";gain.instanceID="selection-gain";
  NativePlugin probe(gain,48000,true);const auto parameters=probe.parameters();check(!parameters.empty(),"Builtin gain parameters available");
  check(probe.parameter(1,-6),"Configure offline gain");gain=probe.state();
  auto effect=renderPatternSelection(prepareSamplingSelection(*d,{0,2,4,0,0}),{gain},{});
  check(energy(effect.pcm,0)<energy(left.pcm,0)*.4&&energy(effect.pcm,0)>energy(left.pcm,0)*.1,"Hosted plugin DSP is retained in selected rendering");
  { auto muted=fixture(MOD_TYPE_S3M);muted->transaction([](CSoundFile &s){s.m_playBehaviour.set(kST3NoMutedChannels);auto &c=*s.Patterns[0].GetpModCommand(2,1);c.command=CMD_TEMPO;c.param=250;});
    const auto isolated=renderPatternSelection(prepareSamplingSelection(*muted,{0,2,4,0,0}),{},{});
    check(isolated.selectedFrames==8640,"Excluded S3M source retains global tempo commands");
    muted->transaction([](CSoundFile &s){s.ChnSettings[1].dwFlags.set(CHN_MUTE);});
    const auto originallyMuted=renderPatternSelection(prepareSamplingSelection(*muted,{0,2,4,0,0}),{},{});
    check(originallyMuted.selectedFrames==17280,"Already-muted ST3 effects retain their original skip behavior"); }
  { auto arranged=fixture();const int pattern=arranged->addPattern(8,false,0);
    arranged->transaction([&](CSoundFile &s){s.Order().assign(2,0);s.Order()[1]=PATTERNINDEX(pattern);auto &c=*s.Patterns[pattern].GetpModCommand(1,0);c.note=61;c.instr=1;});
    const auto range=prepareSamplingSelection(*arranged,{uint16_t(pattern),0,2,0,0});
    auto recorded=renderPatternSelection(range,{gain},{{0,1,-12,0},{0,1,-6,46080}});
    auto manual=renderPatternSelection(range,{gain},{});
    check(recorded.automationStartFrame==46080&&recorded.selectedFrames==17280,"First arrangement occurrence aligns recorded automation");
    check(std::all_of(recorded.pcm.begin(),recorded.pcm.begin()+11520,[](float x){return std::abs(x)<1e-8;}),"No sample voice carries in from preceding pattern");
    for(size_t i=0;i<recorded.pcm.size();++i)check(std::abs(recorded.pcm[i]-manual.pcm[i])<1e-6,"Recorded absolute automation catch-up matches manual preset at selected pattern");
  }
  { auto instrument=fixture();instrument->transaction([](CSoundFile &s){s.m_nInstruments=1;s.Instruments[1]=new ModInstrument(0);for(auto &c:s.Patterns[0])c.Clear();auto &c=*s.Patterns[0].GetpModCommand(0,1);c.note=61;c.instr=1;});
    PluginState synth;synth.descriptor=registerFixtureAUs().back();synth.instrument=1;synth.instanceID="selection-synth";
    const auto included=renderPatternSelection(prepareSamplingSelection(*instrument,{0,0,2,1,1}),{synth},{});
    const auto excluded=renderPatternSelection(prepareSamplingSelection(*instrument,{0,0,2,0,0}),{synth},{});
    check(energy(included.pcm,0)+energy(included.pcm,1)>.01,"Selected hosted instrument receives its notes");
    check(energy(excluded.pcm,0)+energy(excluded.pcm,1)<1e-10,"Excluded hosted instrument notes cannot leak into selection"); }
  const auto added=importRecordedAudio(*d,effect.pcm,48000,2,"Resampled phrase",true);check(added.frames==17280&&added.instrument>0,"Offline phrase imports as playable sample/instrument");
  apiChecks();
  std::cout<<"PASS selection channel isolation, held-note warmup, exact rows/native timing, partitions, tails, native/hosted FX and atomic import\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}}
