#pragma once
#include "PrivateGuiTest.hpp"
#include <filesystem>
#include <fstream>

namespace ScreamSeq::Tests {
namespace PrivateGuiProcessDetail {
inline std::wstring quote(const std::wstring &value) {
  std::wstring result=L"\"";size_t slashes=0;
  for(const auto character:value) {
    if(character==L'\\'){++slashes;continue;}
    result.append(slashes*(character==L'\"'?2:1),L'\\');slashes=0;
    if(character==L'\"')result+=L'\\';result+=character;
  }
  result.append(slashes*2,L'\\');result+=L'\"';return result;
}
inline std::wstring desktopName(HDESK desktop) {
  DWORD bytes=0;GetUserObjectInformationW(desktop,UOI_NAME,nullptr,0,&bytes);
  if(!bytes)throw std::runtime_error(PrivateGuiDetail::error("Read private process desktop name size",GetLastError()));
  std::wstring name(bytes/sizeof(wchar_t),0);
  if(!GetUserObjectInformationW(desktop,UOI_NAME,name.data(),bytes,&bytes))
    throw std::runtime_error(PrivateGuiDetail::error("Read private process desktop name",GetLastError()));
  name.resize(wcslen(name.c_str()));return name;
}
template<class Body>void worker(const std::wstring &name,const std::filesystem::path &log,Body body) {
  // The observer selected this desktop at process creation. Never migrate the
  // child or manufacture a private desktop when worker mode is invoked alone.
  if(desktopName(GetThreadDesktop(GetCurrentThreadId()))!=name)
    throw std::runtime_error("Private GUI child was not started on its expected desktop");
  std::ofstream output(log,std::ios::binary|std::ios::trunc);
  if(!output)throw std::runtime_error("Cannot create private GUI child log");
  const auto oldOut=std::cout.rdbuf(output.rdbuf()),oldError=std::cerr.rdbuf(output.rdbuf());
  struct Streams {std::streambuf *out,*error;~Streams(){std::cout.flush();std::cerr.flush();std::cout.rdbuf(out);std::cerr.rdbuf(error);}} streams{oldOut,oldError};
  std::cout<<"Private GUI child PID="<<GetCurrentProcessId()<<" desktop="<<PrivateGuiDetail::narrow(name.c_str())<<'\n';
  PrivateGuiDetail::Windows windows;PrivateGuiDetail::currentWindows=&windows;
  std::exception_ptr bodyFailure,cleanupFailure;
  try{body();}catch(...){bodyFailure=std::current_exception();}
  try{windows.verify();}catch(...){cleanupFailure=std::current_exception();}
  PrivateGuiDetail::currentWindows=nullptr;
  if(bodyFailure)std::cerr<<"Fixture: "<<PrivateGuiDetail::describe(bodyFailure)<<'\n';
  if(cleanupFailure)std::cerr<<"Fixture cleanup: "<<PrivateGuiDetail::describe(cleanupFailure)<<'\n';
  output.flush();if(!output)throw std::runtime_error("Cannot write private GUI child log");
  if(bodyFailure||cleanupFailure)throw std::runtime_error("Private GUI child failed; see its retained fixture diagnostics");
}
}

// Application fixtures create worker/driver resources beyond one GUI thread.
// Their observer owns the desktop, but no thread in its process attaches to it.
// Only the exact child executable runs there. Process exit is the lifecycle
// boundary before CloseDesktop; no unknown OS windows or threads are modified.
template<class Body>void runPrivateGuiProcess(const wchar_t *prefix,int argc,wchar_t **argv,Body body) {
  if(argc==4&&std::wstring_view(argv[1])==L"--private-gui-worker") {
    if(!std::wstring_view(argv[2]).starts_with(std::wstring(prefix)+L"-"))
      throw std::runtime_error("Unexpected private GUI worker desktop identity");
    PrivateGuiProcessDetail::worker(argv[2],argv[3],body);return;
  }
  if(argc!=1)throw std::runtime_error("Unexpected private GUI fixture arguments");
  const auto observer=GetCurrentThreadId();const auto original=GetThreadDesktop(observer);
  if(!original)throw std::runtime_error(PrivateGuiDetail::error("Read GUI process observer desktop",GetLastError()));
  const auto foreground=GetForegroundWindow();const auto clipboard=GetClipboardSequenceNumber();
  LARGE_INTEGER serial{};QueryPerformanceCounter(&serial);
  const auto name=std::wstring(prefix)+L"-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(serial.QuadPart);
  const auto desktop=CreateDesktopW(name.c_str(),nullptr,nullptr,0,GENERIC_ALL,nullptr);
  const auto creationError=desktop?ERROR_SUCCESS:GetLastError();
  PROCESS_INFORMATION process{};HANDLE job=nullptr;bool exited=false,created=false;
  DWORD exitCode=STILL_ACTIVE;std::filesystem::path folder,log;std::ostringstream failures;
  if(!desktop)failures<<PrivateGuiDetail::error("Create GUI process desktop",creationError)<<'\n';
  else try {
    folder=std::filesystem::temp_directory_path()/name;
    if(!std::filesystem::create_directory(folder))throw std::runtime_error("Cannot create unique private GUI process log directory");
    log=folder/L"child.log";
    std::wstring executable(32768,0);const auto length=GetModuleFileNameW(nullptr,executable.data(),DWORD(executable.size()));
    if(!length||length>=executable.size())throw std::runtime_error("Cannot resolve exact private GUI fixture executable");
    executable.resize(length);
    auto command=PrivateGuiProcessDetail::quote(executable)+L" --private-gui-worker "+PrivateGuiProcessDetail::quote(name)+L" "+PrivateGuiProcessDetail::quote(log.wstring());
    job=CreateJobObjectW(nullptr,nullptr);
    if(!job)throw std::runtime_error(PrivateGuiDetail::error("Create owned private GUI process job",GetLastError()));
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limit{};limit.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if(!SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limit,sizeof(limit)))
      throw std::runtime_error(PrivateGuiDetail::error("Configure owned private GUI process job",GetLastError()));
    STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.lpDesktop=const_cast<wchar_t *>(name.c_str());
    if(!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_SUSPENDED|CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process))
      throw std::runtime_error(PrivateGuiDetail::error("Create owned private GUI child",GetLastError()));
    created=true;
    if(!AssignProcessToJobObject(job,process.hProcess))throw std::runtime_error(PrivateGuiDetail::error("Assign owned private GUI child job",GetLastError()));
    std::cout<<"Private GUI observer PID="<<GetCurrentProcessId()<<" child PID="<<process.dwProcessId<<" desktop="<<PrivateGuiDetail::narrow(name.c_str())<<std::endl;
    if(ResumeThread(process.hThread)==DWORD(-1))throw std::runtime_error(PrivateGuiDetail::error("Resume owned private GUI child",GetLastError()));
    const auto wait=WaitForSingleObject(process.hProcess,90000);
    if(wait!=WAIT_OBJECT_0)throw std::runtime_error(wait==WAIT_TIMEOUT?"Owned private GUI child exceeded its 90-second bound":PrivateGuiDetail::error("Wait for owned private GUI child",GetLastError()));
    exited=true;
    if(!GetExitCodeProcess(process.hProcess,&exitCode))throw std::runtime_error(PrivateGuiDetail::error("Read owned private GUI child result",GetLastError()));
  }catch(const std::exception &error){failures<<error.what()<<'\n';}

  // A bounded failure still joins only this fixture's child before checking
  // isolation. Job ownership also protects cleanup if the observer is killed.
  if(created&&!exited) {
    if(!TerminateProcess(process.hProcess,1))failures<<PrivateGuiDetail::error("Stop failed owned private GUI child",GetLastError())<<'\n';
    exited=WaitForSingleObject(process.hProcess,5000)==WAIT_OBJECT_0;
    if(!exited)failures<<"Owned private GUI child did not exit during failure cleanup\n";
  }
  if(created&&exited) {
    std::ifstream input(log,std::ios::binary|std::ios::ate);
    const auto bytes=input?input.tellg():std::streampos(-1);
    if(bytes<0||bytes>1024*1024)failures<<"Private GUI child log is missing or exceeds its 1 MiB bound\n";
    else {std::string text(size_t(bytes),0);input.seekg(0);input.read(text.data(),std::streamsize(text.size()));if(!input)failures<<"Cannot read complete private GUI child log\n";else std::cout<<text;}
    if(exitCode!=0)failures<<"Owned private GUI child exit code "<<exitCode<<'\n';
  }
  if(process.hThread)CloseHandle(process.hThread);
  if(process.hProcess)CloseHandle(process.hProcess);
  if(job)CloseHandle(job);
  const bool closed=!desktop||CloseDesktop(desktop);const auto closeError=closed?ERROR_SUCCESS:GetLastError();
  if(!closed)failures<<PrivateGuiDetail::error("Close private GUI desktop after child process exit",closeError)<<'\n';
  if(GetCurrentThreadId()!=observer||GetThreadDesktop(observer)!=original)failures<<"Private GUI process observer changed its original desktop\n";
  if(GetForegroundWindow()!=foreground)failures<<"Private GUI process fixture changed the user's foreground window\n";
  if(GetClipboardSequenceNumber()!=clipboard)failures<<"Private GUI process fixture changed the user's clipboard\n";
  if(!failures.str().empty()) {
    if(!log.empty())std::cerr<<"Retained private GUI child log: "<<PrivateGuiDetail::narrow(log.c_str())<<'\n';
    throw std::runtime_error(failures.str());
  }
  std::error_code error;std::filesystem::remove(log,error);std::filesystem::remove(folder,error);
  std::cout<<"PASS private GUI process lifecycle: child exited, desktop closed, observer/foreground/clipboard unchanged\n";
}
}
