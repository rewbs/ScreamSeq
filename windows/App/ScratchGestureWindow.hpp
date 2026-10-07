#pragma once
#include "FormulaWorkbenchWindow.hpp"
#include "ScratchEditorState.hpp"
#include <iomanip>
#include <sstream>

namespace ScreamSeq {
// Native controls expose the same point edits as the paired Direct2D canvases.
// Painting never reads the document. Each gesture commits through the worker
// once on release; stale or rejected writes retain the complete visible draft.
class ScratchGestureWindow final : public NativeToolWindow {
public:
  using Json=Api::Json;
  using Request=std::function<Json(const std::string &,const Json &)>;
  using Context=std::function<std::pair<std::string,std::string>()>;
  using Capture=std::function<std::optional<ScratchPatternTarget>()>;
  using Resolve=std::function<std::optional<ScratchPatternTarget>(const ScratchPatternTarget &)>;
private:
  enum:int{phrase=3101,name,preset,newPhrase,clone,unique,removePhrase,reload,lane,pointsList,pointPosition,pointValue,pointKind,pointFormula,setPoint,deletePoint,expand,reference,snap,zoomIn,zoomOut,fit,capture,returnRow,use,play,undo,redo,addPoint,more,
    intro=3201,linkLabel,targetLabel,statusLabel,positionLabel,valueLabel,formulaLabel,pointLabel};
  Request request_;Context context_;Capture capture_;Resolve resolve_;std::function<void(const ScratchPatternTarget &,bool)> navigate_;
  std::string document_,revision_;std::optional<ScratchPatternTarget> target_;
  ScratchEditorState state_;Json bank_=Json::array(),presets_=Json::array();
  std::array<AutomationCanvas,2> canvases_;std::array<Json,2> values_{Json::array(),Json::array()};
  std::unique_ptr<FormulaWorkbenchWindow> workbench_,reference_;
  bool pending_=false,setting_=false,fieldsDirty_=false,dragging_=false,previewNeeded_=false,saveNeeded_=false,hiding_=false,transportSpace_=false;
  Json dragBefore_;std::array<int,2> dragSelection_{};unsigned snapUnits_=4096;
  void require(bool value,const char *why)const{if(!value)throw std::runtime_error(why);}
  bool draft()const{return state_.dirty()||fieldsDirty_;}
  bool sameDocument()const{return context_().first==document_;}
  bool current()const{return context_()==std::pair(document_,revision_);}
  void status(std::wstring value){status_=std::move(value);set(statusLabel,status_);requestPaint();}
  void error(const std::exception &e)override{status(wide(e.what())+L" · Your draft is retained");}
  double cycleBeats()const{if(target_&&target_->gesture()){const auto &p=target_->command.at("parameters");return p.value("beats",1.0)/std::max(1.0,p.value("repeats",1.0));}return 1;}
  Json previewParams(unsigned which)const{return {{"rows",257},{"rowsPerBeat",256},{"span",65537},{"start",canvases_[0].start},{"end",canvases_[0].end},{"samples",1024},{"scratchBeats",cycleBeats()},{"points",state_.points(which)}};}
  Json call(const std::string &method,Json params,bool write=false){
    require(!pending_,"Scratch editor is busy");require(current(),"Song changed; Reload the retained scratch editor before editing");
    const auto sent=revision_;if(write)params["expectedRevision"]=sent;pending_=true;layout();
    try{auto result=request_(method,params);const auto now=context_();require(now.first==document_,"The captured song is no longer open");
      if(write){revision_=now.second;if(target_&&target_->revision==sent)target_->revision=revision_;}else require(now.second==sent,"Song changed during the read; Reload before editing");
      pending_=false;layout();return result;
    }catch(...){pending_=false;layout();throw;}
  }
  void showPoint(){
    setting_=true;const auto &points=state_.points(state_.lane);const int selected=state_.selected[state_.lane];
    const Json p=selected>=0&&size_t(selected)<points.size()?points[size_t(selected)]:Json{{"position",0},{"value",.5},{"curve","linear"}};
    set(pointPosition,p.at("position").get<double>()/65536);set(pointValue,p.at("value").get<double>()*100);set(pointFormula,p.value("formula",std::string("mix(start,end,t)")));
    int kind=1;for(size_t i=0;i<ScratchJSON::curves.size();++i)if(p.value("curve",std::string("linear"))==ScratchJSON::curves[i])kind=int(i);
    SendMessageW(controls_.at(pointKind),CB_SETCURSEL,kind,0);SendMessageW(controls_.at(lane),CB_SETCURSEL,state_.lane,0);
    SendMessageW(controls_.at(pointsList),LB_RESETCONTENT,0,0);
    for(size_t i=0;i<points.size();++i){std::wostringstream text;text<<i+1<<L" · "<<std::setprecision(5)<<points[i].at("position").get<double>()/65536<<L" cycle · "<<points[i].at("value").get<double>()*100<<L"% · "<<wide(points[i].value("curve",std::string("linear")));SendMessageW(controls_.at(pointsList),LB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.str().c_str()));}
    SendMessageW(controls_.at(pointsList),LB_SETCURSEL,selected,0);fieldsDirty_=false;setting_=false;
  }
  void fillPhrases(){setting_=true;SendMessageW(controls_.at(phrase),CB_RESETCONTENT,0,0);int selected=-1;for(size_t i=0;i<bank_.size();++i){const auto &item=bank_[i];const auto text=std::to_wstring(item.at("id").get<unsigned>())+L" · "+wide(item.at("name").get<std::string>());SendMessageW(controls_.at(phrase),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));if(item.at("id")==state_.id())selected=int(i);}SendMessageW(controls_.at(phrase),CB_SETCURSEL,selected,0);setting_=false;}
  void select(unsigned id){
    const auto found=std::find_if(bank_.begin(),bank_.end(),[&](const auto &p){return p.at("id")==id;});state_.load(found==bank_.end()?Json::object():*found);
    values_={Json::array(),Json::array()};setting_=true;set(name,state_.draft.value("name",std::string()));setting_=false;showPoint();fillPhrases();previewNeeded_=true;SetTimer(window_,3,100,nullptr);layout();
  }
  void load(bool discard=false,unsigned selected=0,bool recapture=false){
    require(!pending_&&!dragging_,"Scratch editor is busy");require(discard||!draft(),"Finish or Reload the retained phrase draft first");
    const auto captured=context_();require(recapture||captured.first==document_,"The captured song is no longer open; Reload explicitly to discard its retained draft");
    const auto candidateTarget=recapture?capture_():target_;pending_=true;layout();
    Json result;try{result=request_("scratch.gestures.get",Json::object());require(context_()==captured,"Song changed while loading; retained draft was not replaced");}
    catch(...){pending_=false;layout();throw;}
    // A failed/busy read must never attach an old draft to a new document.
    pending_=false;document_=captured.first;revision_=captured.second;target_=candidateTarget;bank_=result.at("gestures");presets_=result.at("presets");saveNeeded_=false;
    if(!selected&&recapture&&target_)selected=target_->gesture();if(!selected&&!recapture)selected=state_.id();if(std::none_of(bank_.begin(),bank_.end(),[&](const auto &p){return p.at("id")==selected;})){selected=target_?target_->gesture():0;if(std::none_of(bank_.begin(),bank_.end(),[&](const auto &p){return p.at("id")==selected;}))selected=bank_.empty()?0:bank_[0].at("id").get<unsigned>();}select(selected);
    status(L"Shared phrase edits save immediately · drag then release · Enter sets fields · Ctrl+Z restores an edit");
  }
  void save(){
    require(!fieldsDirty_,"Press Enter to set the point fields first");if(!state_.dirty())return;state_.validate();
    const auto sent=state_.draft;const auto generation=state_.generation;try{call("scratch.gestures.set",sent,true);}catch(const Api::ApiError &e){if(e.code==-32002){saveNeeded_=true;SetTimer(window_,4,120,nullptr);}throw;}state_.accept(sent,generation);if(generation==state_.generation)saveNeeded_=false;
    auto result=call("scratch.gestures.get",Json::object());bank_=result.at("gestures");fillPhrases();status(state_.dirty()?L"Earlier edit saved; newer draft retained":L"Phrase saved · every linked SK use updated · Ctrl+Z to undo");
  }
  void changed(bool commit=true){++state_.generation;if(commit)saveNeeded_=true;values_={Json::array(),Json::array()};previewNeeded_=true;for(unsigned i=0;i<2;++i)canvases_[i].rebuild(state_.points(i),values_[i]);SetTimer(window_,3,100,nullptr);if(commit)save();requestPaint();}
  void pointFields(){
    require(!state_.empty(),"Choose a scratch phrase");const double position=number(pointPosition),value=number(pointValue);require(position>=0&&position<=1&&value>=0&&value<=100,"Use cycle position 0–1 and value 0–100%");
    const auto index=SendMessageW(controls_.at(pointKind),CB_GETCURSEL,0,0);require(index>=0&&index<9,"Choose an outgoing curve");Json point={{"position",unsigned(std::llround(position*65536))},{"value",value/100},{"curve",ScratchJSON::curves[size_t(index)]}};
    if(index==8)point["formula"]=utf8(field(pointFormula));state_.replace(std::move(point));fieldsDirty_=false;showPoint();changed();
  }
  void preview(){
    if(state_.empty()||pending_)return;const auto generation=state_.generation;previewNeeded_=false;
    for(unsigned i=0;i<2;++i){Json result;try{result=call("automation.formula.preview",previewParams(i));}catch(const Api::ApiError &e){if(e.code==-32002){previewNeeded_=true;SetTimer(window_,3,120,nullptr);}throw;}if(generation!=state_.generation)return;values_[i]=result.at("values");canvases_[i].rebuild(state_.points(i),values_[i]);}requestPaint();
  }
  Json formulaRequest(const std::string &method,const Json &params){auto result=request_(method,params);return method=="automation.formula.reference"?scratchFormulaReference(std::move(result)):result;}
  void openFormula(bool referenceOnly){
    if(referenceOnly){if(!reference_)reference_=std::make_unique<FormulaWorkbenchWindow>(window_,L"Scratch formula reference","",Json::object(),-1,[this](const auto &m,const auto &p){return formulaRequest(m,p);});reference_->show();return;}
    if(workbench_&&(workbench_->visible()||workbench_->retainedDraft())){workbench_->show();return;}
    require(!fieldsDirty_,"Set the point fields before expanding its formula");const auto which=state_.lane;const auto index=state_.selected[which];require(index>=0,"Select a scripted point");const auto original=state_.points(which).at(size_t(index));require(original.value("curve",std::string())=="scripted","Choose Scripted for this point first");
    const auto generation=state_.generation;const auto slot=state_.id();const auto revision=revision_;
    auto valid=[this,generation,slot,which,index,original,revision]{return current()&&!pending_&&!fieldsDirty_&&revision_==revision&&state_.generation==generation&&state_.id()==slot&&state_.lane==which&&state_.selected[which]==index&&size_t(index)<state_.points(which).size()&&state_.points(which)[size_t(index)]==original;};
    auto apply=[this,valid,original](const std::string &text){if(!valid())return false;auto point=original;point["formula"]=text;state_.replace(std::move(point));showPoint();changed();return true;};
    auto params=previewParams(which);params["start"]=0;params["end"]=65536;
    workbench_=std::make_unique<FormulaWorkbenchWindow>(window_,L"Scratch phrase · "+field(name)+L" · Formula",original.at("formula").get<std::string>(),params,index,[this](const auto &m,const auto &p){return formulaRequest(m,p);},std::move(valid),std::move(apply));workbench_->show();
  }
  ScratchPatternTarget destination()const{
    require(target_.has_value(),"Capture a pattern FX cell first");require(target_->revision==revision_,"The captured cell revision changed; use Current cursor to capture it again");
    const auto resolved=resolve_(*target_);require(resolved&&resolved->sameCell(*target_)&&resolved->command==target_->command,"The captured FX cell changed or was removed; capture it again");return *resolved;
  }
  void cancelDrag(){if(!dragging_)return;state_.draft=dragBefore_;state_.selected=dragSelection_;++state_.generation;dragging_=false;ReleaseCapture();showPoint();changed(false);}
  void action(int id,unsigned notification)override{
    if(setting_)return;
    if(id==name||id==pointPosition||id==pointValue||id==pointFormula){if(notification!=EN_CHANGE)return;if(pending_)return;if(id==name){state_.draft["name"]=utf8(field(name));++state_.generation;saveNeeded_=true;SetTimer(window_,4,350,nullptr);}else{fieldsDirty_=true;++state_.generation;}return;}
    if(id>=4001&&id<4001+int(presets_.size())){require(!draft(),"Finish or Reload the retained draft first");const auto result=call("scratch.gestures.set",{{"preset",presets_.at(size_t(id-4001)).at("id")}},true);load(false,result.at("id"));return;}
    if(id>=intro)return;
    if(id==phrase||id==preset||id==lane||id==pointKind||id==snap){if(notification!=CBN_SELCHANGE)return;}else if(id==pointsList){if(notification!=LBN_SELCHANGE)return;}else if(notification!=BN_CLICKED)return;
    require(!pending_&&!dragging_,"Scratch editor is busy");
    if(id==reload){load(true,0,!sameDocument());return;}if(id==reference){openFormula(true);return;}
    if(id==more){HMENU menu=CreatePopupMenu();if(!menu)throw std::runtime_error("Cannot open phrase actions");const bool enabled=!state_.empty()&&!draft()&&current();
      AppendMenuW(menu,MF_STRING|(enabled?0:MF_GRAYED),clone,L"Duplicate phrase");AppendMenuW(menu,MF_STRING|(enabled&&target_&&target_->gesture()==state_.id()?0:MF_GRAYED),unique,L"Make unique at captured row");AppendMenuW(menu,MF_STRING|(enabled?0:MF_GRAYED),removePhrase,L"Remove phrase");AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING|(!draft()&&current()?0:MF_GRAYED),undo,L"Undo\tCtrl+Z");AppendMenuW(menu,MF_STRING|(!draft()&&current()?0:MF_GRAYED),redo,L"Redo\tCtrl+Y");
      RECT r{};GetWindowRect(controls_.at(more),&r);const auto picked=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,r.left,r.bottom,0,window_,nullptr);DestroyMenu(menu);if(picked)action(int(picked),BN_CLICKED);return;}

    if(id==phrase){if(draft()){fillPhrases();throw std::runtime_error("Finish or Reload the retained draft before selecting another phrase");}const auto selected=SendMessageW(controls_.at(phrase),CB_GETCURSEL,0,0);if(selected>=0)select(bank_.at(size_t(selected)).at("id"));return;}
    if(id==preset)return;
    if(id==newPhrase){require(!draft(),"Finish or Reload the retained draft first");HMENU menu=CreatePopupMenu();if(!menu)throw std::runtime_error("Cannot open scratch presets");for(size_t i=0;i<presets_.size();++i){const auto title=wide(presets_[i].at("name").get<std::string>());AppendMenuW(menu,MF_STRING,4001+i,title.c_str());}RECT r{};GetWindowRect(controls_.at(newPhrase),&r);const auto picked=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,r.left,r.bottom,0,window_,nullptr);DestroyMenu(menu);if(picked)action(int(picked),BN_CLICKED);return;}
    if(id==capture){require(!draft(),"Finish the retained phrase edit before capturing another cell");require(capture_().has_value(),"Choose an editable pattern FX cell");load(false,0,true);return;}
    if(id==returnRow||id==play){require(!draft(),"Finish the phrase edit before returning or playing");const auto target=destination();navigate_(target,id==play);return;}
    if(id==undo||id==redo){require(!draft(),"Finish or Reload the retained draft before Undo");call(id==undo?"history.undo":"history.redo",Json::object(),true);if(target_){auto value=resolve_(*target_);if(value)target_=*value;}load();return;}
    require(!state_.empty(),"Create or select a scratch phrase first");
    if(id==lane||id==pointsList){if(fieldsDirty_){SendMessageW(controls_.at(lane),CB_SETCURSEL,state_.lane,0);SendMessageW(controls_.at(pointsList),LB_SETCURSEL,state_.selected[state_.lane],0);throw std::runtime_error("Set or discard the point fields before changing selection");}if(id==lane)state_.lane=unsigned(SendMessageW(controls_.at(lane),CB_GETCURSEL,0,0));else state_.selected[state_.lane]=int(SendMessageW(controls_.at(pointsList),LB_GETCURSEL,0,0));showPoint();return;}
    if(id==addPoint){require(!fieldsDirty_,"Set or discard the point fields first");const auto &points=state_.points(state_.lane);require(points.size()<256,"A scratch lane supports at most 256 points");unsigned start=0,gap=0;for(size_t i=1;i<points.size();++i){const auto left=points[i-1].at("position").get<unsigned>(),right=points[i].at("position").get<unsigned>();if(right-left>gap){start=left;gap=right-left;}}require(gap>1,"No free cycle position remains");state_.selected[state_.lane]=-1;showPoint();setting_=true;set(pointPosition,(start+gap/2)/65536.0);setting_=false;fieldsDirty_=true;++state_.generation;layout();SetFocus(controls_.at(pointPosition));return;}
    if(id==pointKind){fieldsDirty_=true;pointFields();return;}if(id==setPoint){pointFields();return;}if(id==deletePoint){require(!fieldsDirty_,"Set or discard the point fields first");state_.remove();showPoint();changed();return;}
    if(id==expand){openFormula(false);return;}
    if(id==snap){const unsigned units[]{4096,2048,1024,1};snapUnits_=units[std::clamp(int(SendMessageW(controls_.at(snap),CB_GETCURSEL,0,0)),0,3)];return;}
    if(id==fit||id==zoomIn||id==zoomOut){if(id==fit){canvases_[0].start=0;canvases_[0].end=65536;}else canvases_[0].zoom(id==zoomIn?2:.5,256);canvases_[1].start=canvases_[0].start;canvases_[1].end=canvases_[0].end;previewNeeded_=true;SetTimer(window_,3,100,nullptr);return;}
    require(!draft(),"Finish or Reload the retained draft first");
    if(id==clone){const auto result=call("scratch.gestures.clone",{{"id",state_.id()}},true);load(false,result.at("id"));return;}
    if(id==removePhrase){call("scratch.gestures.remove",{{"id",state_.id()}},true);state_.load(Json::object());load();return;}
    if(id==use){const auto target=destination();call("pattern.effect.set",target.use(state_.id()),true);target_=resolve_(target);bank_=call("scratch.gestures.get",Json::object()).at("gestures");fillPhrases();status(L"SK placed at the captured cell · pattern values remain editable inline");return;}
    if(id==unique){const auto target=destination();require(target.gesture()==state_.id(),"The captured SK must use the selected phrase before Make unique");makeUnique(target);return;}
  }
  void makeUnique(const ScratchPatternTarget &target){
    const auto result=call("scratch.gestures.clone",{{"id",state_.id()},{"target",{{"pattern",target.pattern},{"row",target.row},{"channel",target.channel},{"column",target.column}}}},true);
    target_=resolve_(target);load(false,result.at("id"));status(L"Independent phrase created and captured SK reassigned · one Undo restores both");
  }
  static LRESULT CALLBACK pointFieldProc(HWND h,UINT message,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR opaque){
    auto &self=*reinterpret_cast<ScratchGestureWindow *>(opaque);
    if(message==WM_KILLFOCUS&&self.ready_&&self.visible()&&!self.hiding_&&!self.pending_&&!self.setting_&&self.fieldsDirty_){
      const auto next=reinterpret_cast<HWND>(w);const auto id=next?GetDlgCtrlID(next):0;
      // Moving among the fields is one point edit. Leaving the row commits it;
      // invalid text remains visible and never changes the selected point.
      if(id!=pointPosition&&id!=pointValue&&id!=pointFormula&&id!=pointKind&&id!=setPoint)
        try{self.pointFields();self.layout();}catch(const std::exception &e){self.error(e);}
    }
    return DefSubclassProc(h,message,w,l);
  }
  bool key(WPARAM code,bool ctrl,bool shift)override{
    const int focus=GetFocus()==window_?0:GetDlgCtrlID(GetFocus());
    if(code==VK_ESCAPE){if(dragging_)cancelDrag();else if(fieldsDirty_){showPoint();layout();}else hide();return true;}
    if(code==VK_F6){SetFocus(GetFocus()==window_?controls_.at(pointsList):window_);return true;}
    if(ctrl&&(code=='Z'||code=='Y')&&(focus==name||focus==pointPosition||focus==pointValue||focus==pointFormula))return false;
    if(ctrl&&(code=='Z'||code=='Y')){action(code=='Y'||shift?redo:undo,BN_CLICKED);return true;}
    if(code==VK_RETURN&&(focus==pointPosition||focus==pointValue||focus==pointFormula)){pointFields();return true;}
    if(code==VK_RETURN&&focus==name){save();return true;}
    if(ctrl&&code==VK_RETURN){if(fieldsDirty_)pointFields();else save();return true;}
    if(code==VK_INSERT&&(focus==0||focus==pointsList)){action(addPoint,BN_CLICKED);return true;}
    if(code==VK_RETURN&&focus>0){wchar_t type[32]{};GetClassNameW(GetFocus(),type,32);if(_wcsicmp(type,L"Button")==0){action(focus,BN_CLICKED);return true;}}
    if(code==VK_SPACE&&!ctrl&&!(GetKeyState(VK_MENU)&0x8000)&&(focus==0||focus==pointsList)){if(!transportSpace_){transportSpace_=true;request_("scratch.transport.toggle",Json::object());}return true;}
    if(pending_||fieldsDirty_||state_.empty()||(focus!=0&&focus!=pointsList)||ctrl)return false;
    if(code==VK_TAB&&focus==0){auto &selected=state_.selected[state_.lane];selected=(selected+(shift?-1:1)+int(state_.points(state_.lane).size()))%int(state_.points(state_.lane).size());showPoint();layout();requestPaint();return true;}
    if(code==VK_DELETE){state_.remove();showPoint();changed();return true;}
    if(focus==0&&state_.selected[state_.lane]>=0&&(code==VK_LEFT||code==VK_RIGHT||code==VK_UP||code==VK_DOWN)){auto p=state_.points(state_.lane)[size_t(state_.selected[state_.lane])];if(code==VK_LEFT||code==VK_RIGHT)p["position"]=unsigned(std::clamp(p.at("position").get<double>()+(code==VK_LEFT?-1:1)*(shift?1.0:snapUnits_),0.,65536.));else p["value"]=std::clamp(p.at("value").get<double>()+(code==VK_UP?1:-1)*(shift?.001:.01),0.,1.);state_.replace(std::move(p));showPoint();changed();return true;}return false;
  }
  bool keyUp(WPARAM key)override{if(key!=VK_SPACE)return false;const bool handled=transportSpace_;transportSpace_=false;return handled;}
  void deactivate()override{transportSpace_=false;}
  void mouse(UINT message,float x,float y,WPARAM)override{
    if(message==WM_CAPTURECHANGED){if(dragging_)cancelDrag();return;}
    if(message==WM_LBUTTONUP&&dragging_){dragging_=false;ReleaseCapture();showPoint();changed();layout();return;}
    if(pending_||state_.empty()||fieldsDirty_)return;
    if(message==WM_LBUTTONDOWN){for(unsigned i=0;i<2;++i)if(canvases_[i].viewport.contains(x,y)){require(current(),"Song changed; Reload before drawing");state_.lane=i;dragBefore_=state_.draft;dragSelection_=state_.selected;state_.selected[i]=canvases_[i].hit(x,y);dragging_=true;SetFocus(window_);SetCapture(window_);if(state_.selected[i]>=0){showPoint();layout();return;}break;}if(!dragging_)return;}
    if((message==WM_MOUSEMOVE||message==WM_LBUTTONDOWN)&&dragging_){const auto &canvas=canvases_[state_.lane];auto position=unsigned(std::clamp(std::round(canvas.position(x)/snapUnits_)*snapUnits_,0.,65536.));const auto selected=state_.selected[state_.lane];
      Json point=selected>=0?state_.points(state_.lane).at(size_t(selected)):Json{{"curve","linear"}};
      if(selected>=0){const auto old=point.at("position").get<unsigned>();if(old==0||old==65536)position=old;}
      for(size_t i=0;i<state_.points(state_.lane).size();++i)if(int(i)!=selected&&state_.points(state_.lane)[i].at("position")==position)return;
      point["position"]=position;point["value"]=canvas.value(y);state_.replace(std::move(point));changed(false);}
  }
  bool wheel(UINT message,float x,float y,WPARAM flags)override{
    if(!canvases_[0].viewport.contains(x,y)&&!canvases_[1].viewport.contains(x,y))return false;
    const auto delta=double(GET_WHEEL_DELTA_WPARAM(flags))/WHEEL_DELTA;if(GET_KEYSTATE_WPARAM(flags)&MK_CONTROL)canvases_[0].zoom(std::pow(2.,delta),256);else canvases_[0].pan((message==WM_MOUSEHWHEEL?1:-1)*delta*(canvases_[0].end-canvases_[0].start)*.125,256);
    canvases_[1].start=canvases_[0].start;canvases_[1].end=canvases_[0].end;previewNeeded_=true;SetTimer(window_,3,100,nullptr);return true;
  }
  void timer(UINT_PTR id)override{KillTimer(window_,id);if(!visible())return;if(pending_||dragging_){SetTimer(window_,id,100,nullptr);return;}if(id==3&&previewNeeded_)preview();if(id==4&&saveNeeded_&&state_.dirty()&&!fieldsDirty_)save();}
  void layout()override{
    if(!ready_)return;const auto [w,h]=size();const float plotWidth=w-286,plotHeight=(h-359)/2;
    place(intro,14,10,w-28,24);place(phrase,14,40,230,250);place(name,252,40,std::max(140.f,w-514),26);place(newPhrase,w-254,40,128,26);place(reload,w-118,40,104,26);
    place(linkLabel,14,75,w-156,22);place(more,w-126,75,112,25);for(int id:{clone,unique,removePhrase,undo,redo})ShowWindow(controls_.at(id),SW_HIDE);
    canvases_[0].viewport={48,136,plotWidth,plotHeight};canvases_[1].viewport={48,171+plotHeight,plotWidth,plotHeight};
    place(lane,w-218,112,202,240);place(pointsList,w-218,144,202,std::max(80.f,h-394));
    const float y=h-178;place(pointLabel,14,y,w-28,21);place(positionLabel,14,y+28,36,24);place(pointPosition,54,y+25,94,26);place(valueLabel,154,y+28,22,24);place(pointValue,178,y+25,80,26);place(pointKind,266,y+25,180,230);place(snap,454,y+25,120,230);place(setPoint,582,y+25,70,26);place(deletePoint,660,y+25,74,26);place(addPoint,742,y+25,92,26);
    place(zoomOut,w-218,h-242,60,25);place(zoomIn,w-150,h-242,60,25);place(fit,w-82,h-242,66,25);
    place(formulaLabel,14,y+60,54,24);place(pointFormula,72,y+57,std::max(120.f,w-284),26);place(expand,w-204,y+57,88,26);place(reference,w-108,y+57,94,26);
    place(targetLabel,14,h-81,std::max(120.f,w-528),26);place(capture,w-502,h-84,114,26);place(returnRow,w-380,h-84,110,26);place(play,w-262,h-84,114,26);place(use,w-140,h-84,126,26);place(statusLabel,14,h-45,w-28,34);
    const bool enabled=!state_.empty()&&!pending_&&sameDocument();const int selected=state_.selected[state_.lane];const bool chosen=selected>=0&&size_t(selected)<state_.points(state_.lane).size();
    for(auto [id,control]:controls_)if(id<intro)EnableWindow(control,!pending_);for(int id:{name,clone,removePhrase,unique,pointsList,lane,pointKind,pointFormula,setPoint,deletePoint,addPoint,expand,use})EnableWindow(controls_.at(id),enabled);
    EnableWindow(controls_.at(pointValue),enabled);EnableWindow(controls_.at(pointPosition),enabled&&(!chosen||(state_.points(state_.lane)[size_t(selected)].at("position")!=0&&state_.points(state_.lane)[size_t(selected)].at("position")!=65536)));
    EnableWindow(controls_.at(unique),enabled&&!draft()&&target_&&target_->gesture()==state_.id());
    EnableWindow(controls_.at(phrase),!pending_&&!draft());EnableWindow(controls_.at(newPhrase),!pending_&&!draft());
    if(!state_.empty()){unsigned uses=0;for(const auto &p:bank_)if(p.at("id")==state_.id())uses=p.value("uses",0u);set(linkLabel,L"Song phrase "+std::to_wstring(state_.id())+L" · "+std::to_wstring(uses)+L" linked SK uses · changes update every use");}else set(linkLabel,L"Choose New phrase to create paired Motion and Fader envelopes.");
    set(pointLabel,state_.lane?L"Fader point · Insert adds · Tab selects · arrows move · Shift gives fine steps · Delete removes interior points":L"Motion point · Insert adds · Tab selects · arrows move · Shift gives fine steps · Delete removes interior points");
    set(targetLabel,target_?L"Pattern "+std::to_wstring(target_->pattern)+L" · row "+std::to_wstring(target_->row)+L" · channel "+std::to_wstring(target_->channel+1)+L" · FX "+std::to_wstring(target_->column+1):L"No captured FX cell");
    for(unsigned i=0;i<2;++i)canvases_[i].rebuild(state_.points(i),values_[i]);
  }
  void paint(RenderSurface &s)override{
    const auto [w,h]=size();s.fill(0,0,w,h,0x18222d);
    for(unsigned lane=0;lane<2;++lane){const auto &canvas=canvases_[lane];const auto &r=canvas.viewport;s.uiText(lane?L"FADER · 100% open / 0% closed":L"RECORD MOTION · rising forward / falling reverse / flat held",r.x,r.y-23,r.w,0x9fb7c9);s.fill(r.x,r.y,r.w,r.h,0x10171f);s.clip(r.x,r.y,r.w,r.h);
      for(int i=0;i<=4;++i){s.line(r.x,r.y+r.h*i/4,r.x+r.w,r.y+r.h*i/4,0x2a3948);s.line(r.x+r.w*i/4,r.y,r.x+r.w*i/4,r.y+r.h,0x2a3948);}
      const auto &path=canvas.curve.empty()?canvas.handles:canvas.curve;for(size_t i=1;i<path.size();++i)s.line(path[i-1].x,path[i-1].y,path[i].x,path[i].y,lane?0xe8ba76:0x68d3bc,2);
      for(size_t i=0;i<canvas.handles.size();++i){const auto p=canvas.handles[i];s.fill(p.x-4,p.y-4,8,8,int(i)==state_.selected[lane]?0xffffff:lane?0xe8ba76:0x68d3bc);}s.unclip();s.outline(r.x,r.y,r.w,r.h,state_.lane==lane?0x68d3bc:0x344858);s.uiText(L"100%",r.x-40,r.y,38,0x9fb7c9);s.uiText(L"0%",r.x-32,r.y+r.h-17,30,0x9fb7c9);
    }
  }
public:
  ScratchGestureWindow(HWND owner,Request request,Context context,Capture captureTarget,Resolve resolve,std::function<void(const ScratchPatternTarget &,bool)> navigate)
    :NativeToolWindow(owner),request_(std::move(request)),context_(std::move(context)),capture_(std::move(captureTarget)),resolve_(std::move(resolve)),navigate_(std::move(navigate)){
    minimumWidth_=960;minimumHeight_=690;create(L"ScreamSeq.ScratchGestures",L"Scratch phrases",1080,780,true);
    for(int id:{phrase,lane,pointKind,snap})combo(id);
    label(intro,L"Two hands, one phrase: Motion moves the sample; Fader cuts the sound. SK controls duration, travel and repeats.");label(linkLabel,L"");label(pointLabel,L"");label(positionLabel,L"Cycle");edit(pointPosition,L"0",32);label(valueLabel,L"%");edit(pointValue,L"0",32);label(formulaLabel,L"Formula");edit(pointFormula,L"mix(start,end,t)",2048);edit(name,L"",256);label(targetLabel,L"");label(statusLabel,L"");
    SendMessageW(controls_.at(name),EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(L"Scratch phrase name"));
    add(pointsList,L"LISTBOX",L"Scratch envelope points",LBS_NOTIFY|WS_VSCROLL|LBS_NOINTEGRALHEIGHT|WS_BORDER);
    for(auto [id,text]:std::initializer_list<std::pair<int,const wchar_t *>>{{newPhrase,L"New phrase…"},{clone,L"Duplicate phrase"},{unique,L"Make unique at captured row"},{removePhrase,L"Remove"},{reload,L"Reload"},{setPoint,L"Set point"},{deletePoint,L"Delete"},{addPoint,L"Add point"},{expand,L"Expand…"},{reference,L"Reference"},{zoomIn,L"+"},{zoomOut,L"−"},{fit,L"Fit"},{capture,L"Current cursor"},{returnRow,L"Return to row"},{use,L"Use in pattern"},{play,L"Play from row"},{undo,L"Undo"},{redo,L"Redo"},{more,L"More…"}})button(id,text);
    for(auto value:{L"Motion points",L"Fader points"})SendMessageW(controls_.at(lane),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(value));SendMessageW(controls_.at(lane),CB_SETCURSEL,0,0);
    for(const auto *value:ScratchJSON::curves){auto text=wide(value);SendMessageW(controls_.at(pointKind),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));}
    for(auto value:{L"1/16 cycle",L"1/32 cycle",L"1/64 cycle",L"Free"})SendMessageW(controls_.at(snap),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(value));SendMessageW(controls_.at(snap),CB_SETCURSEL,0,0);
    for(int id:{pointPosition,pointValue,pointFormula})SetWindowSubclass(controls_.at(id),pointFieldProc,2,reinterpret_cast<DWORD_PTR>(this));
    for(auto &canvas:canvases_){canvas.start=0;canvas.end=65536;}finish();
  }
  ~ScratchGestureWindow()override{ready_=false;for(int id:{pointPosition,pointValue,pointFormula})RemoveWindowSubclass(controls_.at(id),pointFieldProc,2);}
  void openAt(){if(visible()||retainedDraft()){show();return;}load(true,0,true);show();}
  bool retainedDraft()const{return draft()||pending_||(workbench_&&(workbench_->visible()||workbench_->retainedDraft()));}
  void hide()override{if(dragging_)cancelDrag();KillTimer(window_,3);KillTimer(window_,4);hiding_=true;NativeToolWindow::hide();hiding_=false;}
  void show(){NativeToolWindow::show();if(previewNeeded_)SetTimer(window_,3,100,nullptr);if(saveNeeded_&&current())SetTimer(window_,4,350,nullptr);}
  Json snapshot()const{Json lanes=Json::array();for(unsigned i=0;i<2;++i){Json handles=Json::array();for(const auto &p:canvases_[i].handles)handles.push_back({{"x",p.x},{"y",p.y}});const auto &r=canvases_[i].viewport;lanes.push_back({{"points",state_.points(i)},{"selected",state_.selected[i]},{"handles",handles},{"previewSamples",values_[i].size()},{"canvas",{{"x",r.x},{"y",r.y},{"width",r.w},{"height",r.h}}}});}return {{"visible",visible()},{"document",document_},{"expectedRevision",revision_},{"stale",!current()},{"dirty",draft()},{"pending",pending_},{"selected",state_.id()},{"activeLane",state_.lane},{"fieldDraft",fieldsDirty_},{"fields",{{"position",utf8(field(pointPosition))},{"value",utf8(field(pointValue))},{"formula",utf8(field(pointFormula))}}},{"target",target_?Json{{"document",target_->document},{"revision",target_->revision},{"pattern",target_->pattern},{"patternID",target_->patternID},{"row",target_->row},{"channel",target_->channel},{"trackID",target_->trackID},{"column",target_->column},{"gesture",target_->gesture()}}:Json(nullptr)},{"draft",state_.draft},{"lanes",lanes},{"status",utf8(status_)},{"formulaWorkbench",workbench_?workbench_->snapshot():Json{{"visible",false}}},{"formulaReference",reference_?reference_->snapshot():Json{{"visible",false}}}};}
};
}
