#include "editor/TrackerDocument.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <numbers>
using namespace Tracker;
static void check(bool ok,const char *why) { if(!ok)throw std::runtime_error(why); }
template<class F> static void rejects(F f) { bool no=false;try{f();}catch(const std::exception &){no=true;}check(no,"Expected crossfade rejection"); }
static std::vector<std::byte> encode(const std::vector<int> &values,int bits) {
  std::vector<std::byte> bytes(values.size()*bits/8);
  for(size_t i=0;i<values.size();++i) {
    if(bits==8){const auto v=int8_t(values[i]);std::memcpy(bytes.data()+i,&v,1);}
    else {const auto v=int16_t(values[i]);std::memcpy(bytes.data()+2*i,&v,2);}
  }return bytes;
}
static std::vector<int> reference(std::vector<int> original,int bits,int channels,uint32_t start,uint32_t end,const SampleCrossfadeOptions &o) {
  auto result=original;const int scale=bits==8?128:32768;
  for(uint32_t i=0;i<o.frames;++i) for(int c=0;c<channels;++c) {
    const auto incoming=original[(start-(o.mode==SampleCrossfadeMode::Preserve?o.frames:0)+i)*channels+c];
    const auto outgoing=original[(end-o.frames+i)*channels+c];
    const long double t=static_cast<long double>(i)/(o.frames-1);
    long double value=o.curve==SampleCrossfadeCurve::Linear
      ? (static_cast<long double>(incoming)*i+static_cast<long double>(outgoing)*(o.frames-1-i))/(o.frames-1)
      : incoming*std::sin(t*std::numbers::pi_v<long double>/2)+outgoing*std::cos(t*std::numbers::pi_v<long double>/2);
    if(i==0)value=outgoing;if(i+1==o.frames)value=incoming;
    result[(end-o.frames+i)*channels+c]=int(std::round(std::clamp(value,static_cast<long double>(-scale),static_cast<long double>(scale-1))));
  }return result;
}
static void install(Document &d,int bits,int channels,const std::vector<std::byte> &bytes,uint32_t start,uint32_t end,bool sustain=false) {
  auto &s=d.song().GetSample(1);s.Initialize(d.song().GetType());s.nLength=uint32_t(bytes.size()/(bits/8*channels));
  s.uFlags.set(CHN_16BIT,bits==16);s.uFlags.set(CHN_STEREO,channels==2);s.nC5Speed=22050;s.FrequencyToTranspose();s.cues[0]=3;s.cues[1]=end;
  if(sustain){s.nSustainStart=start;s.nSustainEnd=end;s.uFlags.set(CHN_SUSTAINLOOP);}
  else{s.nLoopStart=start;s.nLoopEnd=end;s.uFlags.set(CHN_LOOP);}
  check(s.AllocateSample()!=0,"Allocate crossfade sample");std::memcpy(s.samplev(),bytes.data(),bytes.size());s.PrecomputeLoops(d.song(),false);
  d.song().m_nSamples=1;auto native=d.native();native.reconcile(d.song());d.restoreNative(native);
}
static auto pcm(Document &d) {const auto &s=d.song().GetSample(1);return std::vector<std::byte>(s.sampleb(),s.sampleb()+s.GetSampleSizeInBytes());}
int main() {
 try {
  size_t cases=0;
  constexpr uint32_t frames=2049,start=512,end=1793;
  for(int bits:{8,16})for(int channels:{1,2})for(int shape=0;shape<5;++shape) {
    const int scale=bits==8?128:32768;
    std::vector<int> values(frames*channels);
    for(uint32_t f=0;f<frames;++f)for(int c=0;c<channels;++c)
      values[f*channels+c]=shape==0?int((f*701+c*991)%(2*scale))-scale:shape==1?int(f*43%(2*scale))-scale:
        shape==2?scale-1:shape==3?0:((f+c)%2?-scale:scale-1);
    const auto original=encode(values,bits);
    for(auto mode:{SampleCrossfadeMode::Preserve,SampleCrossfadeMode::Overlap})
      for(auto curve:{SampleCrossfadeCurve::Linear,SampleCrossfadeCurve::EqualPower})for(uint32_t n:{2,3,17,256,512}) {
        SampleCrossfadeOptions o{n,mode,curve};auto plan=planSampleCrossfade({original,frames,uint8_t(bits),uint8_t(channels)},start,end,o);
        auto actual=original;for(const auto &chunk:plan.pcm.chunks)std::memcpy(actual.data()+size_t(chunk.first)*channels*bits/8,chunk.after.data(),chunk.after.size());
        const auto expected=reference(values,bits,channels,start,end,o);
        check(actual==encode(expected,bits),"Crossfade PCM matches independent long-double weighted reference");
        check(plan.loopStart==start+(mode==SampleCrossfadeMode::Overlap?n:0),"Crossfade period is explicit");
        check(plan.pcm.result.first==end-n && plan.pcm.result.last==end,"Exact tail edit bounds");
        size_t changedSamples=0,changedFrames=0,preview=0,clipped=0;double peakBefore=0,peakAfter=0;
        for(uint32_t f=end-n;f<end;++f) {
          bool changed=false;const auto i=f-(end-n);const long double t=static_cast<long double>(i)/(n-1);
          for(int c=0;c<channels;++c) {
            const auto before=values[f*channels+c],after=expected[f*channels+c];
            peakBefore=std::max(peakBefore,std::abs(double(before))/scale);peakAfter=std::max(peakAfter,std::abs(double(after))/scale);
            if(curve==SampleCrossfadeCurve::EqualPower && i && i+1<n) {
              const auto incoming=values[(start-(mode==SampleCrossfadeMode::Preserve?n:0)+i)*channels+c];
              const auto sum=before*std::cos(t*std::numbers::pi_v<long double>/2)+incoming*std::sin(t*std::numbers::pi_v<long double>/2);
              clipped+=sum < -scale || sum > scale-1;
            }
            if(before!=after){++changedSamples;changed=true;if(preview<maximumSamplePreview){const auto &p=plan.pcm.result.preview.at(preview++);check(p.frame==f && p.channel==c && p.before==before && p.after==after,"Exact crossfade preview order");}}
          }changedFrames+=changed;
        }
        check(plan.pcm.result.changedSamples==changedSamples && plan.pcm.result.changedFrames==changedFrames && plan.pcm.result.clippedSamples==clipped &&
          plan.pcm.result.peakBefore==peakBefore && plan.pcm.result.peakAfter==peakAfter,"Exact crossfade counts/clipping/peaks");
        ++cases;
      }
  }
  for(auto type:{MOD_TYPE_MOD,MOD_TYPE_XM,MOD_TYPE_S3M,MOD_TYPE_IT,MOD_TYPE_MPT})for(bool sustain:{false,true})
    for(auto mode:{SampleCrossfadeMode::Preserve,SampleCrossfadeMode::Overlap})for(auto curve:{SampleCrossfadeCurve::Linear,SampleCrossfadeCurve::EqualPower}) {
      Document d(type),expected(type);std::vector<int> values(frames*2);
      for(size_t i=0;i<values.size();++i)values[i]=int(i*997%65535)-32767;
      auto original=encode(values,16);install(d,16,2,original,start,end,sustain);
      SampleCrossfadeOptions o{257,mode,curve,sustain};const auto expectedPCM=encode(reference(values,16,2,start,end,o),16);
      const auto newStart=start+(mode==SampleCrossfadeMode::Overlap?o.frames:0);
      install(expected,16,2,expectedPCM,newStart,end,sustain);
      auto prepared=d.prepareSampleCrossfade(1,o);check(prepared.hasChanges() && d.revision==0 && !d.canUndo(),"Crossfade preparation is read-only");
      d.applySampleProcess(std::move(prepared));check(pcm(d)==expectedPCM,"Document crossfade PCM exact");
      check((sustain?d.song().GetSample(1).nSustainStart:d.song().GetSample(1).nLoopStart)==newStart && d.song().GetSample(1).cues[0]==3 && d.song().GetSample(1).cues[1]==end,"Crossfade updates only target loop start");
      d.undo();check(pcm(d)==original && (sustain?d.song().GetSample(1).nSustainStart:d.song().GetSample(1).nLoopStart)==start,"One Undo restores PCM and loop geometry");
      d.redo();check(pcm(d)==expectedPCM,"Redo restores exact crossfade");
      d.transaction([](auto &song){song.m_songName="Crossfade history";});d.undo();d.undo();check(pcm(d)==original,"Crossfade composes with structural history");d.redo();
      Document reopened(d.snapshotData());check(pcm(reopened)==expectedPCM && (sustain?reopened.song().GetSample(1).nSustainStart:reopened.song().GetSample(1).nLoopStart)==newStart,"Native snapshots preserve crossfade audio/loop geometry for every source format");
      for(auto *doc:{&reopened,&expected}){auto &cell=*doc->song().Patterns[0].GetpModCommand(0,0);cell.note=61;cell.instr=1;cell.command=CMD_VOLUME;cell.param=64;}
      Renderer a(reopened.snapshotData(),48000),b(expected.snapshotData(),48000);std::array<float,8192> x{},y{};bool signal=false;
      for(size_t count:{17u,128u,4096u}){check(a.render(x.data(),count)==count && b.render(y.data(),count)==count,"Render crossfade");check(std::equal(x.begin(),x.begin()+2*count,y.begin()),"Crossfaded rendered audio matches independent PCM and loop reference exactly");signal|=std::any_of(x.begin(),x.begin()+2*count,[](float v){return v!=0;});}
      check(signal,"Crossfade audio fixture must be nonzero");
  }
  Document flat;std::vector<int> flatValues(1024*2,12000);install(flat,16,2,encode(flatValues,16),256,768);
  SampleCrossfadeOptions o{64,SampleCrossfadeMode::Overlap};auto silentChange=flat.prepareSampleCrossfade(1,o);
  check(silentChange.hasChanges() && silentChange.result().changedFrames==0,"Identical overlap audio still changes loop geometry");flat.applySampleProcess(std::move(silentChange));
  check(flat.revision==1 && flat.song().GetSample(1).nLoopStart==320,"Geometry-only overlap creates one edit");flat.undo();
  o.mode=SampleCrossfadeMode::Preserve;const auto revision=flat.revision;auto noOp=flat.prepareSampleCrossfade(1,o);check(!noOp.hasChanges(),"Constant linear preserve is exact no-op");flat.applySampleProcess(std::move(noOp));check(flat.revision==revision && flat.canRedo(),"No-op crossfade preserves redo/revision");
  auto stale=flat.prepareSampleCrossfade(1,o);flat.annotate([](auto &n){n.samples.at(1).annotation="new";});rejects([&]{flat.applySampleProcess(std::move(stale));});
  auto foreign=flat.prepareSampleCrossfade(1,o);Document other;rejects([&]{other.applySampleProcess(std::move(foreign));});
  auto tampered=flat.prepareSampleCrossfade(1,o);flat.song().GetSample(1).sample16()[(256-64)*2]++;rejects([&]{flat.applySampleProcess(std::move(tampered));});
  auto changedLoop=flat.prepareSampleCrossfade(1,o);flat.song().GetSample(1).nLoopStart++;rejects([&]{flat.applySampleProcess(std::move(changedLoop));});
  auto changedTarget=flat.prepareSampleCrossfade(1,o);flat.song().GetSample(1).sample16()[(768-64)*2]++;rejects([&]{flat.applySampleProcess(std::move(changedTarget));});
  flat.song().GetSample(1).uFlags.set(CHN_PINGPONGLOOP);rejects([&]{flat.prepareSampleCrossfade(1,o);});
  flat.song().GetSample(1).uFlags.reset(CHN_PINGPONGLOOP);flat.song().GetSample(1).uFlags.reset(CHN_LOOP);rejects([&]{flat.prepareSampleCrossfade(1,o);});
  auto bytes=encode(flatValues,16);SamplePCMView view{bytes,1024,16,2};
  for(uint32_t n:{0,1,513,1048577}){o.frames=n;rejects([&]{planSampleCrossfade(view,256,768,o);});}
  o={64,SampleCrossfadeMode::Preserve};rejects([&]{planSampleCrossfade(view,0,1024,o);});
  o.mode=SampleCrossfadeMode::Overlap;check(planSampleCrossfade(view,0,1024,o).loopStart==64,"Overlap supports a loop beginning at frame zero");
  o.frames=257;rejects([&]{planSampleCrossfade(view,0,512,o);});
  o={64};o.mode=SampleCrossfadeMode(99);rejects([&]{planSampleCrossfade(view,256,768,o);});o={64};o.curve=SampleCrossfadeCurve(99);rejects([&]{planSampleCrossfade(view,256,768,o);});
  Document large;std::vector<int> largeValues(2097152*2,12000);largeValues[(900000-2)*2]=-8000;largeValues[(900000-1)*2]=-8000;
  install(large,16,2,encode(largeValues,16),900000,1900000);
  large.waveform(1,2048);const auto reads=large.waveformReadFrames();
  large.applySampleProcess(large.prepareSampleCrossfade(1,{2,SampleCrossfadeMode::Overlap}));large.waveform(1,2048);
  check(large.historyBytes()<8192 && large.waveformReadFrames()==reads,
    "Geometry-only crossfade in a two-million-frame sample retains compact history without waveform rescanning");
  large.undo();
  auto shortFade=large.prepareSampleCrossfade(1,{2});large.applySampleProcess(std::move(shortFade));large.waveform(1,2048);
  check(large.historyBytes()<16384 && large.waveformReadFrames()==reads+256,
    "Two-frame crossfade retains only its changed tile and updates one cached waveform tile");
  std::cout<<"PASS "<<cases<<" independent crossfade PCM/curve/layout cases; exact previews, normal/sustain loop history, five-format persistence/audio, no-ops and stale/tampered rejection\n";
 }catch(const std::exception &e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
