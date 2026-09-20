#include "editor/TrackerDocument.hpp"
#include "editor/SampleArchive.hpp"
#include <cmath>
#include <cstring>
#include <iostream>
using namespace Tracker;
#ifdef TRACKER_SANITIZER
static void tracker_audit_begin() {}
static void tracker_audit_end(uint64_t *a,uint64_t *f,uint64_t *l){*a=*f=*l=0;}
#else
extern "C" void tracker_audit_begin();
extern "C" void tracker_audit_end(uint64_t *,uint64_t *,uint64_t *);
#endif
static void check(bool value,const char *why) { if(!value)throw std::runtime_error(why); }
template<class F> static void rejects(F f) { bool rejected=false;try{f();}catch(const std::exception &){rejected=true;}check(rejected,"Expected native reverse rejection"); }
static std::vector<float> render(Renderer &r,size_t total,size_t block) {
  std::vector<float> result(total*2);size_t done=0;
  while(done<total){const auto n=std::min(block,total-done);tracker_audit_begin();const auto rendered=r.render(result.data()+done*2,n);uint64_t a,f,l;tracker_audit_end(&a,&f,&l);check(rendered==n,"Render full reverse fixture");check(!a&&!f&&!l,"Reverse render allocates/frees/locks nothing");done+=n;}
  return result;
}
int main() { try {
  size_t phaseCases=0;
  for(uint32_t start:{0,1,129,100000})for(uint32_t length:{1,2,7,257,100000}) {
    NativeReverseLoopState state;const auto end=start+length;
    SamplePosition position,absolute;
    for(uint32_t n=0;n<1000;++n) {
      state.Attach(&state,start,end,false,position);
      for(int offset=-16;offset<=16;++offset) {
        const int64 timeline=int64(absolute.GetInt())+offset;
        const auto expected=timeline<int64(end)?uint32_t(std::clamp<int64>(timeline,0,end+10-1)):end-1-uint32_t((timeline-end)%length);
        check(state.SourceFrame(int64(position.GetInt())+offset,end+10)==expected,"Reverse traversal/tap history agrees with absolute-time integer oracle");++phaseCases;
      }
      const auto delta=SamplePosition::Ratio(n%7==0?length*3+1:(n*991)%4097+1,13);
      absolute+=delta;position=state.Advance(position+delta);
    }
  }
  size_t cases=0;
  for(auto type:{MOD_TYPE_MOD,MOD_TYPE_S3M,MOD_TYPE_XM,MOD_TYPE_IT,MOD_TYPE_MPT})for(uint32_t rate:{8000,44100,48000,96000,192000})for(int bits:{8,16})for(int channels:{1,2})
    for(uint32_t length:{1,2,7,257})for(bool sustain:{false,true}) {
      Document d(type);auto &s=d.song().GetSample(1);s.Initialize();s.nLength=1024;s.nC5Speed=22050;s.FrequencyToTranspose();
      s.uFlags.set(CHN_16BIT,bits==16);s.uFlags.set(CHN_STEREO,channels==2);
      check(s.AllocateSample()!=0,"Allocate reverse fixture");
      for(size_t i=0;i<1024*channels;++i){if(bits==16)s.sample16()[i]=int16_t(int(i*997%65535)-32767);else s.sample8()[i]=int8_t(int(i*71%255)-127);}
      s.PrecomputeLoops(d.song(),false);d.song().m_nSamples=1;
      auto &note=*d.song().Patterns[0].GetpModCommand(0,0);note.note=61;note.instr=1;
      auto native=d.native();native.reconcile(d.song());d.restoreNative(native);
      const auto base=d.snapshotData();const auto start=length==1?0u:129u,end=start+length;
      const SampleLoopSettings options{start,end,true,false,true};
      d.applySampleProcess(d.prepareSampleLoops(1,sustain?std::nullopt:std::optional(options),sustain?std::optional(options):std::nullopt));
      check(s.nativeReverseLoops==(sustain?2:1),"Native reverse configuration commits");
      const auto snapshot=d.snapshotData();Document reopened(snapshot);
      check(reopened.song().GetSample(1).nativeReverseLoops==s.nativeReverseLoops,"Native reverse mode persists");
      Renderer actual(snapshot,rate),partitioned(snapshot,rate),reference(base,rate);
      // Independently unroll the requested chronological PCM: original attack,
      // duplicated turn endpoint, then repeated descending loop frames. Feed
      // this ordinary non-looping asset to the unchanged forward resampler.
      auto &r=reference.song().GetSample(1);r.FreeSample();r=s;r.pData.pSample=nullptr;r.nLength=32768;
      r.uFlags.reset(CHN_LOOP|CHN_SUSTAINLOOP|CHN_PINGPONGLOOP|CHN_PINGPONGSUSTAIN);r.nativeReverseLoops=0;
      check(r.AllocateSample()!=0,"Allocate independent unrolled reverse waveform");
      const auto stride=s.GetBytesPerSample();
      for(uint32_t i=0;i<r.nLength;++i){const auto source=i<end?i:end-1-((i-end)%length);std::memcpy(r.sampleb()+size_t(i)*stride,s.sampleb()+size_t(source)*stride,stride);}
      r.PrecomputeLoops(reference.song(),false);
      auto x=render(actual,8192,512),y=render(reference,8192,17),z=render(partitioned,8192,127);
      if(x!=y){const auto mismatch=std::mismatch(x.begin(),x.end(),y.begin());std::cerr<<"case rate="<<rate<<" bits="<<bits<<" channels="<<channels<<" length="<<length<<" sustain="<<sustain<<" at="<<(mismatch.first-x.begin())/2<<" actual="<<*mismatch.first<<" expected="<<*mismatch.second<<'\n';throw std::runtime_error("Reverse audio differs from independently unrolled forward waveform");}
      check(x==z && std::any_of(x.begin(),x.end(),[](float v){return v!=0;}),"Reverse audio nonzero and exactly callback-partition invariant");
      if(type==MOD_TYPE_MPT && rate==48000 && bits==16 && channels==2 && length==257) {
        const auto count=d.song().GetNumSamples();auto copied=d.prepareSampleCopy(1,0,1024,SampleChannels::Both);
        check(copied.settings().nativeReverseLoops==s.nativeReverseLoops,"Sample copy retains native reverse mode");
        d.applySampleCopy(std::move(copied));d.undo();check(d.song().GetNumSamples()==count,"Copy undo retains inventory");
        d.applySampleEdit(d.prepareSampleErase(1,start,end));check(!(d.song().GetSample(1).nativeReverseLoops&(sustain?2:1)),"Deleting the complete loop clears its native reverse mode");
        d.undo();check(d.song().GetSample(1).nativeReverseLoops==(sustain?2:1),"Structural Undo restores native reverse mode");
        rejects([&]{d.prepareSampleCrossfade(1,{2,SampleCrossfadeMode::Overlap,SampleCrossfadeCurve::Linear,sustain});});
        rejects([&]{d.validateModuleSampleExport();});
        auto parts=splitSongSnapshot(snapshot);auto archive=std::vector<std::byte>(parts.samples.begin(),parts.samples.end());
        check(archive.back()==std::byte(sustain?2:1),"Native extension has explicit reverse flags");
        for(auto invalid:{std::byte{0},std::byte{4},std::byte{255}}){auto bad=archive;bad.back()=invalid;rejects([&]{Document malformed(packSongSnapshot(parts.module,bad));});}
        for(size_t missing:{1,2,3}){auto bad=archive;bad.resize(bad.size()-missing);rejects([&]{Document malformed(packSongSnapshot(parts.module,bad));});}
        auto badIndex=archive;badIndex[badIndex.size()-3]=std::byte{0};badIndex[badIndex.size()-2]=std::byte{0};rejects([&]{Document malformed(packSongSnapshot(parts.module,badIndex));});
        // Note-Off keeps normal reverse traversal running. Sustain release
        // translates its traversal cursor back to the original PCM coordinate.
        auto &voice=actual.song().m_PlayState.Chn[0];const auto oldPosition=voice.position;
        const auto expectedPhysical=std::max(SamplePosition{},SamplePosition(start+end-1,0)-oldPosition);
        actual.song().KeyOff(voice);
        check(sustain ? voice.position==expectedPhysical && !voice.nativeReverseLoop.sample : voice.position==oldPosition && voice.nativeReverseLoop.reversed,
          "Note-Off translates sustain position exactly and preserves normal reverse phase");
      }
      d.undo();check(!d.song().GetSample(1).nativeReverseLoops,"Reverse undo exact");d.redo();
      check(d.song().GetSample(1).nativeReverseLoops==(sustain?2:1),"Reverse redo exact");++cases;
    }
  std::cout<<"PASS "<<cases<<" independent reverse-loop PCM/unrolled-resampler/persistence/history/callback comparisons; "<<phaseCases<<" absolute-time phase/tap checks; archive rejection, export guard, copy/splice/history and release coordinates\n";
}catch(const std::exception &e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;} }
