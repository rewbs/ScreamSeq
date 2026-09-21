#include "windows/Plugins/WindowsVST3.hpp"
#include <windows.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace Tracker;
static void check(bool b,const char *why){if(!b)throw std::runtime_error(why);}
int main(int argc,char **argv){try{
 check(argc==4,"args");WindowsVST3::configure(argv[1],argv[3]);auto ds=WindowsVST3::rescan(argv[2]);auto p=platformPluginBackendFactory().create(PluginState{ds[0]},48000,true);
 auto dll=GetModuleHandleW(std::filesystem::u8path(ds[0].path).c_str());auto counts=reinterpret_cast<int(*)(int)>(GetProcAddress(dll,"FixtureViews"));check(counts!=nullptr,"fixture views export");
 HWND foreground=GetForegroundWindow();
 for(int repeat=0;repeat<3;++repeat){p->showEditor();check(p->editorOpen(),"editor not open");check(counts(0)==repeat+1&&counts(2)>=repeat+1,"plugin-owned attach/frame-resize callbacks");
  HWND host=FindWindowW(L"ScreamSeq.VST3.PrivateEditor",L"Resonance Test Gain");check(host!=nullptr,"native host HWND absent");DWORD pid=0;GetWindowThreadProcessId(host,&pid);check(pid==GetCurrentProcessId(),"wrong HWND process");check(GetWindow(host,GW_CHILD)!=nullptr,"plugin-owned child HWND absent");RECT client{};GetClientRect(host,&client);check(client.right==520&&client.bottom==220,"IPlugFrame resize did not resize native client");
  uint32_t id=0;float value=0;check(p->popEdit(id,value)&&id==7&&value==.625f,"native editor commit value not queued");std::array<float,128> pcm;pcm.fill(1);check(p->process(pcm.data(),64,0,nullptr,0,{}),"editor edit audio processing");check(pcm[0]==.625f,"native editor gain not applied");
  check(GetForegroundWindow()==foreground,"editor stole foreground focus");
  if(repeat==1){DWORD_PTR result=0;check(SendMessageTimeoutW(host,WM_CLOSE,0,0,SMTO_ABORTIFHUNG,1000,&result)!=0,"scoped HWND close message");}else p->closeEditor();check(!p->editorOpen()&&!IsWindow(host)&&counts(1)==repeat+1,"editor remove/close lifetime");
 }
 std::cout<<"PASS actual HWND attach/frame resize/commit PCM/remove, three reopen cycles, no foreground activation\n";
 return 0;
}catch(const std::exception &e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
