#pragma once
#include <windows.h>
#include <commdlg.h>
#include <cderr.h>
#include <filesystem>
#include <stdexcept>
#include <vector>
#include <shobjidl.h>
#include <wrl/client.h>

namespace ScreamSeq {
inline std::vector<std::filesystem::path> chooseSampleFolders(HWND owner) {
  Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
  auto require=[](HRESULT result){if(FAILED(result))throw std::runtime_error("Cannot open sample folder chooser");};
  require(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog)));
  DWORD options=0;require(dialog->GetOptions(&options));
  require(dialog->SetOptions(options|FOS_PICKFOLDERS|FOS_ALLOWMULTISELECT|FOS_FORCEFILESYSTEM|FOS_NOCHANGEDIR|FOS_DONTADDTORECENT));
  require(dialog->SetTitle(L"Add sample folders"));const auto result=dialog->Show(owner);
  if(result==HRESULT_FROM_WIN32(ERROR_CANCELLED))return {};require(result);
  Microsoft::WRL::ComPtr<IShellItemArray> items;require(dialog->GetResults(&items));DWORD count=0;require(items->GetCount(&count));
  if(count>32)throw std::runtime_error("Choose at most 32 sample folders");
  std::vector<std::filesystem::path> paths;
  for(DWORD i=0;i<count;++i){Microsoft::WRL::ComPtr<IShellItem> item;require(items->GetItemAt(i,&item));PWSTR path{};require(item->GetDisplayName(SIGDN_FILESYSPATH,&path));try{paths.emplace_back(path);}catch(...){CoTaskMemFree(path);throw;}CoTaskMemFree(path);}
  return paths;
}
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
