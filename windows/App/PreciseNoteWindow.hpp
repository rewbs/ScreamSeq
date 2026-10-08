#pragma once
// Retained native precise-row editor; musical edits use the shared API.
// Sole editable precise-row owner. Native identity: preciseNotes.
#include "NativeToolWindow.hpp"
#include "WorkspaceState.hpp"
#include "editor/PreciseNotes.hpp"
#include "editor/PatternPerformance.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <iomanip>
#include <optional>
#include <set>
#include <sstream>
#include <tuple>

namespace ScreamSeq {
class PreciseNoteWindow final : public NativeToolWindow {
public:
  using Json=Api::Json;
  struct Target {
    std::string patternID,trackID;
    unsigned pattern=0,row=0,channel=0; // Slots are resolved from IDs, never authority.
    bool operator==(const Target &)const=default;
  };
  struct SeedCell {unsigned note=0,instrument=0,volumeCommand=0,volume=0,effect=0,parameter=0;};
  struct Context {
    std::string document,revision;
    Json patterns=Json::array(),tracks=Json::array();
    std::optional<Target> selected;
    // MUST capture an immutable published DocumentView by value, not Application.
    // Reload captured can address a row that is no longer under the cursor.
    std::function<SeedCell(unsigned,unsigned,unsigned)> cell;
    std::vector<std::wstring> noteNames;
    // Snapshot of the selected module sound, resolved by stable catalogue ID,
    // or the explicit workspace.input slot (which may currently be empty).
    // Zero means no compatible current selection. Only an empty row uses it;
    // ordinary/precise events keep their own instrument, including zero.
    unsigned insertionInstrument=0;
    uint64_t selectionGeneration=0;
    bool busy=false;
  };
  using Request=std::function<Json(const std::string &,const Json &)>;
  using ContextProvider=std::function<Context()>;
  static constexpr int dockMinimumWidth=440,dockMinimumHeight=300;
  // Existing IDs retain their meaning. 373 remains explicit Use target, not Reload.
  // IDs 9200..9230 are reserved for this native editor's navigation and labels.
  enum : int {list=360,pitch,instrument,velocity,offset,units,snap,localEffect,parameter,
    addHit,removeHit,checkDraft,applyDraft,loadTarget,replaceRow,fillRow,repeatCount,endVelocity,
    pageTimeline=9200,pageHit,pageTools,reloadCapturedCommand,returnPattern,close,
    pageDetails=9206,detailsText,
    titleLabel=9220,statusLabel,pitchLabel,instrumentLabel,velocityLabel,offsetLabel,unitsLabel,
    effectLabel,parameterLabel,repeatLabel,endLabel};
private:
  static constexpr unsigned unitsPerRow=Tracker::performanceUnitsPerRow;
  static constexpr size_t maximumEvents=Tracker::maximumPreciseNotes;
  Request request_;
  static constexpr std::array<int,4> pageIDs={pageTimeline,pageHit,pageTools,pageDetails};
  static constexpr std::array<const wchar_t *,4> pageNames={L"Timeline",L"Hit",L"Tools",L"Details"};
  static int pageIndex(int id){const auto found=std::find(pageIDs.begin(),pageIDs.end(),id);return found==pageIDs.end()?-1:int(found-pageIDs.begin());}
  ContextProvider context_;
  std::function<void()> return_;
  Target target_;
  unsigned insertionInstrument_=0;
  std::string document_,revision_,observedDocument_,observedRevision_;
  std::vector<std::wstring> names_;
  Json events_=Json::array(),draft_=Json::array(),original_=Json::array(),effects_=Json::array();
  Json pointBaseline_=Json::object(),toolsBaseline_=Json::object(),dragBefore_=Json::array();
  std::array<uint32_t,256> density_{};
  uint64_t generation_=0;
  unsigned rows_=0,rowsPerBeat_=4,units_=0,snap_=0;
  int selected_=-1,page_=0,dragSelection_=-1;
  bool initialized_=false,setting_=false,pending_=false,dragging_=false;
  bool replaceLegacy_=true,moveLegacyEffect_=false,canvasVisible_=false;
  HWND pendingFocus_{};
  WorkspaceRect plot_{};

  static void require(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
  static bool sameIdentity(const Target &a,const Target &b){return a.patternID==b.patternID&&a.trackID==b.trackID&&a.row==b.row;}
  bool hasTarget()const{return !document_.empty()&&!target_.patternID.empty()&&!target_.trackID.empty();}
  int selection(int id)const{return int(SendMessageW(controls_.at(id),CB_GETCURSEL,0,0));}
  void selection(int id,int value){NativeControls::select(controls_.at(id),value);}
  void status(std::wstring value){status_=std::move(value);if(ready_)set(statusLabel,status_);requestPaint();}
  void error(const std::exception &e)override{status(wide(e.what()));}
  struct Setting {bool &value;bool old;explicit Setting(bool &v):value(v),old(v){value=true;}~Setting(){value=old;}};
  struct Pending {
    PreciseNoteWindow &owner;
    explicit Pending(PreciseNoteWindow &value):owner(value){require(!owner.pending_,"Precise notes are busy");owner.pending_=true;
      try{owner.layout();}catch(...){owner.pending_=false;throw;}}
    ~Pending(){owner.pending_=false;try{owner.layout();}catch(...){}}
  };

#include "PreciseNoteOwnerModel.inc"
#include "PreciseNoteOwnerPresentation.inc"
#include "PreciseNoteOwnerInteraction.inc"
#include "PreciseNoteOwnerLayout.inc"

public:
  PreciseNoteWindow(HWND owner,Request request,ContextProvider context,std::function<void()> returnToPattern)
    :NativeToolWindow(owner),request_(std::move(request)),context_(std::move(context)),return_(std::move(returnToPattern)){
    require(bool(request_)&&bool(context_),"Precise notes need request and context callbacks");
    minimumClientWidth_=440;minimumClientHeight_=500;
    create(L"ScreamSeq.PreciseNotes",L"Precise notes",760,650,true);
    add(list,L"LISTBOX",L"Precise hits in the captured row",WS_VSCROLL|LBS_NOTIFY|LBS_OWNERDRAWFIXED|LBS_NODATA);
    add(detailsText,L"EDIT",L"",ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL);
    SendMessageW(controls_.at(detailsText),EM_SETLIMITTEXT,65535,0);
    for(int id:{pitch,units,snap,localEffect})combo(id);
    for(int id:{instrument,velocity,offset,parameter,repeatCount,endVelocity})edit(id,L"",32);
    for(auto [id,text]:std::initializer_list<std::pair<int,const wchar_t *>>{
      {addHit,L"Add hit"},{removeHit,L"Remove"},{checkDraft,L"Check"},{applyDraft,L"Apply"},
      {loadTarget,L"Load selection"},{replaceRow,L"Replace row note: on"},{fillRow,L"Fill to row end"},
      {pageTimeline,L"Timeline"},{pageHit,L"Hit"},{pageTools,L"Tools"},{pageDetails,L"Details"},{reloadCapturedCommand,L"Reload captured"},
      {returnPattern,L"Return to pattern"},{close,L"Close"}})button(id,text);
    for(auto [id,text]:std::initializer_list<std::pair<int,const wchar_t *>>{
      {titleLabel,L"Choose a pattern row"},{statusLabel,L""},{pitchLabel,L"Pitch"},{instrumentLabel,L"Instrument"},
      {velocityLabel,L"Velocity"},{offsetLabel,L"Offset"},{unitsLabel,L"Units"},{effectLabel,L"Note-local effect"},
      {parameterLabel,L"Hex"},{repeatLabel,L"Hits (2–64)"},{endLabel,L"End velocity (1–127)"}})label(id,text);
    for(unsigned n=1;n<=122;++n){const auto text=n<=120?L"Note "+std::to_wstring(n):n==121?L"Note off":L"Cut";
      const auto i=SendMessageW(controls_.at(pitch),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));
      SendMessageW(controls_.at(pitch),CB_SETITEMDATA,i,n<=120?n:n==121?255:254);}
    for(auto text:{L"Beats",L"Rows"})SendMessageW(controls_.at(units),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text));
    for(auto text:{L"Free",L"1/16 beat",L"1/32 beat",L"1/64 beat"})SendMessageW(controls_.at(snap),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text));
    {Setting guard(setting_);selection(pitch,60);selection(units,0);selection(snap,0);
      set(instrument,L"0");set(velocity,L"127");set(offset,L"0");set(parameter,L"00");set(repeatCount,L"4");set(endVelocity,L"127");}
    pointBaseline_=pointRaw();toolsBaseline_=toolsRaw();status_=L"Tools → Load selection captures a row";finish();
  }
  ~PreciseNoteWindow()override{ready_=false;dragging_=false;if(owns(GetCapture()))ReleaseCapture();}
  bool retainedDraft()const{return pending_||dragging_||draft_!=original_||pointRaw()!=pointBaseline_||toolsRaw()!=toolsBaseline_;}
  bool pending()const noexcept{return pending_;}
  bool capturedCurrent()const{return current();}
  std::optional<Target> capturedTarget()const{return hasTarget()?std::optional<Target>(target_):std::nullopt;}
  void initializeHidden(std::optional<Target> target={}){
    require(!(GetWindowLongPtrW(window_,GWL_STYLE)&WS_VISIBLE)&&!docked()&&!initialized_&&generation_==0&&!hasTarget()&&!retainedDraft(),
      "Hidden initialization requires a fresh precise-note owner");
    captureSelection(std::move(target),true);
  }
  void openAt(std::optional<Target> target={}){
    if((!visible()&&!retainedDraft())||!initialized_)captureSelection(std::move(target),!initialized_);
    show();focusPage();
  }
  bool followSelection(){
    if(retainedDraft())return false;const auto now=context_();if(now.busy||!now.selected)return false;
    const auto resolved=resolve(now,*now.selected);
    if(now.document==document_&&now.revision==revision_&&resolved==target_)return true;
    captureSelection({},false);return true;
  }
  void loadSelection(){require(!pending_&&!dragging_,"Precise notes are busy");captureSelection({},false);}
  void reloadCaptured(){
    require(hasTarget()&&!pending_&&!dragging_,"Choose a captured row while the editor is ready");const auto now=context_();
    require(now.document==document_,"Document changed / Load selection to capture the new song");load(now,resolve(now,target_),false);
  }
  void observeContext(){const auto now=context_();if(now.document==observedDocument_&&now.revision==observedRevision_)return;
    observedDocument_=now.document;observedRevision_=now.revision;layout();requestPaint();}
  void hide()override{cancelDrag();NativeToolWindow::hide();}
  bool dispatchLegacyAction(int id){
    if(!((id>=addHit&&id<=fillRow)||(id>=reloadCapturedCommand&&id<=close)))return false;
    action(id,BN_CLICKED);layout();requestPaint();return true;
  }
  Json operationGuard()const{return {{"initialized",initialized_},{"document",document_},{"revision",revision_},
    {"patternID",target_.patternID},{"trackID",target_.trackID},{"pattern",target_.pattern},{"row",target_.row},{"channel",target_.channel},
    {"generation",generation_},{"pending",pending_},{"dragging",dragging_},{"selected",selected_},
    {"units",units_},{"snap",snap_},{"retainedDraft",retainedDraft()}};}
  Json snapshot(bool includeDraft=true)const{
    auto result=operationGuard();Json bounds=Json::array();for(const auto &[id,h]:controls_)if(IsWindowVisible(h)){
      RECT r{};GetWindowRect(h,&r);MapWindowPoints(nullptr,window_,reinterpret_cast<POINT *>(&r),2);
      bounds.push_back({{"id",id},{"bounds",{r.left,r.top,r.right,r.bottom}},{"enabled",bool(IsWindowEnabled(h))}});}
    const auto [width,height]=size();result.update({{"visible",visible()},{"docked",docked()},{"dpi",GetDpiForWindow(window_)},{"client",{width,height}},
      {"expectedRevision",revision_},{"stale",hasTarget()&&!current()},
      {"draftCount",draft_.size()},{"selectedEvent",selected_>=0?draft_.at(size_t(selected_)):Json()},
      {"page",std::array<const char *,4>{"timeline","hit","tools","details"}[size_t(page_)]},{"rowsPerBeat",rowsPerBeat_},
      {"timeline",{{"x",plot_.x},{"y",plot_.y},{"width",plot_.w},{"height",plot_.h}}},{"canvasVisible",canvasVisible_},
      {"details",utf8(details())},{"selectedHitSummary",selected_>=0?hitSummary(draft_.at(size_t(selected_))):Json::array()},
      {"raw",pointRaw()},{"tools",toolsRaw()},{"controls",bounds},{"status",utf8(displayStatus())}});if(includeDraft)result["draft"]=draft_;return result;
  }
};
}
