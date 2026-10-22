#pragma once
#include "NativeToolWindow.hpp"
#include "NativeReportList.hpp"
#include <iomanip>
#include <sstream>

namespace ScreamSeq {
// The immutable import archive belongs to this song. This native view has no
// mutation callback: old host clocks and uncertain input cannot become a take.
class PluginRecoveryReviewWindow final:public NativeToolWindow {
public:
  using Json=Api::Json;
  using Read=std::function<Json(const std::string &,const Json &)>;
  using Context=std::function<std::pair<std::string,std::string>()>;
private:
  enum:int {heading=9780,archive,previousArchives,nextArchives,refresh,summary,events,previous,next,page,detail,statusLabel,close,archiveDetails};
  static constexpr UINT_PTR notificationSubclass=0x50525231,listSubclass=0x50525232;
  static constexpr size_t archivePageSize=64,eventPageSize=128;
  Read read_;Context context_;std::string document_,revision_,error_;
  Json entries_=Json::array(),selected_,data_=Json::object(),rows_=Json::array();
  size_t archiveOffset_=0,archiveTotal_=0,offset_=0,total_=0;
  bool pending_=false,setting_=false,archiveDetails_=false;HWND navigationFocus_{};
  std::optional<Tracker::DocumentDraft> documentDraft()const override {
    return describeDraft(document_,revision_,"recovered plugin input",0,false,pending_,false);
  }
  void requireContext()const{if(context_().first!=document_)throw std::runtime_error("This recovered-input window belongs to the previous song");}
  std::wstring cell(size_t row,unsigned column)const {
    if(row>=rows_.size())return {};const auto &event=rows_[row];
    if(column==0){if(!event.contains("frame"))return L"Unmapped";
      std::wostringstream time;time<<std::fixed<<std::setprecision(4)<<double(std::stoull(event.at("frame").get<std::string>()))/48000.<<L" s";return time.str();}
    if(column==1)return wide(event.at("value").dump());
    if(column==2)return wide(event.at("plugin").get<std::string>())+L" / "+std::to_wstring(event.at("parameter").get<uint32_t>());
    return wide(event.at("state").get<std::string>());
  }
  void clearRows() {
    rows_=Json::array();data_=Json::object();total_=0;
    NativeReportList::setItemCount(controls_.at(events),0,0);set(detail,L"");set(summary,L"");set(page,L"No input page loaded");
    InvalidateRect(controls_.at(events),nullptr,FALSE);
  }
  void selectionDetails() {
    const auto row=ListView_GetNextItem(controls_.at(events),-1,LVNI_SELECTED);
    set(detail,!archiveDetails_&&row>=0&&size_t(row)<rows_.size()?wide(rows_[size_t(row)].dump(2)):
      data_.contains("summary")?wide(data_.at("summary").dump(2)):L"Select an input to copy its exact original values and timestamps.");
  }
  void loadPage() {
    requireContext();
    if(selected_.is_null()){clearRows();return;}
    if(selected_.at("index").is_null()){clearRows();throw std::runtime_error(selected_.value("error",std::string("Unsupported recovery collection; original data retained")));}
    const auto params=Json{{"expectedRevision",revision_},{"kind",selected_.at("kind")},{"index",selected_.at("index")},{"offset",offset_},{"limit",eventPageSize}};
    try {
      auto data=read_("automation.recovery.get",params);requireContext();
      if(context_().second!=revision_||data.at("revision")!=revision_)throw std::runtime_error("Song changed during review / Refresh before selecting another input");
      const bool changed=rows_!=data.at("events")||data_.value("kind",Json())!=data.at("kind")||
        data_.value("index",Json())!=data.at("index")||data_.value("offset",Json())!=data.at("offset");
      rows_=data.at("events");total_=data.at("total").get<size_t>();data_=std::move(data);
      if(changed){
        archiveDetails_=false;
        NativeReportList::setItemCount(controls_.at(events),int(rows_.size()),0);
        NativeReportList::setItemState(controls_.at(events),-1,0,LVIS_SELECTED|LVIS_FOCUSED);
        if(!rows_.empty()){NativeReportList::setItemState(controls_.at(events),0,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);NativeReportList::ensureVisible(controls_.at(events),0,FALSE);}
        InvalidateRect(controls_.at(events),nullptr,FALSE);
      }
      selectionDetails();
      const auto &report=data_.at("summary");
      auto text=L"Original take: "+wide(report.at("take").get<std::string>())+L"\r\n";
      if(selected_.at("kind")=="nativeControls")text+=L"Accepted: "+wide(report.at("accepted").get<std::string>())+L" · Lost: "+wide(report.at("lost").get<std::string>());
      else text+=L"Captured input: "+std::to_wstring(total_)+L" · Rejected: "+wide(report.at("rejected").get<std::string>())+L" · "+wide(report.at("failure").get<std::string>());
      set(summary,text);set(page,rows_.empty()?L"No recorded inputs":std::to_wstring(offset_+1)+L"–"+std::to_wstring(offset_+rows_.size())+L" of "+std::to_wstring(total_));
      error_.clear();set(statusLabel,L"Read only · no input is replayed. Exact original data is selectable below the list.");
    }catch(...){clearRows();throw;}
  }
  void refreshCatalogue(bool preserveSelection) {
    requireContext();const auto current=context_();
    const auto old=preserveSelection?selected_:Json();
    auto result=read_("automation.recovery.list",{{"offset",archiveOffset_},{"limit",archivePageSize}});requireContext();
    if(context_()!=current||result.at("revision")!=current.second)throw std::runtime_error("Song changed while reading recovered takes / Refresh again");
    revision_=current.second;entries_=result.at("entries");archiveTotal_=result.at("total").get<size_t>();
    setting_=true;struct Reset{bool &value;~Reset(){value=false;}}reset{setting_};
    NativeInputGate::present(controls_.at(archive),CB_RESETCONTENT,0,0);
    int choice=-1;
    for(size_t i=0;i<entries_.size();++i){const auto &entry=entries_[i];
      const auto label=(entry.at("kind")=="gestures"?L"Plugin gestures · ":L"Native controls · ")+wide(entry.value("take",std::string("Unsupported archive")));
      NativeInputGate::present(controls_.at(archive),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));
      if(!old.is_null()&&entry.at("kind")==old.at("kind")&&entry.at("index")==old.at("index"))choice=int(i);
    }
    if(choice<0&&!entries_.empty())choice=0;
    NativeInputGate::present(controls_.at(archive),CB_SETCURSEL,choice,0);
    selected_=choice>=0?entries_[size_t(choice)]:Json();
    if(!preserveSelection||selected_!=old)offset_=0;
    loadPage();if(entries_.empty()){error_.clear();set(statusLabel,L"This song contains no imported plugin input archives.");}
  }
  template<class Work>void reading(Work work) {
    if(pending_)return;pending_=true;
    struct Reset{PluginRecoveryReviewWindow &self;~Reset(){self.pending_=false;self.layout();}}reset{*this};
    // Freeze this window's native fields while the worker pumps other messages.
    // Keep the focused HWND enabled and never recreate the report control.
    const HWND roots[]={window_};NativeInputGate input(roots);work();
  }
  void action(int id,unsigned notification)override {
    if(setting_||pending_)return;if(id==close){hide();return;}
    if(id==archiveDetails&&notification==BN_CLICKED){archiveDetails_=true;selectionDetails();SetFocus(controls_.at(detail));return;}
    if(id==archive&&notification==CBN_SELCHANGE){reading([&]{
      const auto chosen=SendMessageW(controls_.at(archive),CB_GETCURSEL,0,0);
      if(chosen<0||size_t(chosen)>=entries_.size())return;selected_=entries_[size_t(chosen)];offset_=0;loadPage();});return;}
    if(notification!=BN_CLICKED)return;
    reading([&]{
      if(id==refresh){refreshCatalogue(true);return;}
      if(id==previousArchives||id==nextArchives){
        if(id==previousArchives)archiveOffset_=archiveOffset_>=archivePageSize?archiveOffset_-archivePageSize:0;
        else if(archiveOffset_+entries_.size()<archiveTotal_)archiveOffset_+=archivePageSize;else return;
        refreshCatalogue(false);return;
      }
      if(id==previous||id==next){
        if(id==previous)offset_=offset_>=eventPageSize?offset_-eventPageSize:0;
        else if(offset_+rows_.size()<total_)offset_+=eventPageSize;else return;
        loadPage();
      }
    });
  }
  bool key(WPARAM key,bool ctrl,bool)override {
    if(key==VK_ESCAPE){hide();return true;}if(ctrl&&key=='R'){action(refresh,BN_CLICKED);return true;}
    if(ctrl&&key=='A'&&GetFocus()==controls_.at(detail)){NativeInputGate::present(controls_.at(detail),EM_SETSEL,0,-1);return true;}
    if(key==VK_RETURN){if(GetFocus()==controls_.at(events)){archiveDetails_=false;selectionDetails();return true;}
      wchar_t kind[20]{};GetClassNameW(GetFocus(),kind,20);if(!_wcsicmp(kind,L"Button")){action(GetDlgCtrlID(GetFocus()),BN_CLICKED);return true;}}return false;
  }
  void error(const std::exception &error)override{error_=error.what();set(statusLabel,wide(error_));layout();}
  void layout()override {
    if(!ready_)return;const auto [w,h]=size();place(heading,12,10,w-358,26);place(archiveDetails,w-342,10,160,26);place(refresh,w-174,10,162,26);
    place(archive,12,46,w-204,240);place(previousArchives,w-184,46,82,26);place(nextArchives,w-94,46,82,26);
    place(summary,12,84,w-24,60);place(events,12,150,w-24,std::max(100.f,h-340));
    place(detail,12,h-178,w-24,92);place(previous,12,h-72,84,26);place(next,104,h-72,84,26);place(page,200,h-70,w-300,26);place(close,w-88,h-72,76,26);
    place(statusLabel,12,h-36,w-24,30);
    EnableWindow(controls_.at(previousArchives),!pending_&&archiveOffset_>0);EnableWindow(controls_.at(nextArchives),!pending_&&archiveOffset_+entries_.size()<archiveTotal_);
    EnableWindow(controls_.at(previous),!pending_&&offset_>0&&!data_.empty());EnableWindow(controls_.at(next),!pending_&&offset_+rows_.size()<total_);
  }
  LRESULT notify(NMHDR *header) {
    if(header->hwndFrom!=controls_.at(events))return 0;
    if(header->code==LVN_GETDISPINFOW){auto &info=*reinterpret_cast<NMLVDISPINFOW *>(header);if((info.item.mask&LVIF_TEXT)&&info.item.pszText&&info.item.cchTextMax>0){const auto text=info.item.iItem>=0?cell(size_t(info.item.iItem),unsigned(info.item.iSubItem)):std::wstring();lstrcpynW(info.item.pszText,text.c_str(),info.item.cchTextMax);}return 0;}
    if(header->code==LVN_ITEMCHANGED||header->code==NM_CLICK){if(!pending_)archiveDetails_=false;selectionDetails();return 0;}
    if(header->code==NM_CUSTOMDRAW)return NativeReportList::customDraw(*reinterpret_cast<NMLVCUSTOMDRAW *>(header),[this](size_t row,unsigned column){return cell(row,column);});return 0;
  }
  static LRESULT CALLBACK notifications(HWND window,UINT message,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR data) {
    auto &self=*reinterpret_cast<PluginRecoveryReviewWindow *>(data);
    if(message==WM_NCDESTROY)RemoveWindowSubclass(window,notifications,id);
    if(self.ready_&&NativeReportList::themeMessage(message))NativeReportList::refresh(self.controls_.at(events));
    if(message==WM_NOTIFY&&self.ready_)try{return self.notify(reinterpret_cast<NMHDR *>(l));}catch(const std::exception &error){self.error(error);return 0;}
    return DefSubclassProc(window,message,w,l);
  }
  static LRESULT CALLBACK listNotifications(HWND window,UINT message,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR) {
    if(message==WM_NCDESTROY)RemoveWindowSubclass(window,listNotifications,id);
    if(message==WM_NOTIFY){const auto header=reinterpret_cast<NMHDR *>(l);if(header->hwndFrom==ListView_GetHeader(window)&&header->code==NM_CUSTOMDRAW)return NativeReportList::headerDraw(*reinterpret_cast<NMCUSTOMDRAW *>(header));}
    return DefSubclassProc(window,message,w,l);
  }
public:
  PluginRecoveryReviewWindow(HWND owner,Read read,Context context):NativeToolWindow(owner),read_(std::move(read)),context_(std::move(context)) {
    const auto initial=context_();document_=initial.first;revision_=initial.second;minimumClientWidth_=700;minimumClientHeight_=520;
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_LISTVIEW_CLASSES};check(InitCommonControlsEx(&common),"Initialize recovery input report");
    create(L"ScreamSeq.PluginRecoveryReview",L"Recovered plugin inputs — read only",900,700);combo(archive);
    label(heading,L"RECOVERED PLUGIN INPUTS");for(int id:{summary,page,statusLabel})label(id,L"");
    for(auto [id,text]:std::initializer_list<std::pair<int,const wchar_t *>>{{previousArchives,L"< Takes"},{nextArchives,L"Takes >"},{refresh,L"Refresh (Ctrl+R)"},{previous,L"< Inputs"},{next,L"Inputs >"},{close,L"Close"},{archiveDetails,L"Archive details"}})button(id,text);
    add(detail,L"EDIT",L"",ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL);SendMessageW(controls_.at(detail),EM_SETLIMITTEXT,262144,0);
    accessibleName(archive,L"Recovered input archive");accessibleName(detail,L"Exact original input data, read only");
    const auto list=add(events,WC_LISTVIEWW,L"Recovered original inputs",LVS_REPORT|LVS_OWNERDATA|LVS_SINGLESEL|LVS_SHOWSELALWAYS|WS_BORDER);
    ListView_SetExtendedListViewStyle(list,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);NativeReportList::install(list);
    if(const auto theme=LoadLibraryW(L"uxtheme.dll")){using Theme=HRESULT(WINAPI *)(HWND,LPCWSTR,LPCWSTR);if(const auto setTheme=reinterpret_cast<Theme>(GetProcAddress(theme,"SetWindowTheme")))setTheme(list,L"",L"");FreeLibrary(theme);}
    const wchar_t *names[]={L"Song time",L"Value",L"Original target",L"Delivery"};
    for(int i=0;i<4;++i){LVCOLUMNW column{};column.mask=LVCF_TEXT|LVCF_WIDTH;column.pszText=const_cast<wchar_t *>(names[i]);column.cx=MulDiv(i==2?320:140,GetDpiForWindow(list),96);ListView_InsertColumn(list,i,&column);}
    check(SetWindowSubclass(window_,notifications,notificationSubclass,reinterpret_cast<DWORD_PTR>(this))&&SetWindowSubclass(list,listNotifications,listSubclass,0),"Install recovery review notifications");
    for(int id:{archive,previousArchives,nextArchives,archiveDetails,refresh,events,detail,previous,next,close})check(SetWindowPos(controls_.at(id),HWND_BOTTOM,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE),"Order recovery review controls");
    focusPresentationChanged([this]{if(owns(GetFocus())&&GetFocus()!=window_)navigationFocus_=GetFocus();});
    finish();try{reading([&]{refreshCatalogue(false);});}catch(const std::exception &failure){error(failure);}
  }
  ~PluginRecoveryReviewWindow()override{if(window_)RemoveWindowSubclass(window_,notifications,notificationSubclass);if(controls_.contains(events))RemoveWindowSubclass(controls_.at(events),listNotifications,listSubclass);}
  void show(){const auto focus=navigationFocus_;NativeToolWindow::show();SetFocus(focus&&owns(focus)&&IsWindowVisible(focus)&&IsWindowEnabled(focus)?focus:controls_.at(archive));}
  Json snapshot()const{return {{"document",document_},{"revision",revision_},{"visible",visible()},{"pending",pending_},{"archiveOffset",archiveOffset_},
    {"archiveTotal",archiveTotal_},{"entries",entries_},{"selected",selected_},{"offset",offset_},{"total",total_},{"data",data_},{"events",rows_},{"error",error_}};}
};
}
