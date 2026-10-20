#pragma once
#include <windows.h>
#include <oleacc.h>
#include <string>
#include <stdexcept>

namespace ScreamSeq::Tests {
// Query Windows' provider, not the app's annotation bookkeeping. Call on the
// fixture's COM-initialized UI thread and release before destroying the HWND.
inline std::wstring accessibleName(HWND window) {
  IAccessible *provider=nullptr;
  if(FAILED(AccessibleObjectFromWindow(window,OBJID_CLIENT,IID_IAccessible,reinterpret_cast<void **>(&provider)))||!provider)
    throw std::runtime_error("Native control has no accessible provider");
  struct Release{IAccessible *value;~Release(){value->Release();}} release{provider};
  VARIANT child{};child.vt=VT_I4;child.lVal=CHILDID_SELF;
  BSTR name=nullptr;const auto result=provider->get_accName(child,&name);
  struct Free{BSTR value;~Free(){SysFreeString(value);}} free{name};
  if(FAILED(result))throw std::runtime_error("Cannot query native control accessible name");
  return name?std::wstring(name,SysStringLen(name)):std::wstring();
}
}
