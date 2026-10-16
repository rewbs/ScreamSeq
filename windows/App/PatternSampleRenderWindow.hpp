#pragma once
#include "NativeToolWindow.hpp"
#include "NativeAssetObservation.hpp"

namespace ScreamSeq {
class PatternSampleRenderWindow final : public NativeToolWindow {
public:
  using Json=Api::Json;
  struct Target {std::string document,revision;unsigned pattern=0,firstRow=0,lastRow=0,firstChannel=0,lastChannel=0;};
  using Request=std::function<Json(const std::string &,const Json &)>;
  using Context=std::function<Target()>;
  using Committed=std::function<void(const std::string &,const Json &)>;
private:
  enum:int {useSelection=6601,name,tail,output,check,render,close,acceptState,heading=6650,targetLabel,nameLabel,tailLabel,outputLabel,helpLabel,statusLabel};
  Request request_;Context context_;Committed committed_;Target target_;
  NativeWriteCompletion::Write write_;NativeWriteCompletion completion_;NativeAssetObservation observation_;Json submitted_;bool needsCapture_=false;
  Json report_=Json::object(),baseline_=Json::object();bool captured_=false,pending_=false;uint64_t generation_=0;
  Json raw()const{return Json::array({utf8(field(name)),utf8(field(tail)),choice(output)});}
  std::optional<Tracker::DocumentDraft> documentDraft()const override {
    return describeDraft(target_.document,target_.revision,Json::array({target_.pattern,target_.firstRow,target_.lastRow,target_.firstChannel,target_.lastChannel}).dump(),
      generation_,captured_&&raw()!=baseline_,pending_,!pending_&&completion_.retained());
  }
  bool current()const{const auto now=context_();return captured_&&!needsCapture_&&now.document==target_.document&&now.revision==target_.revision;}
  int choice(int id)const{return int(SendMessageW(controls_.at(id),CB_GETCURSEL,0,0));}
  void status(std::wstring value){status_=std::move(value);set(statusLabel,status_);requestPaint();}
  void error(const std::exception &e)override{status(wide(e.what()));}
  void capture(Target target){
    if(completion_.retained())throw std::runtime_error("Review the render result before choosing another selection");
    target_=std::move(target);captured_=true;needsCapture_=false;++generation_;report_=Json::object();
    set(targetLabel,L"Pattern "+std::to_wstring(target_.pattern)+L" · rows "+std::to_wstring(target_.firstRow)+L"–"+std::to_wstring(target_.lastRow)+L" · channels "+std::to_wstring(target_.firstChannel+1)+L"–"+std::to_wstring(target_.lastChannel+1)+L"\nAll notes and FX in those channels / source pattern remains unchanged");
    status(L"Selection captured / Check validates; Render creates a new sample");
  }
  void apply(bool dryRun){
    if(pending_)return;if(!current())throw std::runtime_error("Song changed / captured selection retained; choose Use current selection before rendering");
    const auto seconds=number(tail);if(seconds<0||seconds>60)throw std::runtime_error("Tail must be between 0 and 60 seconds");
    const Json params={{"pattern",target_.pattern},{"firstRow",target_.firstRow},{"lastRow",target_.lastRow},{"firstChannel",target_.firstChannel},{"lastChannel",target_.lastChannel},
      {"name",utf8(field(name))},{"createInstrument",choice(output)==1},{"tailSeconds",seconds},{"dryRun",dryRun},{"expectedRevision",target_.revision}};
    pending_=true;layout();
    try{
      if(dryRun){report_=request_("sample.renderSelection",params);status(L"Selection validated / Render adds a new sample with one Undo");}
      else{submitted_=params;observation_.clear();completion_.submit(write_,"sample.renderSelection",params,target_.document,generation_,raw());finishResult();}
    }catch(...){pending_=false;layout();throw;}
    pending_=false;layout();
  }
  void finishResult(){
    const auto returned=completion_.returned();
    if(!returned)throw std::runtime_error("Render outcome is still unknown / retained selection cannot be rendered again until the operation is reconciled");
    report_=returned->result;
    auto text=L"Created sample "+std::to_wstring(report_.at("sample").get<unsigned>());
    if(const auto instrument=report_.value("instrument",0u))text+=L" + mapped instrument "+std::to_wstring(instrument);
    const auto now=context_();
    if(now.document==returned->document&&now.revision==returned->revision)committed_(returned->document,report_);
    else text+=L" / song changed since completion; selection left unchanged";
    if(generation_==completion_.generation())baseline_=completion_.fields();
    status(text+L" / result reviewed; the render was not repeated");completion_.finish();
  }
  void reviewResult(){
    if(pending_||!completion_.retained())return;
    pending_=true;layout();
    try{request_("synchronizeView",Json::object());if(completion_.returned())finishResult();else{
      observation_.read(request_,[this]{const auto now=context_();return std::pair(now.document,now.revision);},[this]{return generation_;},target_.document,Json{{"completion",completion_.snapshot()},{"params",submitted_}});
      status(L"Current samples and instruments inspected / earlier render unverified; Accept state acknowledges without rendering again");
    }}
    catch(...){pending_=false;layout();throw;}
    pending_=false;layout();
  }
  void action(int id,unsigned notification)override{
    if((notification==EN_CHANGE&&(id==name||id==tail))||(notification==CBN_SELCHANGE&&id==output)){++generation_;return;}
    if(pending_||notification!=BN_CLICKED)return;
    if(id==acceptState){if(!completion_.retained())throw std::runtime_error("No retained result to acknowledge");const auto now=context_();observation_.check({now.document,now.revision},generation_);report_=observation_.report();needsCapture_=true;completion_.finish();observation_.clear();status(L"Unverified result acknowledged / options retained; Use current selection before a new render");layout();return;}
    if(id==useSelection)capture(context_());else if(id==check){if(!completion_.retained())apply(true);}else if(id==render){if(completion_.retained())reviewResult();else apply(false);}else if(id==close)hide();
  }
  bool key(WPARAM value,bool,bool)override{
    if(value==VK_ESCAPE){hide();return true;}
    if(value==VK_RETURN){wchar_t type[32]{};GetClassNameW(GetFocus(),type,32);if(_wcsicmp(type,L"Button")==0&&IsWindowEnabled(GetFocus()))action(GetDlgCtrlID(GetFocus()),BN_CLICKED);return true;}return false;
  }
  void layout()override{
    if(!ready_)return;const auto [w,h]=size();place(heading,18,12,w-36,24);place(targetLabel,18,48,w-36,52);place(useSelection,18,108,180,28);
    place(nameLabel,18,154,100,20);place(name,18,178,w-270,27);place(tailLabel,w-238,154,220,20);place(tail,w-238,178,110,27);
    place(outputLabel,18,220,100,20);place(output,18,244,250,180);place(helpLabel,18,290,w-36,48);place(statusLabel,18,348,w-36,std::max(48.f,h-410));
    place(check,18,h-46,90,28);place(render,116,h-46,134,28);place(close,w-118,h-46,100,28);place(acceptState,258,h-46,174,28,completion_.retained());EnableWindow(controls_.at(acceptState),!pending_&&observation_.ready());
    for(int id:{useSelection,name,tail,output,check,render,close})EnableWindow(controls_.at(id),!pending_);
    EnableWindow(controls_.at(useSelection),!pending_&&!completion_.retained());
    EnableWindow(controls_.at(check),!pending_&&!completion_.retained()&&current());
    set(render,completion_.retained()?L"Review result":L"Render sample");
    EnableWindow(controls_.at(render),!pending_&&(completion_.retained()||current()));
  }
  void paint(RenderSurface &s)override{const auto [w,h]=size();s.fill(0,0,w,h,0x18222d);}
public:
  bool hasUnresolvedResult()const noexcept{return pending_||completion_.retained();}
  PatternSampleRenderWindow(HWND owner,Request request,Context context,Committed committed,NativeWriteCompletion::Write write):NativeToolWindow(owner),request_(std::move(request)),context_(std::move(context)),committed_(std::move(committed)),write_(std::move(write)){
    minimumWidth_=600;minimumHeight_=490;create(L"ScreamSeq.PatternSampleRender",L"Render pattern selection",660,520);button(acceptState,L"Accept observed state");
    for(auto [id,text]:std::initializer_list<std::pair<int,const wchar_t *>>{{heading,L"RENDER PATTERN SELECTION"},{targetLabel,L""},{nameLabel,L"Sample name"},{tailLabel,L"Tail after selection (seconds)"},{outputLabel,L"Create"},{helpLabel,L"The captured row and channel range includes complete channels.\nMoving the cursor keeps this selection. Song edits require a new capture."},{statusLabel,L""}})label(id,text);
    for(auto [id,text]:std::initializer_list<std::pair<int,const wchar_t *>>{{useSelection,L"Use current selection"},{check,L"Check"},{render,L"Render sample"},{close,L"Close"}})button(id,text);
    edit(name,L"Pattern selection",200);edit(tail,L"0",12);combo(output);
    for(const auto text:{L"Sample",L"Sample + mapped instrument"})ScreamSeq::NativeInputGate::present(controls_.at(output),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text));ScreamSeq::NativeInputGate::present(controls_.at(output),CB_SETCURSEL,0,0);baseline_=raw();finish();
  }
  void openAt(bool instrument,Target target){if(!captured_){capture(std::move(target));ScreamSeq::NativeInputGate::present(controls_.at(output),CB_SETCURSEL,instrument?1:0,0);baseline_=raw();}show();layout();}
  void documentChanged(){if(ready_&&!pending_){layout();requestPaint();}}
  void hide()override{if(pending_){status(L"Wait for the render request to finish before closing");return;}NativeToolWindow::hide();}
  Json snapshot()const{return {{"visible",visible()},{"pending",pending_},{"completion",completion_.snapshot()},{"observation",observation_.report()},{"needsCapture",needsCapture_},{"document",target_.document},{"expectedRevision",target_.revision},{"stale",!current()},{"pattern",target_.pattern},{"firstRow",target_.firstRow},{"lastRow",target_.lastRow},{"firstChannel",target_.firstChannel},{"lastChannel",target_.lastChannel},{"name",utf8(field(name))},{"tailSeconds",utf8(field(tail))},{"createInstrument",choice(output)==1},{"report",report_},{"status",utf8(status_)}};}
};
}
