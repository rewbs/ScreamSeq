#pragma once
// Interpose ONLY this test translation unit's included Registry.cpp calls.
// Production sources/builds contain no injection API or environment switches.
namespace ReviewIO {
enum class Fault { none, write, shortWrite, flush, rename };
inline Fault fault=Fault::none;
inline unsigned calls=0;
inline std::function<void()> beforePublish;
inline BOOL write(HANDLE h,LPCVOID p,DWORD n,LPDWORD written,LPOVERLAPPED overlap){
 if(fault==Fault::write||fault==Fault::shortWrite){++calls;BOOL ok=::WriteFile(h,p,n/2,written,overlap);if(fault==Fault::write){SetLastError(ERROR_DISK_FULL);return FALSE;}return ok;}
 return ::WriteFile(h,p,n,written,overlap);
}
inline BOOL flush(HANDLE h){if(fault==Fault::flush){++calls;SetLastError(ERROR_WRITE_FAULT);return FALSE;}return ::FlushFileBuffers(h);}
inline BOOL move(LPCWSTR from,LPCWSTR to,DWORD flags){if(beforePublish)beforePublish();if(fault==Fault::rename){++calls;SetLastError(ERROR_ACCESS_DENIED);return FALSE;}return ::MoveFileExW(from,to,flags);}
}
