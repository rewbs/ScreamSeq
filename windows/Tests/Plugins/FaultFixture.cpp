// Loaded ONLY by the isolated scanner child; never by the application/tests.
#include <windows.h>
#include "pluginterfaces/base/ipluginbase.h"
extern "C" __declspec(dllexport) bool InitDll(){
#if FIXTURE_FAULT==1
 RaiseException(EXCEPTION_ACCESS_VIOLATION,EXCEPTION_NONCONTINUABLE,0,nullptr);
#elif FIXTURE_FAULT==2
 Sleep(INFINITE);
#elif FIXTURE_FAULT==3
 char bytes[8192]{};for(int i=0;i<256;++i){DWORD n=0;WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),bytes,sizeof bytes,&n,nullptr);}
#endif
 return true;
}
extern "C" __declspec(dllexport) bool ExitDll(){return true;}
extern "C" __declspec(dllexport) Steinberg::IPluginFactory *GetPluginFactory(){return nullptr;}
