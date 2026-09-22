#pragma once
#include <windows.h>
#include <commdlg.h>
#include <cderr.h>
#include <filesystem>
#include <stdexcept>
#include <vector>

namespace ScreamSeq {
// The caller captures document/target identity before this modal OS chooser
// and rechecks it afterwards. No file is decoded or document changed here.
inline std::vector<std::filesystem::path> chooseSampleFiles(HWND owner,const wchar_t *title,bool multiple=false,const wchar_t *filter=nullptr) {
  std::vector<wchar_t> buffer(multiple?65536:32768);
  OPENFILENAMEW choice{};choice.lStructSize=sizeof(choice);choice.hwndOwner=owner;
  choice.lpstrFile=buffer.data();choice.nMaxFile=DWORD(buffer.size());choice.lpstrTitle=title;
  choice.lpstrFilter=filter?filter:L"Audio samples\0*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3;*.its;*.s3i;*.xi\0All files\0*.*\0\0";
  choice.Flags=OFN_EXPLORER|OFN_NOCHANGEDIR|OFN_DONTADDTORECENT|OFN_PATHMUSTEXIST|OFN_FILEMUSTEXIST|(multiple?OFN_ALLOWMULTISELECT:0);
  if(!GetOpenFileNameW(&choice)) {
    const auto error=CommDlgExtendedError();
    if(error==FNERR_BUFFERTOOSMALL)throw std::runtime_error("The selected paths are too long / import fewer samples at once");
    if(error)throw std::runtime_error("Could not open the sample chooser");
    return {};
  }
  const std::filesystem::path first(buffer.data());
  auto cursor=buffer.data()+wcslen(buffer.data())+1;
  if(!multiple||!*cursor)return {first};
  std::vector<std::filesystem::path> paths;
  while(*cursor) {
    if(paths.size()==128)throw std::runtime_error("Choose at most 128 samples per import");
    paths.push_back(first/cursor);cursor+=wcslen(cursor)+1;
  }
  return paths;
}
}
