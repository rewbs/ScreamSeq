#include "editor/TrackerDocument.hpp"
#include "common/FileReader.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
// Expose the protected entry point without changing the production API.
struct DecoderSong : OpenMPT::CSoundFile {
  using CSoundFile::ReadVorbisSample;
  using CSoundFile::ReadMO3;
};
void require(bool ok, const char *why) {if(!ok)throw std::runtime_error(why);}
}
// Minimal independent MO3 v5 writer: literal-only LZ music block, no patterns or
// instruments, one Vorbis sample (or two with a shared setup header).
static std::vector<char> mo3Fixture(const std::vector<char> &ogg,size_t count,bool shared) {
  auto set=[](std::vector<char> &v,size_t at,uint32_t n,size_t width){
    for(size_t i=0;i<width;++i)v.at(at+i)=char(n>>(i*8));};
  std::vector<char> header(422,0);header[0]=1;
  set(header,11,shared?2:1,2);header[13]=6;header[14]=125;set(header,15,0x100,4);
  header[19]=char(128);header[22]=64;
  std::vector<char> music(2,0);music.insert(music.end(),header.begin(),header.end());
  for(int index=0;index<(shared?2:1);++index) {
    music.push_back(0);music.push_back(0);std::vector<char> sample(41,0);
    set(sample,0,24000,4);sample[5]=64;set(sample,6,0xffff,2);set(sample,8,4800,4);
    set(sample,20,shared&&index==0?0x7001:0x3001,2);sample[26]=64;
    set(sample,35,uint32_t(index==0?count:ogg.size()),4);
    if(shared&&index==0)set(sample,39,3404,2);
    music.insert(music.end(),sample.begin(),sample.end());
    if(shared&&index==0){music.push_back(1);music.push_back(0);}
  }
  std::vector<char> packed{music[0]};
  for(size_t i=1;i<music.size();i+=8){packed.push_back(0);packed.insert(packed.end(),music.begin()+i,music.begin()+std::min(i+8,music.size()));}
  std::vector<char> result{'M','O','3',5,0,0,0,0,0,0,0,0};
  set(result,4,uint32_t(music.size()),4);set(result,8,uint32_t(packed.size()),4);
  result.insert(result.end(),packed.begin(),packed.end());
  result.insert(result.end(),ogg.begin()+(shared?3404:0),ogg.begin()+(shared?3404:0)+count);
  if(shared)result.insert(result.end(),ogg.begin(),ogg.end());
  return result;
}
int runMO3VorbisProbeTests() {
  std::ifstream file(std::filesystem::path(ASSET_FIXTURE_DIR)/"mono-24000.ogg",std::ios::binary);
  std::vector<char> ogg((std::istreambuf_iterator<char>(file)),{});require(ogg.size()>3405,"Missing Vorbis fixture");
  int cases=0;
  for(bool shared:{false,true})for(int kind=0;kind<4;++kind) {
    const size_t count=kind==0?ogg.size()-(shared?3404:0):(kind==1?0:(kind==2?1:(shared?28:3432)));
    const auto data=mo3Fixture(ogg,count,shared);
    OpenMPT::FileReader reader(::mpt::byte_cast<::mpt::const_byte_span>(::mpt::as_span(data)));
    auto song=std::make_unique<DecoderSong>();
    std::cout<<"PROBE MO3 shared="<<shared<<" kind="<<kind<<std::endl;
    require(song->ReadMO3(reader,OpenMPT::CSoundFile::loadCompleteModule),"Generated MO3 container rejected");
    if(kind==0){const auto &s=song->GetSample(1);require(s.HasSampleData()&&s.nLength==4800&&s.nC5Speed==24000,"Valid MO3 Vorbis dimensions");
      require(std::any_of(s.sample16(),s.sample16()+s.nLength,[](auto x){return x!=0;}),"Valid MO3 Vorbis decoded silence");}
    // MO3 retains upstream best-effort handling of damaged samples. It must
    // terminate normally; CTest enforces a timeout in a dedicated child.
    ++cases;
  }
  std::cout<<"PASS MO3 Vorbis direct/shared-header paths: "<<cases<<" cases\n";return 0;
}
int runVorbisProbeTests() {
  auto song=std::make_unique<DecoderSong>();song->Create(OpenMPT::MOD_TYPE_MPT,4);
  OpenMPT::FileReader empty;
  std::cout<<"PROBE direct empty Vorbis reader"<<std::endl;
  require(!song->ReadVorbisSample(1,empty),"Empty Vorbis reader accepted");
  require(!song->ReadSampleFromFile(1,empty,false),"Empty fallback sample reader accepted");
  require(!song->GetSample(1).HasSampleData(),"Rejected empty probe changed destination");

  std::ifstream file(std::filesystem::path(ASSET_FIXTURE_DIR)/"mono-24000.ogg",std::ios::binary);
  std::vector<char> bytes((std::istreambuf_iterator<char>(file)),{});
  require(bytes.size()>3405,"Missing generated Vorbis fixture");
  auto reader=[&](size_t length) {return OpenMPT::FileReader(::mpt::byte_cast<::mpt::const_byte_span>(::mpt::span(bytes.data(),length)));};
  auto full=reader(bytes.size());
  require(song->ReadVorbisSample(1,full),"Real Vorbis fixture rejected");
  const auto &sample=song->GetSample(1);
  require(sample.nC5Speed==24000&&sample.GetNumChannels()==1&&sample.GetElementarySampleSize()==2&&sample.nLength>0,"Vorbis dimensions/rate");
  const std::vector<int16_t> pcm(sample.sample16(),sample.sample16()+sample.nLength);
  require(std::any_of(pcm.begin(),pcm.end(),[](auto x){return x!=0;}),"Vorbis decoded silence");
  const auto length=sample.nLength,rate=sample.nC5Speed;const auto flags=sample.uFlags;
  auto unchanged=[&]{require(sample.nLength==length&&sample.nC5Speed==rate&&sample.uFlags==flags
    &&std::equal(pcm.begin(),pcm.end(),sample.sample16()),"Rejected Vorbis changed existing PCM/settings");};
  size_t cases=0;
  // Ogg capture/header boundaries, complete identification/setup pages, and a
  // partial audio page. The latter used to spin forever on need_more_data.
  for(size_t n:std::vector<size_t>{0,1,2,3,4,5,26,27,28,57,58,59,85,3403,3404,3405,3432,bytes.size()-1}) {
    std::cout<<"PROBE truncated Vorbis bytes="<<n<<std::endl;
    auto truncated=reader(n);
    require(!song->ReadVorbisSample(1,truncated),"Truncated Vorbis accepted");unchanged();++cases;
  }
  std::fill(bytes.begin(),bytes.end(),'x');auto garbage=reader(bytes.size());
  require(!song->ReadVorbisSample(1,garbage),"Non-Ogg prefix accepted");unchanged();
  std::cout<<"PASS Vorbis empty/fallback/garbage and "<<cases<<" truncated probes; valid PCM frames="<<length<<'\n';
  return 0;
}
