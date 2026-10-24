#pragma once
#include "ProjectIO.hpp"
#include <ostream>
#include <streambuf>

namespace ScreamSeq::Project {
namespace FileDetail {
// Control/worker use only. Stream directly to the exclusively owned staging
// handle: no whole-WAV buffer and no close/reopen race before publication.
class OutputBuffer final:public std::streambuf {
  HANDLE file_;
  std::streamsize xsputn(const char *bytes,std::streamsize count) override {
    std::streamsize offset=0;
    while(offset<count) {
      const auto chunk=static_cast<DWORD>(std::min<std::streamsize>(count-offset,1024*1024));
      DWORD wrote=0;
      if(!WriteFile(file_,bytes+offset,chunk,&wrote,nullptr))fail("Cannot write export staging file");
      if(!wrote)throw std::runtime_error("Export write made no progress; original retained");
      offset+=wrote;
    }
    return offset;
  }
  int_type overflow(int_type value) override {
    if(traits_type::eq_int_type(value,traits_type::eof()))return traits_type::not_eof(value);
    const auto byte=traits_type::to_char_type(value);
    return xsputn(&byte,1)==1?value:traits_type::eof();
  }
  pos_type seekoff(off_type offset,std::ios_base::seekdir direction,std::ios_base::openmode mode) override {
    if(mode!=std::ios_base::out)return pos_type(off_type(-1));
    DWORD origin;
    if(direction==std::ios_base::beg)origin=FILE_BEGIN;
    else if(direction==std::ios_base::cur)origin=FILE_CURRENT;
    else if(direction==std::ios_base::end)origin=FILE_END;
    else return pos_type(off_type(-1));
    LARGE_INTEGER distance{},position{};distance.QuadPart=offset;
    if(!SetFilePointerEx(file_,distance,&position,origin))return pos_type(off_type(-1));
    return pos_type(position.QuadPart);
  }
  pos_type seekpos(pos_type position,std::ios_base::openmode mode) override {
    return seekoff(off_type(position),std::ios_base::beg,mode);
  }
public:
  explicit OutputBuffer(HANDLE file):file_(file){}
};
}

// The writer must either complete the stream or throw. A failed stream, failed
// flush/close, or a destination created meanwhile never replaces existing data.
// This is filesystem publication, outside document history. The caller guards
// document identity/revision and any protected project-source paths separately.
template<class Writer> void writeAtomicStream(const std::filesystem::path &path,bool overwrite,Writer &&writer) {
  if(!path.is_absolute()||path.filename().empty())throw std::invalid_argument("Export requires an absolute file path");
  if(!std::filesystem::is_directory(path.parent_path()))throw std::invalid_argument("Export directory does not exist");
  if(!overwrite&&std::filesystem::exists(path))throw std::runtime_error("Export destination already exists");
  static std::atomic<uint64_t> serial{0};std::filesystem::path temporary;
  FileDetail::Handle file(INVALID_HANDLE_VALUE);
  for(unsigned attempt=0;attempt<64;++attempt) {
    temporary=path;temporary+=L".export."+std::to_wstring(GetCurrentProcessId())+L"."+
      std::to_wstring(GetTickCount64())+L"."+std::to_wstring(serial.fetch_add(1));
    file.value=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file.value!=INVALID_HANDLE_VALUE)break;
    if(GetLastError()!=ERROR_FILE_EXISTS&&GetLastError()!=ERROR_ALREADY_EXISTS)FileDetail::fail("Cannot create export staging file");
  }
  if(file.value==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot reserve unique export staging file");
  try {
    {
      FileDetail::OutputBuffer buffer(file.value);std::ostream output(&buffer);
      writer(output);output.flush();
      if(!output)throw std::runtime_error("Export stream failed; original retained");
    }
    if(!FlushFileBuffers(file.value))FileDetail::fail("Cannot flush export; original retained");
    const auto handle=file.value;file.value=INVALID_HANDLE_VALUE;
    if(!CloseHandle(handle))FileDetail::fail("Cannot close export staging file");
    const DWORD flags=MOVEFILE_WRITE_THROUGH|(overwrite?MOVEFILE_REPLACE_EXISTING:0);
    if(!MoveFileExW(temporary.c_str(),path.c_str(),flags))FileDetail::fail("Cannot publish export; existing destination retained");
  }catch(...) {
    if(file.value!=INVALID_HANDLE_VALUE){CloseHandle(file.value);file.value=INVALID_HANDLE_VALUE;}
    DeleteFileW(temporary.c_str());throw;
  }
}
}
