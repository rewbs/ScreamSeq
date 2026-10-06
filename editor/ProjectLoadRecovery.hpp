#pragma once
#include "soundlib/Sndfile.h"
#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace Tracker {
// File-open diagnostics only. Musical API writes and canonical saves remain
// strict; recovering a file never grants permission to overwrite its source.
struct ProjectLoadRecovery {
  std::vector<std::string> warnings;
  bool lossy=false,protectSource=false;
  size_t omittedWarnings=0;
  void warn(std::string message,bool skipped=false,bool unsafeToOverwrite=false) {
    lossy|=skipped;protectSource|=skipped||unsafeToOverwrite;
    constexpr size_t maximumDetails=100;
    if(warnings.size()<maximumDetails)warnings.push_back(std::move(message));
    else {
      ++omittedWarnings;
      auto summary=std::to_string(omittedWarnings)+" additional project recovery warnings omitted";
      if(warnings.size()==maximumDetails)warnings.push_back(std::move(summary));else warnings.back()=std::move(summary);
    }
  }
};
// Historical RSONGS1 framing is verified against the retained db496a399
// SampleArchive.cpp: magic + module/sample u32 lengths + the two payloads.
// Current RSONGS2 adds a timing-length u32 (zero when no timing override exists).
inline bool recoverLegacySongSnapshot(std::vector<std::byte> &bytes,ProjectLoadRecovery &report) {
  constexpr char oldMagic[8]={'R','S','O','N','G','S','1','\0'};
  if(bytes.size()<8||std::memcmp(bytes.data(),oldMagic,8))return false;
  if(bytes.size()<16||bytes.size()>512u*1024u*1024u-4)throw std::invalid_argument("Invalid historical song snapshot size");
  auto u32=[&](size_t offset){uint32_t value=0;for(unsigned i=0;i<4;++i)value|=uint32_t(std::to_integer<uint8_t>(bytes[offset+i]))<<(8*i);return value;};
  const auto module=u32(8),samples=u32(12);
  if(!module||!samples||uint64_t(module)+samples+16!=bytes.size())throw std::invalid_argument("Invalid historical song snapshot framing");
  bytes.insert(bytes.begin()+16,4,std::byte{});bytes[6]=std::byte{'2'};
  report.warn("Converted historical song snapshot framing",false,true);return true;
}
inline unsigned projectPatternRowsPerBeat(const OpenMPT::CSoundFile &song,uint16_t pattern) {
  if(!song.Patterns.IsValidPat(pattern))throw std::invalid_argument("Recovered command refers to an unavailable pattern");
  const auto &p=song.Patterns[pattern];
  return std::max(1u,p.GetOverrideSignature()?unsigned(p.GetRowsPerBeat()):song.m_nDefaultRowsPerBeat?unsigned(song.m_nDefaultRowsPerBeat):4u);
}
inline double legacyNudgeDurationBeats(const OpenMPT::CSoundFile &song,uint16_t pattern,uint32_t rowDuration) {
  return double(rowDuration)/(65536.0*projectPatternRowsPerBeat(song,pattern));
}
}
