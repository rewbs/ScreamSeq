#import "../Bridge/TrackerSession.h"
#include "editor/TrackerDocument.hpp"
#include "editor/InstrumentEnvelopeTools.hpp"
#include "editor/SampleArchive.hpp"
#include <iostream>
using namespace Tracker;using namespace OpenMPT;
static void check(bool value,const char *why){if(!value)throw std::runtime_error(why);}
static std::unique_ptr<Document> fixture(MODTYPE type){auto doc=std::make_unique<Document>(type);
  doc->transaction([](CSoundFile &s){s.m_nInstruments=1;s.Instruments[1]=new ModInstrument(1);s.m_nSamples=1;
    auto &sample=s.GetSample(1);sample.Initialize();sample.nLength=64;sample.nLoopStart=0;sample.nLoopEnd=64;sample.uFlags.set(CHN_16BIT|CHN_LOOP);
    if(!sample.AllocateSample())throw std::bad_alloc();for(int i=0;i<64;++i)sample.sample16()[i]=int16_t((i%16-8)*1600);sample.PrecomputeLoops(s,false);
    auto &e=s.Instruments[1]->VolEnv;e.push_back(0,0);e.push_back(4,64);e.push_back(8,32);e.push_back(12,0);e.dwFlags.set(ENV_ENABLED);
    auto &note=*s.Patterns[0].GetpModCommand(0,0);note.note=61;note.instr=1;s.Patterns[0].GetpModCommand(8,0)->note=NOTE_KEYOFF;
  });return doc;}
int main(){@autoreleasepool{try{
  NSString *folder=[NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString];[NSFileManager.defaultManager createDirectoryAtPath:folder withIntermediateDirectories:NO attributes:nil error:nil];
  for(auto type:{MOD_TYPE_XM,MOD_TYPE_IT,MOD_TYPE_MPT}) {
    auto source=fixture(type);NSString *path=[folder stringByAppendingPathComponent:@"source.module"];source->save(path.UTF8String);
    TrackerSession *session=[TrackerSession new];NSError *error=nil;check([session openPath:path error:&error],"Open sample instrument fixture");
    NSString *identity=[session snapshot:0][@"instruments"][0][@"id"];
    auto call=[&](NSString *method,NSDictionary *params,bool write=false){auto p=[params mutableCopy];if(write)p[@"expectedRevision"]=session.automationRevision;
      auto result=[session automationMethod:method params:p error:&error];if(!result)throw std::runtime_error(error.localizedDescription.UTF8String);return result;};
    NSDictionary *target=@{@"instrument":identity,@"envelope":@"volume"};const auto before=call(@"instrument.envelope.get",target)[@"data"];
    NSDictionary *params=@{@"instrument":identity,@"envelope":@"volume",@"operation":@"flip-values",@"start":@0,@"end":@13};
    auto dry=[params mutableCopy];dry[@"dryRun"]=@YES;NSString *revision=session.automationRevision;
    const auto preview=call(@"instrument.envelope.transform",dry,true)[@"data"];
    check([preview[@"after"][@"points"] isEqual:@[@[@0,@64],@[@4,@0],@[@8,@32],@[@12,@64]]]&&[preview[@"wouldChange"] boolValue]&&[revision isEqual:session.automationRevision],"Dry run returns independently calculated native vertices without editing");
    call(@"instrument.envelope.transform",params,true);auto after=call(@"instrument.envelope.get",target)[@"data"];
    call(@"history.undo",@{@"domain":@"document"},true);check([call(@"instrument.envelope.get",target)[@"data"] isEqual:before],"Undo restores complete envelope");
    auto noop=[params mutableCopy];noop[@"operation"]=@"scale";noop[@"options"]=@{@"amount":@1};
    check(![call(@"instrument.envelope.transform",noop,true)[@"changed"] boolValue]&&session.canRedo,"No-op retains pending Redo");
    call(@"history.redo",@{@"domain":@"document"},true);check([call(@"instrument.envelope.get",target)[@"data"] isEqual:after],"Redo restores transformed envelope");
    NSData *saved=[session serializedData];NSString *native=[folder stringByAppendingPathComponent:@"native.resonance"],*wav=[folder stringByAppendingPathComponent:@"native.wav"];
    [saved writeToFile:native atomically:YES];check([session openPath:native error:&error],"Reopen envelope project");
    check([call(@"instrument.envelope.get",target)[@"data"] isEqual:after],"Native recall preserves stable ID, points and markers");
    check([TrackerSession exportData:saved path:wav error:&error],"Envelope renders to offline WAV");
    source->transaction([](CSoundFile &s){auto &e=s.Instruments[1]->VolEnv;e[0].value=64;e[1].value=0;e[2].value=32;e[3].value=64;});
    Renderer reference(source->snapshotData(),48000);NSData *audio=[NSData dataWithContentsOfFile:wav];size_t offset=44;double signal=0;
    std::array<float,1024> expected{};while(auto count=reference.render(expected.data(),512)){
      check(offset+count*8<=audio.length,"WAV contains every reference frame");const auto *bytes=static_cast<const char *>(audio.bytes)+offset;
      check(std::memcmp(bytes,expected.data(),count*8)==0,"Complete envelope WAV matches independently edited sample renderer");
      for(size_t i=0;i<count*2;++i)signal+=std::abs(expected[i]);offset+=count*8;
    }check(offset==audio.length&&signal>1,"Envelope audio is complete and audible");
    auto malformed=[params mutableCopy];malformed[@"operation"]=@"shift";malformed[@"options"]=@{@"amount":@1};malformed[@"expectedRevision"]=session.automationRevision;
    revision=session.automationRevision;check(![session automationMethod:@"instrument.envelope.transform" params:malformed error:&error]&&error.code==-32602&&[revision isEqual:session.automationRevision],"Removing tick zero rejects before history or audio changes");
  }
  [NSFileManager.defaultManager removeItemAtPath:folder error:nil];std::cout<<"PASS instrument envelope sessions: independent preview/history/native recall, complete three-format WAV equality and rejected edits\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}}
