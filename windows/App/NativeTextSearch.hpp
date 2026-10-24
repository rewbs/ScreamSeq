#pragma once
#include <windows.h>
#include <limits>
#include <string_view>

namespace ScreamSeq {
// Native catalogue filtering only. Match ordinally without case sensitivity,
// independent of the current locale, and never reinterpret a result row as an ID.
inline bool nativeTextSearchMatches(std::wstring_view text,std::wstring_view query) {
  if(query.empty())return true;
  if(query.size()>text.size()||query.size()>size_t((std::numeric_limits<int>::max)()))return false;
  for(size_t i=0;i<=text.size()-query.size();++i)
    if(CompareStringOrdinal(text.data()+i,int(query.size()),query.data(),int(query.size()),TRUE)==CSTR_EQUAL)return true;
  return false;
}
}
