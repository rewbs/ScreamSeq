#include "WindowsVST3.hpp"
#include "ScanProtocol.hpp"
#include <objbase.h>
#include <cstdio>
int wmain(int argc,wchar_t **argv){
 SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
 if(argc!=3||wcscmp(argv[1],L"--scan"))return 2;
 auto hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);if(FAILED(hr))return 3;
 int code=0;try{auto s=Tracker::WindowsVST3::encodeScan(Tracker::WindowsVST3::scanInThisProcess(Tracker::WindowsVST3::narrow(argv[2]))).dump();if(s.size()>1024*1024)throw std::runtime_error("scan output too large");fwrite(s.data(),1,s.size(),stdout);fflush(stdout);}catch(...){code=4;}
 CoUninitialize();return code;
}
