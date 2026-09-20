#include "TrackerDocument.hpp"
#include "common/FileReader.h"
#include "soundlib/ModInstrument.h"
#include "soundlib/mod_specifications.h"
#include <filesystem>
#include <fstream>
#include <set>

namespace Tracker {
namespace {
std::unique_ptr<Document> decodeSamples(const std::vector<std::string> &paths) {
  auto decoded=std::make_unique<Document>();std::set<std::filesystem::path> unique;
  uint64_t encoded=0,pcm=0;
  for(size_t i=0;i<paths.size();++i) {
    const auto path=std::filesystem::weakly_canonical(paths[i]);
    if(!unique.insert(path).second) throw std::invalid_argument("The same file appears more than once in the import");
    try {
      if(!std::filesystem::is_regular_file(path)) throw std::runtime_error("Not a regular file");
      std::ifstream file(path,std::ios::binary|std::ios::ate);
      const auto size=file ? std::streamoff(file.tellg()) : -1;
      if(size<=0 || uint64_t(size)>256*1024*1024-encoded) throw std::runtime_error("Import at most 256 MB of files at once");
      encoded+=uint64_t(size);std::vector<std::byte> bytes(static_cast<size_t>(size));file.seekg(0);
      if(!file.read(reinterpret_cast<char *>(bytes.data()),size)) throw std::runtime_error("Cannot read file");
      FileReader reader(::mpt::as_span(bytes));auto &s=decoded->song();const auto index=SAMPLEINDEX(i+1);
      if(!s.ReadSampleFromFile(index,reader,false)) throw std::runtime_error("Unsupported or damaged sample");
      const auto &sample=s.GetSample(index);pcm+=uint64_t(sample.nLength)*sample.GetBytesPerSample();
      if(!sample.nLength || !sample.HasSampleData()) throw std::runtime_error("The file contains no sample audio");
      if(pcm>256*1024*1024) throw std::runtime_error("Decoded audio exceeds the 256 MB batch limit");
      s.m_szNames[index]=::OpenMPT::mpt::ToCharset(s.GetCharsetInternal(),::OpenMPT::mpt::Charset::UTF8,path.stem().string());
    } catch(const std::exception &e) {throw std::runtime_error(path.filename().string()+": "+e.what());}
  }
  return decoded;
}
}

std::vector<Document::ImportedSample> Document::importSamples(const std::vector<std::string> &paths, bool instruments, bool dryRun) {
  if(!editable()) throw std::invalid_argument("This document is read-only");
  if(paths.empty() || paths.size()>128) throw std::invalid_argument("Select between 1 and 128 samples per import");
  const auto oldSamples=song_->GetNumSamples(),oldInstruments=song_->GetNumInstruments();
  const auto &spec=song_->GetModSpecifications();
  if(paths.size()>size_t(std::max(0,std::min<int>(spec.samplesMax,MAX_SAMPLES-1)-oldSamples)))
    throw std::invalid_argument("Not enough free sample slots in this song format");
  // Starting instrument mode must preserve ordinary notes that previously
  // referred directly to sample slots.
  const int firstInstrument=oldInstruments ? oldInstruments : oldSamples;
  if(instruments && (!spec.instrumentsMax || firstInstrument+paths.size()>size_t(std::min<int>(spec.instrumentsMax,MAX_INSTRUMENTS-1))))
    throw std::invalid_argument("Not enough instrument slots; load samples without creating instruments");
  auto decoded=decodeSamples(paths);
  std::vector<ImportedSample> result;
  for(size_t i=0;i<paths.size();++i)result.push_back({paths[i],int(oldSamples+i+1),instruments ? int(firstInstrument+i+1) : 0});
  if(dryRun)return result;
  transaction([&](OpenMPT::CSoundFile &song) {
    if(instruments && !oldInstruments)for(SAMPLEINDEX i=1;i<=oldSamples;++i) {
      song.Instruments[i]=new OpenMPT::ModInstrument(i);song.Instruments[i]->name=song.GetSampleName(i);song.m_nInstruments=i;
    }
    for(size_t i=0;i<result.size();++i) {
      const auto slot=SAMPLEINDEX(result[i].sample);
      if(!song.ReadSampleFromSong(slot,decoded->song(),SAMPLEINDEX(i+1)))throw std::runtime_error("Cannot copy decoded sample");
      song.m_nSamples=std::max(song.m_nSamples,slot);
      if(instruments) {
        const auto instrument=INSTRUMENTINDEX(result[i].instrument);
        song.Instruments[instrument]=new OpenMPT::ModInstrument(slot);
        song.Instruments[instrument]->name=song.GetSampleName(slot);song.m_nInstruments=instrument;
      }
    }
  });
  return result;
}
Document::ImportedMultisample Document::importMultisample(std::vector<MultisampleSource> sources, const std::string &name, bool dryRun) {
  if(!editable())throw std::invalid_argument("This document is read-only");
  if(sources.size()<2 || sources.size()>128)throw std::invalid_argument("A multi-sample instrument needs 2 to 128 samples");
  if(name.empty() || name.size()>128 || name.find('\0')!=std::string::npos)throw std::invalid_argument("Use an instrument name of 1 to 128 UTF-8 bytes");
  const auto &spec=song_->GetModSpecifications();
  const int oldSamples=song_->GetNumSamples(),oldInstruments=song_->GetNumInstruments();
  const int instrument=(oldInstruments ? oldInstruments : oldSamples)+1;
  if(!spec.instrumentsMax || instrument>std::min<int>(spec.instrumentsMax,MAX_INSTRUMENTS-1))
    throw std::invalid_argument("Not enough instrument slots in this song format");
  if(sources.size()>size_t(std::max(0,std::min<int>(spec.samplesMax,MAX_SAMPLES-1)-oldSamples)))
    throw std::invalid_argument("Not enough sample slots in this song format");
  std::sort(sources.begin(),sources.end(),[](const auto &a,const auto &b){return a.rootNote<b.rootNote;});
  std::vector<std::string> paths;
  ImportedMultisample result;result.instrument=instrument;
  for(size_t i=0;i<sources.size();++i) {
    const auto &source=sources[i];
    if(source.rootNote<spec.noteMin || source.rootNote>std::min<int>(spec.noteMax,120))
      throw std::invalid_argument("A root note is outside this song format's note range; adjust the octave offset");
    if(i && source.rootNote==sources[i-1].rootNote)throw std::invalid_argument("Two samples have the same root note; separate velocity or round-robin variants first");
    paths.push_back(source.path);
    const int low=i ? (sources[i-1].rootNote+source.rootNote)/2+1 : source.rootNote;
    const int high=i+1<sources.size() ? (source.rootNote+sources[i+1].rootNote)/2 : source.rootNote;
    result.zones.push_back({source.path,oldSamples+int(i)+1,source.rootNote,low,high});
  }
  auto decoded=decodeSamples(paths);
  if(dryRun)return result;
  transaction([&](OpenMPT::CSoundFile &song) {
    if(!oldInstruments)for(SAMPLEINDEX i=1;i<=oldSamples;++i) {
      song.Instruments[i]=new OpenMPT::ModInstrument(i);song.Instruments[i]->name=song.GetSampleName(i);song.m_nInstruments=i;
    }
    auto *ins=new OpenMPT::ModInstrument(0);song.Instruments[instrument]=ins;song.m_nInstruments=INSTRUMENTINDEX(instrument);
    ins->name=::OpenMPT::mpt::ToCharset(song.GetCharsetInternal(),::OpenMPT::mpt::Charset::UTF8,name);
    for(size_t i=0;i<result.zones.size();++i) {
      const auto &zone=result.zones[i];const auto slot=SAMPLEINDEX(zone.sample);
      if(!song.ReadSampleFromSong(slot,decoded->song(),SAMPLEINDEX(i+1)))throw std::runtime_error("Cannot copy decoded sample");
      song.m_nSamples=std::max(song.m_nSamples,slot);
      for(int key=zone.lowNote;key<=zone.highNote;++key) {
        // Each recorded root plays at its original rate. Only gaps transpose.
        // Preserve sample tuning/PCM rather than destructively resampling it.
        const int played=OpenMPT::NOTE_MIDDLEC+key-zone.rootNote;
        if(played<OpenMPT::NOTE_MIN || played>OpenMPT::NOTE_MAX)throw std::runtime_error("Mapped note exceeds the playback range");
        ins->Keyboard[key-1]=slot;ins->NoteMap[key-1]=uint8_t(played);
      }
    }
  });
  return result;
}

}
