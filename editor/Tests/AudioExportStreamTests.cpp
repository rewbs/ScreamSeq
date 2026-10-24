#include "editor/hosted/AudioExportStream.hpp"
#include "editor/hosted/HostedAudio.hpp"
#include "editor/TrackerDocument.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <sstream>
#include <stdexcept>

using namespace Tracker;
static void check(bool value,const char *message) {if(!value)throw std::runtime_error(message);}
template<class F> static void rejects(F &&operation,const char *message) {
  bool rejected=false;try{operation();}catch(const std::exception &){rejected=true;}check(rejected,message);
}
static std::vector<float> render(const std::vector<std::byte> &module,const std::vector<PluginState> &plugins={},
                                 const std::vector<ParameterChange> &points={},const NativeSong *native=nullptr,
                                 uint32_t block=512) {
  std::ostringstream output(std::ios::binary|std::ios::out);
  const auto frames=writeProjectAudio(module,plugins,points,output,UINT32_MAX,native,block);
  const auto bytes=output.str();check(bytes.size()==44+frames*8,"WAV frame count and byte size differ");
  auto u32=[&](size_t at){uint32_t v=0;for(unsigned n=0;n<4;++n)v|=uint32_t(uint8_t(bytes.at(at+n)))<<(8*n);return v;};
  check(bytes.substr(0,4)=="RIFF"&&bytes.substr(8,8)=="WAVEfmt "&&bytes.substr(36,4)=="data","WAV chunk identity");
  check(u32(4)==bytes.size()-8&&u32(16)==16&&u32(20)==0x20003&&u32(24)==48000&&
        u32(28)==384000&&u32(32)==0x200008&&u32(40)==frames*8,"WAV float stereo header and exact sizes");
  std::vector<float> pcm(size_t(frames)*2);std::memcpy(pcm.data(),bytes.data()+44,pcm.size()*sizeof(float));
  check(std::all_of(pcm.begin(),pcm.end(),[](float value){return std::isfinite(value);}),"Non-finite exported PCM");
  return pcm;
}
// Seekable failure injection reaches the data write, not an invalid-stream guard.
class FailedDataBuffer final:public std::stringbuf {
  std::streamsize xsputn(const char *data,std::streamsize count) override {
    if(count>44)return 0;return std::stringbuf::xsputn(data,count);
  }
};
int main() {
  try {
    auto document=Document::demo();const auto module=document->snapshotData();
    const auto native=document->native();const auto revision=document->revision;
    const auto undo=document->historyHead(false),redo=document->historyHead(true);
    const auto dry=render(module);check(!dry.empty(),"Empty demo export");
    check(std::any_of(dry.begin(),dry.end(),[](float value){return std::abs(value)>1e-5f;}),"Fixture must contain audible samples");
    auto reference=std::make_unique<Renderer>(module,48000);
    std::array<float,1024> buffer{};size_t offset=0;
    while(const auto frames=reference->render(buffer.data(),512)) {
      check(offset+frames*2<=dry.size(),"Export truncated core PCM");
      check(std::equal(buffer.begin(),buffer.begin()+frames*2,dry.begin()+offset),"Dry WAV differs from independent core PCM");
      offset+=frames*2;
    }
    check(offset==dry.size(),"Dry WAV contains extra padding");
    bool dryPartitionExact=true;
    for(const uint32_t block:{17u,4096u}) {
      const auto partitioned=render(module,{}, {},nullptr,block);
      auto core=std::make_unique<Renderer>(module,48000);
      std::vector<float> coreBuffer(size_t(block)*2);size_t at=0;
      while(const auto frames=core->render(coreBuffer.data(),block)) {
        check(at+frames*2<=partitioned.size(),"Partitioned WAV truncated independent core PCM");
        check(std::equal(coreBuffer.begin(),coreBuffer.begin()+frames*2,partitioned.begin()+at),
              "Partitioned WAV differs from independent core PCM at the same block size");
        at+=frames*2;
      }
      check(at==partitioned.size(),"Partitioned WAV added padding absent from independent core PCM");
      size_t changed=0,first=SIZE_MAX;double maximum=0;
      for(size_t n=0;n<std::min(dry.size(),partitioned.size());++n)if(dry[n]!=partitioned[n]) {
        if(first==SIZE_MAX)first=n;++changed;
        maximum=std::max(maximum,std::abs(double(dry[n])-partitioned[n]));
      }
      std::cerr<<"DRY PARTITION block="<<block<<" frames="<<partitioned.size()/2
        <<" baselineFrames="<<dry.size()/2<<" changedSamples="<<changed
        <<" firstSample="<<first<<" maximumDelta="<<maximum<<" exactIndependentCore=true\n";
      dryPartitionExact &= partitioned==dry;
    }
    auto muted=native;for(const auto &[index,track]:muted.tracks)muted.columnMutes[track.id]=true;
    const auto quiet=render(module,{}, {},&muted);
    check(quiet.size()==dry.size()&&std::all_of(quiet.begin(),quiet.end(),[](float value){return value==0.f;}),
          "Native column mutes were omitted from export");
    auto precise=native;
    precise.preciseNotes.push_back({precise.patterns.at(0).id,precise.tracks.at(0).id,32768,1,73,91});
    precise.validate(document->song());
    const auto notes=render(module,{}, {},&precise);
    check(notes!=dry,"Precise sub-row note was omitted from export");
    auto preciseReference=std::make_unique<Renderer>(module,48000,0,false,std::string{},UINT32_MAX,PlaybackRegion{},&precise);
    offset=0;
    while(const auto frames=preciseReference->render(buffer.data(),512)) {
      check(offset+frames*2<=notes.size(),"Precise export truncated core PCM");
      check(std::equal(buffer.begin(),buffer.begin()+frames*2,notes.begin()+offset),"Precise export differs from independent core PCM");
      offset+=frames*2;
    }
    check(offset==notes.size(),"Precise WAV contains extra padding");
    PluginState gain;gain.descriptor.format="Built-in";gain.descriptor.classID="resonance.gainer.v1";gain.instanceID="export-gain";
    auto probe=std::make_unique<NativePlugin>(gain,48000,true);
    check(probe->parameter(1,-6),"Configure gain fixture");gain=probe->state();
    const std::vector<PluginState> plugins{gain};
    const std::vector<ParameterChange> points{{0,1,-12,0},{0,1,-3,24017}};
    const auto pluginBefore=plugins;const auto pointBefore=points;
    const auto hosted=render(module,plugins,points,&native);
    const auto partitioned=render(module,plugins,points,&native,17);
    check(hosted.size()==dry.size()&&partitioned.size()==hosted.size(),"Zero-tail gain changed duration");
    check(hosted!=dry&&hosted!=render(module,plugins,{},&native),"Export ignored hosted DSP or recorded automation");
    for(size_t n=0;n<hosted.size();++n)check(std::abs(hosted[n]-partitioned[n])<2e-6f,"Recorded export depends on callback partition");
    // A recorded decay increase must retain its audible tail. Compare the
    // complete WAV with an independently scheduled native effect, including
    // all samples after the shorter startup tail would have cut the export.
    PluginState comb;comb.descriptor.format="Built-in";comb.descriptor.classID="resonance.comb-filter.v1";comb.instanceID="export-decay";
    auto combSetup=std::make_unique<NativePlugin>(comb,48000,true);
    check(combSetup->parameter(1,36)&&combSetup->parameter(3,0)&&combSetup->parameter(4,100)&&combSetup->parameter(5,5),
          "Configure short comb decay");comb=combSetup->state();
    auto independent=std::make_unique<NativePlugin>(comb,48000,true);
    const uint64_t shortTail=uint64_t(std::ceil(independent->tail()*48000));
    constexpr uint64_t decayChange=1024;
    const auto decayed=render(module,{comb},{{0,3,90,decayChange}});
    std::vector<float> expected;
    uint64_t expectedEnd=UINT64_MAX,longTail=0;
    for(uint64_t start=0;start<expectedEnd;) {
      if(start==decayChange) {
        check(independent->parameter(3,90),"Configure recorded comb feedback");
        longTail=uint64_t(std::ceil(independent->tail()*48000));
        check(longTail>shortTail+4800,"Recorded fixture must grow decay by more than 100 ms");
        expectedEnd=dry.size()/2+longTail;
      }
      check(start<48000*120,"Independent decay fixture exceeded its two-minute bound");
      const auto count=uint32_t(std::min<uint64_t>(512,expectedEnd-start));
      buffer.fill(0);
      for(size_t n=0;n<count*2&&start*2+n<dry.size();++n)buffer[n]=dry[size_t(start)*2+n];
      check(independent->process(buffer.data(),count,start),"Independent comb render failed");
      expected.insert(expected.end(),buffer.begin(),buffer.begin()+count*2);start+=count;
    }
    check(decayed.size()==expected.size(),"Recorded feedback growth truncated the exported plugin tail");
    check(std::any_of(expected.begin()+dry.size()+shortTail*2,expected.end(),[](float v){return std::abs(v)>1e-6f;}),
          "Decay regression fixture has no audible samples beyond its startup tail");
    for(size_t n=0;n<expected.size();++n)check(std::abs(decayed[n]-expected[n])<2e-6f,"Recorded decay export differs from independent complete PCM");
    std::ostringstream invalid;rejects([&]{writeProjectAudio(module,{}, {},invalid,UINT32_MAX,nullptr,0);},"Zero block accepted");
    rejects([&]{writeProjectAudio(module,{}, {},invalid,UINT32_MAX,nullptr,4097);},"Oversize block accepted");
    check(invalid.str().empty(),"Invalid block wrote output");
    invalid<<"retained";rejects([&]{writeProjectAudio(module,{}, {},invalid);},"Nonzero stream position accepted");
    check(invalid.str()=="retained","Invalid stream changed caller bytes");
    FailedDataBuffer failed;std::ostream broken(&failed);
    rejects([&]{writeProjectAudio(module,{}, {},broken);},"Failed data write reported success");
    check(failed.str().size()==44,"Failure injection did not reach the first data write");
    check(document->snapshotData()==module&&document->native()==native&&document->revision==revision&&
          document->historyHead(false)==undo&&document->historyHead(true)==redo&&plugins==pluginBefore&&points==pointBefore,
          "Export mutated source, plugin state, recording or history");
    // Keep the failed exact assertion mandatory. Diagnose all later export
    // behavior in the same bounded run before reporting this known failure.
    check(dryPartitionExact,"Dry export depends on callback partition");
    std::cout<<"PASS shared WAV header, exact dry PCM, partitioned recorded DSP, failure and source preservation\n";
    return 0;
  }catch(const std::exception &error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
