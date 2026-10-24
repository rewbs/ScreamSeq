#include "windows/Plugins/WindowsVST3.hpp"
#include "windows/Audio/HostClock.hpp"
#include <windows.h>
#include <commctrl.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace Tracker;
static void check(bool b,const char *why){if(!b)throw std::runtime_error(why);}
int main(int argc,char **argv){try{
 check(argc==4,"args");WindowsVST3::configure(argv[1],argv[3]);auto ds=WindowsVST3::rescan(argv[2]);auto p=platformPluginBackendFactory().create(PluginState{ds[0]},48000,true);
 auto dll=GetModuleHandleW(std::filesystem::u8path(ds[0].path).c_str());auto counts=reinterpret_cast<int(*)(int)>(GetProcAddress(dll,"FixtureViews"));check(counts!=nullptr,"fixture views export");
 auto initial=reinterpret_cast<double(*)()>(GetProcAddress(dll,"FixtureInitialViewParameter"));check(initial!=nullptr,"fixture initial controller value export");
 HWND foreground=GetForegroundWindow();
 for(int repeat=0;repeat<3;++repeat){const auto baseline=.125*(repeat+1);check(p->parameter(7,baseline,0),"manual baseline rejected");
  uint32_t pendingID=0;float pendingValue=0;check(!p->popEdit(pendingID,pendingValue),"manual baseline fabricated an editor gesture");
  // No process/state/timer call may synchronize the controller before show.
  const auto before=ScreamSeq::hostTime100ns();p->showEditor();const auto after=ScreamSeq::hostTime100ns();
  check(initial()==baseline,"createView observed stale controller baseline");check(p->editorOpen(),"editor not open");check(counts(0)==repeat+1&&counts(2)>=repeat+1,"plugin-owned attach/frame-resize callbacks");
  HWND host=FindWindowW(L"ScreamSeq.VST3.PrivateEditor",L"Resonance Test Gain");check(host!=nullptr,"native host HWND absent");DWORD pid=0;GetWindowThreadProcessId(host,&pid);check(pid==GetCurrentProcessId(),"wrong HWND process");check(GetWindow(host,GW_CHILD)!=nullptr,"plugin-owned child HWND absent");RECT client{};GetClientRect(host,&client);check(client.right==520&&client.bottom==220,"IPlugFrame resize did not resize native client");
  Sleep(2);const auto drain=ScreamSeq::hostTime100ns();PluginEdit event;
  check(p->popEdit(event)&&event.id==7&&event.value==.625f,"native editor commit value not queued");
  check(before&&event.hostTime>=before&&event.hostTime<=after&&event.hostTime<drain,"native editor timestamp lost across delayed drain");
  check(p->droppedEdits()==0,"ordinary reopen cycle lost notifications");
  std::array<float,128> pcm;pcm.fill(1);check(p->process(pcm.data(),64,0,nullptr,0,{}),"editor edit audio processing");check(pcm[0]==.625f,"native editor gain not applied");
  const auto panel=GetWindow(host,GW_CHILD),slider=GetDlgItem(panel,7001);
  check(slider&&IsWindowVisible(slider)&&(GetWindowLongPtrW(slider,GWL_STYLE)&WS_TABSTOP),"vendor gain slider absent or inaccessible");
  check(SendMessageW(slider,TBM_GETPOS,0,0)==625,"vendor slider did not show its attach gesture");
  const auto gestureStart=ScreamSeq::hostTime100ns();
  SendMessageW(slider,TBM_SETPOS,TRUE,250);
  SendMessageW(panel,WM_HSCROLL,MAKEWPARAM(TB_THUMBTRACK,250),reinterpret_cast<LPARAM>(slider));
  SendMessageW(panel,WM_HSCROLL,TB_ENDTRACK,reinterpret_cast<LPARAM>(slider));
  const auto gestureEnd=ScreamSeq::hostTime100ns();
  check(p->popEdit(event)&&event.id==7&&event.value==.25f&&event.hostTime>=gestureStart&&event.hostTime<=gestureEnd,"vendor slider lost original input or timestamp");
  check(!p->popEdit(event),"slider end-track duplicated a gesture");
  SendMessageW(slider,WM_KEYDOWN,VK_RIGHT,0);SendMessageW(slider,WM_KEYUP,VK_RIGHT,0);
  check(SendMessageW(slider,TBM_GETPOS,0,0)==251,"native arrow key did not change vendor slider");
  check(p->popEdit(event)&&event.id==7&&event.value==.251f,"vendor keyboard input was not recorded");
  check(!p->popEdit(event)&&p->droppedEdits()==0,"vendor control fabricated or lost input");
  pcm.fill(1);check(p->process(pcm.data(),64,0,nullptr,0,{})&&pcm[0]==.251f,"vendor slider did not change actual PCM");
  check(GetForegroundWindow()==foreground,"editor stole foreground focus");
  if(repeat==1){DWORD_PTR result=0;check(SendMessageTimeoutW(host,WM_CLOSE,0,0,SMTO_ABORTIFHUNG,1000,&result)!=0,"scoped HWND close message");}else p->closeEditor();check(!p->editorOpen()&&!IsWindow(host)&&counts(1)==repeat+1,"editor remove/close lifetime");
 }
 std::cout<<"PASS actual HWND attach/frame resize/slider/keyboard/original timestamps/PCM/remove, three reopen cycles, no foreground activation\n";
 return 0;
}catch(const std::exception &e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
