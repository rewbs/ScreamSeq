#pragma once
#include <stdexcept>
#include <string>
#include <string_view>

namespace ScreamSeq {
// US-labelled physical positions; native key translation stays in the host.
// Preserve Windows' existing optional top C on I, including old profiles.
struct MusicalKeyMap {
  std::string lower="ZSXDCVGBHNJM",upper="Q2W3ER5T6Y7UI";
  bool operator==(const MusicalKeyMap &)const=default;
  static MusicalKeyMap validated(std::string lower,std::string upper) {
    if(lower.size()!=12||(upper.size()!=12&&upper.size()!=13))
      throw std::invalid_argument("Use 12 lower-octave keys and 12 upper-octave keys, with an optional 13th key for top C");
    std::string seen;
    for(auto *row:{&lower,&upper})for(auto &key:*row) {
      if(key>='a'&&key<='z')key=char(key-'a'+'A');
      const bool punctuation=std::string_view("-=[];'`\\,./<").find(key)!=std::string_view::npos;
      if(!((key>='A'&&key<='Z')||(key>='0'&&key<='9')||punctuation)||seen.find(key)!=std::string::npos)
        throw std::invalid_argument("Use distinct physical keys with US unshifted labels (or < for the extra ISO key); spaces and duplicate keys are not allowed");
      seen+=key;
    }
    return {std::move(lower),std::move(upper)};
  }
  int offset(unsigned physical)const noexcept {
    if(physical>127)return -1;const auto key=char(physical);const auto low=lower.find(key),high=upper.find(key);
    return low!=std::string::npos?int(low):high!=std::string::npos?12+int(high):-1;
  }
};
}
