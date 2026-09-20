#import "../Bridge/TrackerSession.h"
#include "editor/TrackerDocument.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace Tracker;
static void check(bool ok, const char *why) { if (!ok) throw std::runtime_error(why); }
template<class F> static void rejects(F f) { bool caught=false; try { f(); } catch(const std::exception &) { caught=true; } check(caught,"Expected loop rejection"); }
static auto geometry(const ModSample &s) { return SampleEditGeometry{s.nLength,s.nLoopStart,s.nLoopEnd,s.nSustainStart,s.nSustainEnd,s.uFlags.GetRaw(),s.cues,s.nativeReverseLoops}; }
static auto pcm(const ModSample &s) { return std::vector<std::byte>(s.sampleb(),s.sampleb()+s.GetSampleSizeInBytes()); }
static void install(Document &d,int bits=16,int channels=2,uint32_t frames=1025) {
  auto &s=d.song().GetSample(1);s.Initialize(d.song().GetType());s.nLength=frames;
  s.uFlags.set(CHN_16BIT,bits==16);s.uFlags.set(CHN_STEREO,channels==2);s.nC5Speed=22050;s.FrequencyToTranspose();
  s.nLoopStart=9;s.nLoopEnd=17;s.nSustainStart=21;s.nSustainEnd=29;s.cues[0]=7;
  check(s.AllocateSample()!=0,"Allocate loop source");
  for(size_t i=0;i<size_t(frames)*channels;++i) { if(bits==16)s.sample16()[i]=int16_t(int(i*997%65535)-32767);else s.sample8()[i]=int8_t(int(i*73%255)-127); }
  s.PrecomputeLoops(d.song(),false);d.song().m_nSamples=1;
  auto &note=*d.song().Patterns[0].GetpModCommand(0,0);note.note=61;note.instr=1;note.command=CMD_VOLUME;note.param=64;
  d.song().Patterns[0].GetpModCommand(2,0)->note=NOTE_KEYOFF;
  *d.song().Patterns[0].GetpModCommand(4,0)=note;d.song().Patterns[0].GetpModCommand(6,0)->note=NOTE_KEYOFF;
  auto native=d.native();native.reconcile(d.song());d.restoreNative(native);
}
static void independent(ModSample &s,int normal,int sustain) {
  s.nativeReverseLoops=(normal==3?1:0)|(sustain==3?2:0);
  s.nLoopStart=65;s.nLoopEnd=997;s.nSustainStart=131;s.nSustainEnd=769;
  s.uFlags.set(CHN_LOOP,normal>0);s.uFlags.set(CHN_PINGPONGLOOP,normal==2);
  s.uFlags.set(CHN_SUSTAINLOOP,sustain>0);s.uFlags.set(CHN_PINGPONGSUSTAIN,sustain==2);
}
int main() { @autoreleasepool { try {
  size_t cases=0;
  for(auto type:{MOD_TYPE_MOD,MOD_TYPE_S3M,MOD_TYPE_XM,MOD_TYPE_IT,MOD_TYPE_MPT})for(int bits:{8,16})for(int channels:{1,2})
    for(int normal=0;normal<4;++normal)for(int sustain=0;sustain<4;++sustain) {
      Document d(type,4);install(d,bits,channels);const auto before=geometry(d.song().GetSample(1));const auto original=pcm(d.song().GetSample(1)); const auto base=d.snapshotData();
      auto prepared=d.prepareSampleLoops(1,SampleLoopSettings{65,997,normal>0,normal==2,normal==3},SampleLoopSettings{131,769,sustain>0,sustain==2,sustain==3});
      check(d.revision==0 && !d.canUndo() && geometry(d.song().GetSample(1))==before,"Preparation preserves document");
      const auto after=prepared.geometry()->second;auto expected=d.song().GetSample(1);independent(expected,normal,sustain);
      check(after==geometry(expected),"Prepared loop fields match independent expectation");
      d.applySampleProcess(std::move(prepared));check(geometry(d.song().GetSample(1))==after && pcm(d.song().GetSample(1))==original,"Loop edit preserves exact PCM and unrelated fields");
      d.undo();check(geometry(d.song().GetSample(1))==before,"One Undo restores both loops and inactive settings");
      d.redo();Document reopened(d.snapshotData());check(geometry(reopened.song().GetSample(1))==after && pcm(reopened.song().GetSample(1))==original,"Five-format native snapshot preserves both loop modes");
      d.transaction([](auto &song){song.m_songName="Mixed history";});d.undo();d.undo();check(geometry(d.song().GetSample(1))==before,"Mixed document and loop history exact");d.redo();
      Renderer a(reopened.snapshotData(),48000),reference(base,48000);
      auto &s=reference.song().GetSample(1);s.FreeSample();s=expected;s.pData.pSample=nullptr;
      check(s.AllocateSample()!=0,"Allocate independent loop renderer");std::memcpy(s.samplev(),original.data(),original.size());s.PrecomputeLoops(reference.song(),false);
      std::array<float,8192>x{},y{};bool signal=false;
      for(size_t count:{17u,128u,4096u,4096u,4096u,4096u,4096u,4096u,4096u,4096u}) {
        const auto n=a.render(x.data(),count);const auto rn=reference.render(y.data(),count);
        if(n!=rn || !std::equal(x.begin(),x.begin()+n*2,y.begin())) { std::cerr<<"audio case "<<int(type)<<" bits="<<bits<<" channels="<<channels<<" normal="<<normal<<" sustain="<<sustain<<" block="<<count<<"\n"; throw std::runtime_error("Loop audio matches independent settings across held notes and release"); }
        signal|=std::any_of(x.begin(),x.begin()+n*2,[](float v){return v!=0;});
      }check(signal,"Loop audio reference must be nonzero");++cases;
    }
  for(uint32_t first:{0u,1024u})for(bool ping:{false,true})for(bool held:{false,true}) {
    Document tiny;install(tiny);const SampleLoopSettings one{first,first+1,true,ping};
    tiny.applySampleProcess(tiny.prepareSampleLoops(1,held?std::nullopt:std::optional(one),held?std::optional(one):std::nullopt));
    const auto bytes=tiny.snapshotData();Renderer a(bytes,48000),b(bytes,48000);
    std::vector<float> x(32768),y(32768);size_t at=0;
    while(at<16384){const auto count=std::min<size_t>(512,16384-at);check(a.render(x.data()+at*2,count)==count,"Single-frame loop renders");at+=count;}
    at=0;while(at<16384){const auto count=std::min<size_t>(17,16384-at);check(b.render(y.data()+at*2,count)==count,"Partitioned single-frame loop renders");at+=count;}
    check(x==y && std::any_of(x.begin(),x.end(),[](float v){return v!=0;}),"One-frame normal/sustain forward/ping-pong loops at both asset edges remain nonzero and callback-independent");
  }
  Document large;install(large,16,2,2097152);large.waveform(1,1024);auto scans=large.waveformReadFrames();
  const SampleLoopSettings normal{100,1900000,true,false},sustain{500,1500000,true,true};
  large.applySampleProcess(large.prepareSampleLoops(1,normal,sustain));large.waveform(1,1024);
  check(large.historyBytes()<8192 && large.waveformReadFrames()==scans,"Loop-only changes keep compact history and cached waveform");
  const auto changed=geometry(large.song().GetSample(1));large.applySampleProcess(large.prepareSampleLoops(1,SampleLoopSettings{100,1900000,false,false},std::nullopt));large.undo();
  const auto revision=large.revision;large.applySampleProcess(large.prepareSampleLoops(1,normal,sustain));
  check(large.revision==revision && large.canRedo() && geometry(large.song().GetSample(1))==changed,"No-op preserves redo/revision");
  auto stale=large.prepareSampleLoops(1,normal,sustain);large.annotate([](auto &n){n.samples.at(1).annotation="changed";});rejects([&]{large.applySampleProcess(std::move(stale));});
  auto foreign=large.prepareSampleLoops(1,normal,sustain);Document other;rejects([&]{other.applySampleProcess(std::move(foreign));});
  auto tampered=large.prepareSampleLoops(1,normal,sustain);large.song().GetSample(1).nSustainStart++;rejects([&]{large.applySampleProcess(std::move(tampered));});
  for(auto invalid:{SampleLoopSettings{2,1,true,false},{0,0,true,false},{0,2097153,true,false},{0,100,false,true}})
    rejects([&]{large.prepareSampleLoops(1,normal,invalid);});
  rejects([&]{large.prepareSampleLoops(1,std::nullopt,std::nullopt);});
  Document empty;empty.song().m_nSamples=1;auto off=empty.prepareSampleLoops(1,SampleLoopSettings{},SampleLoopSettings{});check(!off.hasChanges(),"Empty sample accepts disabled empty loops");
  large.song().GetSample(1).uFlags.set(CHN_ADLIB);rejects([&]{large.prepareSampleLoops(1,normal,sustain);});

  NSString *folder=[NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString];
  std::filesystem::create_directories(folder.UTF8String);NSString *input=[folder stringByAppendingPathComponent:@"input.snapshot"],*project=[folder stringByAppendingPathComponent:@"loop.resonance"],*wave=[folder stringByAppendingPathComponent:@"loop.wav"];
  size_t projects=0;
  for(auto type:{MOD_TYPE_MOD,MOD_TYPE_S3M,MOD_TYPE_XM,MOD_TYPE_IT,MOD_TYPE_MPT})for(int normal=0;normal<4;++normal)for(int sustain=0;sustain<4;++sustain) {
    Document source(type,4);install(source);auto snapshot=source.snapshotData();
    check([[NSData dataWithBytes:snapshot.data() length:snapshot.size()] writeToFile:input atomically:YES],"Write loop project fixture");
    TrackerSession *session=[TrackerSession new];NSError *error=nil;check([session openPath:input error:&error],"Open loop fixture");
    auto call=[&](NSString *method,NSDictionary *params,bool mutation=false){auto p=[params mutableCopy];if(mutation)p[@"expectedRevision"]=session.automationRevision;auto r=[session automationMethod:method params:p error:&error];if(!r)throw std::runtime_error(error.localizedDescription.UTF8String);return r;};
    auto before=call(@"sample.get",@{@"sample":@1})[@"data"];
    NSDictionary *params=@{@"sample":@1,@"normal":@{@"start":@65,@"end":@997,@"enabled":@(normal>0),@"pingpong":@(normal==2),@"reverse":@(normal==3)},@"sustain":@{@"start":@131,@"end":@769,@"enabled":@(sustain>0),@"pingpong":@(sustain==2),@"reverse":@(sustain==3)}};
    auto previewParams=[params mutableCopy];previewParams[@"dryRun"]=@YES;auto preview=call(@"sample.loops.set",previewParams,true);check(![preview[@"changed"] boolValue],"Loop API preview read-only");
    auto applied=call(@"sample.loops.set",params,true);check([applied[@"changed"] boolValue] && [preview[@"data"][@"after"] isEqual:applied[@"data"][@"after"]],"Loop API exact preview/apply");
    auto after=call(@"sample.get",@{@"sample":@1})[@"data"];
    call(@"history.undo",@{@"domain":@"document"},true);check([before isEqual:call(@"sample.get",@{@"sample":@1})[@"data"]],"API loop Undo exact");call(@"history.redo",@{@"domain":@"document"},true);
    call(@"document.save",@{@"path":project,@"overwrite":@YES},true);check([session openPath:project error:&error] && [after isEqual:call(@"sample.get",@{@"sample":@1})[@"data"]],"Native project loop settings exact");
    Renderer reference(snapshot,48000);auto &s=reference.song().GetSample(1);s.FreeSample();s=source.song().GetSample(1);s.pData.pSample=nullptr;independent(s,normal,sustain);
    check(s.AllocateSample()!=0,"Allocate project reference");auto original=pcm(source.song().GetSample(1));std::memcpy(s.samplev(),original.data(),original.size());s.PrecomputeLoops(reference.song(),false);
    check([TrackerSession exportData:session.serializedData path:wave error:&error],"Export loop project WAV");
    std::ifstream audio(wave.UTF8String,std::ios::binary);audio.seekg(44);std::array<float,1024>x{},y{};size_t frames=0;bool signal=false;
    while(auto count=reference.render(y.data(),512)){audio.read(reinterpret_cast<char *>(x.data()),count*2*sizeof(float));check(bool(audio)&&std::equal(x.begin(),x.begin()+2*count,y.begin()),"Whole loop WAV matches independent normal/sustain settings");frames+=count;signal|=std::any_of(x.begin(),x.begin()+2*count,[](float v){return v!=0;});}
    check(frames>48000 && signal && audio.peek()==std::char_traits<char>::eof(),"Loop WAV duration and nonzero signal exact");
    // General sample settings must preserve both loops when the caller edits only a name.
    check([session sampleSettings:1 values:@{@"name":@"Renamed"} error:&error],"Partial sample settings accepted");
    auto renamed=[after mutableCopy];renamed[@"name"]=@"Renamed";
    check([renamed isEqual:call(@"sample.get",@{@"sample":@1})[@"data"]],"Non-loop properties do not reset saved loops");
    ++projects;
  }
  std::filesystem::remove_all(folder.UTF8String);
  std::cout<<"PASS "<<cases<<" loop mode/layout/format history and audio cases; "<<projects<<" API/native-project/whole-WAV references; compact history, no-op, stale/foreign/tampered and invalid input checks\n";
  return 0;
}catch(const std::exception &e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;} } }
