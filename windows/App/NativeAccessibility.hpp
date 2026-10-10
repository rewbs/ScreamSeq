#pragma once
#include <windows.h>
#include <commctrl.h>
#include <oleacc.h>
#include <UIAutomationCore.h>
#include <UIAutomationCoreApi.h>
#include <memory>
#include <new>
#include <string>

namespace ScreamSeq::NativeAccessibility {
// Annotate standard HWND providers; do not replace their roles, values, native
// text operations or selection. All calls and retirement belong to the UI thread.
inline constexpr UINT_PTR subclassID=0x53514143;
struct Annotation {
  HWND window{};
  IAccPropServices *services{};
  HRESULT apartment=E_FAIL;
  bool named=false;
  std::wstring text;
  HRESULT setName(const wchar_t *value) noexcept {
    try {
      std::wstring next(value); // Allocate before changing either provider's name.
      auto result=services->SetHwndPropStr(window,OBJID_CLIENT,CHILDID_SELF,PROPID_ACC_NAME,value);
      if(FAILED(result))return result;named=true;
      result=services->SetHwndPropStr(window,OBJID_CLIENT,CHILDID_SELF,Name_Property_GUID,value);
      if(FAILED(result)) {
        if(text.empty()){const MSAAPROPID properties[]={PROPID_ACC_NAME,Name_Property_GUID};services->ClearHwndProps(window,OBJID_CLIENT,CHILDID_SELF,properties,2);}
        else {services->SetHwndPropStr(window,OBJID_CLIENT,CHILDID_SELF,PROPID_ACC_NAME,text.c_str());
          services->SetHwndPropStr(window,OBJID_CLIENT,CHILDID_SELF,Name_Property_GUID,text.c_str());}
        return result;
      }
      text.swap(next);return S_OK;
    }catch(const std::bad_alloc &){return E_OUTOFMEMORY;}catch(...){return E_FAIL;}
  }
  ~Annotation() {
    if(services) {
      if(named) {const MSAAPROPID properties[]={PROPID_ACC_NAME,Name_Property_GUID};
        services->ClearHwndProps(window,OBJID_CLIENT,CHILDID_SELF,properties,2);}
      services->Release();
    }
    if(SUCCEEDED(apartment))CoUninitialize();
  }
};
inline LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR data) {
  if(message==WM_NCDESTROY) {
    RemoveWindowSubclass(window,procedure,subclassID);
    // Clear before native destruction, including when HWND values are reused.
    delete reinterpret_cast<Annotation *>(data);
  }
  return DefSubclassProc(window,message,w,l);
}
inline HRESULT name(HWND window,const wchar_t *text) {
  DWORD process=0;
  if(!text||!*text||!IsWindow(window))return E_INVALIDARG;
  if(GetWindowThreadProcessId(window,&process)!=GetCurrentThreadId()||process!=GetCurrentProcessId())return RPC_E_WRONG_THREAD;
  DWORD_PTR data=0;
  if(GetWindowSubclass(window,procedure,subclassID,&data)) {
    const auto result=reinterpret_cast<Annotation *>(data)->setName(text);
    if(SUCCEEDED(result))NotifyWinEvent(EVENT_OBJECT_NAMECHANGE,window,OBJID_CLIENT,CHILDID_SELF);
    return result;
  }
  std::unique_ptr<Annotation> annotation(new(std::nothrow) Annotation);
  if(!annotation)return E_OUTOFMEMORY;
  annotation->window=window;
  // GUI harnesses may not have initialized COM. Balance only our own reference;
  // an existing MTA is usable and must not be changed or uninitialized here.
  annotation->apartment=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
  if(FAILED(annotation->apartment)&&annotation->apartment!=RPC_E_CHANGED_MODE)return annotation->apartment;
  auto result=CoCreateInstance(CLSID_AccPropServices,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&annotation->services));
  if(FAILED(result))return result;
  result=annotation->setName(text);
  if(FAILED(result))return result;
  if(!SetWindowSubclass(window,procedure,subclassID,reinterpret_cast<DWORD_PTR>(annotation.get())))return E_FAIL;
  annotation.release();
  NotifyWinEvent(EVENT_OBJECT_NAMECHANGE,window,OBJID_CLIENT,CHILDID_SELF);
  return S_OK;
}
}
