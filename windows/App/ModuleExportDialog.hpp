#pragma once
#include <windows.h>
#include <shobjidl.h>
#include <wrl/client.h>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace ScreamSeq {
// UI owner only. The caller keeps a captured document/revision across Show.
inline std::filesystem::path chooseExportFile(HWND owner,const std::wstring &format,const std::wstring &label,const wchar_t *title) {
	Microsoft::WRL::ComPtr<IFileSaveDialog> dialog;
	const auto require=[](HRESULT result) {
		if(FAILED(result))throw std::runtime_error("Cannot open the export chooser");
	};
	require(CoCreateInstance(CLSID_FileSaveDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog)));
	DWORD options=0;require(dialog->GetOptions(&options));
	require(dialog->SetOptions(options|FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST|FOS_NOCHANGEDIR|FOS_DONTADDTORECENT|FOS_OVERWRITEPROMPT));
	const auto pattern=L"*."+format;
	const COMDLG_FILTERSPEC filter{label.c_str(),pattern.c_str()};
	require(dialog->SetFileTypes(1,&filter));require(dialog->SetDefaultExtension(format.c_str()));
	require(dialog->SetFileName((L"Untitled."+format).c_str()));
	require(dialog->SetTitle(title));require(dialog->SetOkButtonLabel(L"Export"));
	const auto result=dialog->Show(owner);
	if(result==HRESULT_FROM_WIN32(ERROR_CANCELLED))return {};
	require(result);
	Microsoft::WRL::ComPtr<IShellItem> item;require(dialog->GetResult(&item));
	PWSTR path=nullptr;require(item->GetDisplayName(SIGDN_FILESYSPATH,&path));
	try {std::filesystem::path destination(path);CoTaskMemFree(path);return destination;}
	catch(...) {CoTaskMemFree(path);throw;}
}
inline std::filesystem::path chooseModuleExport(HWND owner,const std::wstring &format) {
	return chooseExportFile(owner,format,format+L" module (*."+format+L")",L"Export module");
}
inline std::filesystem::path chooseAudioExport(HWND owner) {
	return chooseExportFile(owner,L"wav",L"Stereo float WAV (48 kHz)",L"Export audio");
}
}
