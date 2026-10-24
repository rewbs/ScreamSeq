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
    check(render(module,{}, {},nullptr,17)==dry&&render(module,{}, {},nullptr,4096)==dry,"Dry export depends on callback partition");
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
    std::cout<<"PASS shared WAV header, exact dry PCM, partitioned recorded DSP, failure and source preservation\n";
    return 0;
  }catch(const std::exception &error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
