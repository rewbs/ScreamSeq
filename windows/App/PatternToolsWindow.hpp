#pragma once
#include "NativeToolWindow.hpp"
#include "NativeWriteCompletion.hpp"
#include "editor/PatternToolCatalog.h"
#include <array>

namespace ScreamSeq {
class PatternToolsWindow final : public NativeToolWindow {
public:
  using Json=Api::Json;
  struct Callbacks {
    std::function<Json()> context;
    std::function<Json(const std::string &,const Json &)> read;
    NativeWriteCompletion::Write write;
    std::function<void()> returnToPattern;
  };
private:
  enum:int {operation=8001,scope,target,curve,filter,from,to,amount,seed,noteField,instrumentField,volumeField,effectField,
    swap,allowLoss,captureSelection,preview,apply,acceptState,returnPattern,close,details,
    heading=8100,targetLabel,operationLabel,scopeLabel,valueLabel,rangeLabel,amountLabel,seedLabel,statusLabel,helpLabel};
  inline static constexpr std::array<const char *,5> scopes{"selection","channel","pattern","song","note-track"};
  inline static constexpr std::array<const char *,5> targets{"volume","panning","note","instrument","effectParameter"};
  inline static constexpr std::array<const char *,3> curves{"linear","exponential","logarithmic"},filters{"values","notes","all"};
  Callbacks callbacks_;NativeWriteCompletion completion_;
  Json captured_=Json::object(),baseline_=Json::object(),preview_=nullptr,prepared_=nullptr,observation_=nullptr;
  uint64_t generation_=0;bool setting_=false,pending_=false,completed_=false,opened_=false;
  static void require(bool value,const char *message){if(!value)throw std::runtime_error(message);}
  int selected(int id)const{return int(SendMessageW(controls_.at(id),CB_GETCURSEL,0,0));}
  bool checked(int id)const{return SendMessageW(controls_.at(id),BM_GETCHECK,0,0)==BST_CHECKED;}
  auto descriptor()const{return ScreamSeqPatternToolAt(uint32_t(selected(operation)));}
  bool has(uint32_t option)const{return (descriptor().options&option)!=0;}
  Json raw()const {
    return {{"operation",selected(operation)},{"scope",selected(scope)},{"target",selected(target)},
      {"curve",selected(curve)},{"filter",selected(filter)},{"from",utf8(field(from))},{"to",utf8(field(to))},
      {"amount",utf8(field(amount))},{"seed",utf8(field(seed))},{"swap",checked(swap)},{"allowDataLoss",checked(allowLoss)},
      {"fields",Json::array({checked(noteField),checked(instrumentField),checked(volumeField),checked(effectField)})}};
  }
  bool sameSong(const Json &now)const{return !captured_.empty()&&now.at("documentId")==captured_.at("documentId");}
  bool stale(const Json &now)const{return !captured_.empty()&&(!sameSong(now)||now.at("revision")!=captured_.at("revision"));}
  bool available()const {
    const auto now=callbacks_.context();return !captured_.empty()&&!pending_&&!completed_&&!completion_.retained()&&!stale(now)&&
      now.at("editable").get<bool>()&&!now.at("busy").get<bool>();
  }
  std::wstring displayedStatus()const {
    if(!pending_&&!completed_&&!completion_.retained()&&stale(callbacks_.context()))return L"Song changed / draft retained. Capture the current selection and Preview again.";
    return status_;
  }
  void message(const std::wstring &text){status_=text;set(statusLabel,text);requestPaint();}
  void invalidate(){preview_=prepared_=nullptr;observation_=nullptr;set(details,L"");}
  void changed(){if(setting_)return;++generation_;invalidate();message(L"Settings retained / Preview before applying");layout();}
  void error(const std::exception &error)override{message(wide(error.what()));layout();}
  void capture() {
    require(!pending_&&!completion_.retained(),"Review the previous result before capturing another target");
    const auto now=callbacks_.context();require(!now.at("busy").get<bool>(),"The song is busy");
    captured_=now;completed_=false;++generation_;invalidate();
    const auto &selection=now.at("selection");
    set(targetLabel,L"Pattern "+std::to_wstring(now.at("pattern").get<unsigned>())+L" / rows "+
      std::to_wstring(selection.at("startRow").get<unsigned>())+L"–"+std::to_wstring(selection.at("endRow").get<unsigned>())+
      L" / columns "+std::to_wstring(selection.at("startChannel").get<unsigned>()+1)+L"–"+std::to_wstring(selection.at("endChannel").get<unsigned>()+1));
    message(L"Target captured / navigation will not redirect these settings");layout();
  }
  Json parameters()const {
    require(available(),"Captured song changed or is unavailable; capture a target and Preview again");
    const auto tool=descriptor();require(tool.identifier!=nullptr,"Choose a pattern operation");
    const auto selectedScope=scopes.at(size_t(selected(scope)));
    Json p={{"expectedRevision",captured_.at("revision")},{"operation",tool.identifier},{"scope",selectedScope},{"dryRun",true}};
    const std::string scopeName=selectedScope;
    if(scopeName!="song")p["pattern"]=captured_.at("pattern");
    if(scopeName=="selection") {
      const auto &s=captured_.at("selection");p["startRow"]=s.at("startRow");p["rowCount"]=s.at("endRow").get<unsigned>()-s.at("startRow").get<unsigned>()+1;
      p["startChannel"]=s.at("startChannel");p["channelCount"]=s.at("endChannel").get<unsigned>()-s.at("startChannel").get<unsigned>()+1;
    } else if(scopeName=="channel")p["startChannel"]=captured_.at("channel");
    else if(scopeName=="note-track") {require(captured_.at("track").is_string(),"Capture a column in a grouped note track first");p["track"]=captured_.at("track");}
    if(has(SCREAMSEQ_TOOL_FIELDS)) {
      p["fields"]=Json::array();const std::array<const char *,4> names{"note","instrument","volume","effect"};
      for(size_t i=0;i<names.size();++i)if(checked(noteField+int(i)))p["fields"].push_back(names[i]);
    }
    if(has(SCREAMSEQ_TOOL_AMOUNT))p["amount"]=number(amount);
    if(has(SCREAMSEQ_TOOL_LOSS))p["allowDataLoss"]=checked(allowLoss);
    if(has(SCREAMSEQ_TOOL_TARGET)){p["target"]=targets.at(size_t(selected(target)));p["only"]=filters.at(size_t(selected(filter)));}
    if(has(SCREAMSEQ_TOOL_FROM))p["from"]=number(from);
    if(has(SCREAMSEQ_TOOL_TO))p["to"]=number(to);
    if(has(SCREAMSEQ_TOOL_CURVE))p["curve"]=curves.at(size_t(selected(curve)));
    if(has(SCREAMSEQ_TOOL_SEED))p["seed"]=number(seed);
    if(has(SCREAMSEQ_TOOL_REMAP)){p["fromInstrument"]=number(from);p["toInstrument"]=number(to);p["swap"]=checked(swap);}
    return p;
  }
  static void validateResult(const Json &result) {
    require(result.is_object()&&result.at("changedCells").is_number_integer()&&result.at("effectsChanged").is_boolean()&&
      result.at("changes").is_array()&&result.at("previewTruncated").is_boolean(),"Incomplete pattern result; review before repeating");
  }
  static bool changes(const Json &result){return result.at("changedCells").get<uint64_t>()!=0||result.at("effectsChanged").get<bool>();}
  void showChanges(const Json &result) {
    std::wstring text;
    for(const auto &change:result.at("changes")) {
      text+=L"P"+std::to_wstring(change.at("pattern").get<unsigned>())+L" R"+std::to_wstring(change.at("row").get<unsigned>())+
        L" Ch"+std::to_wstring(change.at("channel").get<unsigned>()+1)+L"  ";
      bool separator=false;
      for(const auto *key:{"note","instrument","volumeCommand","volume","effect","parameter"})if(change.at("before").at(key)!=change.at("after").at(key)){
        if(separator)text+=L", ";separator=true;text+=wide(key)+L" "+wide(change.at("before").at(key).dump())+L" → "+wide(change.at("after").at(key).dump());
      }
      text+=L"\r\n";
    }
    if(result.at("effectsChanged").get<bool>())text+=L"Native precise notes or FX also change; the cell list describes module cells only.\r\n";
    if(result.at("previewTruncated").get<bool>())text+=L"Showing the first 512 cell changes. The operation applies the complete validated scope.\r\n";
    set(details,text);
  }
  void finishResult() {
    const auto result=completion_.returned();require(bool(result),"Outcome is unknown; Review current song state before continuing");
    validateResult(result->result);require(sameSong(callbacks_.context()),"Original document unavailable; result retained");
    baseline_=completion_.fields();completed_=true;preview_=prepared_=nullptr;observation_=nullptr;
    const bool newer=generation_!=completion_.generation();showChanges(result->result);completion_.finish();
    message(newer?L"Request completed / newer settings retained. Capture and Preview before another edit":
      changes(result->result)?L"Transform applied / one Undo restores the operation. Capture and Preview before another edit":L"No musical change / no history entry added");
  }
  void submit(bool dry) {
    if(pending_)return;require(available(),"Capture a current target before Preview or Apply");
    Json params;
    if(dry)params=parameters();
    else {require(prepared_.is_object()&&preview_.is_object()&&changes(preview_),"Preview the current settings before Apply");params=prepared_;params["dryRun"]=false;}
    const auto fields=raw(),target=captured_;const auto generation=generation_;
    pending_=true;message(dry?L"Preparing preview…":L"Applying transform… Newer settings remain editable");layout();
    try {
      if(dry) {
        const auto result=callbacks_.read("pattern.transform",params);validateResult(result);const auto now=callbacks_.context();
        if(generation_==generation&&sameSong(now)&&now.at("revision")==target.at("revision")) {
          preview_=result;prepared_=params;showChanges(result);
          message(std::to_wstring(result.at("changedCells").get<uint64_t>())+L" module cells change"+
            (result.at("effectsChanged").get<bool>()?L"; native notes or FX also change":L"")+
            (changes(result)?L" / Apply creates one Undo; structural playback stops":L" / nothing to apply"));
        }else{invalidate();message(L"Preview was for an earlier target or settings / current text retained");}
      }else{completion_.submit(callbacks_.write,"pattern.transform",params,target.at("documentId").get<std::string>(),generation,fields);finishResult();}
      pending_=false;layout();
    }catch(...){pending_=false;layout();throw;}
  }
  void review() {
    if(pending_||!completion_.retained())return;pending_=true;layout();
    try {
      callbacks_.read("synchronizeView",Json::object());
      if(completion_.returned())finishResult();
      else {const auto now=callbacks_.context();require(sameSong(now),"Original document unavailable; result retained");
        observation_=now;message(L"Earlier outcome is unverified. Inspect affected music using Pattern / F6, then accept the current state without repeating the write.");}
      pending_=false;layout();
    }catch(...){pending_=false;layout();throw;}
  }
  void acceptObservation() {
    const auto now=callbacks_.context();require(!pending_&&completion_.retained()&&observation_.is_object()&&sameSong(now)&&
      now.at("revision")==observation_.at("revision"),"Review the current song state first");
    completion_.finish();completed_=true;preview_=prepared_=observation_=nullptr;
    message(L"Observed state accepted / earlier outcome remains unverified / settings retained");layout();
  }
  void layout()override {
    if(!ready_)return;const auto [w,h]=size();const bool value=has(SCREAMSEQ_TOOL_TARGET),range=has(SCREAMSEQ_TOOL_FROM)||has(SCREAMSEQ_TOOL_REMAP);
    place(heading,18,12,w-36,24);place(targetLabel,18,46,w-238,42);place(captureSelection,w-208,48,190,28);
    place(operationLabel,18,96,280,20);place(operation,18,120,260,280);place(scopeLabel,298,96,w-316,20);place(scope,298,120,w-316,200);
    place(valueLabel,18,166,68,22,value);place(target,88,162,172,180,value);place(filter,280,162,230,170,value);
    place(rangeLabel,18,210,68,22,range);place(from,88,204,88,28,range);place(to,190,204,88,28,has(SCREAMSEQ_TOOL_TO)||has(SCREAMSEQ_TOOL_REMAP));
    place(curve,298,204,212,170,has(SCREAMSEQ_TOOL_CURVE));
    place(amountLabel,18,254,68,22,has(SCREAMSEQ_TOOL_AMOUNT));place(amount,88,248,88,28,has(SCREAMSEQ_TOOL_AMOUNT));
    place(seedLabel,204,254,56,22,has(SCREAMSEQ_TOOL_SEED));place(seed,262,248,128,28,has(SCREAMSEQ_TOOL_SEED));place(swap,298,248,w-316,28,has(SCREAMSEQ_TOOL_REMAP));
    for(int i=0;i<4;++i)place(noteField+i,18+float(i)*152,294,146,26,has(SCREAMSEQ_TOOL_FIELDS));
    place(allowLoss,18,330,w-36,26,has(SCREAMSEQ_TOOL_LOSS));place(helpLabel,18,366,w-36,30);
    place(details,18,402,w-36,std::max(80.f,h-540));place(statusLabel,18,h-122,w-36,62);
    place(preview,18,h-44,88,28);place(apply,114,h-44,110,28);place(acceptState,232,h-44,170,28,completion_.retained());
    place(returnPattern,w-228,h-44,124,28);place(close,w-96,h-44,78,28);
    EnableWindow(controls_.at(captureSelection),!pending_&&!completion_.retained()&&!callbacks_.context().at("busy").get<bool>());
    EnableWindow(controls_.at(preview),available());EnableWindow(controls_.at(apply),!pending_&&(completion_.retained()||(available()&&preview_.is_object()&&changes(preview_))));
    EnableWindow(controls_.at(acceptState),!pending_&&observation_.is_object());set(apply,completion_.retained()?L"Review result":L"Apply changes");set(statusLabel,displayedStatus());
  }
  void paint(RenderSurface &surface)override{const auto [w,h]=size();surface.fill(0,0,w,h,0x18222d);}
  void action(int id,unsigned notification)override {
    if(setting_)return;
    if((id>=operation&&id<=filter&&notification==CBN_SELCHANGE)||(id>=from&&id<=seed&&notification==EN_CHANGE)||
      (id>=noteField&&id<=allowLoss&&notification==BN_CLICKED)){changed();return;}
    if(notification!=BN_CLICKED)return;
    if(id==captureSelection)capture();else if(id==preview)submit(true);else if(id==apply){if(completion_.retained())review();else submit(false);}
    else if(id==acceptState)acceptObservation();else if(id==returnPattern)callbacks_.returnToPattern();else if(id==close)hide();
  }
  bool key(WPARAM key,bool ctrl,bool shift)override {
    if(!visible()||!owns(GetFocus())||(GetKeyState(VK_MENU)&0x8000))return false;
    if(key==VK_TAB&&!ctrl&&GetFocus()==window_){if(auto next=GetNextDlgTabItem(window_,nullptr,shift))SetFocus(next);return true;}
    if(key==VK_ESCAPE&&!ctrl){hide();return true;}
    if(key==VK_F6&&!ctrl&&!shift){callbacks_.returnToPattern();return true;}
    if(key==VK_RETURN&&ctrl&&!shift){action(apply,BN_CLICKED);return true;}
    if(key==VK_RETURN&&!ctrl&&!shift){for(int id:{captureSelection,preview,apply,acceptState,returnPattern,close})if(GetFocus()==controls_.at(id)){if(IsWindowEnabled(GetFocus()))action(id,BN_CLICKED);return true;}}
    return false;
  }
public:
  PatternToolsWindow(HWND owner,Callbacks callbacks):NativeToolWindow(owner),callbacks_(std::move(callbacks)) {
    minimumClientWidth_=680;minimumClientHeight_=660;create(L"ScreamSeq.PatternTools",L"Pattern tools",760,720);setting_=true;
    label(heading,L"PATTERN TOOLS");label(targetLabel,L"");label(operationLabel,L"Operation");label(scopeLabel,L"Apply to");label(valueLabel,L"Value");label(rangeLabel,L"From / to");label(amountLabel,L"Amount");label(seedLabel,L"Seed");label(statusLabel,L"");
    label(helpLabel,L"Preview keeps the song unchanged. Apply uses the captured target and revision; Undo restores the whole operation.");
    for(int id:{operation,scope,target,curve,filter})combo(id);
    const auto addChoice=[&](int id,const std::wstring &text){SendMessageW(controls_.at(id),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));};
    for(uint32_t i=0;i<SCREAMSEQ_PATTERN_TOOL_COUNT;++i)addChoice(operation,wide(ScreamSeqPatternToolAt(i).title));
    for(const auto *text:{L"Selection",L"Current column",L"Current pattern",L"All patterns, including unused",L"Current note track"})addChoice(scope,text);
    for(const auto *text:{L"Volume",L"Panning",L"Note",L"Instrument",L"Effect parameter"})addChoice(target,text);
    for(const auto *text:{L"Linear",L"Exponential",L"Logarithmic"})addChoice(curve,text);
    for(const auto *text:{L"Existing values",L"Rows with notes",L"All cells"})addChoice(filter,text);
    for(int id:{operation,scope,target,curve,filter})SendMessageW(controls_.at(id),CB_SETCURSEL,id==filter?1:0,0);
    edit(from,L"4",64);edit(to,L"64",64);edit(amount,L"2",64);edit(seed,L"1",64);
    const std::array<const wchar_t *,4> names{L"Notes",L"Instruments",L"Volume / pan",L"Effects"};
    for(int i=0;i<4;++i){add(noteField+i,L"BUTTON",names[size_t(i)],BS_AUTOCHECKBOX);SendMessageW(controls_.at(noteField+i),BM_SETCHECK,BST_CHECKED,0);}
    add(swap,L"BUTTON",L"Swap the two instruments",BS_AUTOCHECKBOX);add(allowLoss,L"BUTTON",L"Allow existing notes or fields to be discarded",BS_AUTOCHECKBOX);
    add(details,L"EDIT",L"",ES_READONLY|ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL);SendMessageW(controls_.at(details),EM_SETLIMITTEXT,262144,0);
    button(captureSelection,L"Use current selection");button(preview,L"Preview");button(apply,L"Apply changes");button(acceptState,L"Accept observed state");button(returnPattern,L"Pattern / F6");button(close,L"Close");
    accessibleName(operation,L"Pattern operation");accessibleName(scope,L"Apply to");accessibleName(target,L"Value to transform");
    accessibleName(curve,L"Interpolation curve");accessibleName(filter,L"Cells to affect");
    accessibleName(from,L"From value");accessibleName(to,L"To value");accessibleName(amount,L"Amount");accessibleName(seed,L"Random seed");
    accessibleName(details,L"Pattern operation preview");baseline_=raw();setting_=false;finish();
  }
  void open(){const auto focus=GetFocus();const bool wasVisible=visible();if(!opened_){clampToOwnerWorkArea();opened_=true;}show();if(captured_.empty())capture();
    if(wasVisible&&owns(focus)&&IsWindowEnabled(focus))SetFocus(focus);else SetFocus(controls_.at(operation));layout();}
  void update(){layout();requestPaint();}
  void hide()override{const bool focused=owns(GetFocus());NativeToolWindow::hide();if(focused)callbacks_.returnToPattern();}
  std::optional<Tracker::DocumentDraft> documentDraft()const override {
    return describeDraft(captured_.value("documentId",std::string()),captured_.value("revision",std::string()),
      Json::array({"pattern-tools",captured_.value("patternID",Json(nullptr)),captured_.value("columnID",Json(nullptr)),captured_.value("selection",Json::object())}).dump(),
      generation_,!captured_.empty()&&(raw()!=baseline_||preview_.is_object()),pending_,!pending_&&completion_.retained());
  }
  Json snapshot()const{return {{"visible",visible()},{"captured",captured_},{"draft",raw()},{"preview",preview_},{"prepared",prepared_},
    {"dirty",!captured_.empty()&&(raw()!=baseline_||preview_.is_object())},
    {"pending",pending_},{"completed",completed_},{"generation",generation_},{"stale",stale(callbacks_.context())},
    {"applyEnabled",available()&&preview_.is_object()&&changes(preview_)},{"completion",completion_.snapshot()},{"observation",observation_},{"status",utf8(displayedStatus())}};}
};
}
