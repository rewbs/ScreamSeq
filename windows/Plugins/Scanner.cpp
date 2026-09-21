#include "WindowsVST3.hpp"
#include "ScanProtocol.hpp"
#include <array>
namespace Tracker::WindowsVST3 {
static void need(bool b,const char *s){if(!b)throw std::runtime_error(s);}
static std::wstring quote(const std::wstring &s){std::wstring o=L"\"";size_t back=0;for(auto c:s){if(c==L'\\'){++back;continue;}o.append(back*(c==L'"'?2:1),L'\\');back=0;if(c==L'"')o+=L'\\';o+=c;}o.append(back*2,L'\\');o+=L'"';return o;}
Scan scanChild(const std::string &exe,const std::string &path,uint32_t timeoutMs){
 need(timeoutMs>=50&&timeoutMs<=60000,"Scanner timeout out of bounds");auto before=fingerprint(path);auto executable=std::filesystem::canonical(nativePath(exe));
 SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};HANDLE rd=nullptr,wr=nullptr;need(CreatePipe(&rd,&wr,&sa,0),"Cannot create scanner pipe");Handle reader(rd),writer(wr);
 need(SetHandleInformation(rd,HANDLE_FLAG_INHERIT,0),"Cannot protect scanner pipe");Handle nil(CreateFileW(L"NUL",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,OPEN_EXISTING,0,nullptr));need(nil.h!=INVALID_HANDLE_VALUE,"Cannot open scanner NUL");
 Handle job(CreateJobObjectW(nullptr,nullptr));need(job.h!=nullptr,"Cannot create scanner job");JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE|JOB_OBJECT_LIMIT_ACTIVE_PROCESS;limits.BasicLimitInformation.ActiveProcessLimit=1;need(SetInformationJobObject(job.h,JobObjectExtendedLimitInformation,&limits,sizeof limits),"Cannot constrain scanner job");
 SIZE_T attrBytes=0;InitializeProcThreadAttributeList(nullptr,1,0,&attrBytes);std::vector<unsigned char> attrs(attrBytes);auto *list=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrs.data());need(InitializeProcThreadAttributeList(list,1,0,&attrBytes),"Cannot initialize scanner attributes");
 struct Attr{LPPROC_THREAD_ATTRIBUTE_LIST p;~Attr(){DeleteProcThreadAttributeList(p);}}attr{list};HANDLE inherited[]={wr,nil.h};need(UpdateProcThreadAttribute(list,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,inherited,sizeof inherited,nullptr,nullptr),"Cannot restrict scanner inheritance");
 STARTUPINFOEXW start{};start.StartupInfo.cb=sizeof start;start.StartupInfo.dwFlags=STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW;start.StartupInfo.wShowWindow=SW_HIDE;start.StartupInfo.hStdOutput=wr;start.StartupInfo.hStdError=nil.h;start.StartupInfo.hStdInput=nil.h;start.lpAttributeList=list;
 PROCESS_INFORMATION pi{};auto cmd=quote(executable.native())+L" --scan "+quote(wide(before.path));
 need(CreateProcessW(executable.c_str(),cmd.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|CREATE_SUSPENDED|EXTENDED_STARTUPINFO_PRESENT,nullptr,executable.parent_path().c_str(),&start.StartupInfo,&pi),"Cannot start isolated VST3 scanner");Handle process(pi.hProcess),thread(pi.hThread);
 struct Kill{HANDLE h;~Kill(){if(WaitForSingleObject(h,0)==WAIT_TIMEOUT){TerminateProcess(h,9);WaitForSingleObject(h,1000);}}}kill{pi.hProcess};
 need(AssignProcessToJobObject(job.h,pi.hProcess),"Cannot isolate scanner in job");need(ResumeThread(pi.hThread)!=DWORD(-1),"Cannot run scanner");CloseHandle(writer.h);writer.h=nullptr;
 auto deadline=GetTickCount64()+timeoutMs;std::string output;output.reserve(16384);std::array<char,8192> data{};
 for(;;){DWORD available=0;if(PeekNamedPipe(rd,nullptr,0,nullptr,&available,nullptr)&&available){DWORD n=0;need(ReadFile(rd,data.data(),std::min<DWORD>(available,DWORD(data.size())),&n,nullptr),"Cannot read scanner output");need(output.size()+n<=1024*1024,"Scanner output exceeded 1 MiB");output.append(data.data(),n);}
 else if(WaitForSingleObject(pi.hProcess,0)==WAIT_OBJECT_0)break;
 else WaitForSingleObject(pi.hProcess,2);
 need(GetTickCount64()<deadline,"VST3 scanner timeout (isolated child terminated)");}
 DWORD code=0;need(GetExitCodeProcess(pi.hProcess,&code)&&code==0,"VST3 scanner crashed or rejected module");
 auto scan=decodeScan(JSON::parse(output,[](int depth,JSON::parse_event_t,JSON&){if(depth>16)throw std::runtime_error("Scanner response nesting exceeds limit");return true;}));need(scan.file.path==before.path&&scan.file.sha256==before.sha256&&scan.file.machine==before.machine,"Scanner identity changed or invalid response");auto after=fingerprint(path);need(after.sha256==before.sha256,"VST3 changed while scanning");return scan;
}
}
