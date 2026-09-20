#import "../Bridge/TrackerSession.h"
#include "editor/TrackerDocument.hpp"
#include "soundlib/ModInstrument.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <algorithm>
using namespace Tracker;
static void check(bool ok,const char *text){if(!ok)throw std::runtime_error(text);}
static void wave(const std::string &path,int rate,int channels) {
  std::ofstream out(path,std::ios::binary);auto u16=[&](uint16_t n){out.put(char(n));out.put(char(n>>8));};auto u32=[&](uint32_t n){u16(n);u16(n>>16);};
  const uint32_t size=1024*channels*2;out.write("RIFF",4);u32(36+size);out.write("WAVEfmt ",8);u32(16);u16(1);u16(channels);u32(rate);u32(rate*channels*2);u16(channels*2);u16(16);out.write("data",4);u32(size);
  for(int frame=0;frame<1024;++frame)for(int c=0;c<channels;++c)u16(uint16_t(int16_t(std::sin(frame*.1+c)*16000)));
}
static void multisampleTests(const std::string &first,const std::string &second,const std::string &broken) {
  for(auto type:{MOD_TYPE_MPT,MOD_TYPE_IT,MOD_TYPE_XM}) {
    auto doc=Document::demo(type);const auto before=doc->snapshotData();const auto rev=doc->revision;
    const std::vector<Document::MultisampleSource> sources={{second,61},{first,49}};
    const auto planned=doc->importMultisample(sources,"Keys",true);
    check(doc->snapshotData()==before&&doc->revision==rev,"Multi-sample dry run preserves exact song and revision");
    for(auto invalid:std::vector<std::vector<Document::MultisampleSource>>{{{first,49},{second,49}},{{first,49},{broken,61}},{{first,0},{second,61}},{{first,49},{first,61}}}) {
      bool failed=false;try{doc->importMultisample(invalid,"Bad");}catch(const std::exception &){failed=true;}
      check(failed&&doc->snapshotData()==before&&doc->revision==rev,"Invalid multi-sample roots/files fail atomically");
    }
    const auto imported=doc->importMultisample(sources,"Keys");const auto after=doc->snapshotData();
    check(doc->revision==rev+1&&imported.instrument==planned.instrument&&imported.zones.size()==2,"Multi-sample import creates one instrument in one revision");
    auto &ins=*doc->song().Instruments[imported.instrument];
    check(ins.Keyboard[48]==imported.zones[0].sample&&ins.NoteMap[48]==61&&ins.Keyboard[60]==imported.zones[1].sample&&ins.NoteMap[60]==61,"Every recorded root plays its own sample at recorded pitch");
    check(ins.Keyboard[54]==imported.zones[0].sample&&ins.NoteMap[54]==67&&ins.Keyboard[55]==imported.zones[1].sample&&ins.NoteMap[55]==56,"Gaps choose nearest root with lower-note ties and correct transposition");
    check(ins.Keyboard[47]==0&&ins.Keyboard[61]==0,"Keys outside the reviewed range remain unmapped");
    doc->undo();check(doc->snapshotData()==before,"One Undo removes the instrument and all appended samples");
    doc->redo();check(doc->snapshotData()==after,"Multi-sample Redo restores PCM and both mappings exactly");
    for(const auto [key,expected]:std::vector<std::pair<int,double>>{{49,24000*.1/(2*3.141592653589793)},{55,24000*.1/(2*3.141592653589793)*std::sqrt(2.)},{61,48000*.1/(2*3.141592653589793)},{48,0},{62,0}}) {
      Document audible(after);
      audible.transaction([&](CSoundFile &s){for(auto &p:s.Patterns)if(p.IsValid())for(auto &cell:p)cell.Clear();s.Patterns[0].Resize(4);s.Order().assign(1,0);
        auto &cell=*s.Patterns[0].GetpModCommand(0,0);cell.note=uint8_t(key);cell.instr=uint8_t(imported.instrument);});
      Renderer renderer(audible.snapshotData(),48000);std::vector<float> audio(4096*2);renderer.render(audio.data(),4096);
      check(std::all_of(audio.begin(),audio.end(),[](float v){return std::isfinite(v);}),"Multi-sample audio remains finite");
      if(expected==0){check(std::all_of(audio.begin(),audio.end(),[](float v){return std::abs(v)<1e-7;}),"Unmapped notes render silence");continue;}
      std::vector<double> crossings;
      for(size_t frame=200;frame<1100;++frame){const double a=audio[(frame-1)*2],b=audio[frame*2];if(a<=0&&b>0)crossings.push_back(frame-1-a/(b-a));}
      check(crossings.size()>4,"Mapped sample produces measurable audio");
      const double frequency=48000*(crossings.size()-1)/(crossings.back()-crossings.front());
      check(std::abs(frequency-expected)<2.,"Rendered pitch matches source root and nearest-note transposition");
    }
  }
}
int main(){@autoreleasepool{try {
  const std::filesystem::path root=std::filesystem::temp_directory_path()/NSUUID.UUID.UUIDString.UTF8String;
  std::filesystem::create_directory(root);
  struct Cleanup{std::filesystem::path root;~Cleanup(){std::error_code error;std::filesystem::remove_all(root,error);}} cleanup{root};
  const auto first=(root/"Kick one.wav").string(),second=(root/"Stereo snare.wav").string(),broken=(root/"Broken.wav").string();
  wave(first,24000,1);wave(second,48000,2);std::ofstream(broken)<<"not audio";
  multisampleTests(first,second,broken);
  auto doc=Document::demo();const auto before=doc->snapshotData();const auto revision=doc->revision;const int samples=doc->song().GetNumSamples();
  const auto preview=doc->importSamples({first,second},true,true);
  check(doc->revision==revision&&doc->snapshotData()==before&&preview.size()==2,"Dry run decodes all files without changing the song");
  for(const auto &paths:std::vector<std::vector<std::string>>{{first,broken},{first,first},{first,(root/"missing.wav").string()}}) {
    bool rejected=false;try{doc->importSamples(paths,true);}catch(const std::exception &){rejected=true;}
    check(rejected&&doc->revision==revision&&doc->snapshotData()==before,"Invalid batches cannot partially import or consume history");
  }
  const auto imported=doc->importSamples({first,second},true);
  check(imported[0].sample==samples+1&&imported[1].sample==samples+2&&doc->revision==revision+1,"Bulk append is exactly one revision");
  check(doc->song().GetSample(imported[1].sample).GetNumChannels()==2&&doc->song().GetSample(imported[0].sample).nC5Speed==24000,"Import preserves channels and source sample rate");
  for(int i=1;i<=samples;++i)check(doc->song().Instruments[i]&&doc->song().Instruments[i]->Keyboard[60]==i,"Entering instrument mode preserves existing sample assignments");
  check(doc->song().Instruments[imported[1].instrument]->Keyboard[60]==imported[1].sample,"Each loaded instrument maps to its own sample");
  const auto after=doc->snapshotData();doc->undo();check(doc->snapshotData()==before,"One Undo restores every sample and instrument");
  doc->redo();check(doc->snapshotData()==after,"Redo restores the complete batch exactly");
  TrackerSession *session=[TrackerSession new];NSError *error=nil;NSString *start=session.automationRevision;
  NSDictionary *params=@{@"expectedRevision":start,@"paths":@[@(first.c_str()),@(second.c_str())],@"createInstruments":@YES};
  auto result=[session automationMethod:@"sample.importMany" params:params error:&error];
  check(result&&[result[@"data"][@"samples"] count]==2,"Bulk import is agent-driven through the same API");
  check(![session automationMethod:@"sample.importMany" params:params error:&error]&&error.code==-32001,"Bulk API rejects stale song revisions");
  auto decoded=[TrackerSession inspectSampleFile:@(second.c_str()) error:&error];
  check(decoded&&[decoded[@"channels"] intValue]==2&&[decoded[@"rate"] intValue]==48000&&[decoded[@"frames"] intValue]==1024,"Browser inspection uses the native sample decoder");
  NSData *pcm=decoded[@"pcm"];float sample=0;std::memcpy(&sample,static_cast<const char *>(pcm.bytes)+100*2*4,4);
  check(std::abs(sample-float(int16_t(std::sin(10.)*16000))/32768.f)<1e-7,"Preview float audio matches imported source PCM");
  check([decoded[@"peaks"] count]==512&&pcm.length==1024*2*4,"Browser returns bounded waveform and PCM");
  check(![TrackerSession inspectSampleFile:@(broken.c_str()) error:&error],"Malformed samples fail inspection safely");
  NSString *project=@((root/"batch.resonance").string().c_str());
  check([session savePath:project error:&error]&&[session openPath:project error:&error],"Bulk samples and instruments survive native project recall");
  NSDictionary *multiParams=@{@"expectedRevision":session.automationRevision,@"name":@"Mapped keys",@"samples":@[@{@"path":@(first.c_str()),@"rootNote":@49},@{@"path":@(second.c_str()),@"rootNote":@61}]};
  auto multi=[session automationMethod:@"instrument.importMultisample" params:multiParams error:&error];check(multi!=nil,"Multi-sample API import succeeds");
  const auto index=[multi[@"data"][@"instrument"] intValue];const auto map=[session instrumentInfo:index];
  check([map[@"noteMapping"] count]==128&&[map[@"noteMapping"][48] intValue]==61,"Agent can inspect transposition as well as sample mapping");
  check([session savePath:project error:&error]&&[session openPath:project error:&error]&&[[session instrumentInfo:index] isEqual:map],"Native project recall preserves the multi-sample instrument exactly");
  std::cout<<"PASS multisample: roots, nearest-note zones, duplicate/out-of-range rejection, atomic Undo/Redo, native recall and measured offline pitch in MPT/IT/XM\n";
  std::cout<<"PASS bulk sample import: preflight/rollback, one Undo, exact PCM/rate/channels, instrument mapping, API revision and preview decoder\n";return 0;
}catch(const std::exception &e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}}
