#include "editor/TrackerDocument.hpp"
#include "soundlib/ModInstrument.h"
#import "../Bridge/TrackerSession.h"
#include "GraphRealtimeAudit.hpp"
#include <iostream>
using namespace Tracker;
using namespace OpenMPT;
static void check(bool ok,const char *why){if(!ok)throw std::runtime_error(why);}
int main(){@autoreleasepool{try{
  for(uint32_t rate:{44100,48000,96000}) {
    auto doc=Document::demo();
    doc->transaction([](CSoundFile &s){
      s.m_nInstruments=1;s.Instruments[1]=new ModInstrument(1);
      s.Instruments[1]->VolEnv.push_back(0,64);s.Instruments[1]->VolEnv.push_back(30,32);s.Instruments[1]->VolEnv.dwFlags.set(ENV_ENABLED);
      auto &sample=s.GetSample(1);sample.nLoopStart=100;sample.nLoopEnd=500;sample.uFlags.set(CHN_LOOP);sample.PrecomputeLoops(s);
    });
    Renderer renderer(doc->serialize(),rate,0,true);
    std::array<float,256> block{};
    check(renderer.preview({61,1,100,true}),"First preview note accepted");renderer.render(block.data(),128);
    check(renderer.preview({65,1,100,true}),"Overlapping preview note accepted");
    for(int i=0;i<50;++i){uint64_t a,f,l;tracker_audit_begin();renderer.render(block.data(),128);tracker_audit_end(&a,&f,&l);check(a+f+l==0,"Voice telemetry allocates/frees/locks nothing in rendering");}
    auto positions=renderer.voicePositions();
    check(positions.size()==2,"Both overlapping voices have independent markers");
    check(positions[0].channel!=positions[1].channel && positions[0].sample==1 && positions[0].instrument==1,"Markers identify sample, instrument and channel");
    check(positions[0].sampleFrame<500 && positions[1].sampleFrame<500,"Waveform position follows the loop instead of elapsed time");
    check(positions[0].envelopeTicks[0]>0,"Envelope cursor advances with real voice envelope state");
    renderer.panic();renderer.render(block.data(),128);check(renderer.voicePositions().empty(),"Panic retires playback markers");
  }
  auto doc=Document::demo();doc->transaction([](CSoundFile &s){
    s.m_nTempoMode=TempoMode::Classic;s.Order().SetDefaultSpeed(6);s.Order().SetDefaultTempo(TEMPO(125.0));
    s.Order().assign(2,0);
    for(auto &pattern:s.Patterns)if(pattern.IsValid())for(ROWINDEX r=0;r<pattern.GetNumRows();++r)for(CHANNELINDEX c=0;c<s.GetNumChannels();++c)*pattern.GetpModCommand(r,c)={};
    auto &p=s.Patterns[0];p.Resize(8);p.GetpModCommand(0,0)->command=CMD_TEMPO;p.GetpModCommand(0,0)->param=125;
    p.GetpModCommand(4,0)->command=CMD_TEMPO;p.GetpModCommand(4,0)->param=250;
  });
  NSString *path=[NSTemporaryDirectory() stringByAppendingPathComponent:[NSUUID.UUID.UUIDString stringByAppendingString:@".mptm"]];
  const auto bytes=doc->serialize();[[NSData dataWithBytes:bytes.data() length:bytes.size()] writeToFile:path atomically:YES];
  TrackerSession *session=[TrackerSession new];NSError *error=nil;check([session openPath:path error:&error],"Timing fixture opens");
  auto call=[&](NSString *method,NSDictionary *params,bool write=false)->NSDictionary *{auto p=[params mutableCopy];if(write)p[@"expectedRevision"]=session.automationRevision;auto r=[session automationMethod:method params:p error:&error];if(!r)throw std::runtime_error(error.localizedDescription.UTF8String);return r[@"data"];};
  auto times=call(@"pattern.timeline.get",@{@"pattern":@0,@"order":@1})[@"positions"];
  check(std::abs([times[0][@"songSeconds"] doubleValue]-.72)<1e-6,"Repeated pattern uses selected order's absolute time");
  check(std::abs([times[4][@"patternSeconds"] doubleValue]-.48)<1e-6 && std::abs([times[7][@"patternSeconds"] doubleValue]-.66)<1e-6,"Timeline follows in-pattern tempo changes");
  auto mixer=call(@"mixer.enable",@{},true);(void)mixer;
  auto buses=call(@"mixer.get",@{})[@"buses"];auto bus=buses[0][@"id"];
  call(@"mixer.bus.set",@{@"bus":bus,@"output":NSNull.null},true);
  auto saved=[session serializedData];auto project=[NSPropertyListSerialization propertyListWithData:saved options:0 format:nil error:&error];
  check([project[@"native"][@"version"] intValue]==17,"Disconnected output is versioned for older readers");
  call(@"history.undo",@{@"domain":@"document"},true);check([call(@"mixer.get",@{})[@"buses"][0][@"output"] length]>0,"Undo reconnects the output");
  call(@"history.redo",@{@"domain":@"document"},true);check([call(@"mixer.get",@{})[@"buses"][0][@"output"] length]==0,"Redo disconnects the output");
  NSString *projectPath=[path stringByAppendingString:@".screamseq"];[saved writeToFile:projectPath atomically:YES];
  check([session openPath:projectPath error:&error],"Disconnected bus project reopens");check([call(@"mixer.get",@{})[@"buses"][0][@"output"] length]==0,"Save/reopen preserves disconnected routing");
  auto detachedProject=[NSPropertyListSerialization propertyListWithData:[session serializedData] options:NSPropertyListMutableContainers format:nil error:&error];
  detachedProject[@"native"][@"mixer"][@"instruments"]=@[@{@"plugin":@"unavailable-synth",@"target":@"",@"output":@0}];
  auto detachedBytes=[NSPropertyListSerialization dataWithPropertyList:detachedProject format:NSPropertyListBinaryFormat_v1_0 options:0 error:&error];
  [detachedBytes writeToFile:projectPath atomically:YES];
  check([session openPath:projectPath error:&error],"Disconnected plugin output metadata reopens even when its plugin is unavailable");
  check([call(@"mixer.get",@{})[@"instruments"][0][@"target"] isEqual:@""],"Disconnected plugin destination survives save/reopen");
  [[NSFileManager defaultManager] removeItemAtPath:path error:nil];[[NSFileManager defaultManager] removeItemAtPath:projectPath error:nil];
  std::cout<<"PASS voice cursors: overlap, loops, envelopes, panic and realtime audit at three rates; timeline tempo/order occurrence; disconnected routing Undo/Redo and persistence\n";return 0;
}catch(const std::exception &e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}}
