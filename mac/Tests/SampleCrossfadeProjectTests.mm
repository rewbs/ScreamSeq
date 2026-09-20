#import "../Bridge/TrackerSession.h"
#include "editor/TrackerDocument.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numbers>
using namespace Tracker;
static void check(bool ok,const char *why){if(!ok)throw std::runtime_error(why);}
int main(){@autoreleasepool{try{
  NSString *folder=[NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString];
  std::filesystem::create_directories(folder.UTF8String);
  NSString *input=[folder stringByAppendingPathComponent:@"input.snapshot"],*project=[folder stringByAppendingPathComponent:@"edited.resonance"],*wave=[folder stringByAppendingPathComponent:@"edited.wav"];
  size_t cases=0;
  for(auto type:{MOD_TYPE_MOD,MOD_TYPE_XM,MOD_TYPE_S3M,MOD_TYPE_IT,MOD_TYPE_MPT})for(bool sustain:{false,true})
    for(bool overlap:{false,true})for(bool power:{false,true}) {
      Document source(type,4);auto &sample=source.song().GetSample(1);sample.Initialize(type);
      sample.nLength=1025;sample.uFlags.set(CHN_16BIT|CHN_STEREO);sample.nC5Speed=22050;sample.FrequencyToTranspose();
      sample.uFlags.set(sustain?CHN_SUSTAINLOOP:CHN_LOOP);
      if(sustain){sample.nSustainStart=129;sample.nSustainEnd=1001;}else{sample.nLoopStart=129;sample.nLoopEnd=1001;}
      sample.cues[0]=271;check(sample.AllocateSample()!=0,"Allocate crossfade project source");
      std::vector<int16_t> original(2050),expected;
      for(size_t i=0;i<original.size();++i)sample.sample16()[i]=original[i]=int16_t(int(i*701%65535)-32767);
      sample.PrecomputeLoops(source.song(),false);source.song().m_nSamples=1;
      auto &note=*source.song().Patterns[0].GetpModCommand(0,0);note.note=61;note.instr=1;note.command=CMD_VOLUME;note.param=64;
      auto native=source.native();native.reconcile(source.song());source.restoreNative(native);
      auto snapshot=source.snapshotData();check([[NSData dataWithBytes:snapshot.data() length:snapshot.size()] writeToFile:input atomically:YES],"Write exact sample fixture");
      TrackerSession *session=[TrackerSession new];NSError *error=nil;check([session openPath:input error:&error],"Open native sample fixture");
      auto call=[&](NSString *method,NSDictionary *params,bool mutation=false){NSMutableDictionary *p=[params mutableCopy];if(mutation)p[@"expectedRevision"]=session.automationRevision;auto r=[session automationMethod:method params:p error:&error];if(!r)throw std::runtime_error(error.localizedDescription.UTF8String);return r;};
      NSDictionary *before=call(@"sample.get",@{@"sample":@1})[@"data"];
      NSMutableDictionary *params=[@{@"sample":@1,@"loop":sustain?@"sustain":@"normal",@"mode":overlap?@"overlap":@"preserve",@"curve":power?@"equal-power":@"linear",@"frames":@129,@"dryRun":@YES} mutableCopy];
      auto preview=call(@"sample.crossfade",params,true);check(![preview[@"changed"] boolValue],"API preview neutral");
      params[@"dryRun"]=@NO;auto applied=call(@"sample.crossfade",params,true);
      check([applied[@"changed"] boolValue] && [applied[@"data"][@"loopAfter"][@"start"] intValue]==(overlap?258:129),"API commits explicit crossfade loop period");
      expected=original;
      for(int i=0;i<129;++i)for(int c=0;c<2;++c){const long double t=static_cast<long double>(i)/128;const auto a=original[(872+i)*2+c],b=original[((overlap?129:0)+i)*2+c];
        long double mixed=power?a*std::cos(t*std::numbers::pi_v<long double>/2)+b*std::sin(t*std::numbers::pi_v<long double>/2):(static_cast<long double>(a)*(128-i)+static_cast<long double>(b)*i)/128;
        if(!i)mixed=a;if(i==128)mixed=b;expected[(872+i)*2+c]=int16_t(std::round(std::clamp(mixed,-32768.0L,32767.0L)));}
      auto expectedData=[NSData dataWithBytes:expected.data() length:expected.size()*2];
      auto pcm=[&](){return call(@"sample.pcm.get",@{@"sample":@1})[@"data"][@"data"];};
      check([pcm() isEqual:[expectedData base64EncodedStringWithOptions:0]],"API exact independent crossfade PCM");
      NSDictionary *after=call(@"sample.get",@{@"sample":@1})[@"data"];
      call(@"history.undo",@{@"domain":@"document"},true);
      check([before isEqual:call(@"sample.get",@{@"sample":@1})[@"data"]] && [pcm() isEqual:[[NSData dataWithBytes:original.data() length:original.size()*2] base64EncodedStringWithOptions:0]],"One API Undo restores exact PCM and all exposed loop settings");
      call(@"history.redo",@{@"domain":@"document"},true);call(@"document.save",@{@"path":project,@"overwrite":@YES},true);
      check([session openPath:project error:&error],"Reopen crossfade native project");
      check([pcm() isEqual:[expectedData base64EncodedStringWithOptions:0]] && [after isEqual:call(@"sample.get",@{@"sample":@1})[@"data"]],"Native project retains both crossfade PCM and normal/sustain geometry");
      // Build a renderer from only the unedited base module; install independently calculated
      // PCM and the original fixture header with just the specified loop start moved.
      Renderer reference(source.serialize(),48000);auto &s=reference.song().GetSample(1);s.FreeSample();s=sample;s.pData.pSample=nullptr;
      if(overlap){if(sustain)s.nSustainStart=258;else s.nLoopStart=258;}
      check(s.AllocateSample()!=0,"Independent crossfade renderer allocation");std::memcpy(s.samplev(),expected.data(),expected.size()*2);s.PrecomputeLoops(reference.song(),false);
      check([TrackerSession exportData:session.serializedData path:wave error:&error],"Export crossfaded project WAV");
      std::ifstream audio(wave.UTF8String,std::ios::binary);audio.seekg(44);std::array<float,1024> actual{},expectedAudio{};size_t frames=0;bool signal=false;
      while(auto count=reference.render(expectedAudio.data(),512)){audio.read(reinterpret_cast<char *>(actual.data()),count*2*sizeof(float));check(bool(audio)&&std::equal(actual.begin(),actual.begin()+2*count,expectedAudio.begin()),"Whole exported crossfade WAV matches independent PCM/loop reference exactly");frames+=count;signal|=std::any_of(actual.begin(),actual.begin()+2*count,[](float v){return v!=0;});}
      check(frames>48000 && signal && audio.peek()==std::char_traits<char>::eof(),"Crossfade export has exact complete duration and nonzero audio");++cases;
    }
  std::filesystem::remove_all(folder.UTF8String);
  std::cout<<"PASS "<<cases<<" crossfade API/native save/reopen/full WAV comparisons: five formats, normal/sustain, preserve/overlap and linear/equal-power\n";
  return 0;
}catch(const std::exception &e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}}
