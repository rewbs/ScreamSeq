#pragma once
#include "NativeToolWindow.hpp"
#include "NativeReportList.hpp"
#include <array>
#include <set>

namespace ScreamSeq {
// UI-thread presentation only. The application owns capture, storage and restore.
class RecoveryWindow final : public NativeToolWindow {
public:
  using Json=Api::Json;
private:
  enum:int {copies=7001,reload,save,restore,close,titleDetail,sourceDetail,
    heading=7100,explanation,titleLabel,sourceLabel,statusLabel};
  enum class Operation {none,reload,save,restore};
  struct Entry {std::string id;std::array<std::wstring,3> cells;std::wstring title,source;};
  std::function<void()> reload_,save_;
  std::function<void(std::string)> restore_;
  Json copies_=Json::array(),autosave_=Json::object();
  std::vector<Entry> entries_;
  std::string selected_,restoring_,error_;
  Operation operation_=Operation::none;
  bool setting_=false,loaded_=false,externalBusy_=false,resizingColumns_=false;
  HWND pendingFocus_{};
  HIMAGELIST rowHeight_{};
  std::array<float,3> columnWidths_{170,210,238};
  float listWidth_=-1;UINT columnDpi_=0;
  static constexpr UINT_PTR notificationSubclass=0x52435731,listSubclass=0x52435732;

  const char *operationName()const{
    switch(operation_){case Operation::reload:return "reload";case Operation::save:return "save";case Operation::restore:return "restore";default:return "";}
  }
  bool unavailable()const{return externalBusy_||operation_!=Operation::none||autosave_.value("saving",false);}
  bool restoreReady()const{return !unavailable()&&!selected_.empty();}
  int indexOf(const std::string &id)const{
    for(size_t i=0;i<entries_.size();++i)if(entries_[i].id==id)return int(i);return -1;
  }
  static std::string textValue(const Json &value,const char *key,size_t limit,bool optional=false){
    const auto found=value.find(key);
    if(optional&&(found==value.end()||found->is_null()))return {};
    if(found==value.end()||!found->is_string())throw std::runtime_error("Invalid recovery copy information");
    const auto result=found->get<std::string>();
    if(result.size()>limit||result.find('\0')!=std::string::npos)throw std::runtime_error("Recovery copy information is too long");return result;
  }
  static std::wstring localDate(const std::string &value){
    const auto raw=wide(value);
    if(value.size()<20||value[4]!='-'||value[7]!='-'||value[10]!='T'||value[13]!=':'||value[16]!=':'||value.back()!='Z')return raw;
    auto part=[&](size_t start,size_t count){WORD n=0;for(size_t i=start;i<start+count;++i){if(value[i]<'0'||value[i]>'9')return WORD(0);n=WORD(n*10+value[i]-'0');}return n;};
    SYSTEMTIME utc{};utc.wYear=part(0,4);utc.wMonth=part(5,2);utc.wDay=part(8,2);utc.wHour=part(11,2);utc.wMinute=part(14,2);utc.wSecond=part(17,2);
    SYSTEMTIME local{};FILETIME valid{};
    if(!SystemTimeToFileTime(&utc,&valid)||!SystemTimeToTzSpecificLocalTime(nullptr,&utc,&local))return raw;
    wchar_t date[128]{},time[128]{};
    if(!GetDateFormatEx(LOCALE_NAME_USER_DEFAULT,DATE_SHORTDATE,&local,nullptr,date,128,nullptr)||!GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT,0,&local,nullptr,time,128))return raw;
    return std::wstring(date)+L"  "+time;
  }
  void detailText(int id,const std::wstring &value){
    const auto control=controls_.at(id);DWORD first=0,last=0;
    SendMessageW(control,EM_GETSEL,reinterpret_cast<WPARAM>(&first),reinterpret_cast<LPARAM>(&last));
    set(id,value);
    ScreamSeq::NativeInputGate::present(control,EM_SETSEL,std::min<size_t>(first,value.size()),std::min<size_t>(last,value.size()));
  }
  void details(){
    const auto index=indexOf(selected_);
    detailText(titleDetail,index>=0?entries_[size_t(index)].title:L"");
    detailText(sourceDetail,index>=0?entries_[size_t(index)].source:L"");
  }
  void statusText(){
    if(!error_.empty())status_=wide(error_);
    else if(autosave_.contains("error")&&autosave_["error"].is_string()&&!autosave_["error"].get_ref<const std::string &>().empty())status_=L"Autosave failed: "+wide(autosave_["error"].get<std::string>());
    else if(operation_==Operation::restore)status_=L"Protecting the current song and restoring the selected copy…";
    else if(operation_==Operation::reload)status_=L"Loading recovery copies…";
    else if(operation_==Operation::save||autosave_.value("saving",false))status_=L"Saving a recovery copy…";
    else if(entries_.empty())status_=L"No recovery copies yet. Changes are protected automatically when autosave is enabled.";
    else if(selected_.empty())status_=L"Choose a copy to restore. If a copy cannot be opened, select an older one.";
    else if(autosave_.contains("lastSavedAt")&&autosave_["lastSavedAt"].is_string())status_=L"Last autosave: "+localDate(autosave_["lastSavedAt"].get<std::string>())+L". Select an older copy if needed.";
    else status_=std::to_wstring(entries_.size())+L" recovery copies. Restore opens the selected song unsaved, ready for Save As.";
    set(statusLabel,status_);requestPaint();
  }
  void error(const std::exception &value)override{fail(value.what());}
  void begin(Operation operation){
    if(unavailable()||(operation==Operation::restore&&selected_.empty())||(operation==Operation::save&&!autosave_.value("enabled",false)))return;
    operation_=operation;error_.clear();if(operation==Operation::restore)restoring_=selected_;
    const auto target=restoring_;statusText();layout();
    try{if(operation==Operation::reload)reload_();else if(operation==Operation::save)save_();else restore_(target);}
    catch(const std::exception &value){if(operation==Operation::restore)completeRestore(target,false,value.what());else fail(value.what());}
  }
  void action(int id,unsigned notification)override{
    if(setting_||notification!=BN_CLICKED)return;
    if(id==close){hide();return;}
    if(id==reload)begin(Operation::reload);else if(id==save)begin(Operation::save);else if(id==restore)begin(Operation::restore);
  }
  bool key(WPARAM value,bool ctrl,bool shift)override{
    if(value==VK_ESCAPE){hide();return true;}
    if(ctrl&&value=='R'){begin(Operation::reload);return true;}
    if(value==VK_F5&&!ctrl){begin(Operation::reload);return true;}
    if(value==VK_TAB&&GetFocus()==window_){const auto next=GetNextDlgTabItem(window_,nullptr,shift);if(next)SetFocus(next);return true;}
    if(value==VK_RETURN&&!ctrl){
      const auto focus=GetFocus();if(focus==controls_.at(copies)||focus==window_){begin(Operation::restore);return true;}
      for(const int id:{reload,save,restore,close})if(focus==controls_.at(id)){action(id,BN_CLICKED);return true;}
    }
    return false;
  }
  void resizeColumns(){
    const auto list=controls_.at(copies);RECT bounds{};GetClientRect(list,&bounds);
    const auto dpi=GetDpiForWindow(window_);const float width=bounds.right*96.f/dpi;
    if(width==listWidth_&&dpi==columnDpi_)return;
    if(listWidth_>0)columnWidths_[2]=std::max(140.f,columnWidths_[2]+width-listWidth_);
    else columnWidths_[2]=std::max(140.f,width-columnWidths_[0]-columnWidths_[1]);
    listWidth_=width;columnDpi_=dpi;resizingColumns_=true;
    for(int i=0;i<3;++i)NativeReportList::setColumnWidth(list,i,int(std::lround(columnWidths_[size_t(i)]*dpi/96.f)));
    resizingColumns_=false;
  }
  void layout()override{
    if(!ready_)return;const auto [w,h]=size();const auto focus=GetFocus();
    place(heading,16,12,w-32,26);place(explanation,16,48,w-32,38);
    place(copies,16,94,w-32,std::max(100.f,h-266));
    place(titleLabel,16,h-157,60,24);place(titleDetail,84,h-160,w-100,26);
    place(sourceLabel,16,h-125,64,24);place(sourceDetail,84,h-128,w-100,26);
    place(statusLabel,16,h-94,w-32,40);
    place(reload,16,h-46,84,30);place(save,108,h-46,154,30);
    place(restore,w-284,h-46,184,30);place(close,w-92,h-46,76,30);
    resizeColumns();
    EnableWindow(controls_.at(reload),!unavailable());
    EnableWindow(controls_.at(save),!unavailable()&&autosave_.value("enabled",false));
    EnableWindow(controls_.at(restore),restoreReady());
    // Keep the report and selectable details active during background work.
    // Only restore a button's focus if disabling it caused that focus to vanish.
    if(unavailable()&&focus&&IsChild(window_,focus)&&!GetFocus())pendingFocus_=focus;
    if(!unavailable()&&pendingFocus_){const auto old=pendingFocus_;pendingFocus_=nullptr;if(!GetFocus()&&IsWindowVisible(old)&&IsWindowEnabled(old)&&GetActiveWindow()==GetAncestor(window_,GA_ROOT))SetFocus(old);}
  }
  void paint(RenderSurface &surface)override{const auto [w,h]=size();surface.fill(0,0,w,h,0x18222d);}
  void fontsChanged()override{
    if(!controls_.contains(copies))return;
    const auto dpi=GetDpiForWindow(window_);
    const auto replacement=ImageList_Create(1,std::max(1,MulDiv(27,dpi,96)),ILC_COLOR32,1,1);
    if(replacement){ListView_SetImageList(controls_.at(copies),replacement,LVSIL_SMALL);if(rowHeight_)ImageList_Destroy(rowHeight_);rowHeight_=replacement;}
    columnDpi_=0;
  }
  LRESULT notify(NMHDR *header){
    const auto list=controls_.at(copies);if(header->hwndFrom!=list)return 0;
    if(header->code==LVN_GETDISPINFOW){
      auto &info=*reinterpret_cast<NMLVDISPINFOW *>(header);
      if((info.item.mask&LVIF_TEXT)&&info.item.pszText&&info.item.cchTextMax>0){
        const auto valid=info.item.iItem>=0&&size_t(info.item.iItem)<entries_.size()&&info.item.iSubItem>=0&&info.item.iSubItem<3;
        const auto *value=valid?entries_[size_t(info.item.iItem)].cells[size_t(info.item.iSubItem)].c_str():L"";
        lstrcpynW(info.item.pszText,value,info.item.cchTextMax);
      }return 0;
    }
    if(header->code==LVN_ITEMCHANGING&&operation_==Operation::restore&&!setting_){const auto &change=*reinterpret_cast<NMLISTVIEW *>(header);if(change.uChanged&LVIF_STATE)return TRUE;}
    if(header->code==LVN_ITEMCHANGED&&!setting_){
      const auto index=ListView_GetNextItem(list,-1,LVNI_SELECTED);const auto next=index>=0&&size_t(index)<entries_.size()?entries_[size_t(index)].id:std::string();
      if(next!=selected_){selected_=next;details();statusText();layout();}return 0;
    }
    if(header->code==NM_DBLCLK&&!setting_){const auto row=reinterpret_cast<NMITEMACTIVATE *>(header)->iItem;if(row>=0&&size_t(row)<entries_.size()&&entries_[size_t(row)].id==selected_)begin(Operation::restore);return 0;}
    if(header->code==LVN_ODFINDITEMW){
      const auto &find=*reinterpret_cast<NMLVFINDITEMW *>(header);
      if(!(find.lvfi.flags&LVFI_STRING)||!find.lvfi.psz||entries_.empty())return -1;
      const auto query=std::wstring_view(find.lvfi.psz);const auto start=size_t(std::max(0,find.iStart));
      for(size_t i=0;i<entries_.size();++i){const auto row=(start+i)%entries_.size();const auto &title=entries_[row].title;if(title.size()>=query.size()&&CompareStringOrdinal(title.data(),int(query.size()),query.data(),int(query.size()),TRUE)==CSTR_EQUAL)return LRESULT(row);}return -1;
    }
    if(header->code==NM_CUSTOMDRAW)return NativeReportList::customDraw(*reinterpret_cast<NMLVCUSTOMDRAW *>(header),[this](size_t row,unsigned column){return row<entries_.size()&&column<entries_[row].cells.size()?std::wstring_view(entries_[row].cells[column]):std::wstring_view();});
    return 0;
  }
  static LRESULT CALLBACK notifications(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR data){
    auto &self=*reinterpret_cast<RecoveryWindow *>(data);
    if(m==WM_NCDESTROY)RemoveWindowSubclass(h,notifications,id);
    if(self.ready_&&NativeReportList::themeMessage(m))NativeReportList::refresh(self.controls_.at(copies));
    if(m==WM_NOTIFY&&self.ready_)try{return self.notify(reinterpret_cast<NMHDR *>(l));}catch(const std::exception &error){self.error(error);return 0;}
    return DefSubclassProc(h,m,w,l);
  }
  static LRESULT CALLBACK listNotifications(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR data){
    auto &self=*reinterpret_cast<RecoveryWindow *>(data);
    if(m==WM_NCDESTROY)RemoveWindowSubclass(h,listNotifications,id);
    if(m==WM_NOTIFY){const auto header=reinterpret_cast<NMHDR *>(l);
      if(header->hwndFrom==ListView_GetHeader(h)){
        if((header->code==HDN_ENDTRACKW||header->code==HDN_ENDTRACKA)&&!self.resizingColumns_){
          for(int i=0;i<3;++i)self.columnWidths_[size_t(i)]=ListView_GetColumnWidth(h,i)*96.f/GetDpiForWindow(h);
          const auto &changed=*reinterpret_cast<NMHEADERW *>(header);
          if(changed.iItem>=0&&changed.iItem<3&&changed.pitem&&(changed.pitem->mask&HDI_WIDTH))self.columnWidths_[size_t(changed.iItem)]=changed.pitem->cxy*96.f/GetDpiForWindow(h);
        }
        if(header->code==NM_CUSTOMDRAW)return NativeReportList::headerDraw(*reinterpret_cast<NMCUSTOMDRAW *>(header));
      }
    }
    return DefSubclassProc(h,m,w,l);
  }
public:
  RecoveryWindow(HWND owner,std::function<void()> reloadCallback,std::function<void()> saveCallback,std::function<void(std::string)> restoreCallback)
    :NativeToolWindow(owner),reload_(std::move(reloadCallback)),save_(std::move(saveCallback)),restore_(std::move(restoreCallback)){
    minimumClientWidth_=650;minimumClientHeight_=430;
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_LISTVIEW_CLASSES};if(!InitCommonControlsEx(&common))throw std::runtime_error("Cannot initialize recovery list");
    create(L"ScreamSeq.Recovery",L"Recover a song",780,550);
    const auto list=add(copies,WC_LISTVIEWW,L"Recovery copies, newest first",LVS_REPORT|LVS_OWNERDATA|LVS_SINGLESEL|LVS_SHOWSELALWAYS|WS_BORDER);
    ListView_SetExtendedListViewStyle(list,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);
    // Classic report drawing honors the application's surface colors. Native
    // list/header hit testing, accessibility and column resizing remain intact.
    if(const auto theme=LoadLibraryW(L"uxtheme.dll")){using Theme=HRESULT(WINAPI *)(HWND,LPCWSTR,LPCWSTR);if(const auto setTheme=reinterpret_cast<Theme>(GetProcAddress(theme,"SetWindowTheme")))setTheme(list,L"",L"");FreeLibrary(theme);}
    NativeReportList::install(list);
    int index=0;for(const auto *name:{L"Saved",L"Song",L"Original file"}){LVCOLUMNW column{};column.mask=LVCF_TEXT|LVCF_WIDTH;column.pszText=const_cast<wchar_t *>(name);column.cx=100;ListView_InsertColumn(list,index++,&column);}
    add(titleDetail,L"EDIT",L"",ES_READONLY|ES_AUTOHSCROLL);add(sourceDetail,L"EDIT",L"",ES_READONLY|ES_AUTOHSCROLL);
    for(const auto [id,value]:std::initializer_list<std::pair<int,const wchar_t *>>{{reload,L"Reload"},{save,L"Save recovery copy"},{restore,L"Restore selected"},{close,L"Close"}})button(id,value);
    for(const auto [id,value]:std::initializer_list<std::pair<int,const wchar_t *>>{{heading,L"RECOVER A SONG"},{explanation,L"Up to ten copies are kept per session. Restoring first protects your current unsaved song.\nThe recovered song opens unsaved; use Save As to keep it."},{titleLabel,L"Song"},{sourceLabel,L"Source"},{statusLabel,L""}})label(id,value);
    if(!SetWindowSubclass(window_,notifications,notificationSubclass,reinterpret_cast<DWORD_PTR>(this))||!SetWindowSubclass(list,listNotifications,listSubclass,reinterpret_cast<DWORD_PTR>(this)))throw std::runtime_error("Cannot initialize recovery notifications");
    finish();statusText();
  }
  ~RecoveryWindow()override{
    if(window_)RemoveWindowSubclass(window_,notifications,notificationSubclass);
    if(controls_.contains(copies)){const auto list=controls_.at(copies);RemoveWindowSubclass(list,listNotifications,listSubclass);ListView_SetImageList(list,nullptr,LVSIL_SMALL);}
    if(rowHeight_)ImageList_Destroy(rowHeight_);
  }
  void show(){const auto focus=GetFocus();const bool existing=visible();NativeToolWindow::show();if(existing&&owns(focus))SetFocus(focus);else SetFocus(controls_.at(copies));}
  void update(const Json &copiesValue,const Json &autosave,std::string error={}){
    if(!copiesValue.is_array()||copiesValue.size()>10000||!autosave.is_object())throw std::runtime_error("Invalid recovery list");
    std::vector<Entry> next;std::set<std::string> identities;
    for(const auto &value:copiesValue){
      Entry entry;entry.id=textValue(value,"id",200);if(entry.id.empty()||!identities.insert(entry.id).second)throw std::runtime_error("Invalid recovery copy identity");
      entry.title=wide(textValue(value,"title",16384));if(entry.title.empty())entry.title=L"Untitled";
      const auto source=textValue(value,"source",131072,true);entry.source=source.empty()?L"Unsaved song":wide(source);
      const auto date=textValue(value,"savedAt",128);if(value.contains("hasRecording")&&!value["hasRecording"].is_boolean())throw std::runtime_error("Invalid recovery recording information");
      auto filename=entry.source;const auto slash=filename.find_last_of(L"/\\");if(slash!=std::wstring::npos)filename.erase(0,slash+1);
      entry.cells={localDate(date),(value.value("hasRecording",false)?L"Recording · ":L"")+entry.title,std::move(filename)};next.push_back(std::move(entry));
    }
    const auto list=controls_.at(copies);const auto oldTop=ListView_GetTopIndex(list);const auto topID=oldTop>=0&&size_t(oldTop)<entries_.size()?entries_[size_t(oldTop)].id:std::string();
    auto copied=copiesValue,settings=autosave;copies_=std::move(copied);autosave_=std::move(settings);entries_=std::move(next);error_=std::move(error);
    if(!loaded_&&selected_.empty()&&!entries_.empty())selected_=entries_.front().id;
    else if(indexOf(selected_)<0)selected_.clear();loaded_=loaded_||!entries_.empty();
    setting_=true;SendMessageW(list,WM_SETREDRAW,FALSE,0);
    NativeReportList::setItemCount(list,int(entries_.size()),LVSICF_NOINVALIDATEALL|LVSICF_NOSCROLL);NativeReportList::setItemState(list,-1,0,LVIS_SELECTED|LVIS_FOCUSED);
    const auto selected=indexOf(selected_);if(selected>=0)NativeReportList::setItemState(list,selected,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
    const auto top=indexOf(topID);if(top>=0){RECT row{};if(ListView_GetItemRect(list,top,&row,LVIR_BOUNDS))NativeReportList::scroll(list,0,(top-ListView_GetTopIndex(list))*(row.bottom-row.top));}
    SendMessageW(list,WM_SETREDRAW,TRUE,0);InvalidateRect(list,nullptr,FALSE);setting_=false;
    if(operation_!=Operation::restore)operation_=Operation::none;
    details();statusText();layout();
  }
  void updateStatus(const Json &autosave){if(!autosave.is_object())throw std::runtime_error("Invalid recovery status");autosave_=autosave;statusText();layout();}
  void setBusy(bool busy){externalBusy_=busy;layout();}
  void completeRestore(const std::string &id,bool success,std::string error={}){
    if(operation_!=Operation::restore||restoring_!=id)return;
    operation_=Operation::none;restoring_.clear();error_=std::move(error);statusText();layout();if(success)hide();
  }
  void fail(std::string error){if(operation_!=Operation::restore)operation_=Operation::none;error_=std::move(error);statusText();layout();}
  Json snapshot()const{return {{"visible",visible()},{"pending",unavailable()},{"operation",operationName()},{"selected",selected_},{"restoring",restoring_},{"copies",copies_},{"autosave",autosave_},{"status",utf8(status_)},{"error",error_},{"restoreEnabled",restoreReady()}};}
};
}
