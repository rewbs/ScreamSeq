#pragma once
// Retained graph-source curve owner, independent of routing-canvas placement.
#include "EnvelopeBankWindow.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <set>

namespace ScreamSeq {
class GraphCurveWindow final : public NativeToolWindow {
public:
  using Json=Api::Json;
  struct Target {
    std::string graph,node,patternID;
    unsigned pattern=0; // Re-resolved from patternID; never identity authority.
    bool operator==(const Target &)const=default;
  };
  struct Context {
    std::string document,revision;
    Json patterns=Json::array();
    std::optional<Target> selected;
    std::wstring selectedTitle;
    uint64_t selectionGeneration=0;
    bool busy=false;
  };
  using Request=std::function<Json(const std::string &,const Json &)>;
  using ContextProvider=std::function<Context()>;
  static constexpr int dockMinimumWidth=440,dockMinimumHeight=300;
  // Existing command numbers preserved, but these HWNDs belong only to this tool.
  enum : int {pattern=480,kind,snap,pointRow,pointValue,formula,apply,reload,
    setPoint,deletePoint,ramp,clear,fit,zoomIn,zoomOut,preview,enabled,bank,expand,reference,
    pageCurve=9100,pageFormula,pageTools,follow,returnPattern,close,
    titleLabel=9120,rowLabel,valueLabel,formulaLabel,statusLabel,helpLabel};
private:
  static constexpr std::array<const char *,9> curves={"step","linear","smooth","exponential","logarithmic","step-next","exponential-reverse","logarithmic-reverse","scripted"};
  Request request_;
  ContextProvider context_;
  std::function<void()> return_;
  Target target_;
  std::string document_,revision_;
  Json patterns_=Json::array(),points_=Json::array(),values_=Json::array();
  unsigned rows_=64,rowsPerBeat_=4,snapUnits_=256;
  int selected_=-1,kind_=1,snapIndex_=0,page_=0;
  uint64_t generation_=0;
  bool initialized_=false,setting_=false,dirty_=false,pointFields_=false,pending_=false;
  bool enabled_=true,previewNeeded_=false,canvasVisible_=false,openingChild_=false;
  bool dragging_=false,dragDirty_=false;
  int dragSelection_=-1;
  Json dragBefore_=Json::array();
  HWND pendingFocus_{};
  std::wstring title_=L"Choose a graph automation source";
  std::string observedDocument_,observedRevision_;
  AutomationCanvas canvas_;
  // Keep child lifetimes subordinate to the sole editable owner.
  std::unique_ptr<EnvelopeBankWindow> bank_;
  std::unique_ptr<FormulaWorkbenchWindow> workbench_,reference_;

  static void require(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
  bool hasTarget()const{return !target_.graph.empty()&&!target_.node.empty()&&!target_.patternID.empty();}
  bool draft()const{return dirty_||pointFields_;}
  static bool sameIdentity(const Target &a,const Target &b){return a.graph==b.graph&&a.node==b.node&&a.patternID==b.patternID;}
  static Target resolve(const Context &context,Target target){
    require(!target.graph.empty()&&!target.node.empty()&&!target.patternID.empty(),"Choose a graph automation source and pattern");
    const auto found=std::find_if(context.patterns.begin(),context.patterns.end(),[&](const auto &p){return p.at("id")==target.patternID;});
    require(found!=context.patterns.end(),"Captured pattern was removed / Follow selection to choose another target");
    target.pattern=found->at("index").template get<unsigned>();return target;
  }
  bool current()const{
    if(!hasTarget())return false;
    const auto now=context_();if(now.document!=document_||now.revision!=revision_)return false;
    const auto found=std::find_if(now.patterns.begin(),now.patterns.end(),[&](const auto &p){return p.at("id")==target_.patternID;});
    return found!=now.patterns.end()&&found->at("index")==target_.pattern;
  }
  void requireCurrent()const{require(current(),"Song changed / captured curve retained; Reload before applying");}
  void status(std::wstring text){status_=std::move(text);if(ready_)set(statusLabel,status_);requestPaint();}
  void error(const std::exception &e)override{status(wide(e.what()));}
  void rebuild(){canvas_.rebuild(points_,values_);requestPaint();}
  void schedule(){previewNeeded_=true;if(visible())resumeVisiblePresentation();}
  void changed(){dirty_=true;++generation_;values_=Json::array();schedule();rebuild();status(L"Curve draft / Apply saves one document Undo step");}
  void selection(int id,int value){NativeControls::select(controls_.at(id),value);}
  int selection(int id)const{return int(SendMessageW(controls_.at(id),CB_GETCURSEL,0,0));}
  struct Pending {
    GraphCurveWindow &owner;
    explicit Pending(GraphCurveWindow &value):owner(value){require(!owner.pending_,"Curve editor is busy");owner.pending_=true;owner.layout();}
    ~Pending(){owner.pending_=false;try{owner.layout();}catch(...){}}
  };

#include "GraphCurveOwnerModel.inc"
#include "GraphCurveOwnerChildren.inc"
#include "GraphCurveOwnerInteraction.inc"
#include "GraphCurveOwnerLayout.inc"

public:
  GraphCurveWindow(HWND owner,Request request,ContextProvider context,std::function<void()> returnToPattern)
    :NativeToolWindow(owner),request_(std::move(request)),context_(std::move(context)),return_(std::move(returnToPattern)){
    require(bool(request_)&&bool(context_),"Curve editor needs request and context callbacks");
    minimumClientWidth_=440;minimumClientHeight_=500;
    create(L"ScreamSeq.GraphCurve",L"Graph source pattern curve",760,650);
    for(int id:{pattern,kind,snap})combo(id);
    for(int id:{pointRow,pointValue,formula})edit(id,L"",id==formula?2048:32);
    for(auto [id,text]:std::initializer_list<std::pair<int,const wchar_t *>>{
      {apply,L"Apply curve"},{reload,L"Reload captured"},{setPoint,L"Set point"},{deletePoint,L"Delete point"},
      {ramp,L"Ramp"},{clear,L"Clear"},{fit,L"Fit"},{zoomIn,L"+"},{zoomOut,L"−"},{preview,L"Check / preview"},
      {enabled,L"Enabled"},{bank,L"Envelope bank…"},{expand,L"Expand…"},{reference,L"Guide"},
      {pageCurve,L"Curve"},{pageFormula,L"Formula"},{pageTools,L"Tools"},{follow,L"Load selection"},
      {returnPattern,L"Return to pattern"},{close,L"Close"}})button(id,text);
    for(auto [id,text]:std::initializer_list<std::pair<int,const wchar_t *>>{
      {titleLabel,L"Choose a graph automation source"},{rowLabel,L"Row"},{valueLabel,L"Value %"},
      {formulaLabel,L"Formula"},{statusLabel,L""},{helpLabel,L""}})label(id,text);
    for(auto text:{L"Step",L"Linear",L"Smooth",L"Exponential",L"Logarithmic",L"Step at start",L"Exponential reversed",L"Logarithmic reversed",L"Scripted"})SendMessageW(controls_.at(kind),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text));
    for(auto text:{L"1 row",L"½ row",L"¼ row",L"1/256 row"})SendMessageW(controls_.at(snap),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text));
    setting_=true;selection(kind,1);selection(snap,0);set(pointRow,L"0");set(pointValue,L"50");set(formula,L"mix(start,end,t)");setting_=false;
    status_=L"Choose a source in Graph / Tools → Load selection";finish();
  }
  ~GraphCurveWindow()override{
    // A child may still own a callback into this model. Retire it first.
    KillTimer(window_,3);dragging_=false;ready_=false;
    if(owns(GetCapture()))ReleaseCapture();
    bank_.reset();workbench_.reset();reference_.reset();
  }
  bool retainedDraft()const{
    return pending_||dragging_||openingChild_||draft()||
      (bank_&&(bank_->visible()||bank_->retainedDraft()))||
      (workbench_&&(workbench_->visible()||workbench_->retainedDraft()));
  }
  bool pending()const noexcept{return pending_||openingChild_;}
  bool capturedCurrent()const{return current();}
  std::optional<Target> capturedTarget()const{return hasTarget()?std::optional<Target>(target_):std::nullopt;}
  Json formulaWorkbenchSnapshot()const{return workbench_?workbench_->snapshot():Json{{"visible",false}};}
  Json formulaReferenceSnapshot()const{return reference_?reference_->snapshot():Json{{"visible",false}};}
  Json envelopeBankSnapshot()const{return bank_?bank_->snapshot():Json{{"visible",false}};}
  void openFormulaReference(){
    if(reference_){reference_->show();return;}
    OpeningChild opening(*this);
    auto next=std::make_unique<FormulaWorkbenchWindow>(window_,L"Envelope formula reference","",Json::object(),-1,request_);
    reference_=std::move(next);reference_->show();
  }
  void hide()override{if(dragging_)cancelDrag();KillTimer(window_,3);NativeToolWindow::hide();}
  void initializeHidden(std::optional<Target> target={}){
    require(!(GetWindowLongPtrW(window_,GWL_STYLE)&WS_VISIBLE)&&!docked()&&!initialized_&&document_.empty()&&generation_==0&&!bank_&&!workbench_&&!reference_&&!retainedDraft(),"Hidden initialization requires a fresh undocked curve editor");
    captureSelection(std::move(target),true);
  }
  void openAt(std::optional<Target> target={}){
    const bool retain=visible()||retainedDraft();
    // Layout restore may show the existing Guide-only owner without reading.
    // The first explicit open still captures a source into that same HWND.
    if(!retain||(!hasTarget()&&!retainedDraft()))captureSelection(std::move(target),!initialized_);
    show();focusPage();
  }
  bool followSelection(){
    if(retainedDraft())return false;const auto now=context_();if(now.busy)return false;
    if(!now.selected){status(L"No selected automation source / captured curve retained");return false;}
    const auto target=resolve(now,*now.selected);
    if(now.document==document_&&now.revision==revision_&&sameIdentity(target,target_))return true;
    captureSelection({},false);return true;
  }
  void reloadCaptured(){
    require(!pending_&&!dragging_&&!openingChild_,"Curve editor is busy");
    require(hasTarget(),"Choose a graph automation source first");
    const auto now=context_();require(now.document==document_,"Document changed / Follow selection to capture the new song");
    load(now,resolve(now,target_),false);
  }
  void observeContext(){
    // Root calls on published context changes, including while this tool is pinned.
    // No worker request, selection capture, raw-field write or focus change here.
    const auto now=context_();if(now.document==observedDocument_&&now.revision==observedRevision_)return;
    observedDocument_=now.document;observedRevision_=now.revision;layout();requestPaint();
  }
  Json operationGuard()const{return {{"initialized",initialized_},{"document",document_},{"revision",revision_},
    {"graph",target_.graph},{"node",target_.node},{"patternID",target_.patternID},{"pattern",target_.pattern},
    {"generation",generation_},{"selected",selected_},{"dirty",dirty_},{"fieldDraft",pointFields_},{"pending",pending()},
    {"dragging",dragging_},{"retainedDraft",retainedDraft()}};}
  bool dispatchLegacyAction(int id){
    // Root forwards named commands; edit/combo notifications stay with this HWND.
    if(id<apply||id>reference)return false;action(id,BN_CLICKED);layout();requestPaint();return true;
  }
  Json snapshot()const{
    auto result=operationGuard();Json handles=Json::array(),bounds=Json::array();
    for(size_t i=0;i<canvas_.handles.size();++i)handles.push_back({{"index",i},{"x",canvas_.handles[i].x},{"y",canvas_.handles[i].y}});
    for(const auto &[id,h]:controls_)if(IsWindowVisible(h)){RECT r{};GetWindowRect(h,&r);MapWindowPoints(nullptr,window_,reinterpret_cast<POINT *>(&r),2);bounds.push_back({{"id",id},{"bounds",{r.left,r.top,r.right,r.bottom}},{"enabled",bool(IsWindowEnabled(h))}});}
    const auto r=canvas_.viewport;result.update({{"visible",visible()},{"stale",hasTarget()&&!current()},{"expectedRevision",revision_},
      {"page",std::array<const char *,3>{"curve","formula","tools"}[size_t(page_)]},{"canvasVisible",canvasVisible_},
      {"points",points_},{"enabled",enabled_},{"selectedPoint",selected_},{"rows",rows_},{"rowsPerBeat",rowsPerBeat_},
      {"start",canvas_.start},{"end",canvas_.end},{"previewSamples",canvas_.curve.size()},{"handles",handles},
      {"canvas",{{"x",r.x},{"y",r.y},{"width",r.w},{"height",r.h}}},{"controls",bounds},
      {"raw",{{"row",utf8(field(pointRow))},{"value",utf8(field(pointValue))},{"formula",utf8(field(formula))}}},
      {"status",utf8(displayStatus())},{"envelopeBank",bank_?bank_->snapshot():Json{{"visible",false}}},
      {"formulaWorkbench",workbench_?workbench_->snapshot():Json{{"visible",false}}},
      {"formulaReference",reference_?reference_->snapshot():Json{{"visible",false}}}});return result;
  }
};
}
