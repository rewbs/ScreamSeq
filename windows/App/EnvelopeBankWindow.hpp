#pragma once
#include "FormulaWorkbenchWindow.hpp"
#include "NativeWriteCompletion.hpp"
#include <set>

namespace ScreamSeq {
class EnvelopeBankWindow final : public NativeToolWindow {
  using Json=Api::Json;
  using Request=std::function<Json(const std::string &,const Json &)>;
  using Context=std::function<std::pair<std::string,std::string>()>;
  enum : int {scope=1001,list,name,captureName,saveCurrent,saveMaster,useCopy,useLinked,unlink,publish,replace,remove,importEntry,reload,catalogueDestination,pointRow,pointValue,pointKind,pointFormula,setPoint,deletePoint,span,beat,preview,setTiming,expandFormula,referenceFormula,reviewResult,acceptResult,
    linkLabel=1100,statusLabel,rowLabel,valueLabel,spanLabel,beatLabel};
  static constexpr const char *curves[]{"step","linear","smooth","exponential","logarithmic","step-next","exponential-reverse","logarithmic-reverse","scripted"};
  Request request_;Context context_;std::function<bool()> sourceCurrent_;
  NativeWriteCompletion::Write write_;NativeWriteCompletion completion_;
  Json submission_=Json::object(),resultReport_=Json::object();
  std::wstring captureBaseline_=L"New envelope";
  bool captureDirty_=false,needsReload_=false,needsAcknowledgement_=false;
  int submittedAction_=0;uint64_t observedGeneration_=0;
  std::string document_,revision_,catalogueRevision_,selected_,linked_;
  std::wstring sourceLink_=L"Captured source is independent";
  Json target_,capturedShape_,entries_=Json::array(),catalogue_=Json::array(),shape_=Json::object(),values_=Json::array();
  bool catalogueScope_=false,dirty_=false,pointFields_=false,timingFields_=false,pending_=false,setting_=false,previewNeeded_=false,dragging_=false;
  uint64_t generation_=0;int selectedPoint_=-1,kind_=1;Json dragBefore_;bool dragDirty_=false;int dragSelection_=-1;
  AutomationCanvas canvas_;
  std::unique_ptr<FormulaWorkbenchWindow> workbench_,referenceWindow_;
  void openReference(){if(!referenceWindow_)referenceWindow_=std::make_unique<FormulaWorkbenchWindow>(window_,L"Envelope formula reference","",Json::object(),-1,request_);referenceWindow_->show();}
  void openFormula(){
    if(workbench_&&(workbench_->visible()||workbench_->retainedDraft())){workbench_->show();return;}
    require(!catalogueScope_&&!pending_,"Choose a song template before expanding its formula");
    if(pointFields_)setPointFields();require(!timingFields_,"Set timing before expanding the formula");
    require(selectedPoint_>=0&&size_t(selectedPoint_)<points().size()&&points()[size_t(selectedPoint_)].at("curve")=="scripted","Select a scripted point to expand its formula");
    const auto document=document_,revision=revision_,entry=selected_;const auto generation=generation_;const auto index=selectedPoint_;const auto original=points()[size_t(index)];
    auto current=[this,document,revision,entry,generation,index,original]{return context_().first==document&&document_==document&&revision_==revision&&selected_==entry&&generation_==generation&&selectedPoint_==index&&!pointFields_&&!timingFields_&&!pending_&&!catalogueScope_&&size_t(index)<points().size()&&points()[size_t(index)]==original;};
    auto use=[this,current](const std::string &text){if(!current())return false;auto point=points()[size_t(selectedPoint_)];point["formula"]=text;replacePoint(std::move(point));return true;};
    const auto length=shape_.at("span").get<unsigned>();workbench_=std::make_unique<FormulaWorkbenchWindow>(window_,L"Song template · "+field(name)+L" · Formula",original.at("formula").get<std::string>(),Json{{"points",points()},{"rows",(length+255)/256},{"span",length},{"rowsPerBeat",shape_.value("rowsPerBeat",4)}},index,request_,std::move(current),std::move(use));workbench_->show();
  }
  const Json &points()const{static const Json empty=Json::array();return shape_.contains("points")?shape_.at("points"):empty;}
  bool draft()const{return dirty_||pointFields_||timingFields_;}
  void require(bool ok,const char *why)const{if(!ok)throw std::runtime_error(why);}
  void current()const{require(!needsReload_,"Reviewed result retained / Reload before another bank operation");const auto [document,revision]=context_();require(document==document_,"Document changed; the captured bank is retained. Close and reopen it from the new document");require(revision==revision_,"Song changed; bank draft retained. Reload before saving");}
  void status(std::wstring text){status_=std::move(text);set(statusLabel,status_);requestPaint();}
  void error(const std::exception &e)override{auto text=wide(e.what());if(completion_.retained())text+=L" / Review result; the operation will not be repeated";status(std::move(text));}
  Json call(const std::string &method,const Json &params){require(!pending_,"Envelope bank is busy");current();pending_=true;layout();try{auto data=request_(method,params);require(context_()==std::pair(document_,revision_),"Song changed during envelope read");pending_=false;layout();return data;}catch(...){pending_=false;layout();throw;}}
  void changed(){dirty_=true;++generation_;values_=Json::array();previewNeeded_=true;canvas_.rebuild(points(),values_);SetTimer(window_,3,120,nullptr);status(L"Unsaved song template / Save master updates its linked uses together");}
  void showPoint(){setting_=true;const auto p=selectedPoint_>=0&&size_t(selectedPoint_)<points().size()?points()[size_t(selectedPoint_)]:Json{{"position",0},{"value",.5},{"curve","linear"}};set(pointRow,p.at("position").get<double>()/256);set(pointValue,p.at("value").get<double>()*100);set(pointFormula,p.value("formula",std::string("mix(start,end,t)")));kind_=1;for(int i=0;i<9;++i)if(p.at("curve")==curves[i])kind_=i;ScreamSeq::NativeInputGate::present(controls_.at(pointKind),CB_SETCURSEL,kind_,0);pointFields_=false;setting_=false;}
  void select(size_t index){require(index<entries_.size(),"Select an envelope template");selected_=entries_[index].at("id").get<std::string>();shape_=entries_[index].at("shape");selectedPoint_=-1;dirty_=pointFields_=timingFields_=false;++generation_;setting_=true;set(name,entries_[index].at("name"));set(span,shape_.at("span").get<double>()/256);set(beat,shape_.value("rowsPerBeat",4));setting_=false;canvas_.start=0;canvas_.end=shape_.at("span").get<double>();values_=Json::array();previewNeeded_=true;showPoint();SetTimer(window_,3,120,nullptr);status(catalogueScope_?L"Independent catalogue copy / import into this song before editing or using it":L"Edit the song master here / saving updates every linked use in one Undo step");}
  void fillList(){setting_=true;ScreamSeq::NativeInputGate::present(controls_.at(list),LB_RESETCONTENT,0,0);size_t chosen=0;for(size_t i=0;i<entries_.size();++i){auto text=wide(entries_[i].at("name").get<std::string>());ScreamSeq::NativeInputGate::present(controls_.at(list),LB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));if(entries_[i].at("id")==selected_)chosen=i;}setting_=false;if(entries_.empty()){selected_.clear();shape_=Json::object();values_=Json::array();canvas_.handles.clear();canvas_.curve.clear();selectedPoint_=-1;dirty_=pointFields_=timingFields_=false;status(L"Save the captured curve to start this song's bank");}else{ScreamSeq::NativeInputGate::present(controls_.at(list),LB_SETCURSEL,chosen,0);select(chosen);}}
  #include "EnvelopeBankReadback.inc"
  #include "EnvelopeBankCompletion.inc"
  void previewNow(){if(shape_.empty()||pending_)return;const auto token=generation_;const auto length=shape_.at("span").get<unsigned>();previewNeeded_=false;try{auto result=call("automation.formula.preview",{{"points",points()},{"rows",(length+255)/256},{"span",length},{"rowsPerBeat",shape_.value("rowsPerBeat",4)},{"samples",1024}});if(token==generation_){values_=result.at("values");canvas_.rebuild(points(),values_);requestPaint();}}catch(const std::exception &e){if(token==generation_){values_=Json::array();canvas_.rebuild(points(),values_);error(e);}}}
  void setTimingFields(){const auto length=number(span)*256,signature=number(beat);require(length>=1&&length<=16777216&&std::floor(length)==length&&signature>=1&&signature<=65536&&std::floor(signature)==signature,"Use a duration from 1/256 to 65536 rows and an integer beat signature");for(const auto &p:points())require(p.at("position").get<double>()<length,"The shorter duration would exclude an existing point");shape_["span"]=unsigned(length);shape_["rowsPerBeat"]=unsigned(signature);canvas_.start=0;canvas_.end=length;timingFields_=false;changed();}
  void replacePoint(Json point){const auto position=point.at("position");for(size_t i=0;i<points().size();++i)require(int(i)==selectedPoint_||points()[i].at("position")!=position,"Another point already occupies this position");auto next=points();if(selectedPoint_>=0)next[size_t(selectedPoint_)]=std::move(point);else{require(next.size()<4096,"A template supports at most 4096 points");next.push_back(std::move(point));}std::sort(next.begin(),next.end(),[](const auto &a,const auto &b){return a.at("position").template get<unsigned>()<b.at("position").template get<unsigned>();});if(next!=points()){shape_["points"]=std::move(next);changed();}for(size_t i=0;i<points().size();++i)if(points()[i].at("position")==position)selectedPoint_=int(i);showPoint();}
  void setPointFields(){require(!timingFields_,"Set timing before changing a point");const auto position=number(pointRow)*256,value=number(pointValue)/100;require(position>=0&&position<=shape_.at("span").get<double>()-1&&value>=0&&value<=1,"Use a position inside the template and 0–100%");Json p={{"position",unsigned(std::llround(position))},{"value",value},{"curve",curves[kind_]}};if(kind_==8){auto text=utf8(field(pointFormula));require(!text.empty(),"A scripted point needs a formula");p["formula"]=text;}replacePoint(std::move(p));}
  void restoreSelection(){for(size_t i=0;i<entries_.size();++i)if(entries_[i].at("id")==selected_)ScreamSeq::NativeInputGate::present(controls_.at(list),LB_SETCURSEL,i,0);}
  void action(int id,unsigned notification)override{
    if(setting_)return;
    if(id==captureName){if(notification==EN_CHANGE){captureDirty_=field(captureName)!=captureBaseline_;++generation_;}return;}
    if(id==pointKind){if(notification!=CBN_SELCHANGE||catalogueScope_||selected_.empty())return;kind_=int(SendMessageW(controls_.at(pointKind),CB_GETCURSEL,0,0));pointFields_=true;++generation_;return;}
    if(id==name||id==pointRow||id==pointValue||id==pointFormula||id==span||id==beat){if(notification!=EN_CHANGE)return;if(catalogueScope_||selected_.empty())return;if(id==name)dirty_=true;else if(id==span||id==beat)timingFields_=true;else pointFields_=true;++generation_;status(L"Template fields pending / set point or timing before saving");return;}
    if(id>=linkLabel)return;
    if(id==scope||id==catalogueDestination||id==pointKind){if(notification!=CBN_SELCHANGE)return;}else if(id==list){if(notification!=LBN_SELCHANGE)return;}else if(notification!=BN_CLICKED)return;
    require(!pending_&&!dragging_,"Envelope bank is busy");
    if(id==reviewResult){reviewCompletion();return;}if(id==acceptResult){acknowledgeResult();return;}
    if(completion_.retained()){
      if(id==scope)ScreamSeq::NativeInputGate::present(controls_.at(scope),CB_SETCURSEL,catalogueScope_?1:0,0);
      if(id==list)restoreSelection();
      throw std::runtime_error("Review the retained result before another bank action");
    }
    if(id==referenceFormula){openReference();return;}if(id==expandFormula){openFormula();return;}
    if(id==scope){const bool next=SendMessageW(controls_.at(scope),CB_GETCURSEL,0,0)==1;if(draft()){ScreamSeq::NativeInputGate::present(controls_.at(scope),CB_SETCURSEL,catalogueScope_?1:0,0);throw std::runtime_error("Save or reload the master draft before changing banks");}const bool old=catalogueScope_;catalogueScope_=next;try{refresh();}catch(...){catalogueScope_=old;ScreamSeq::NativeInputGate::present(controls_.at(scope),CB_SETCURSEL,old?1:0,0);throw;}return;}
    if(id==list){if(draft()){restoreSelection();throw std::runtime_error("Save or reload the template draft before changing selection");}const auto index=SendMessageW(controls_.at(list),LB_GETCURSEL,0,0);if(index>=0)select(size_t(index));return;}
    if(id==catalogueDestination)return;
    if(id==reload){require(context_().first==document_,"Original song is unavailable / retain this bank until document departure is resolved");refresh(true);return;}
    if(id==saveCurrent){require(!draft()&&sourceCurrent_(),"Keep the source editor unchanged and save or discard the master draft first");Json p={{"name",utf8(field(captureName))}};if(!capturedShape_.is_null())p["shape"]=capturedShape_;else p["target"]=target_;mutate(id,"envelope.bank.save",std::move(p));return;}
    if(id==unlink){require(!draft()&&sourceCurrent_(),"Save or reload the master draft and keep the source editor unchanged before unlinking");mutate(id,"envelope.bank.unlink",{{"target",target_}});return;}
    require(!selected_.empty(),"Select a template first");
    if(id==importEntry){require(catalogueScope_&&!draft(),"Choose an unchanged catalogue entry");mutate(id,"envelope.catalogue.import",{{"catalogueID",selected_},{"expectedCatalogueRevision",catalogueRevision_}});return;}
    if(id==preview){if(pointFields_)setPointFields();if(timingFields_)setTimingFields();previewNow();return;}
    require(!catalogueScope_,"Import this catalogue copy into the song bank before editing or using it");
    if(id==setPoint){setPointFields();return;}if(id==setTiming){setTimingFields();return;}
    if(id==deletePoint){require(!pointFields_&&!timingFields_,"Set or discard pending fields before deleting a point");if(selectedPoint_>=0){shape_["points"].erase(shape_["points"].begin()+selectedPoint_);selectedPoint_=points().empty()?-1:std::min(selectedPoint_,int(points().size())-1);changed();showPoint();}return;}
    if(id==saveMaster){require(!pointFields_&&!timingFields_,"Set point and timing fields before saving the master");mutate(id,"envelope.bank.save",{{"id",selected_},{"name",utf8(field(name))},{"shape",shape_}});return;}
    require(!draft(),"Save or reload the master draft before this action");
    if(id==useCopy||id==useLinked){require(sourceCurrent_(),"Source editor changed; reopen the bank from that draft before replacing it");mutate(id,"envelope.bank.apply",{{"template",selected_},{"target",target_},{"linked",id==useLinked}});return;}
    if(id==remove){mutate(id,"envelope.bank.remove",{{"id",selected_}});return;}
    if(id==publish||id==replace){require(!catalogueRevision_.empty(),"Reload an available catalogue before publishing");Json p={{"template",selected_},{"expectedCatalogueRevision",catalogueRevision_}};if(id==replace){const auto index=SendMessageW(controls_.at(catalogueDestination),CB_GETCURSEL,0,0);require(index>=0&&size_t(index)<catalogue_.size(),"Select the catalogue entry to replace");p["catalogueID"]=catalogue_[size_t(index)].at("id");}mutate(id,"envelope.catalogue.publish",std::move(p));return;}

  }
  bool key(WPARAM code,bool ctrl,bool shift)override{
    if(code==VK_F6){SetFocus(GetFocus()==window_?controls_.at(list):window_);return true;}
    const int id=GetFocus()==window_?0:GetDlgCtrlID(GetFocus());if(code==VK_ESCAPE){if(dragging_){cancelDrag();return true;}if(pointFields_||timingFields_){++generation_;showPoint();setting_=true;set(span,shape_.at("span").get<double>()/256);set(beat,shape_.value("rowsPerBeat",4));setting_=false;timingFields_=false;layout();return true;}hide();return true;}
    if(ctrl&&code=='S'){action(saveMaster,BN_CLICKED);layout();return true;}
    if(code==VK_RETURN){if(id==pointRow||id==pointValue||id==pointFormula){action(setPoint,BN_CLICKED);layout();return true;}if(id==span||id==beat){action(setTiming,BN_CLICKED);layout();return true;}wchar_t type[32]{};GetClassNameW(GetFocus(),type,32);if(_wcsicmp(type,L"Button")==0){action(id,BN_CLICKED);layout();return true;}}
    if(GetFocus()!=window_||ctrl||catalogueScope_||pending_)return false;
    if(code==VK_DELETE){action(deletePoint,BN_CLICKED);return true;}if(code==VK_TAB){if(!pointFields_&&!timingFields_&&!points().empty()){selectedPoint_=(selectedPoint_+(shift?-1:1)+int(points().size()))%int(points().size());showPoint();requestPaint();}return true;}
    if(selectedPoint_>=0&&!pointFields_&&!timingFields_&&(code==VK_LEFT||code==VK_RIGHT||code==VK_UP||code==VK_DOWN)){auto p=points()[size_t(selectedPoint_)];if(code==VK_LEFT||code==VK_RIGHT)p["position"]=unsigned(std::clamp(p.at("position").get<double>()+(code==VK_LEFT?-1:1)*(shift?1.0:256),0.0,shape_.at("span").get<double>()-1));else p["value"]=std::clamp(p.at("value").get<double>()+(code==VK_UP?1:-1)*(shift?.001:.01),0.0,1.0);replacePoint(std::move(p));return true;}return false;
  }
  void cancelDrag(){shape_["points"]=dragBefore_;dirty_=dragDirty_;selectedPoint_=dragSelection_;dragging_=false;ReleaseCapture();++generation_;values_=Json::array();previewNeeded_=true;showPoint();canvas_.rebuild(points(),values_);SetTimer(window_,3,120,nullptr);}
  void mouse(UINT message,float x,float y,WPARAM buttons)override{
    if(message==WM_CAPTURECHANGED){if(dragging_)cancelDrag();return;}
    if(message==WM_LBUTTONUP){if(dragging_){dragging_=false;ReleaseCapture();dragBefore_=Json();}return;}
    if(message==WM_LBUTTONDOWN){auto r=canvas_.viewport;const WorkspaceRect hit{r.x-7,r.y-7,r.w+14,r.h+14};if(!hit.contains(x,y)||shape_.empty()||catalogueScope_||pending_)return;require(!pointFields_&&!timingFields_,"Set or discard the pending fields before selecting a point");SetFocus(window_);dragBefore_=points();dragDirty_=dirty_;dragSelection_=selectedPoint_;selectedPoint_=canvas_.hit(x,y);if(selectedPoint_<0){Json p={{"position",unsigned(std::clamp(std::round(canvas_.position(x)),0.0,shape_.at("span").get<double>()-1))},{"value",canvas_.value(y)},{"curve",curves[kind_]}};if(kind_==8)p["formula"]=utf8(field(pointFormula));replacePoint(std::move(p));}else showPoint();dragging_=true;SetCapture(window_);return;}
    if(message==WM_MOUSEMOVE&&dragging_&&(buttons&MK_LBUTTON)&&selectedPoint_>=0){auto p=points()[size_t(selectedPoint_)];p["position"]=unsigned(std::clamp(std::round(canvas_.position(x)),0.0,shape_.at("span").get<double>()-1));p["value"]=canvas_.value(y);replacePoint(std::move(p));}
  }
  void timer(UINT_PTR id)override{if(id!=3)return;KillTimer(window_,3);if(!visible()||completion_.retained()||needsReload_)return;if(pending_||dragging_){SetTimer(window_,3,120,nullptr);return;}if(previewNeeded_)previewNow();}
  void layout()override{
    if(!ready_)return;const auto [w,h]=size();const float right=252,editorWidth=w-right-16;
    place(scope,16,14,236,210);place(captureName,264,14,w-472,26);place(saveCurrent,w-196,14,180,26);place(linkLabel,16,50,w-32,24);
    place(list,16,82,220,std::max(70.0f,h-230));place(name,right,82,editorWidth,26);place(spanLabel,right,116,106,25);place(span,right+108,116,84,26);place(beatLabel,right+204,116,86,25);place(beat,right+294,116,60,26);place(setTiming,right+362,116,editorWidth-362,26);
    canvas_.viewport={right+38,160,std::max(1.0f,editorWidth-48),std::max(1.0f,h-396)};canvas_.rebuild(points(),values_);
    const float y=h-216;place(rowLabel,right,y,36,25);place(pointRow,right+38,y,76,26);place(valueLabel,right+122,y,20,25);place(pointValue,right+144,y,65,26);place(pointKind,right+217,y,std::max(90.0f,editorWidth-409),210);place(setPoint,w-198,y,86,26);place(deletePoint,w-104,y,88,26);
    place(pointFormula,right,h-182,editorWidth-324,26,kind_==8);place(expandFormula,w-324,h-182,78,26,kind_==8);place(referenceFormula,w-240,h-182,80,26);place(preview,w-152,h-182,136,26);
    place(useCopy,16,h-132,105,26,!catalogueScope_);place(useLinked,129,h-132,107,26,!catalogueScope_);place(saveMaster,right,h-132,166,26,!catalogueScope_);place(reload,right+174,h-132,112,26);place(remove,right+294,h-132,editorWidth-294,26,!catalogueScope_);place(importEntry,right,h-132,166,26,catalogueScope_);
    place(unlink,16,h-98,220,26);place(publish,right,h-98,166,26,!catalogueScope_);place(catalogueDestination,right+174,h-98,editorWidth-324,210,!catalogueScope_);place(replace,w-158,h-98,142,26,!catalogueScope_);place(statusLabel,16,h-57,w-(completion_.retained()?364:32),46);
    place(reviewResult,w-340,h-57,156,26,completion_.retained());place(acceptResult,w-176,h-57,160,26,completion_.retained());
    EnableWindow(controls_.at(reviewResult),!pending_);EnableWindow(controls_.at(acceptResult),!pending_&&needsAcknowledgement_);
    const bool editable=!catalogueScope_&&!selected_.empty();for(int id:{name,pointRow,pointValue,pointKind,pointFormula,span,beat})EnableWindow(controls_.at(id),editable);
    for(int id:{saveCurrent,saveMaster,useCopy,useLinked,unlink,publish,replace,remove,importEntry,reload,setPoint,deletePoint,setTiming,preview,scope,list,catalogueDestination})EnableWindow(controls_.at(id),!pending_&&!completion_.retained()&&(id==reload||id==scope||id==list||id==saveCurrent||!selected_.empty()));
    refreshSourceState();
    EnableWindow(controls_.at(expandFormula),editable&&!pending_);EnableWindow(controls_.at(referenceFormula),!pending_);
  }
  void paint(RenderSurface &surface)override{const auto [w,h]=size();surface.fill(0,0,w,h,0x18222d);const auto &r=canvas_.viewport;surface.fill(r.x,r.y,r.w,r.h,0x10171f);surface.clip(r.x,r.y,r.w,r.h);for(int i=0;i<=4;++i)surface.line(r.x,r.y+r.h*i/4,r.x+r.w,r.y+r.h*i/4,0x2a3948);for(size_t i=1;i<canvas_.curve.size();++i)surface.line(canvas_.curve[i-1].x,canvas_.curve[i-1].y,canvas_.curve[i].x,canvas_.curve[i].y,0x68d3bc,2);for(size_t i=0;i<canvas_.handles.size();++i){auto p=canvas_.handles[i];surface.fill(p.x-4,p.y-4,8,8,int(i)==selectedPoint_?0xf3dfb0:0x7ce5cd);}surface.unclip();surface.uiText(L"100%",r.x-37,r.y-4,35,0x94a4b4);surface.uiText(L"0%",r.x-37,r.y+r.h-13,35,0x94a4b4);surface.uiText(L"0 rows",r.x,r.y+r.h+4,90,0x94a4b4);}
public:
  EnvelopeBankWindow(HWND owner,Json target,Json shape,std::string document,std::string revision,const std::wstring &sourceTitle,Request request,Context context,std::function<bool()> sourceCurrent)
    :EnvelopeBankWindow(owner,std::move(target),std::move(shape),std::move(document),std::move(revision),sourceTitle,request,context,std::move(sourceCurrent),
      [request,context](const auto &method,const auto &params){const auto before=context();auto result=request(method,params);return Api::CompletedCall{method,before.first,context().second,std::move(result)};}){}
  EnvelopeBankWindow(HWND owner,Json target,Json shape,std::string document,std::string revision,const std::wstring &sourceTitle,Request request,Context context,std::function<bool()> sourceCurrent,NativeWriteCompletion::Write write)
    :NativeToolWindow(owner),request_(std::move(request)),context_(std::move(context)),sourceCurrent_(std::move(sourceCurrent)),write_(std::move(write)),document_(std::move(document)),revision_(std::move(revision)),target_(std::move(target)),capturedShape_(std::move(shape)){
    create(L"ScreamSeq.EnvelopeBank",(L"Envelope bank — "+sourceTitle).c_str());
    combo(scope);combo(pointKind);combo(catalogueDestination);add(list,L"LISTBOX",L"",LBS_NOTIFY|LBS_NOINTEGRALHEIGHT|WS_VSCROLL);
    for(auto [id,text]:std::initializer_list<std::pair<int,const wchar_t *>>{{name,L""},{captureName,L"New envelope"},{pointRow,L"0"},{pointValue,L"50"},{pointFormula,L"mix(start,end,t)"},{span,L"64"},{beat,L"4"}})edit(id,text,id==pointFormula?2048:id==name||id==captureName?256:32);
    for(auto [id,text]:std::initializer_list<std::pair<int,const wchar_t *>>{{saveCurrent,L"Save captured curve"},{saveMaster,L"Save song master"},{useCopy,L"Use copy"},{useLinked,L"Use linked"},{unlink,L"Make source independent"},{publish,L"Publish catalogue copy"},{replace,L"Replace selected copy"},{remove,L"Remove song template"},{importEntry,L"Copy into song bank"},{reload,L"Reload / discard"},{setPoint,L"Set point"},{deletePoint,L"Delete"},{preview,L"Check / preview"},{setTiming,L"Set timing"}})button(id,text);
    for(auto [id,text]:std::initializer_list<std::pair<int,const wchar_t *>>{{linkLabel,L"Captured source"},{statusLabel,L""},{rowLabel,L"Row"},{valueLabel,L"%"},{spanLabel,L"Duration / rows"},{beatLabel,L"Rows / beat"}})label(id,text);
    button(expandFormula,L"Expand…");button(referenceFormula,L"Reference");button(reviewResult,L"Review result");button(acceptResult,L"Accept observed state");
    for(auto text:{L"This song · linked templates",L"App catalogue · independent copies"})ScreamSeq::NativeInputGate::present(controls_.at(scope),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text));ScreamSeq::NativeInputGate::present(controls_.at(scope),CB_SETCURSEL,0,0);
    for(auto text:{L"Step",L"Linear",L"Smooth",L"Exponential",L"Logarithmic",L"Step at start",L"Exponential reversed",L"Logarithmic reversed",L"Scripted"})ScreamSeq::NativeInputGate::present(controls_.at(pointKind),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text));ScreamSeq::NativeInputGate::present(controls_.at(pointKind),CB_SETCURSEL,1,0);
    finish();refresh();
  }
  void refreshSourceState(){
    if(!ready_)return;
    const bool available=!pending_&&!completion_.retained()&&!needsReload_&&sourceCurrent_();
    EnableWindow(controls_.at(saveCurrent),available);
    for(int id:{useCopy,useLinked})EnableWindow(controls_.at(id),available&&!selected_.empty());
    EnableWindow(controls_.at(unlink),available&&!linked_.empty());
    set(linkLabel,sourceCurrent_()?sourceLink_:L"Source changed / captured curve retained; reopen the bank to capture a new draft");
    if(workbench_)workbench_->refreshSourceState();
  }
  void show(){refreshSourceState();NativeToolWindow::show();if(previewNeeded_)SetTimer(window_,3,120,nullptr);}
  bool retainedDraft()const{return draft()||captureDirty_||pending_||completion_.retained()||(workbench_&&(workbench_->visible()||workbench_->retainedDraft()));}
  std::optional<Tracker::DocumentDraft> documentDraft()const override {
    // Catalogue browsing/preferences are not an unsaved song template. A
    // pending bank operation still owns this captured song until it completes.
    return describeDraft(document_,revision_,Json::array({target_,selected_}).dump(),generation_,
      captureDirty_||(!catalogueScope_&&(draft()||dragging_)),pending_,!pending_&&completion_.retained());
  }
  Json snapshot()const{Json handles=Json::array();for(size_t i=0;i<canvas_.handles.size();++i)handles.push_back({{"index",i},{"x",canvas_.handles[i].x},{"y",canvas_.handles[i].y}});return {{"visible",visible()},{"formulaWorkbench",workbench_?workbench_->snapshot():Json{{"visible",false}}},{"formulaReference",referenceWindow_?referenceWindow_->snapshot():Json{{"visible",false}}},{"target",target_},{"document",document_},{"expectedRevision",revision_},{"catalogueRevision",catalogueRevision_},{"scope",catalogueScope_?"catalogue":"song"},{"selected",selected_},{"linkedTemplate",linked_},{"dirty",dirty_},{"fieldDraft",pointFields_||timingFields_},{"pending",pending_},{"captureDraft",captureDirty_},{"captureName",utf8(field(captureName))},{"completion",completion_.snapshot()},{"submission",submission_},{"resultReport",resultReport_},{"needsAcknowledgement",needsAcknowledgement_},{"needsReload",needsReload_},{"generation",generation_},{"sourceCurrent",sourceCurrent_()},{"shape",shape_},{"selectedPoint",selectedPoint_},{"previewSamples",canvas_.curve.size()},{"handles",handles},{"status",utf8(status_)}};}
};
}
