#pragma once
#include <windows.h>
#include <UIAutomation.h>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <new>
#include <string>
#include <limits>
#include <utility>

namespace ScreamSeq::PatternAccessibility {
struct Info {
  std::string key,revision,viewport;
  std::wstring name,help;
  UiaRect windowBounds{},bounds{};
  int rows=0,columns=0,row=0,column=0;
  bool visible=false,enabled=false,focused=false;
};
struct Cell {std::wstring name,value,help;UiaRect bounds{};bool offscreen=true;};
struct Callbacks {
  std::function<Info()> info;
  std::function<Cell(int,int)> cell;
  std::function<std::pair<int,int>(double,double)> hit;
  // Focus uses native cursor navigation; scroll changes only the viewport.
  std::function<HRESULT(const Info &,int,int,bool)> act;
};
struct Context {
  HWND window{};DWORD thread=GetCurrentThreadId();bool alive=true;
  Callbacks callbacks;IRawElementProviderFragmentRoot *root=nullptr;
  std::string key;int generation=0;
  Info read() {
    auto value=callbacks.info();
    if(key!=value.key) {
      if(generation==std::numeric_limits<int>::max())throw std::bad_alloc();
      key=value.key;++generation;
    }
    return value;
  }
};
enum class Kind {window,grid,cell};
// UIA marshals these STA providers to the owning UI thread (UseComThreading).
// No worker reads Application, and no provider keeps an Application reference
// after retirement. Cells are allocated on demand, never once per painted row.
class Node final : public IRawElementProviderSimple,public IRawElementProviderFragment,
  public IRawElementProviderFragmentRoot,public IGridProvider,public IGridItemProvider,
  public IValueProvider,public IScrollItemProvider {
  std::atomic<ULONG> references_{1};std::shared_ptr<Context> context_;
  Kind kind_;int row_=0,column_=0,generation_=0;Info captured_;
  template<typename F> HRESULT safe(F &&function) noexcept {
    if(GetCurrentThreadId()!=context_->thread)return RPC_E_WRONG_THREAD;
    if(!context_->alive)return UIA_E_ELEMENTNOTAVAILABLE;
    try {return function();}catch(const std::bad_alloc &){return E_OUTOFMEMORY;}catch(...){return E_FAIL;}
  }
  bool valid(const Info &info)const {
    return kind_!=Kind::cell||(captured_.key==info.key&&generation_==context_->generation&&info.visible&&
      row_>=0&&row_<info.rows&&column_>=0&&column_<info.columns);
  }
  Node *make(Kind kind,const Info &info,int row=0,int column=0){return new Node(context_,kind,info,row,column);}
  static HRESULT string(const std::wstring &text,VARIANT *value) {
    value->vt=VT_BSTR;value->bstrVal=SysAllocStringLen(text.data(),UINT(text.size()));return value->bstrVal?S_OK:E_OUTOFMEMORY;
  }
  HRESULT action(bool focus) {
    return safe([&]()->HRESULT{const auto info=context_->read();if(!valid(info)||!info.visible)return UIA_E_ELEMENTNOTAVAILABLE;
      if(!info.enabled)return UIA_E_ELEMENTNOTENABLED;
      if(kind_==Kind::cell&&captured_.revision!=info.revision)return UIA_E_ELEMENTNOTAVAILABLE;
      const auto callback=context_->callbacks.act; // A pumped native action may retire its owner.
      return callback(kind_==Kind::cell?captured_:info,kind_==Kind::cell?row_:info.row,kind_==Kind::cell?column_:info.column,focus);});
  }
public:
  Node(std::shared_ptr<Context> context,Kind kind,const Info &captured,int row=0,int column=0)
    :context_(std::move(context)),kind_(kind),row_(row),column_(column),generation_(context_->generation),captured_(captured){}
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void **result) override {
    if(!result)return E_POINTER;*result=nullptr;
    if(iid==IID_IUnknown||iid==__uuidof(IRawElementProviderSimple))*result=static_cast<IRawElementProviderSimple *>(this);
    else if(iid==__uuidof(IRawElementProviderFragment))*result=static_cast<IRawElementProviderFragment *>(this);
    else if(kind_==Kind::window&&iid==__uuidof(IRawElementProviderFragmentRoot))*result=static_cast<IRawElementProviderFragmentRoot *>(this);
    else if(kind_==Kind::grid&&iid==__uuidof(IGridProvider))*result=static_cast<IGridProvider *>(this);
    else if(kind_==Kind::cell&&iid==__uuidof(IGridItemProvider))*result=static_cast<IGridItemProvider *>(this);
    else if(kind_==Kind::cell&&iid==__uuidof(IValueProvider))*result=static_cast<IValueProvider *>(this);
    else if(kind_==Kind::cell&&iid==__uuidof(IScrollItemProvider))*result=static_cast<IScrollItemProvider *>(this);
    if(!*result)return E_NOINTERFACE;AddRef();return S_OK;
  }
  ULONG STDMETHODCALLTYPE AddRef()override{return ++references_;}
  ULONG STDMETHODCALLTYPE Release()override{const auto remaining=--references_;if(!remaining)delete this;return remaining;}
  HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions *value)override {
    if(!value)return E_POINTER;*value=ProviderOptions(ProviderOptions_ServerSideProvider|ProviderOptions_UseComThreading|ProviderOptions_ProviderOwnsSetFocus);return S_OK;
  }
  HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID id,IUnknown **value)override {
    if(!value)return E_POINTER;*value=nullptr;return safe([&]()->HRESULT{
      if(kind_==Kind::grid&&id==UIA_GridPatternId)*value=static_cast<IGridProvider *>(this);
      if(kind_==Kind::cell&&id==UIA_GridItemPatternId)*value=static_cast<IGridItemProvider *>(this);
      if(kind_==Kind::cell&&id==UIA_ValuePatternId)*value=static_cast<IValueProvider *>(this);
      if(kind_==Kind::cell&&id==UIA_ScrollItemPatternId)*value=static_cast<IScrollItemProvider *>(this);
      if(*value)AddRef();return S_OK;});
  }
  HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID id,VARIANT *value)override {
    if(!value)return E_POINTER;VariantInit(value);return safe([&]()->HRESULT{
      const auto info=context_->read();if(!valid(info))return UIA_E_ELEMENTNOTAVAILABLE;
      if(kind_==Kind::window)return S_OK; // Native HWND provider retains window/menu/chrome properties.
      if(id==UIA_ControlTypePropertyId){value->vt=VT_I4;value->lVal=kind_==Kind::grid?UIA_DataGridControlTypeId:UIA_DataItemControlTypeId;}
      else if(id==UIA_NamePropertyId)return string(kind_==Kind::grid?info.name:context_->callbacks.cell(row_,column_).name,value);
      else if(id==UIA_HelpTextPropertyId)return string(kind_==Kind::grid?info.help:context_->callbacks.cell(row_,column_).help,value);
      else if(id==UIA_AutomationIdPropertyId)return string(kind_==Kind::grid?L"pattern-grid":L"pattern-row-"+std::to_wstring(row_)+L"-column-"+std::to_wstring(column_),value);
      else if(id==UIA_IsControlElementPropertyId||id==UIA_IsContentElementPropertyId||id==UIA_IsKeyboardFocusablePropertyId){value->vt=VT_BOOL;value->boolVal=VARIANT_TRUE;}
      else if(id==UIA_IsEnabledPropertyId){value->vt=VT_BOOL;value->boolVal=info.enabled?VARIANT_TRUE:VARIANT_FALSE;}
      else if(id==UIA_IsOffscreenPropertyId){value->vt=VT_BOOL;value->boolVal=(!info.visible||(kind_==Kind::cell&&context_->callbacks.cell(row_,column_).offscreen))?VARIANT_TRUE:VARIANT_FALSE;}
      else if(id==UIA_HasKeyboardFocusPropertyId){value->vt=VT_BOOL;value->boolVal=info.focused&&kind_==Kind::cell&&row_==info.row&&column_==info.column?VARIANT_TRUE:VARIANT_FALSE;}
      return S_OK;});
  }
  HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple **value)override {
    if(!value)return E_POINTER;*value=nullptr;return safe([&]{return kind_==Kind::window?UiaHostProviderFromHwnd(context_->window,value):S_OK;});
  }
  HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection direction,IRawElementProviderFragment **value)override {
    if(!value)return E_POINTER;*value=nullptr;return safe([&]()->HRESULT{
      const auto info=context_->read();if(!valid(info))return UIA_E_ELEMENTNOTAVAILABLE;Node *next=nullptr;
      if(kind_==Kind::window&&(direction==NavigateDirection_FirstChild||direction==NavigateDirection_LastChild)&&info.visible)next=make(Kind::grid,info);
      else if(kind_==Kind::grid) {
        if(direction==NavigateDirection_Parent)return context_->root->QueryInterface(IID_PPV_ARGS(value));
        if(info.visible&&info.rows&&info.columns) {
          if(direction==NavigateDirection_FirstChild)next=make(Kind::cell,info);
          else if(direction==NavigateDirection_LastChild)next=make(Kind::cell,info,info.rows-1,info.columns-1);
        }
      }else if(kind_==Kind::cell) {
        if(direction==NavigateDirection_Parent)next=make(Kind::grid,info);
        else if(direction==NavigateDirection_NextSibling||direction==NavigateDirection_PreviousSibling) {
          const int64_t index=int64_t(row_)*info.columns+column_+(direction==NavigateDirection_NextSibling?1:-1);
          if(index>=0&&index<int64_t(info.rows)*info.columns)next=make(Kind::cell,info,int(index/info.columns),int(index%info.columns));
        }
      }
      if(next)*value=static_cast<IRawElementProviderFragment *>(next);return S_OK;});
  }
  HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY **value)override {
    if(!value)return E_POINTER;*value=nullptr;return safe([&]()->HRESULT{
      if(kind_==Kind::window)return S_OK;const auto info=context_->read();if(!valid(info))return UIA_E_ELEMENTNOTAVAILABLE;
      const int parts[]={UiaAppendRuntimeId,kind_==Kind::grid?1:2,generation_,row_,column_};
      const LONG count=kind_==Kind::grid?2:5;*value=SafeArrayCreateVector(VT_I4,0,ULONG(count));if(!*value)return E_OUTOFMEMORY;
      for(LONG i=0;i<count;++i){auto part=parts[i];const auto result=SafeArrayPutElement(*value,&i,&part);if(FAILED(result)){SafeArrayDestroy(*value);*value=nullptr;return result;}}return S_OK;});
  }
  HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect *value)override {
    if(!value)return E_POINTER;*value={};return safe([&]()->HRESULT{const auto info=context_->read();if(!valid(info))return UIA_E_ELEMENTNOTAVAILABLE;
      *value=kind_==Kind::window?info.windowBounds:kind_==Kind::grid?info.bounds:context_->callbacks.cell(row_,column_).bounds;return S_OK;});
  }
  HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY **value)override{if(!value)return E_POINTER;*value=nullptr;return safe([]{return S_OK;});}
  HRESULT STDMETHODCALLTYPE SetFocus()override {
    if(kind_!=Kind::window)return action(true);
    return safe([&]()->HRESULT{if(!context_->read().enabled)return UIA_E_ELEMENTNOTENABLED;
      ::SetFocus(context_->window);return ::GetFocus()==context_->window?S_OK:E_FAIL;});
  }
  HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot **value)override {
    if(!value)return E_POINTER;*value=nullptr;return safe([&]()->HRESULT{*value=context_->root;(*value)->AddRef();return S_OK;});
  }
  HRESULT STDMETHODCALLTYPE ElementProviderFromPoint(double x,double y,IRawElementProviderFragment **value)override {
    if(!value)return E_POINTER;*value=nullptr;return safe([&]()->HRESULT{
      const auto info=context_->read();const auto hit=context_->callbacks.hit(x,y);
      if(info.visible&&hit.first>=0&&hit.first<info.rows&&hit.second>=0&&hit.second<info.columns)*value=make(Kind::cell,info,hit.first,hit.second);
      else if(info.visible&&x>=info.bounds.left&&x<info.bounds.left+info.bounds.width&&y>=info.bounds.top&&y<info.bounds.top+info.bounds.height&&hit.first!=-2)*value=make(Kind::grid,info);
      else {*value=static_cast<IRawElementProviderFragment *>(this);AddRef();}return S_OK;});
  }
  HRESULT STDMETHODCALLTYPE GetFocus(IRawElementProviderFragment **value)override {
    if(!value)return E_POINTER;*value=nullptr;return safe([&]()->HRESULT{const auto info=context_->read();
      if(info.visible&&info.focused&&info.row>=0&&info.row<info.rows&&info.column>=0&&info.column<info.columns)*value=make(Kind::cell,info,info.row,info.column);return S_OK;});
  }
  HRESULT STDMETHODCALLTYPE GetItem(int row,int column,IRawElementProviderSimple **value)override {
    if(!value)return E_POINTER;*value=nullptr;return safe([&]()->HRESULT{const auto info=context_->read();
      if(!info.visible)return UIA_E_ELEMENTNOTAVAILABLE;if(row<0||row>=info.rows||column<0||column>=info.columns)return E_INVALIDARG;
      *value=make(Kind::cell,info,row,column);return S_OK;});
  }
  HRESULT STDMETHODCALLTYPE get_RowCount(int *value)override{if(!value)return E_POINTER;*value=0;return safe([&]{*value=context_->read().rows;return S_OK;});}
  HRESULT STDMETHODCALLTYPE get_ColumnCount(int *value)override{if(!value)return E_POINTER;*value=0;return safe([&]{*value=context_->read().columns;return S_OK;});}
  HRESULT STDMETHODCALLTYPE get_Row(int *value)override{if(!value)return E_POINTER;*value=row_;return safe([&]{return valid(context_->read())?S_OK:UIA_E_ELEMENTNOTAVAILABLE;});}
  HRESULT STDMETHODCALLTYPE get_Column(int *value)override{if(!value)return E_POINTER;*value=column_;return safe([&]{return valid(context_->read())?S_OK:UIA_E_ELEMENTNOTAVAILABLE;});}
  HRESULT STDMETHODCALLTYPE get_RowSpan(int *value)override{if(!value)return E_POINTER;*value=1;return safe([&]{return valid(context_->read())?S_OK:UIA_E_ELEMENTNOTAVAILABLE;});}
  HRESULT STDMETHODCALLTYPE get_ColumnSpan(int *value)override{return get_RowSpan(value);}
  HRESULT STDMETHODCALLTYPE get_ContainingGrid(IRawElementProviderSimple **value)override {
    if(!value)return E_POINTER;*value=nullptr;return safe([&]()->HRESULT{const auto info=context_->read();if(!valid(info))return UIA_E_ELEMENTNOTAVAILABLE;*value=make(Kind::grid,info);return S_OK;});
  }
  HRESULT STDMETHODCALLTYPE SetValue(LPCWSTR)override{return safe([]{return UIA_E_INVALIDOPERATION;});}
  HRESULT STDMETHODCALLTYPE get_Value(BSTR *value)override {
    if(!value)return E_POINTER;*value=nullptr;return safe([&]()->HRESULT{const auto info=context_->read();if(!valid(info))return UIA_E_ELEMENTNOTAVAILABLE;
      const auto text=context_->callbacks.cell(row_,column_).value;*value=SysAllocStringLen(text.data(),UINT(text.size()));return *value?S_OK:E_OUTOFMEMORY;});
  }
  HRESULT STDMETHODCALLTYPE get_IsReadOnly(BOOL *value)override{if(!value)return E_POINTER;*value=TRUE;return safe([]{return S_OK;});}
  HRESULT STDMETHODCALLTYPE ScrollIntoView()override{return action(false);}
};
class Host {
  std::shared_ptr<Context> context_;Node *root_{};Info previous_;bool notified_=false;
public:
  Host(HWND window,Callbacks callbacks):context_(std::make_shared<Context>()) {
    context_->window=window;context_->callbacks=std::move(callbacks);
    root_=new Node(context_,Kind::window,{});context_->root=static_cast<IRawElementProviderFragmentRoot *>(root_);
  }
  ~Host(){retire();}
  Host(const Host &)=delete;Host &operator=(const Host &)=delete;
  IRawElementProviderSimple *provider()const{return root_;}
  LRESULT object(WPARAM w,LPARAM l){return root_?UiaReturnRawElementProvider(context_->window,w,l,root_):0;}
  void retire() noexcept {
    if(!root_)return;context_->alive=false;context_->callbacks={};context_->root=nullptr;
    UiaDisconnectProvider(root_);root_->Release();root_=nullptr;
  }
  void notify() noexcept {
    if(!root_||!UiaClientsAreListening())return;
    auto *root=root_;root->AddRef();struct Release{Node *value;~Release(){value->Release();}} release{root};
    try {
      const auto info=context_->read();const auto previous=previous_;const bool first=!notified_;previous_=info;notified_=true;
      if(first||info.key!=previous.key||info.revision!=previous.revision||info.visible!=previous.visible)
        UiaRaiseStructureChangedEvent(root,StructureChangeType_ChildrenInvalidated,nullptr,0);
      if(!context_->alive)return;
      if(info.visible&&info.focused&&(first||!previous.focused||info.key!=previous.key||info.row!=previous.row||info.column!=previous.column||info.help!=previous.help)) {
        auto *cell=new Node(context_,Kind::cell,info,info.row,info.column);
        UiaRaiseAutomationEvent(cell,UIA_AutomationFocusChangedEventId);cell->Release();
      }
      if(context_->alive&&info.viewport!=previous.viewport)UiaRaiseAutomationEvent(root,UIA_LayoutInvalidatedEventId);
    }catch(...) {} // Accessibility notification cannot abort drawing or audio service.
  }
};
}
