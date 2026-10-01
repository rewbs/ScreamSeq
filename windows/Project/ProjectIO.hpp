#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <system_error>
#include <vector>

namespace ScreamSeq::Project {
inline constexpr size_t maximumProjectBytes=600u*1024u*1024u;
namespace FileDetail {
inline void fail(const char *operation) {throw std::system_error(GetLastError(),std::system_category(),operation);}
struct Handle {
	HANDLE value=INVALID_HANDLE_VALUE;
	~Handle() {if(value!=INVALID_HANDLE_VALUE) CloseHandle(value);}
	Handle(const Handle&)=delete;
	Handle& operator=(const Handle&)=delete;
	explicit Handle(HANDLE h):value(h) {}
};
}
inline std::vector<std::byte> readProjectBytes(const std::filesystem::path &path,size_t limit=maximumProjectBytes) {
	FileDetail::Handle file(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));
	if(file.value==INVALID_HANDLE_VALUE) FileDetail::fail("Cannot open project");
	LARGE_INTEGER length{};if(!GetFileSizeEx(file.value,&length)) FileDetail::fail("Cannot size project");
	if(length.QuadPart<0 || static_cast<uint64_t>(length.QuadPart)>limit) throw std::runtime_error("Project exceeds the read limit");
	std::vector<std::byte> bytes(static_cast<size_t>(length.QuadPart));size_t offset=0;
	while(offset<bytes.size()) {
		DWORD got=0,chunk=static_cast<DWORD>(std::min<size_t>(bytes.size()-offset,1024u*1024u));
		if(!ReadFile(file.value,bytes.data()+offset,chunk,&got,nullptr)) FileDetail::fail("Cannot read project");
		if(!got) throw std::runtime_error("Project was truncated while reading");offset+=got;
	}
	return bytes;
}
inline void writeProjectFile(const std::filesystem::path &path,std::span<const std::byte> bytes,bool overwrite) {
	if(!path.is_absolute() || path.filename().empty()) throw std::invalid_argument("Project save requires an absolute file path");
	if(bytes.size()>maximumProjectBytes) throw std::invalid_argument("Project exceeds the save limit");
	if(!std::filesystem::is_directory(path.parent_path())) throw std::invalid_argument("Save directory does not exist");
	static std::atomic<uint64_t> serial{0};std::filesystem::path temporary;
	FileDetail::Handle file(INVALID_HANDLE_VALUE);
	for(unsigned attempt=0;attempt<64;++attempt) {
		temporary=path;temporary+=L".staged."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(GetTickCount64())+L"."+std::to_wstring(serial.fetch_add(1));
		file.value=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
		if(file.value!=INVALID_HANDLE_VALUE) break;
		if(GetLastError()!=ERROR_FILE_EXISTS && GetLastError()!=ERROR_ALREADY_EXISTS) FileDetail::fail("Cannot create project staging file");
	}
	if(file.value==INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot reserve a unique project staging file");
	try {
		size_t offset=0;
		while(offset<bytes.size()) {
			DWORD wrote=0,chunk=static_cast<DWORD>(std::min<size_t>(bytes.size()-offset,1024u*1024u));
			if(!WriteFile(file.value,bytes.data()+offset,chunk,&wrote,nullptr)) FileDetail::fail("Cannot write complete project; original retained");
			if(!wrote) throw std::runtime_error("Project save made no progress; original retained");offset+=wrote;
		}
		if(!FlushFileBuffers(file.value)) FileDetail::fail("Cannot flush project; original retained");
		HANDLE h=file.value;file.value=INVALID_HANDLE_VALUE;if(!CloseHandle(h)) FileDetail::fail("Cannot close project staging file");
		DWORD flags=MOVEFILE_WRITE_THROUGH|(overwrite ? MOVEFILE_REPLACE_EXISTING : 0);
		if(!MoveFileExW(temporary.c_str(),path.c_str(),flags)) FileDetail::fail("Cannot publish project; existing destination retained");
	} catch(...) {
		if(file.value!=INVALID_HANDLE_VALUE) {CloseHandle(file.value);file.value=INVALID_HANDLE_VALUE;}
		DeleteFileW(temporary.c_str());throw;
	}
}
}
