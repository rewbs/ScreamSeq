#include "editor/TrackerDocument.hpp"
#include "editor/SongTiming.hpp"
#include "editor/SampleArchive.hpp"
#include <iostream>
#include <cmath>
using namespace Tracker;
using namespace OpenMPT;
#ifdef TRACKER_SANITIZER
static void tracker_audit_begin() {}
static void tracker_audit_end(uint64_t *a, uint64_t *f, uint64_t *l) { *a = *f = *l = 0; }
#else
extern "C" void tracker_audit_begin();
extern "C" void tracker_audit_end(uint64_t *, uint64_t *, uint64_t *);
#endif
static void check(bool result, const char *message) { if (!result) throw std::runtime_error(message); }
template<class F> static void rejects(F f) { bool caught=false; try { f(); } catch(const std::exception &) { caught=true; } check(caught,"Invalid timing must reject"); }
struct Tick { uint32_t row, tick, length; uint64_t frame; bool operator==(const Tick &) const = default; };
struct Clock {
  uint64_t position=0;
  std::vector<Tick> ticks;
  static void observe(void *context, const PlayState &s, uint32_t frames) noexcept {
    auto &clock=*static_cast<Clock *>(context);
    if(s.AtStartOfTick()) clock.ticks.push_back({s.m_nRow,s.m_nTickCount,s.m_nSamplesPerTick,clock.position});
    clock.position+=frames;
  }
};
int main() {
  try {
    const std::array weights{1.5,.5,1.25,.75};
    const auto groove=normalizedGroove(weights);
    check(groove[0]==TempoSwing::Unity*3/2 && groove[1]==TempoSwing::Unity/2,"Known exact groove factors");
    rejects([] { normalizedGroove(std::array{.25,4.,4.,4.}); });
    rejects([] { normalizedGroove(std::array{double(NAN),1.}); });
    for(auto type:{MOD_TYPE_MOD,MOD_TYPE_XM,MOD_TYPE_S3M,MOD_TYPE_IT,MOD_TYPE_MPT}) {
      auto doc=Document::demo(type);
      auto old=songTiming(doc->song());
      auto next=old; next.mode=TempoMode::Modern;next.rowsPerBeat=4;next.rowsPerMeasure=12;next.groove=groove;
      next.sequences[0]={1271250,7};
      doc->transaction([&](CSoundFile &s){applySongTiming(s,next);});
      auto snapshot=doc->snapshotData(); Document reopened(snapshot);
      check(songTiming(reopened.song())==next,"Exact five-format snapshot timing");
      doc->undo();check(songTiming(doc->song())==old,"Undo retains preexisting timing");
      doc->redo();check(songTiming(doc->song())==next,"Redo restores fractional timing and groove");
      doc->transaction([](CSoundFile &s){s.m_songName="Keep timing through structural edits";});
      doc->undo();check(songTiming(doc->song())==next,"Structural undo preserves native timing corrections");
      Document module(doc->serialize());
      if(songTiming(module.song())!=next) rejects([&]{doc->validateModuleSampleExport();});
      auto parts=splitSongSnapshot(snapshot);
      check((songTiming(module.song())==next)==parts.timing.empty(),"Only loss-prone formats use optional timing payload");
      if(!parts.timing.empty()) {
        for(size_t cut=0;cut<parts.timing.size();++cut) {
          if(!cut) continue; // An absent payload selects the older snapshot envelope.
          rejects([&]{Document bad(packSongSnapshot(parts.module,parts.samples,parts.timing.first(cut)));});
        }
        for(size_t field:{0u,4u,8u,12u,16u,20u,24u,28u,32u}) {
          auto bad=std::vector<std::byte>(parts.timing.begin(),parts.timing.end());
          for(size_t i=0;i<4;++i)bad.at(field+i)=std::byte{255};
          rejects([&]{Document broken(packSongSnapshot(parts.module,parts.samples,bad));});
        }
        auto extra=std::vector<std::byte>(parts.timing.begin(),parts.timing.end());extra.push_back(std::byte{0});
        rejects([&]{Document broken(packSongSnapshot(parts.module,parts.samples,extra));});
      }
      for(uint32_t rate:{44100u,48000u,96000u}) {
        auto render=[&](uint32_t block) {
          Renderer renderer(snapshot,rate); Clock clock;clock.ticks.reserve(2048);
          renderer.song().nativeMixContext=&clock;renderer.song().nativeMixObserver=Clock::observe;
          std::vector<float> audio(size_t(rate)*2); // One second, less than the pattern duration.
          for(uint32_t pos=0;pos<rate;pos+=block) {
            uint64_t a,f,l;tracker_audit_begin();
            renderer.render(audio.data()+pos*2,std::min(block,rate-pos));
            tracker_audit_end(&a,&f,&l);check(a+f+l==0&&!renderer.faulted(),"Timing render callback has no allocation/free/lock");
          }
          return std::pair{audio,clock.ticks};
        };
        const auto reference=render(1);
        double largestPeak=0;
        for(auto block:{17u,128u,512u,4096u}) {
          const auto other=render(block);
          if(other.second!=reference.second) {std::cerr<<"Tick mismatch type "<<type<<" rate "<<rate<<" block "<<block<<" counts "<<reference.second.size()<<"/"<<other.second.size()<<'\n';
            for(size_t i=0;i<std::min(reference.second.size(),other.second.size());++i)if(reference.second[i]!=other.second[i]) {auto a=reference.second[i],b=other.second[i];std::cerr<<i<<" row "<<a.row<<"/"<<b.row<<" tick "<<a.tick<<"/"<<b.tick<<" length "<<a.length<<"/"<<b.length<<" frame "<<a.frame<<"/"<<b.frame<<'\n';break;}}
          if(other.first!=reference.first) {size_t mismatch=0;double peak=0;for(size_t i=0;i<other.first.size();++i)if(other.first[i]!=reference.first[i]){++mismatch;peak=std::max(peak,std::abs(double(other.first[i])-reference.first[i]));}largestPeak=std::max(largestPeak,peak);check(peak<1e-6,"Sample mix partition rounding remains below one millionth");}
          check(other.second==reference.second,"Exact tick timing independent of callback partition");
        }
        long double frame=0;
        for(const auto &tick:reference.second) {
          // Independent absolute-time integration, independent of engine tick rounding/carry implementation.
          check(std::abs(double(tick.frame)-double(frame))<1.00001,"Groove tick onset agrees with independent continuous-time clock within one sample");
          frame+=static_cast<long double>(rate)*60/127.125L/4/7*weights[tick.row%4];
        }
      }
      doc->transaction([&](CSoundFile &s){applySongTiming(s,old);});
      check(songTiming(Document(doc->snapshotData()).song())==old,"Clearing timing restores source-compatible defaults");
    }
    // Different beat sizes, speed changes and imported pattern overrides use the
    // same engine clock. Capture exact tick boundaries without plugin code.
    for(uint32_t beat:{3u,7u}) for(uint32_t rate:{44100u,48000u,96000u}) {
      Document doc;
      doc.transaction([&](CSoundFile &song) {
        auto timing=songTiming(song);timing.mode=TempoMode::Modern;timing.rowsPerBeat=beat;timing.rowsPerMeasure=beat*3;
        timing.sequences[0]={1373750,5};std::vector<double> weights(beat,1);weights[0]=1.5;weights[1]=.5;
        timing.groove=normalizedGroove(weights);applySongTiming(song,timing);
        auto &tempo=*song.Patterns[0].GetpModCommand(2,0);tempo.command=CMD_TEMPO;tempo.param=173;
        auto &speed=*song.Patterns[0].GetpModCommand(4,0);speed.command=CMD_SPEED;speed.param=3;
      });
      Renderer renderer(doc.snapshotData(),rate);Clock clock;clock.ticks.reserve(2048);
      renderer.song().nativeMixContext=&clock;renderer.song().nativeMixObserver=Clock::observe;
      std::vector<float> audio(rate*2);renderer.render(audio.data(),rate);
      long double time=0;
      for(const auto &tick:clock.ticks) {
        check(std::abs(double(tick.frame)-double(time))<1.00001,"Independent clock follows odd beat lengths and source tempo/speed commands");
        const double weight=tick.row%beat==0?1.5:tick.row%beat==1?.5:1;
        time+=static_cast<long double>(rate)*60/(tick.row<2?137.375L:173.0L)/beat/(tick.row<4?5:3)*weight;
      }
    }
    {
      Document doc;
      doc.transaction([](CSoundFile &song) {
        check(song.Order.AddSequence()==1,"Add timing sequence");song.Order.SetSequence(1);song.Order().assign(1,0);
        auto timing=songTiming(song);timing.mode=TempoMode::Modern;timing.rowsPerBeat=4;timing.rowsPerMeasure=16;
        timing.sequences={{1234567,7},{2345678,3}};timing.groove=normalizedGroove(std::array{1.5,.5,1.5,.5});applySongTiming(song,timing);
        check(song.Patterns[0].SetSignature(3,9),"Pattern timing override");TempoSwing local;local.assign(3,TempoSwing::Unity);song.Patterns[0].SetTempoSwing(local);
      });
      const auto snapshot=doc.snapshotData();Document reopened(snapshot);
      check(songTiming(doc.song())==songTiming(reopened.song()) && reopened.song().Order.GetCurrentSequenceIndex()==1,"Every sequence retains exact tempo/speed and selected identity");
      Renderer renderer(snapshot,48000,0,false,{},UINT32_MAX);Clock clock;clock.ticks.reserve(2048);
      renderer.song().nativeMixContext=&clock;renderer.song().nativeMixObserver=Clock::observe;
      std::vector<float> audio(48000);renderer.render(audio.data(),24000);
      long double frame=0;
      for(const auto &tick:clock.ticks) {check(std::abs(double(tick.frame)-double(frame))<1.00001,"Pattern override wins over global beat and groove");frame+=48000.L*60/234.5678L/3/3;}
    }
    std::cout<<"PASS musical timing: five-format fractional tempo/groove snapshots, optional payload validation, loss-safe exports, structural Undo/Redo, independent sample timing and callback-size audio (peak delta below 1e-6) at three sample rates; no callback allocations/frees/locks\n";
    return 0;
  } catch(const std::exception &e) { std::cerr<<e.what()<<'\n';return 1; }
}
