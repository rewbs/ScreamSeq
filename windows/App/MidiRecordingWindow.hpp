#pragma once
#include "NativeToolWindow.hpp"
#include "NativeReportList.hpp"
#include <array>
#include <set>

namespace ScreamSeq {
// Retained presentation only. The host owns MIDI connections and the take.
class MidiRecordingWindow final : public NativeToolWindow {
public:
  using Json=Api::Json;
  struct Callbacks {
    std::function<void()> rescan,chooseSound;
    std::function<Json(Json)> applySettings;
    std::function<void(std::string)> finish,discard,review;
    std::function<void(std::string,Json)> navigate;
  };
private:
  enum:int {source=7201,rescan,arm,sound,chooseSound,columns,quantum,latency,apply,revert,
    events,finishTake,discardTake,refreshReview,showEvent,close,
    heading=7300,help,sourceLabel,connection,soundLabel,columnsLabel,quantumLabel,latencyLabel,
    latencyUnits,draftStatus,takeStatus,eventDetail,statusLabel};
  enum class Operation {none,rescan,apply,finish,discard,review,navigate,sound};
  inline static constexpr std::array<const wchar_t *,6> headings{L"Pattern",L"Track",L"Row + offset",L"Note",L"Sound",L"Velocity"};
  Callbacks callbacks_;
  Json state_=Json::object(),settings_=Json::object(),devices_=Json::array(),recording_=Json::object(),events_=Json::array();
  std::string baseRevision_,draftSource_,reviewTake_,selectedKey_,error_,actionTake_;
  std::vector<std::string> sourceIDs_;
  unsigned draftQuantum_=0,selectedOccurrence_=0;
  uint64_t draftGeneration_=0;
  bool draftArmed_=false,dirty_=false,loaded_=false,setting_=false,externalBusy_=false,sourceRefresh_=false,resizingColumns_=false;
  Operation operation_=Operation::none;
  HWND pendingFocus_{};
  int selected_=-1;
  HIMAGELIST rowHeight_{};
  std::array<float,6> columnWidths_{120,92,150,88,124,86};
  float listWidth_=-1;UINT columnDpi_=0;
  inline static constexpr UINT_PTR notificationSubclass=0x4d525731,listSubclass=0x4d525732;

  static void require(bool condition,const char *message){if(!condition)throw std::runtime_error(message);}
  static std::string string(const Json &value,const char *key,size_t limit,bool optional=false){
    const auto it=value.find(key);if(optional&&(it==value.end()||it->is_null()))return {};
    require(it!=value.end()&&it->is_string(),"Invalid MIDI recording information");auto result=it->get<std::string>();
    require(result.size()<=limit&&result.find('\0')==std::string::npos,"MIDI recording information is too long");return result;
  }
  static unsigned integer(const Json &value,const char *key,unsigned low,unsigned high){
    const auto it=value.find(key);require(it!=value.end()&&it->is_number_integer()&&!it->is_boolean()&&*it>=low&&*it<=high,"Invalid MIDI recording number");return it->get<unsigned>();
  }
  static void validateSettings(const Json &value){
    require(value.is_object(),"Invalid MIDI settings");require(!string(value,"revision",200).empty(),"MIDI settings revision is missing");string(value,"source",4096);
    require(value.contains("armed")&&value["armed"].is_boolean(),"Invalid MIDI arm setting");integer(value,"channelsCount",1,127);integer(value,"quantization",0,65536);
    require(value.contains("latencyMS")&&value["latencyMS"].is_number()&&!value["latencyMS"].is_boolean(),"Invalid MIDI timing adjustment");const auto adjustment=value["latencyMS"].get<double>();
    require(std::isfinite(adjustment)&&adjustment>=-500&&adjustment<=500,"Input adjustment must be -500 to +500 ms");
    if(value.contains("connected"))require(value["connected"].is_boolean(),"Invalid MIDI connection state");string(value,"error",16384,true);
  }
  static bool sameSettings(const Json &a,const Json &b){for(const auto *key:{"revision","source","armed","channelsCount","quantization","latencyMS"})if(!a.contains(key)||!b.contains(key)||a[key]!=b[key])return false;return true;}
  static void validateDevices(const Json &value){
    require(value.is_array()&&value.size()<=1024,"Invalid MIDI source list");std::set<std::string> ids;
    for(const auto &device:value){const auto id=string(device,"id",4096);require(!id.empty()&&ids.insert(id).second,"Invalid MIDI source identity");string(device,"name",4096);}
  }
  static void validateRecording(const Json &value){
    require(value.is_object(),"Invalid recording take");string(value,"take",200);string(value,"baseRevision",200,true);string(value,"inputError",512,true);
    for(const auto *key:{"capturing","compatible"})if(value.contains(key))require(value[key].is_boolean(),"Invalid recording state");
    for(const auto *key:{"eventCount","missingTime","exhaustedVoices","overflow"})integer(value,key,0,key==std::string_view("eventCount")?65536:UINT32_MAX);
    if(value.contains("events")){
      require(value["events"].is_array()&&value["events"].size()<=65536,"Invalid take review");
      // Compact summaries carry an empty events array even while a take grows.
      if(!value["events"].empty())require(value["events"].size()==value["eventCount"].get<size_t>(),"Incomplete take review");
      for(const auto &event:value["events"]){string(event,"patternID",80);string(event,"track",80);integer(event,"position",0,UINT32_MAX);
        const auto note=integer(event,"note",1,255);require(note<=120||note==254||note==255,"Invalid recorded note");integer(event,"instrument",0,255);integer(event,"velocity",1,127);
        for(const auto *label:{"patternLabel","trackLabel","soundLabel"})string(event,label,4096,true);
        if(event.contains("pattern"))integer(event,"pattern",0,UINT32_MAX);if(event.contains("channel"))integer(event,"channel",0,127);
      }
    }
  }
  std::string take()const{return recording_.value("take",std::string());}
  bool unavailable()const{return operation_!=Operation::none||externalBusy_||state_.value("busy",false);}
  bool compatible()const{return recording_.value("compatible",false);}
  const char *operationName()const{
    switch(operation_){case Operation::rescan:return "rescan";case Operation::apply:return "apply";case Operation::finish:return "finish";case Operation::discard:return "discard";case Operation::review:return "review";case Operation::navigate:return "navigate";case Operation::sound:return "sound";default:return "";}
  }
  static std::string eventKey(const Json &event){return Json::array({event.at("patternID"),event.at("track"),event.at("position"),event.at("note"),event.at("instrument"),event.at("velocity")}).dump();}
  std::wstring cell(size_t row,unsigned column)const{
    if(row>=events_.size()||column>=6)return {};const auto &event=events_[row];
    if(column==0){if(event.contains("patternLabel")&&event["patternLabel"].is_string())return wide(event["patternLabel"]);if(event.contains("pattern"))return L"Pattern "+std::to_wstring(event["pattern"].get<unsigned>());return L"Unavailable pattern";}
    if(column==1){if(event.contains("trackLabel")&&event["trackLabel"].is_string())return wide(event["trackLabel"]);if(event.contains("channel"))return L"CH "+std::to_wstring(event["channel"].get<unsigned>()+1);return L"Unavailable track";}
    const auto note=event["note"].get<unsigned>();
    if(column==2){const auto position=event["position"].get<uint32_t>();return std::to_wstring(position/65536)+L" + "+std::to_wstring(position%65536)+L"/65536";}
    if(column==3){if(note==254)return L"Cut";if(note==255)return L"Release";static constexpr const wchar_t *names[]={L"C",L"C#",L"D",L"D#",L"E",L"F",L"F#",L"G",L"G#",L"A",L"A#",L"B"};return std::wstring(names[(note-1)%12])+L"-"+std::to_wstring((note-1)/12);}
    if(column==4){if(event.contains("soundLabel")&&event["soundLabel"].is_string())return wide(event["soundLabel"]);const auto slot=event["instrument"].get<unsigned>();return slot?L"Slot "+std::to_wstring(slot):L"Held sound";}
    return note<128?std::to_wstring(event["velocity"].get<unsigned>()):L"—";
  }
  void details(){
    std::wstring text=selected_>=0?cell(size_t(selected_),0)+L" / "+cell(size_t(selected_),1)+L" / "+cell(size_t(selected_),2)+L" / "+cell(size_t(selected_),3):L"Select a captured event to show it in the note editor.";
    set(eventDetail,text);
  }
  void sourceOptions(){
    const auto control=controls_.at(source);if(SendMessageW(control,CB_GETDROPPEDSTATE,0,0)){sourceRefresh_=true;return;}
    setting_=true;ScreamSeq::NativeInputGate::present(control,CB_RESETCONTENT,0,0);sourceIDs_.clear();
    auto addOption=[&](std::string id,const std::wstring &name){ScreamSeq::NativeInputGate::present(control,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name.c_str()));sourceIDs_.push_back(std::move(id));};
    addOption({},L"Disconnected");for(const auto &device:devices_)addOption(device.at("id"),wide(device.at("name")));
    auto found=std::find(sourceIDs_.begin(),sourceIDs_.end(),draftSource_);if(found==sourceIDs_.end()){addOption(draftSource_,L"Selected source unavailable");found=sourceIDs_.end()-1;}
    ScreamSeq::NativeInputGate::present(control,CB_SETCURSEL,found-sourceIDs_.begin(),0);sourceRefresh_=false;setting_=false;
  }
  void quantumOptions(){
    const auto control=controls_.at(quantum);ScreamSeq::NativeInputGate::present(control,CB_RESETCONTENT,0,0);int selected=-1;
    for(const auto &[label,value]:std::array<std::pair<const wchar_t *,unsigned>,4>{{{L"Keep exact timing",0},{L"1/16 row",4096},{L"1/4 row",16384},{L"Whole row",65536}}}){
      const auto index=ScreamSeq::NativeInputGate::present(control,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));ScreamSeq::NativeInputGate::present(control,CB_SETITEMDATA,index,value);if(value==draftQuantum_)selected=int(index);
    }
    if(selected<0){const auto label=L"Custom: "+std::to_wstring(draftQuantum_)+L"/65536 row";selected=int(ScreamSeq::NativeInputGate::present(control,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str())));ScreamSeq::NativeInputGate::present(control,CB_SETITEMDATA,selected,draftQuantum_);}
    ScreamSeq::NativeInputGate::present(control,CB_SETCURSEL,selected,0);
  }
  void savedFields(){
    if(settings_.empty())return;setting_=true;baseRevision_=settings_.at("revision");draftSource_=settings_.at("source");draftArmed_=settings_.at("armed");draftQuantum_=settings_.at("quantization");
    set(columns,settings_.at("channelsCount"));set(latency,settings_.at("latencyMS"));quantumOptions();setting_=false;sourceOptions();dirty_=false;++draftGeneration_;
  }
  Json parameters()const{
    require(loaded_,"MIDI settings are not available yet");const auto count=number(columns);require(count>=1&&count<=127&&std::floor(count)==count,"Use 1 to 127 adjacent note columns");
    const auto adjustment=number(latency);require(adjustment>=-500&&adjustment<=500,"Input adjustment must be -500 to +500 ms");
    return {{"expectedMidiRevision",baseRevision_},{"source",draftSource_},{"armed",draftArmed_},{"channelsCount",unsigned(count)},{"quantization",draftQuantum_},{"latencyMS",adjustment}};
  }
  void textStatus(){
    set(arm,draftArmed_?L"Record notes: on":L"Record notes: off");
    const auto target=state_.value("target",Json::object());const auto label=string(target,"label",16384,true);set(sound,label.empty()?L"Choose a sound in the main window":wide(label));
    std::wstring connected=settings_.value("connected",false)?L"Connected":L"Disconnected";
    if(!settings_.value("source",std::string()).empty()&&!settings_.value("connected",false))connected=L"Selected source is unavailable. Rescan or choose another source.";
    const auto serviceError=string(settings_,"error",16384,true);if(!serviceError.empty())connected=wide(serviceError);set(connection,connected);
    if(dirty_)set(draftStatus,baseRevision_!=settings_.value("revision",std::string())?L"Settings changed elsewhere. Use saved settings to reload.":L"Unapplied settings / Ctrl+Enter applies");
    else if(target.contains("firstChannel")&&target["firstChannel"].is_number_integer()&&target.contains("availableChannels")&&target["availableChannels"].is_number_integer())set(draftStatus,L"Start: CH "+std::to_wstring(target["firstChannel"].get<unsigned>()+1)+L" / "+std::to_wstring(target["availableChannels"].get<unsigned>())+L" columns available. Captured on Play.");
    else set(draftStatus,L"Sound and starting column are captured when recording starts.");
    const auto count=recording_.value("eventCount",0u),missing=recording_.value("missingTime",0u),exhausted=recording_.value("exhaustedVoices",0u),overflow=recording_.value("overflow",0u);
    std::wstring summary;
    if(take().empty())summary=settings_.value("armed",false)?L"ARMED / Play starts a take; stopped input enters notes at the cursor.":L"NO TAKE / Arm recording, then Play to capture a performance.";
    else summary=(!compatible()?L"SONG CHANGED / RETAINED TAKE":recording_.value("capturing",false)?L"RECORDING":L"STOPPED TAKE")+std::wstring(L" / ")+std::to_wstring(count)+L" events / Reviewed "+std::to_wstring(events_.size())+L" of "+std::to_wstring(count);
    if(!take().empty())summary+=L"\nMissing time "+std::to_wstring(missing)+L" / No free column "+std::to_wstring(exhausted)+L" / Overflow "+std::to_wstring(overflow);
    set(takeStatus,summary);
    if(!error_.empty())status_=wide(error_);
    else if(operation_!=Operation::none)status_=operation_==Operation::finish?L"Stopping and finishing the captured take…":operation_==Operation::discard?L"Discarding the captured take…":operation_==Operation::rescan?L"Looking for MIDI sources…":operation_==Operation::review?L"Reading captured events…":operation_==Operation::apply?L"Applying MIDI settings…":L"Opening the selected target…";
    else if(state_.contains("error")&&state_["error"].is_string()&&!state_["error"].get_ref<const std::string &>().empty())status_=wide(state_["error"]);
    else if(const auto reason=string(recording_,"inputError",512,true);!reason.empty())status_=wide(reason);
    else if(!take().empty()&&events_.size()!=count)status_=L"Refresh review for the latest events. Finish replaces notes in recorded rows as one Undo.";
    else status_=L"Finish replaces notes in recorded rows as one Undo. Enter shows a selected event; Escape closes this window.";
    set(statusLabel,status_);requestPaint();
  }
  void changed(){if(setting_)return;dirty_=true;++draftGeneration_;error_.clear();textStatus();layout();}
  void installEvents(const Json &next,const std::string &identity){
    const bool same=identity==reviewTake_;const bool first=events_.empty();const auto oldKey=same?selectedKey_:std::string();const auto occurrence=selectedOccurrence_;
    const auto list=controls_.at(events);const auto oldTop=ListView_GetTopIndex(list);events_=next;reviewTake_=identity;selected_=-1;selectedKey_.clear();selectedOccurrence_=0;
    if(same&&!oldKey.empty()){unsigned seen=0;for(size_t i=0;i<events_.size();++i)if(eventKey(events_[i])==oldKey&&seen++==occurrence){selected_=int(i);selectedKey_=oldKey;selectedOccurrence_=occurrence;break;}}
    if((!same||first)&&!events_.empty()){selected_=0;selectedKey_=eventKey(events_[0]);}
    setting_=true;SendMessageW(list,WM_SETREDRAW,FALSE,0);ListView_SetItemCountEx(list,int(events_.size()),LVSICF_NOINVALIDATEALL|LVSICF_NOSCROLL);ListView_SetItemState(list,-1,0,LVIS_SELECTED|LVIS_FOCUSED);
    if(selected_>=0)ListView_SetItemState(list,selected_,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
    if(same&&oldTop>=0&&size_t(oldTop)<events_.size()){RECT row{};if(ListView_GetItemRect(list,oldTop,&row,LVIR_BOUNDS))ListView_Scroll(list,0,(oldTop-ListView_GetTopIndex(list))*(row.bottom-row.top));}
    SendMessageW(list,WM_SETREDRAW,TRUE,0);InvalidateRect(list,nullptr,FALSE);setting_=false;details();
  }
  void begin(Operation operation){
    const bool navigation=operation==Operation::navigate||operation==Operation::sound;
    if((unavailable()&&!navigation)||(navigation&&operation_==operation))return;
    if(operation==Operation::apply&&!dirty_)return;
    const auto identity=take();if((operation==Operation::finish||operation==Operation::discard||operation==Operation::review)&&identity.empty())return;
    if(operation==Operation::finish&&!compatible())return;
    if(operation==Operation::navigate&&(selected_<0||reviewTake_!=identity))return;
    Json params;if(operation==Operation::apply)params=parameters();const auto generation=draftGeneration_;
    const auto event=operation==Operation::navigate?events_.at(size_t(selected_)):Json();
    const auto previous=operation_;const auto previousTake=actionTake_;operation_=operation;actionTake_=identity;error_.clear();textStatus();layout();
    try{
      if(operation==Operation::rescan){if(callbacks_.rescan)callbacks_.rescan();}
      else if(operation==Operation::apply){require(bool(callbacks_.applySettings),"MIDI settings are unavailable");auto applied=callbacks_.applySettings(std::move(params));validateSettings(applied);settings_=std::move(applied);state_["settings"]=settings_;loaded_=true;if(generation==draftGeneration_)savedFields();}
      else if(operation==Operation::finish){if(callbacks_.finish)callbacks_.finish(identity);}
      else if(operation==Operation::discard){if(callbacks_.discard)callbacks_.discard(identity);}
      else if(operation==Operation::review){if(callbacks_.review)callbacks_.review(identity);}
      else if(operation==Operation::navigate){if(callbacks_.navigate)callbacks_.navigate(identity,event);}
      else if(operation==Operation::sound){if(callbacks_.chooseSound)callbacks_.chooseSound();}
    }catch(const std::exception &error){error_=error.what();}
    operation_=previous;actionTake_=previousTake;textStatus();layout();
  }
  void action(int id,unsigned notification)override{
    if(setting_)return;
    if(id==source&&notification==CBN_CLOSEUP){if(sourceRefresh_)sourceOptions();return;}
    if(id==source&&notification==CBN_SELCHANGE){const auto index=SendMessageW(controls_.at(source),CB_GETCURSEL,0,0);if(index>=0&&size_t(index)<sourceIDs_.size()){draftSource_=sourceIDs_[size_t(index)];changed();}return;}
    if(id==quantum&&notification==CBN_SELCHANGE){const auto index=SendMessageW(controls_.at(quantum),CB_GETCURSEL,0,0);if(index!=CB_ERR){draftQuantum_=unsigned(SendMessageW(controls_.at(quantum),CB_GETITEMDATA,index,0));changed();}return;}
    if((id==columns||id==latency)&&notification==EN_CHANGE){changed();return;}
    if(notification!=BN_CLICKED)return;
    if(id==close){hide();return;}if(id==showEvent){begin(Operation::navigate);return;}if(id==chooseSound){begin(Operation::sound);return;}
    if(unavailable())return;
    if(id==arm){draftArmed_=!draftArmed_;changed();}else if(id==rescan)begin(Operation::rescan);else if(id==apply)begin(Operation::apply);else if(id==revert){savedFields();error_.clear();textStatus();layout();}
    else if(id==finishTake)begin(Operation::finish);else if(id==discardTake)begin(Operation::discard);else if(id==refreshReview)begin(Operation::review);
  }
  bool key(WPARAM value,bool ctrl,bool shift)override{
    if(value==VK_ESCAPE){hide();return true;}
    if(value==VK_F5&&!ctrl){begin(Operation::rescan);return true;}
    if(value==VK_RETURN&&ctrl){begin(shift?Operation::finish:Operation::apply);return true;}
    if(value==VK_TAB&&GetFocus()==window_){const auto next=GetNextDlgTabItem(window_,nullptr,shift);if(next)SetFocus(next);return true;}
    if(!ctrl&&(value==VK_RETURN||value==VK_SPACE)){
      const auto focus=GetFocus();if(value==VK_RETURN&&focus==controls_.at(events)){begin(Operation::navigate);return true;}
      for(const auto id:{rescan,arm,chooseSound,apply,revert,finishTake,discardTake,refreshReview,showEvent,close})if(focus==controls_.at(id)){action(id,BN_CLICKED);return true;}
    }
    return false;
  }
  void error(const std::exception &error)override{fail(error.what());}
  void resizeColumns(){
    const auto list=controls_.at(events);RECT bounds{};GetClientRect(list,&bounds);const auto dpi=GetDpiForWindow(window_);const float width=bounds.right*96.f/dpi;
    if(width==listWidth_&&dpi==columnDpi_)return;if(listWidth_>0)columnWidths_[2]=std::max(130.f,columnWidths_[2]+width-listWidth_);else columnWidths_[2]=std::max(130.f,width-columnWidths_[0]-columnWidths_[1]-columnWidths_[3]-columnWidths_[4]-columnWidths_[5]);
    listWidth_=width;columnDpi_=dpi;resizingColumns_=true;for(int i=0;i<6;++i)ListView_SetColumnWidth(list,i,int(std::lround(columnWidths_[size_t(i)]*dpi/96.f)));resizingColumns_=false;
  }
  void layout()override{
    if(!ready_)return;const auto [w,h]=size();const auto focus=GetFocus();
    place(heading,14,10,w-28,26);place(help,14,42,w-28,36);
    place(sourceLabel,14,87,72,24);place(source,92,84,w-206,28);place(rescan,w-102,84,88,28);place(connection,14,117,w-28,20);
    place(arm,14,144,150,28);place(soundLabel,178,147,42,24);place(sound,224,144,w-346,28);place(chooseSound,w-114,144,100,28);
    place(columnsLabel,14,184,156,20);place(quantumLabel,184,184,234,20);place(latencyLabel,434,184,w-448,20);
    place(columns,14,206,156,28);place(quantum,184,206,234,28);place(latency,434,206,120,28);place(latencyUnits,566,208,w-580,24);
    place(apply,14,244,146,28);place(revert,170,244,150,28);place(draftStatus,334,242,w-348,36);
    place(takeStatus,14,286,w-28,44);place(events,14,340,w-28,std::max(104.f,h-458));place(eventDetail,14,h-108,w-28,20);
    place(statusLabel,14,h-80,w-28,34);place(finishTake,14,h-42,140,28);place(discardTake,164,h-42,116,28);place(refreshReview,290,h-42,130,28);place(showEvent,430,h-42,164,28);place(close,w-104,h-42,90,28);
    resizeColumns();const bool available=!unavailable();
    for(const auto id:{source,rescan,arm,columns,quantum,latency,apply,revert})EnableWindow(controls_.at(id),available&&(id==rescan||loaded_));
    EnableWindow(controls_.at(apply),available&&loaded_&&dirty_);EnableWindow(controls_.at(revert),available&&loaded_&&dirty_);
    EnableWindow(controls_.at(finishTake),available&&!take().empty()&&compatible());EnableWindow(controls_.at(discardTake),available&&!take().empty());EnableWindow(controls_.at(refreshReview),available&&!take().empty());
    EnableWindow(controls_.at(showEvent),selected_>=0&&reviewTake_==take()&&operation_!=Operation::navigate);EnableWindow(controls_.at(chooseSound),operation_!=Operation::sound);
    if(unavailable()&&focus&&IsChild(window_,focus)&&!GetFocus())pendingFocus_=focus;
    if(!unavailable()&&pendingFocus_){const auto old=pendingFocus_;pendingFocus_=nullptr;if(!GetFocus()&&IsWindowVisible(old)&&IsWindowEnabled(old)&&GetActiveWindow()==GetAncestor(window_,GA_ROOT))SetFocus(old);}
  }
  void paint(RenderSurface &surface)override{const auto [w,h]=size();surface.fill(0,0,w,h,0x18222d);}
  void fontsChanged()override{
    if(!controls_.contains(events))return;const auto replacement=ImageList_Create(1,std::max(1,MulDiv(25,GetDpiForWindow(window_),96)),ILC_COLOR32,1,1);
    if(replacement){ListView_SetImageList(controls_.at(events),replacement,LVSIL_SMALL);if(rowHeight_)ImageList_Destroy(rowHeight_);rowHeight_=replacement;}columnDpi_=0;
  }
  LRESULT notify(NMHDR *header){
    if(header->hwndFrom!=controls_.at(events))return 0;
    if(header->code==LVN_GETDISPINFOW){auto &info=*reinterpret_cast<NMLVDISPINFOW *>(header);if((info.item.mask&LVIF_TEXT)&&info.item.pszText&&info.item.cchTextMax>0){const auto value=info.item.iItem>=0?cell(size_t(info.item.iItem),unsigned(info.item.iSubItem)):std::wstring();lstrcpynW(info.item.pszText,value.c_str(),info.item.cchTextMax);}return 0;}
    if(header->code==LVN_ITEMCHANGED&&!setting_){selected_=ListView_GetNextItem(controls_.at(events),-1,LVNI_SELECTED);selectedKey_.clear();selectedOccurrence_=0;if(selected_>=0&&size_t(selected_)<events_.size()){selectedKey_=eventKey(events_[size_t(selected_)]);for(int i=0;i<selected_;++i)selectedOccurrence_+=eventKey(events_[size_t(i)])==selectedKey_;}else selected_=-1;details();layout();return 0;}
    if(header->code==NM_DBLCLK&&!setting_){const auto row=reinterpret_cast<NMITEMACTIVATE *>(header)->iItem;if(row>=0&&row==selected_)begin(Operation::navigate);return 0;}
    if(header->code==LVN_ODFINDITEMW){const auto &find=*reinterpret_cast<NMLVFINDITEMW *>(header);if(!(find.lvfi.flags&LVFI_STRING)||!find.lvfi.psz||events_.empty())return -1;const auto query=std::wstring_view(find.lvfi.psz);const auto start=size_t(std::max(0,find.iStart));for(size_t i=0;i<events_.size();++i){const auto row=(start+i)%events_.size();for(const auto column:{0u,1u,3u}){const auto value=cell(row,column);if(value.size()>=query.size()&&CompareStringOrdinal(value.data(),int(query.size()),query.data(),int(query.size()),TRUE)==CSTR_EQUAL)return LRESULT(row);}}return -1;}
    if(header->code==NM_CUSTOMDRAW)return NativeReportList::customDraw(*reinterpret_cast<NMLVCUSTOMDRAW *>(header),[this](size_t row,unsigned column){return cell(row,column);});
    return 0;
  }
  static LRESULT CALLBACK notifications(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR data){
    auto &self=*reinterpret_cast<MidiRecordingWindow *>(data);if(m==WM_NCDESTROY)RemoveWindowSubclass(h,notifications,id);
    if(self.ready_&&NativeReportList::themeMessage(m))NativeReportList::refresh(self.controls_.at(events));
    if(m==WM_NOTIFY&&self.ready_)try{return self.notify(reinterpret_cast<NMHDR *>(l));}catch(const std::exception &error){self.error(error);return 0;}return DefSubclassProc(h,m,w,l);
  }
  static LRESULT CALLBACK listNotifications(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR data){
    auto &self=*reinterpret_cast<MidiRecordingWindow *>(data);if(m==WM_NCDESTROY)RemoveWindowSubclass(h,listNotifications,id);
    if(m==WM_NOTIFY){const auto header=reinterpret_cast<NMHDR *>(l);if(header->hwndFrom==ListView_GetHeader(h)){
      if((header->code==HDN_ENDTRACKW||header->code==HDN_ENDTRACKA)&&!self.resizingColumns_){for(int i=0;i<6;++i)self.columnWidths_[size_t(i)]=ListView_GetColumnWidth(h,i)*96.f/GetDpiForWindow(h);const auto &change=*reinterpret_cast<NMHEADERW *>(header);if(change.iItem>=0&&change.iItem<6&&change.pitem&&(change.pitem->mask&HDI_WIDTH))self.columnWidths_[size_t(change.iItem)]=change.pitem->cxy*96.f/GetDpiForWindow(h);}
      if(header->code==NM_CUSTOMDRAW)return NativeReportList::headerDraw(*reinterpret_cast<NMCUSTOMDRAW *>(header));
    }}return DefSubclassProc(h,m,w,l);
  }
public:
  MidiRecordingWindow(HWND owner,Callbacks callbacks):NativeToolWindow(owner),callbacks_(std::move(callbacks)){
    minimumClientWidth_=720;minimumClientHeight_=570;INITCOMMONCONTROLSEX common{sizeof(common),ICC_LISTVIEW_CLASSES};require(InitCommonControlsEx(&common),"Cannot initialize take review");create(L"ScreamSeq.MidiRecording",L"MIDI & recording",860,700);
    combo(source);button(rescan,L"Rescan");button(arm,L"Record notes: off");add(sound,L"EDIT",L"",ES_READONLY|ES_AUTOHSCROLL);button(chooseSound,L"Choose…");edit(columns,L"1",3);combo(quantum);edit(latency,L"0",20);button(apply,L"Apply settings");button(revert,L"Use saved settings");
    const auto list=add(events,WC_LISTVIEWW,L"Captured notes",LVS_REPORT|LVS_OWNERDATA|LVS_SINGLESEL|LVS_SHOWSELALWAYS|WS_BORDER);ListView_SetExtendedListViewStyle(list,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);
    if(const auto theme=LoadLibraryW(L"uxtheme.dll")){using Theme=HRESULT(WINAPI *)(HWND,LPCWSTR,LPCWSTR);if(const auto setTheme=reinterpret_cast<Theme>(GetProcAddress(theme,"SetWindowTheme")))setTheme(list,L"",L"");FreeLibrary(theme);}
    NativeReportList::install(list);
    for(int i=0;i<6;++i){LVCOLUMNW column{};column.mask=LVCF_TEXT|LVCF_WIDTH;column.pszText=const_cast<wchar_t *>(headings[size_t(i)]);column.cx=100;ListView_InsertColumn(list,i,&column);}
    button(finishTake,L"Finish take");button(discardTake,L"Discard take");button(refreshReview,L"Refresh review");button(showEvent,L"Show in note editor");button(close,L"Close");
    for(const auto &[id,text]:std::initializer_list<std::pair<int,const wchar_t *>>{{heading,L"MIDI / RECORDING"},{help,L"Record a performance into precise pattern notes. Positive input adjustment places notes earlier.\nNormal keyboard note entry and Live keys keep their current behavior."},{sourceLabel,L"MIDI source"},{connection,L"Disconnected"},{soundLabel,L"Sound"},{columnsLabel,L"Adjacent note columns"},{quantumLabel,L"Timing grid"},{latencyLabel,L"Input adjustment"},{latencyUnits,L"milliseconds"},{draftStatus,L""},{takeStatus,L""},{eventDetail,L""},{statusLabel,L""}})label(id,text);
    require(SetWindowSubclass(window_,notifications,notificationSubclass,reinterpret_cast<DWORD_PTR>(this))&&SetWindowSubclass(list,listNotifications,listSubclass,reinterpret_cast<DWORD_PTR>(this)),"Cannot initialize take review notifications");
    setting_=true;quantumOptions();setting_=false;sourceOptions();finish();details();textStatus();
  }
  ~MidiRecordingWindow()override{if(window_)RemoveWindowSubclass(window_,notifications,notificationSubclass);if(controls_.contains(events)){RemoveWindowSubclass(controls_.at(events),listNotifications,listSubclass);ListView_SetImageList(controls_.at(events),nullptr,LVSIL_SMALL);}if(rowHeight_)ImageList_Destroy(rowHeight_);}
  void show(){const auto focus=GetFocus();const bool existing=visible();NativeToolWindow::show();if(existing&&owns(focus))SetFocus(focus);else SetFocus(controls_.at(source));}
  void update(const Json &snapshot){
    require(snapshot.is_object(),"Invalid MIDI recording snapshot");auto next=state_;for(auto it=snapshot.begin();it!=snapshot.end();++it)next[it.key()]=it.value();
    if(next.contains("settings"))validateSettings(next["settings"]);if(next.contains("devices"))validateDevices(next["devices"]);if(next.contains("recording"))validateRecording(next["recording"]);
    if(next.contains("busy"))require(next["busy"].is_boolean(),"Invalid recording request state");
    if(next.contains("target")){require(next["target"].is_object(),"Invalid recording target");string(next["target"],"label",16384,true);if(next["target"].contains("firstChannel"))integer(next["target"],"firstChannel",0,127);if(next["target"].contains("availableChannels"))integer(next["target"],"availableChannels",0,128);}
    string(next,"error",16384,true);
    if(operation_==Operation::review&&next.contains("recording")&&next["recording"].value("take",std::string())==actionTake_&&take()!=actionTake_)throw std::runtime_error("Recording take changed. Refresh its review again.");
    const bool devicesChanged=next.value("devices",devices_)!=devices_;const bool settingsChanged=!loaded_||!sameSettings(next.value("settings",settings_),settings_);const auto oldTake=take();state_=std::move(next);
    if(state_.contains("settings")){settings_=state_["settings"];loaded_=true;if(settingsChanged&&!dirty_&&operation_!=Operation::apply)savedFields();}
    if(state_.contains("devices"))devices_=state_["devices"];if(devicesChanged||sourceRefresh_)sourceOptions();
    if(state_.contains("recording")){
      recording_=state_["recording"];const auto identity=take();
      if(identity!=oldTake){installEvents(Json::array(),identity);if(operation_==Operation::none)error_.clear();}
      const bool full=recording_.contains("events")&&(!recording_["events"].empty()||recording_.value("eventCount",0u)==0);
      if(full&&recording_["events"]!=events_)installEvents(recording_["events"],identity);
      recording_.erase("events");state_["recording"]=recording_;
    }
    textStatus();layout();
  }
  void setBusy(bool busy){externalBusy_=busy;textStatus();layout();}
  void fail(std::string error){error_=std::move(error);textStatus();layout();}
  bool retainedDraft()const{return dirty_;}
  Json snapshot()const{return {{"visible",visible()},{"pending",unavailable()},{"operation",operationName()},{"actionTake",actionTake_},{"dirty",dirty_},{"settings",settings_},{"recording",recording_},{"reviewedCount",events_.size()},{"reviewTake",reviewTake_},{"selectedEvent",selected_>=0?events_[size_t(selected_)]:Json()},
    {"draft",{{"baseRevision",baseRevision_},{"source",draftSource_},{"armed",draftArmed_},{"channelsCountText",utf8(field(columns))},{"quantization",draftQuantum_},{"latencyMSText",utf8(field(latency))}}},{"status",utf8(status_)},{"error",error_}};}
};
}
