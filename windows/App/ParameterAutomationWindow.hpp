#pragma once
#include "EnvelopeBankWindow.hpp"
#include <cwctype>
#include <set>

namespace ScreamSeq {
class ParameterAutomationWindow final : public NativeToolWindow {
  using Json=Api::Json;
public:
  struct Cursor {std::string document,revision;unsigned pattern=0;Json patterns,plugins;};
  static constexpr int dockMinimumWidth=440,dockMinimumHeight=300;
private:
  using Request=std::function<Json(const std::string &,const Json &)>;
  enum : int {pattern=4201,plugin,search,parameters,kind,snap,pointRow,pointValue,formula,setPoint,deletePoint,rampUp,rampDown,enabled,apply,verify,remove,reload,fromCursor,bank,expand,reference,lastTouched,openRack,fit,zoomOut,zoomIn,panLeft,panRight,tool,rangeStart,rangeEnd,toolValue0,toolValue1,toolValue2,toolValue3,copyRange,previewTool,close,
    absolute=4240,pageTarget,pageCurve,pageFormula,pageTools,
    heading=4300,targetLabel,rowLabel,valueLabel,formulaLabel,rangeLabel,toolLabel0,toolLabel1,toolLabel2,toolLabel3,statusLabel,pageHelp};
  static constexpr std::array<const char *,9> curves={"step","linear","smooth","exponential","logarithmic","step-next","exponential-reverse","logarithmic-reverse","scripted"};
  static constexpr std::array<const char *,9> operations={"flip-time","flip-values","shift","scale","ramp","sine","humanize","paste","insert"};
  Request request_;std::function<Cursor()> context_;std::function<void(const std::string &,uint32_t)> inspect_;
  std::function<void(const std::string &,uint32_t)> absolute_;
  Cursor captured_;std::string patternID_,pluginID_,laneID_;std::optional<uint32_t> parameter_;
  Json lanes_=Json::array(),plugins_=Json::array(),catalog_=Json::array(),points_=Json::array(),values_=Json::array(),clip_;
  std::vector<size_t> filtered_;unsigned rows_=64,rowsPerBeat_=4,snap_=256;int selected_=-1,kind_=1,tool_=0;
  bool setting_=false,pending_=false,dirty_=false,pointFields_=false,enabled_=true,previewNeeded_=false,dragging_=false,dragDirty_=false;
  // Compact pages move the same native controls; drafts, selections and raw
  // field text are never reconstructed when changing pages or window size.
  bool compact_=false,shortDock_=false,canvasVisible_=true,toolFieldsDirty_=false;
  int compactPage_=1;
  HWND pendingFocus_{};
  uint64_t generation_=0;Json dragBefore_;int dragSelection_=-1;
  AutomationCanvas canvas_;
  std::unique_ptr<EnvelopeBankWindow> bank_;
  std::unique_ptr<FormulaWorkbenchWindow> workbench_,reference_;
  void focusPage(){
    HWND target=controls_.at(parameters);
    if(compact_)target=compactPage_==0?controls_.at(parameters):compactPage_==1?window_:compactPage_==2?controls_.at(formula):controls_.at(tool);
    if(!IsWindowVisible(target)||!IsWindowEnabled(target))target=compact_?controls_.at(pageTarget+compactPage_):window_;
    SetFocus(target);requestPaint();
  }
  void choosePage(int page,bool focus=true){
    if(dragging_){dragging_=false;ReleaseCapture();schedule();}
    compactPage_=std::clamp(page,0,3);layout();if(focus)focusPage();requestPaint();
  }
  int selection(int id)const{return int(SendMessageW(controls_.at(id),CB_GETCURSEL,0,0));}
  void choose(int id,int index){ScreamSeq::NativeInputGate::present(controls_.at(id),CB_SETCURSEL,index,0);}
  void require(bool value,const char *message)const{if(!value)throw std::runtime_error(message);}
  bool draft()const{return dirty_||pointFields_;}
  bool current()const{const auto now=context_();return captured_.document==now.document&&captured_.revision==now.revision;}
  void requireCurrent()const{require(current(),"Song changed / captured automation retained; Reload before applying");}
  void status(std::wstring value){status_=std::move(value);set(statusLabel,status_);requestPaint();}
  void error(const std::exception &e)override{status(wide(e.what()));}
  void rebuild(){canvas_.rebuild(points_,values_);requestPaint();}
  void schedule(){previewNeeded_=true;if(visible())resumeVisiblePresentation();}
  void changed(){dirty_=true;++generation_;values_=Json::array();schedule();rebuild();status(L"Curve draft / Apply saves one document Undo step");}
  std::wstring parameterName()const{for(const auto &p:catalog_)if(parameter_&&p.at("id")==*parameter_)return wide(p.at("name").get<std::string>());return L"No parameter";}
  void showPoint(){
    setting_=true;const auto p=selected_>=0&&size_t(selected_)<points_.size()?points_[size_t(selected_)]:Json{{"position",0},{"value",.5},{"curve",curves[size_t(kind_)]}};
    set(pointRow,Json(p.at("position").get<double>()/256));set(pointValue,Json(p.at("value").get<double>()*100));set(formula,p.value("formula",std::string("mix(start,end,t)")));
    for(size_t i=0;i<curves.size();++i)if(p.at("curve")==curves[i])kind_=int(i);choose(kind,kind_);pointFields_=false;setting_=false;layout();
  }
  void selectParameter(uint32_t id,std::optional<uint32_t> position={}){
    parameter_=id;laneID_.clear();points_=Json::array();enabled_=true;selected_=-1;
    for(const auto &l:lanes_)if(l.at("plugin")==pluginID_&&l.at("parameter")==id){laneID_=l.at("id");points_=l.at("points");enabled_=l.at("enabled");break;}
    if(position)for(size_t i=0;i<points_.size();++i)if(points_[i].at("position")==*position)selected_=int(i);
    dirty_=pointFields_=false;++generation_;values_=Json::array();showPoint();schedule();rebuild();set(targetLabel,parameterName()+L" · "+(laneID_.empty()?L"new envelope":wide(laneID_)));
  }
  void filter(){
    auto term=field(search);std::transform(term.begin(),term.end(),term.begin(),[](wchar_t c){return wchar_t(std::towlower(c));});filtered_.clear();int selected=-1;
    SendMessageW(controls_.at(parameters),WM_SETREDRAW,FALSE,0);ScreamSeq::NativeInputGate::present(controls_.at(parameters),LB_RESETCONTENT,0,0);
    for(size_t i=0;i<catalog_.size();++i){auto name=wide(catalog_[i].at("name").get<std::string>()),folded=name;std::transform(folded.begin(),folded.end(),folded.begin(),[](wchar_t c){return wchar_t(std::towlower(c));});if(!term.empty()&&folded.find(term)==std::wstring::npos)continue;
      if(parameter_&&catalog_[i].at("id")==*parameter_)selected=int(filtered_.size());filtered_.push_back(i);ScreamSeq::NativeInputGate::present(controls_.at(parameters),LB_ADDSTRING,0,reinterpret_cast<LPARAM>(name.c_str()));}
    ScreamSeq::NativeInputGate::present(controls_.at(parameters),LB_SETCURSEL,selected,0);SendMessageW(controls_.at(parameters),WM_SETREDRAW,TRUE,0);InvalidateRect(controls_.at(parameters),nullptr,FALSE);
  }
  void choices(){
    setting_=true;ScreamSeq::NativeInputGate::present(controls_.at(pattern),CB_RESETCONTENT,0,0);int i=0;
    for(const auto &p:captured_.patterns){auto name=L"Pattern "+std::to_wstring(p.at("index").get<unsigned>())+L" · "+wide(p.at("name").get<std::string>());ScreamSeq::NativeInputGate::present(controls_.at(pattern),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name.c_str()));if(p.at("id")==patternID_)choose(pattern,i);++i;}
    ScreamSeq::NativeInputGate::present(controls_.at(plugin),CB_RESETCONTENT,0,0);i=0;for(const auto &p:plugins_){auto name=wide(p.at("name").get<std::string>());ScreamSeq::NativeInputGate::present(controls_.at(plugin),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name.c_str()));if(p.at("instanceID")==pluginID_)choose(plugin,i);++i;}
    setting_=false;filter();
  }
  void load(bool follow,std::string wantedPlugin={},std::optional<uint32_t> wantedParameter={},std::optional<unsigned> wantedPattern={},bool allowMissingPlugin=false,bool strictTarget=false){
    if(pending_)return;const auto now=context_();require(follow||now.document==captured_.document,"Document changed / use From cursor to capture the new song");
    unsigned index=follow?now.pattern:captured_.pattern;
    if(wantedPattern)index=*wantedPattern;else if(!follow){auto found=std::find_if(now.patterns.begin(),now.patterns.end(),[&](const auto &p){return p.at("id")==patternID_;});require(found!=now.patterns.end(),"Captured pattern was removed / use From cursor");index=found->at("index");}
    if(strictTarget){require(!wantedPlugin.empty()&&wantedParameter&&std::any_of(now.plugins.begin(),now.plugins.end(),[&](const auto &p){return p.at("instanceID")==wantedPlugin;}),"Captured source plugin is unavailable");require(std::any_of(now.patterns.begin(),now.patterns.end(),[&](const auto &p){return p.at("index")==index;}),"Captured source pattern is unavailable");}
    const auto token=generation_;const bool sameSong=now.document==captured_.document;
    auto wanted=wantedPlugin.empty()&&sameSong?pluginID_:wantedPlugin;auto wantedID=wantedParameter?wantedParameter:sameSong&&wantedPlugin.empty()?parameter_:std::optional<uint32_t>{};
    auto selectedPosition=selected_>=0&&size_t(selected_)<points_.size()?std::optional<uint32_t>(points_[size_t(selected_)].at("position").get<uint32_t>()):std::nullopt;
    pending_=true;layout();
    try{
      auto data=request_("automation.pattern.get",{{"pattern",index}});auto plugins=now.plugins;for(const auto &lane:data.at("lanes"))if(std::none_of(plugins.begin(),plugins.end(),[&](const auto &p){return p.at("instanceID")==lane.at("plugin");}))plugins.push_back({{"instanceID",lane.at("plugin")},{"name","Unavailable · "+lane.at("plugin").get<std::string>()},{"unavailable",true}});
      auto chosen=std::find_if(plugins.begin(),plugins.end(),[&](const auto &p){return p.at("instanceID")==wanted;});
      if(chosen==plugins.end()){require(follow||wanted.empty()||allowMissingPlugin,"Captured plugin was removed / use From cursor or another plugin");chosen=plugins.begin();}
      Json catalog=Json::array();std::wstring catalogueError;
      if(chosen!=plugins.end()){
        wanted=chosen->at("instanceID");if(!chosen->value("unavailable",false))try{catalog=request_("plugin.parameters.get",{{"slot",size_t(chosen-plugins.begin())}});}catch(const std::exception &e){if(strictTarget)throw;catalogueError=wide(e.what());}
        for(const auto &l:data.at("lanes"))if(l.at("plugin")==wanted&&std::none_of(catalog.begin(),catalog.end(),[&](const auto &p){return p.at("id")==l.at("parameter");}))catalog.push_back({{"id",l.at("parameter")},{"name","Unavailable parameter "+std::to_string(l.at("parameter").get<uint32_t>())},{"unavailable",true}});
      }else wanted.clear();
      const auto after=context_();require(after.document==now.document&&after.revision==now.revision&&token==generation_,"Song or draft changed while loading / captured editor retained");
      if(strictTarget)require(std::any_of(catalog.begin(),catalog.end(),[&](const auto &p){return p.at("id")==*wantedParameter&&!p.value("unavailable",false);}),"Captured source parameter is unavailable");
      const bool samePattern=sameSong&&data.at("patternID")==patternID_;captured_=now;captured_.pattern=index;patternID_=data.at("patternID");rows_=data.at("rows");rowsPerBeat_=data.at("rowsPerBeat");plugins_=std::move(plugins);pluginID_=wanted;lanes_=data.at("lanes");catalog_=std::move(catalog);
      if(!samePattern){canvas_.fit(rows_);selectedPosition.reset();setting_=true;set(rangeStart,L"0");set(rangeEnd,std::to_wstring(rows_));setting_=false;}else if(canvas_.end>rows_*256)canvas_.fit(rows_);
      parameter_.reset();if(!catalog_.empty()){auto p=std::find_if(catalog_.begin(),catalog_.end(),[&](const auto &v){return wantedID&&v.at("id")==*wantedID;});if(p==catalog_.end())p=catalog_.begin();selectParameter(p->at("id"),selectedPosition);}
      else {points_=values_=Json::array();laneID_.clear();selected_=-1;dirty_=pointFields_=false;++generation_;set(targetLabel,L"No available parameter");showPoint();rebuild();}
      choices();pending_=false;if(!samePattern)toolFieldsDirty_=false;status(!catalogueError.empty()?catalogueError:catalog_.empty()?L"Add a plugin in the rack, then reload to choose its parameter":L"Click or drag points / Apply saves; the pattern cursor remains independent");
    }catch(...){pending_=false;layout();throw;}layout();
  }
  void replacePoint(Json point){
    const auto position=point.at("position");for(size_t i=0;i<points_.size();++i)require(int(i)==selected_||points_[i].at("position")!=position,"Another point occupies this position");
    auto next=points_;if(selected_>=0)next[size_t(selected_)]=std::move(point);else {require(next.size()<4096,"An envelope supports at most 4096 points");next.push_back(std::move(point));}
    std::sort(next.begin(),next.end(),[](const auto &a,const auto &b){return a.at("position").template get<uint32_t>()<b.at("position").template get<uint32_t>();});
    selected_=-1;for(size_t i=0;i<next.size();++i)if(next[i].at("position")==position)selected_=int(i);if(next!=points_){points_=std::move(next);changed();}showPoint();
  }
  void setPointFields(){
    require(parameter_.has_value(),"Choose a parameter");const auto pos=number(pointRow)*256,value=number(pointValue)/100;require(pos>=0&&pos<double(rows_)*256&&std::llround(pos)<int64_t(rows_)*256&&value>=0&&value<=1,"Choose a row inside the pattern and a value from 0 to 100%");
    Json point={{"position",uint32_t(std::llround(pos))},{"value",value},{"curve",curves[size_t(kind_)]}};if(kind_==8){auto source=utf8(field(formula));require(!source.empty(),"A scripted point needs a formula");point["formula"]=source;}replacePoint(std::move(point));
  }
  void previewNow(){
    if(pending_||dragging_)return;previewNeeded_=false;if(points_.empty()){values_=Json::array();rebuild();return;}
    const auto token=generation_;const auto start=canvas_.start,end=canvas_.end;pending_=true;layout();
    try{auto data=request_("automation.formula.preview",{{"points",points_},{"rows",rows_},{"rowsPerBeat",rowsPerBeat_},{"start",start},{"end",end},{"samples",1024}});pending_=false;
      if(token==generation_&&start==canvas_.start&&end==canvas_.end){values_=data.at("values");rebuild();}}
    catch(const Api::ApiError &e){pending_=false;if(e.code==-32002){schedule();}else if(token==generation_){values_=Json::array();rebuild();error(e);}}
    catch(const std::exception &e){pending_=false;if(token==generation_){values_=Json::array();rebuild();error(e);}}layout();
  }
  void commit(bool dry,bool removing=false){
    requireCurrent();require(parameter_.has_value(),"Choose a parameter");require(!pointFields_,"Set or discard the point fields before applying");require(!removing||!laneID_.empty(),"This parameter has no saved envelope");require(removing||!points_.empty(),"Add points, or use Remove lane to delete the envelope");
    Json p={{"expectedRevision",captured_.revision},{"dryRun",dry}};if(removing)p["lane"]=laneID_;else {p["pattern"]=captured_.pattern;p["plugin"]=pluginID_;p["parameter"]=*parameter_;p["enabled"]=enabled_;p["points"]=points_;}
    const auto token=generation_;pending_=true;layout();try{const auto result=request_(removing?"automation.pattern.remove":"automation.pattern.set",p);pending_=false;require(context_().document==captured_.document&&token==generation_,"Source changed during Apply / newer draft retained");
      if(!dry)load(false,{},{},{},removing);status(dry?L"Valid / Verify left the song and Undo unchanged":result.at("wouldChange").get<bool>()?removing?L"Envelope removed / document Undo restores the curve":L"Envelope saved / document Undo restores the previous curve":L"This envelope is already saved");
    }catch(...){pending_=false;layout();throw;}layout();
  }
  Json toolSignature()const{return Json::array({tool_,field(rangeStart),field(rangeEnd),field(toolValue0),field(toolValue1),field(toolValue2),field(toolValue3),snap_,kind_});}
  std::pair<uint32_t,uint32_t> range()const{const auto first=number(rangeStart)*256,last=number(rangeEnd)*256;require(first>=0&&first<last&&last<=rows_*256&&first==std::floor(first)&&last==std::floor(last),"Choose a nonempty range inside the pattern in steps of 1/256 row");return {uint32_t(first),uint32_t(last)};}
  void transform(bool copy){
    requireCurrent();require(!draft()&&!laneID_.empty(),"Apply the curve before copying or transforming saved points");auto [start,end]=range();Json p={{"lane",laneID_},{"start",start}};
    if(copy)p["end"]=end;else{
      p["expectedRevision"]=captured_.revision;p["dryRun"]=true;p["operation"]=operations[size_t(tool_)];if(tool_<7)p["end"]=end;Json options=Json::object();
      if(tool_==2)options["amount"]=number(toolValue0)*256;
      if(tool_==3)options={{"amount",number(toolValue0)},{"offset",number(toolValue1)/100}};
      if(tool_==4)options={{"from",number(toolValue0)/100},{"to",number(toolValue1)/100},{"curve",curves[size_t(kind_)]}};
      if(tool_==5)options={{"center",number(toolValue0)/100},{"amplitude",number(toolValue1)/100},{"cycles",number(toolValue2)},{"phase",number(toolValue3)},{"spacing",snap_},{"curve",curves[size_t(kind_)]}};
      if(tool_==6)options={{"amount",number(toolValue0)/100},{"jitter",number(toolValue1)*256},{"seed",number(toolValue2)}};
      if(tool_>=7){require(!clip_.is_null(),"Copy an envelope range first");options={{"clip",clip_},{"repeats",number(toolValue0)}};}p["options"]=options;
    }
    const auto token=generation_;const auto signature=toolSignature();pending_=true;layout();try{auto result=request_(copy?"automation.pattern.copy":"automation.pattern.transform",p);pending_=false;requireCurrent();require(generation_==token&&signature==toolSignature(),"Curve or tool settings changed / preview discarded");
      if(copy){clip_=std::move(result);status(L"Range copied inside this editor / choose Paste or Insert paste");}
      else if(result.at("wouldChange").get<bool>()){points_=result.at("after");selected_=-1;changed();showPoint();status(L"Tool preview · "+std::to_wstring(result.at("clippedValues").get<unsigned>())+L" values clipped / Apply saves; Reload discards");}
      else status(L"The tool leaves this envelope unchanged");toolFieldsDirty_=false;
    }catch(...){pending_=false;layout();throw;}layout();
  }
  void toolFields(){
    static const std::array<std::vector<std::pair<const wchar_t *,const wchar_t *>>,9> fields={{{},{},{{L"Shift / rows",L"1"}},{{L"Multiply",L"1"},{L"Add / %",L"0"}},{{L"From / %",L"0"},{L"To / %",L"100"}},{{L"Center / %",L"50"},{L"Amplitude / %",L"50"},{L"Cycles",L"1"},{L"Phase / degrees",L"0"}},{{L"Value jitter / %",L"5"},{L"Time jitter / rows",L"0"},{L"Seed",L"0"}},{{L"Repeats",L"1"}},{{L"Repeats",L"1"}}}};
    setting_=true;for(int i=0;i<4;++i)if(size_t(i)<fields[size_t(tool_)].size()){set(toolLabel0+i,fields[size_t(tool_)][size_t(i)].first);set(toolValue0+i,fields[size_t(tool_)][size_t(i)].second);}setting_=false;layout();
  }
  void openFormula(){
    if(workbench_&&(workbench_->visible()||workbench_->retainedDraft())){workbench_->show();return;}requireCurrent();if(pointFields_)setPointFields();require(selected_>=0&&points_[size_t(selected_)].at("curve")=="scripted","Select a scripted point first");
    const auto doc=captured_.document,revision=captured_.revision,plugin=pluginID_,pattern=patternID_;const auto parameter=parameter_;const auto token=generation_;const auto selected=selected_;const auto point=points_[size_t(selected)];
    auto current=[this,doc,revision,plugin,pattern,parameter,token,selected,point]{return context_().document==doc&&captured_.document==doc&&captured_.revision==revision&&pluginID_==plugin&&patternID_==pattern&&parameter_==parameter&&generation_==token&&selected_==selected&&!pending_&&!pointFields_&&size_t(selected)<points_.size()&&points_[size_t(selected)]==point;};
    auto use=[this,current](const std::string &source){if(!current())return false;auto p=points_[size_t(selected_)];p["formula"]=source;replacePoint(std::move(p));return true;};
    workbench_=std::make_unique<FormulaWorkbenchWindow>(window_,parameterName()+L" · Formula",point.at("formula").get<std::string>(),Json{{"points",points_},{"rows",rows_},{"rowsPerBeat",rowsPerBeat_}},selected,request_,std::move(current),std::move(use));workbench_->show();
  }
  void openBank(){
    if(bank_&&(bank_->visible()||bank_->retainedDraft())){bank_->show();return;}requireCurrent();require(parameter_&&!pointFields_,"Choose a parameter and set pending point fields first");
    const auto doc=captured_.document,plugin=pluginID_,pattern=patternID_;const auto parameter=parameter_;auto token=std::make_shared<uint64_t>(generation_);
    auto source=[this,doc,plugin,pattern,parameter,token]{return context_().document==doc&&captured_.document==doc&&pluginID_==plugin&&patternID_==pattern&&parameter_==parameter&&generation_==*token&&!pointFields_;};
    auto context=[this]{const auto c=context_();return std::pair(c.document,c.revision);};
    auto request=[this,source,token](const std::string &method,const Json &p){const bool owned=source(),wasPending=pending_;if(owned)pending_=true;Json result;try{result=request_(method,p);}catch(...){pending_=wasPending;throw;}pending_=wasPending;
      if(p.contains("expectedRevision")&&owned&&source()){captured_.revision=context_().revision;if(method=="envelope.bank.apply"||((method=="envelope.bank.unlink"||method=="envelope.bank.save")&&!draft())){const auto before=generation_;load(false);if(!draft()&&generation_==before+1)*token=generation_;}}return result;};
    Json target={{"kind","parameter"},{"pattern",captured_.pattern},{"plugin",pluginID_},{"parameter",*parameter_}},shape;
    if(!points_.empty())shape={{"span",rows_*256},{"rowsPerBeat",rowsPerBeat_},{"points",points_}};
    bank_=std::make_unique<EnvelopeBankWindow>(window_,target,shape,captured_.document,captured_.revision,parameterName()+L" · Pattern "+std::to_wstring(captured_.pattern),std::move(request),std::move(context),std::move(source));bank_->show();
  }
  void touch(){require(!draft(),"Apply or Reload the current curve draft first");const auto data=request_("automation.target.get",Json::object());const auto &target=data.at("target");require(!target.is_null()&&target.value("available",false),"Touch a parameter in the rack or a plugin editor first");load(true,target.at("plugin"),target.at("parameter").get<uint32_t>());}
  void action(int id,unsigned notification)override{
    if(setting_)return;if(id==close){if(dragging_)cancelDrag();hide();return;}
    if(id>=pageTarget&&id<=pageTools&&notification==BN_CLICKED){choosePage(id-pageTarget);return;}
    if(notification==EN_CHANGE){if(id==search){filter();return;}if(id==pointRow||id==pointValue||id==formula){pointFields_=true;++generation_;status(L"Point fields pending / Set point before Apply");}else if(id==rangeStart||id==rangeEnd||(id>=toolValue0&&id<=toolValue3)){toolFieldsDirty_=true;++generation_;}return;}
    if(pending_)return;
    if(notification==CBN_SELCHANGE){
      if(id==kind){kind_=std::clamp(selection(kind),0,8);pointFields_=true;++generation_;layout();return;}if(id==snap){snap_=std::array<unsigned,4>{256,128,64,1}.at(size_t(std::max(0,selection(snap))));return;}
      if(id==tool){tool_=std::clamp(selection(tool),0,8);toolFields();return;}
      if(id==pattern||id==plugin){if(draft()){choices();throw std::runtime_error("Apply or Reload the curve draft before changing target");}const auto index=selection(id);if(index<0)return;
        try{if(id==pattern)load(false,{},parameter_,captured_.patterns.at(size_t(index)).at("index").get<unsigned>());else load(false,plugins_.at(size_t(index)).at("instanceID"),std::nullopt);}catch(...){choices();throw;}return;}
    }
    if(id==parameters&&notification==LBN_SELCHANGE){const auto index=SendMessageW(controls_.at(parameters),LB_GETCURSEL,0,0);if(draft()){filter();throw std::runtime_error("Apply or Reload the curve draft before changing parameter");}if(index>=0&&size_t(index)<filtered_.size()){selectParameter(catalog_.at(filtered_[size_t(index)]).at("id"));filter();}return;}
    if(id==parameters&&notification==LBN_DBLCLK&&compact_&&parameter_){choosePage(1);return;}
    if(notification!=BN_CLICKED)return;
    if(id==reload||id==fromCursor){load(id==fromCursor);toolFieldsDirty_=false;}else if(id==lastTouched)touch();else if(id==bank)openBank();else if(id==expand)openFormula();
    else if(id==reference){if(!reference_)reference_=std::make_unique<FormulaWorkbenchWindow>(window_,L"Envelope formula reference","",Json::object(),-1,request_);reference_->show();}
    else if(id==openRack){requireCurrent();require(parameter_.has_value(),"Choose a parameter");inspect_(pluginID_,*parameter_);}
    else if(id==absolute){requireCurrent();require(parameter_.has_value(),"Choose a parameter");absolute_(pluginID_,*parameter_);}
    else if(id==setPoint)setPointFields();else if(id==apply||id==verify||id==remove)commit(id==verify,id==remove);
    else if(id==copyRange||id==previewTool){transform(id==copyRange);if(id==previewTool&&compact_)choosePage(1);}
    else if(id==enabled){enabled_=!enabled_;changed();}
    else if(id==deletePoint){require(!pointFields_,"Set or discard the point fields first");if(selected_>=0){points_.erase(points_.begin()+selected_);selected_=points_.empty()?-1:std::min(selected_,int(points_.size()-1));changed();showPoint();}}
    else if(id==rampUp||id==rampDown){require(parameter_&&!pointFields_,"Choose a parameter and set pending point fields first");points_=Json::array({{{"position",0},{"value",id==rampDown?1:0},{"curve",curves[size_t(kind_)]}},{{"position",rows_*256-1},{"value",id==rampDown?0:1},{"curve",curves[size_t(kind_)]}}});if(kind_==8)for(auto &p:points_)p["formula"]="mix(start,end,t)";selected_=0;changed();showPoint();}
    else if(id==fit||id==zoomIn||id==zoomOut||id==panLeft||id==panRight){if(id==fit)canvas_.fit(rows_);else if(id==panLeft||id==panRight)canvas_.pan((canvas_.end-canvas_.start)*(id==panLeft?-.25:.25),rows_);else canvas_.zoom(id==zoomIn?2:.5,rows_);schedule();rebuild();}
  }
  void cancelDrag(){if(!dragging_)return;dragging_=false;points_=dragBefore_;dirty_=dragDirty_;selected_=dragSelection_;++generation_;values_=Json::array();schedule();showPoint();rebuild();ReleaseCapture();}
  void mouse(UINT message,float x,float y,WPARAM)override{
    if(message==WM_CAPTURECHANGED){cancelDrag();return;}if(message==WM_LBUTTONUP){dragging_=false;ReleaseCapture();schedule();return;}
    if(!canvasVisible_)return;
    if(message==WM_LBUTTONDOWN&&canvas_.viewport.contains(x,y)&&parameter_&&!pending_&&!pointFields_){SetFocus(window_);dragBefore_=points_;dragDirty_=dirty_;dragSelection_=selected_;selected_=canvas_.hit(x,y);
      if(selected_<0){auto position=uint32_t(std::clamp(std::round(canvas_.position(x)/snap_)*snap_,0.0,double(rows_)*256-1));for(size_t i=0;i<points_.size();++i)if(points_[i].at("position")==position)selected_=int(i);
        if(selected_<0){Json p={{"position",position},{"value",canvas_.value(y)},{"curve",curves[size_t(kind_)]}};if(kind_==8)p["formula"]="mix(start,end,t)";replacePoint(std::move(p));}}
      showPoint();dragging_=true;SetCapture(window_);return;}
    if(message==WM_MOUSEMOVE&&dragging_&&selected_>=0){auto p=points_[size_t(selected_)];const auto position=uint32_t(std::clamp(std::round(canvas_.position(x)/snap_)*snap_,0.0,double(rows_)*256-1));for(size_t i=0;i<points_.size();++i)if(int(i)!=selected_&&points_[i].at("position")==position)return;p["position"]=position;p["value"]=canvas_.value(y);replacePoint(std::move(p));}
  }
  bool wheel(UINT message,float x,float y,WPARAM w)override{
    if(!canvasVisible_||!canvas_.viewport.contains(x,y)||dragging_)return false;const double delta=GET_WHEEL_DELTA_WPARAM(w)/120.0;const bool ctrl=GET_KEYSTATE_WPARAM(w)&MK_CONTROL,shift=GET_KEYSTATE_WPARAM(w)&MK_SHIFT;
    if(ctrl){if(shift)canvas_.zoomValues(std::pow(1.25,delta));else canvas_.zoom(std::pow(1.25,delta),rows_);}else if(shift)canvas_.panValues(delta*(canvas_.valueHigh-canvas_.valueLow)*.1);else canvas_.pan((message==WM_MOUSEHWHEEL?1:-1)*delta*(canvas_.end-canvas_.start)*.1,rows_);schedule();rebuild();return true;
  }
  bool key(WPARAM k,bool ctrl,bool shift)override{
    if(k==VK_ESCAPE){if(dragging_)cancelDrag();else if(pointFields_){++generation_;showPoint();}else hide();return true;}
    if(compact_&&ctrl&&(k==VK_PRIOR||k==VK_NEXT)){choosePage((compactPage_+(k==VK_PRIOR?3:1))%4);return true;}
    if(k==VK_F6){if(compact_){if(canvasVisible_&&IsWindowEnabled(controls_.at(pointRow)))SetFocus(GetFocus()==window_?controls_.at(pointRow):window_);else focusPage();}else SetFocus(GetFocus()==window_?controls_.at(parameters):window_);requestPaint();return true;}
    if(ctrl&&k=='R'){action(reload,BN_CLICKED);return true;}if(ctrl&&k==VK_RETURN){action(apply,BN_CLICKED);return true;}
    if(k==VK_RETURN){const auto id=GetDlgCtrlID(GetFocus());if(id==pointRow||id==pointValue||id==formula){action(setPoint,BN_CLICKED);return true;}wchar_t klass[32]{};GetClassNameW(GetFocus(),klass,32);if(_wcsicmp(klass,L"Button")==0){action(id,BN_CLICKED);return true;}}
    if(GetFocus()!=window_||pending_||!canvasVisible_)return false;
    if(ctrl&&(k==VK_LEFT||k==VK_RIGHT)){action(k==VK_LEFT?panLeft:panRight,BN_CLICKED);return true;}if(ctrl)return false;
    if(k==VK_HOME){action(fit,BN_CLICKED);return true;}if(k==VK_OEM_PLUS||k==VK_ADD||k==VK_OEM_MINUS||k==VK_SUBTRACT){action(k==VK_OEM_PLUS||k==VK_ADD?zoomIn:zoomOut,BN_CLICKED);return true;}
    if(k==VK_DELETE){action(deletePoint,BN_CLICKED);return true;}if(pointFields_)return false;
    if(k==VK_TAB&&!points_.empty()){selected_=(selected_+(shift?-1:1)+int(points_.size()))%int(points_.size());showPoint();requestPaint();return true;}
    if(selected_>=0&&(k==VK_LEFT||k==VK_RIGHT||k==VK_UP||k==VK_DOWN)){auto p=points_[size_t(selected_)];if(k==VK_LEFT||k==VK_RIGHT)p["position"]=uint32_t(std::clamp(p.at("position").get<double>()+(k==VK_LEFT?-1:1)*(shift?1.0:double(snap_)),0.0,double(rows_)*256-1));else p["value"]=std::clamp(p.at("value").get<double>()+(k==VK_UP?1:-1)*(shift?.001:.01),0.0,1.0);replacePoint(std::move(p));return true;}return false;
  }
  void resumeVisiblePresentation()noexcept override{if(previewNeeded_)SetTimer(window_,3,120,nullptr);}
  void timer(UINT_PTR id)override{if(id!=3)return;KillTimer(window_,3);if(!visible())return;if(pending_||dragging_){SetTimer(window_,3,120,nullptr);return;}if(previewNeeded_)previewNow();}
  void drawControl(const DRAWITEMSTRUCT &d)override{
    if(d.CtlType!=ODT_BUTTON||d.CtlID<pageTarget||d.CtlID>pageTools){NativeToolWindow::drawControl(d);return;}
    NativeControls::recordDraw(d.hwndItem);const auto saved=SaveDC(d.hDC);
    const bool selected=int(d.CtlID)-pageTarget==compactPage_,contrast=NativeControls::highContrast();
    NativeControls::fill(d.hDC,d.rcItem,contrast?GetSysColor(selected?COLOR_HIGHLIGHT:COLOR_WINDOW):selected?RGB(38,69,75):RGB(35,49,63));
    SetBkMode(d.hDC,TRANSPARENT);SelectObject(d.hDC,font_);
    SetTextColor(d.hDC,contrast?GetSysColor(selected?COLOR_HIGHLIGHTTEXT:COLOR_WINDOWTEXT):selected?RGB(164,240,221):RGB(218,232,241));
    RECT label=d.rcItem;label.left+=5;label.right-=5;
    static constexpr std::array<const wchar_t *,4> names={L"Target",L"Curve",L"Formula",L"Tools"};
    DrawTextW(d.hDC,names[size_t(d.CtlID-pageTarget)],-1,&label,DT_SINGLELINE|DT_VCENTER|DT_CENTER|DT_NOPREFIX);
    if(selected){RECT edge=d.rcItem;edge.top=edge.bottom-std::max(1,MulDiv(2,GetDpiForWindow(window_),96));NativeControls::fill(d.hDC,edge,contrast?GetSysColor(COLOR_HIGHLIGHTTEXT):RGB(114,216,191));}
    if((d.itemState&ODS_FOCUS)&&!(d.itemState&ODS_NOFOCUSRECT)){RECT r=d.rcItem;InflateRect(&r,-3,-3);DrawFocusRect(d.hDC,&r);}RestoreDC(d.hDC,saved);
  }
  void layout()override{
    if(bank_)bank_->refreshSourceState();
    if(workbench_)workbench_->refreshSourceState();
    // Preserve the former 1040x760 outer-window layout after nonclient chrome;
    // use pages when the available client area becomes materially smaller.
    const auto [w,h]=size();const auto previousFocus=GetFocus();const bool wasShort=shortDock_;
    shortDock_=docked()&&h<500;compact_=shortDock_||w<1000||h<700;std::set<int> shown;
    // A first shrink may page controls that were all visible in the wide form.
    // Reveal the existing focused field without rebuilding its text/selection.
    if(shortDock_&&!wasShort&&previousFocus&&IsChild(window_,previousFocus)) {
      const auto id=GetDlgCtrlID(previousFocus);
      if(id==formula||id==expand||id==reference||id==bank||id==rampUp||id==rampDown)compactPage_=2;
      else if(id==tool||id==rangeStart||id==rangeEnd||(id>=toolValue0&&id<=toolValue3)||id==copyRange||id==previewTool||id==panLeft||id==panRight)compactPage_=3;
      else if(id==pattern||id==plugin||id==search||id==parameters||id==lastTouched||id==openRack||id==absolute||id==remove||id==fromCursor||id==close)compactPage_=0;
      else if(id==fit||id==zoomOut||id==zoomIn)compactPage_=1;
      else if((id==pointRow||id==pointValue||id==setPoint||id==deletePoint||id==enabled)&&compactPage_!=2)compactPage_=1;
      else if((id==kind||id==snap)&&compactPage_==0)compactPage_=1;
    }
    set(rowLabel,shortDock_?L"Row":L"Row / 1⁄256");set(valueLabel,shortDock_?L"Value %":L"Value / %");
    auto put=[&](int id,float x,float y,float width,float height,bool show=true){if(show)shown.insert(id);place(id,x,y,width,height,show);};
    const int valueCount=std::array<int,9>{0,0,1,2,2,4,3,1,1}.at(size_t(tool_));
    if(shortDock_){
      // Explicit 440x300-DIP dock body. Keep curve editing and Apply visible;
      // details use the same retained pages instead of scrolling the whole form.
      const float left=8,inner=std::max(1.f,w-16),gap=6,half=(inner-gap)/2,third=(inner-2*gap)/3,quarter=(inner-3*gap)/4;
      for(int i=0;i<4;++i)put(pageTarget+i,left+i*(quarter+gap),4,quarter,26);
      put(targetLabel,left,32,inner,18);
      for(int i=0;i<3;++i)put(std::array<int,3>{apply,verify,reload}[size_t(i)],left+i*(third+gap),h-60,third,26);
      put(statusLabel,left,h-30,inner,26);
      const auto pointFields=[&](float y){
        const float fieldWidth=(inner-30-46-176-5*gap)/2;float x=left;
        put(rowLabel,x,y+4,30,18);x+=30+gap;put(pointRow,x,y,fieldWidth,26);x+=fieldWidth+gap;
        put(valueLabel,x,y+4,46,18);x+=46+gap;put(pointValue,x,y,fieldWidth,26);x+=fieldWidth+gap;
        put(setPoint,x,y,88,26);x+=88+gap;put(deletePoint,x,y,88,26);
      };
      canvasVisible_=compactPage_==1;canvas_.viewport={};
      if(compactPage_==0){
        put(pattern,left,52,half,230);put(plugin,left+half+gap,52,half,260);
        put(search,left,84,inner,26);put(parameters,left,116,inner,std::max(1.f,h-246));
        for(int i=0;i<3;++i){
          put(std::array<int,3>{lastTouched,openRack,absolute}[size_t(i)],left+i*(third+gap),h-124,third,26);
          put(std::array<int,3>{remove,fromCursor,close}[size_t(i)],left+i*(third+gap),h-92,third,26);
        }
      }else if(compactPage_==1){
        float x=left;const float kindWidth=inner-278;
        for(auto [id,width]:std::initializer_list<std::pair<int,float>>{{kind,kindWidth},{snap,82.f},{enabled,74.f},{fit,40.f},{zoomOut,26.f},{zoomIn,26.f}}){put(id,x,52,width,id==kind?230.f:id==snap?220.f:26.f);x+=width+gap;}
        canvas_.viewport={left,100,inner,std::max(1.f,h-198)};
        pointFields(h-92);
      }else if(compactPage_==2){
        put(kind,left,52,inner-174,230);put(snap,w-176,52,82,220);put(enabled,w-88,52,80,26);
        put(formulaLabel,left,86,inner,18);put(formula,left,104,inner,26);
        put(expand,left,136,half,26);put(reference,left+half+gap,136,half,26);
        pointFields(170);
        for(int i=0;i<3;++i)put(std::array<int,3>{bank,rampUp,rampDown}[size_t(i)],left+i*(third+gap),h-92,third,26);
      }else{
        const float rangeWidth=(inner-100-2*gap)/2;
        put(rangeLabel,left,52,100,26);put(rangeStart,left+106,52,rangeWidth,26);put(rangeEnd,left+112+rangeWidth,52,rangeWidth,26);
        put(tool,left,84,inner-244,230);put(kind,w-246,84,144,230);put(snap,w-96,84,88,220);
        for(int i=0;i<4;++i){const float x=left+(i%2)*(half+gap),y=112+(i/2)*46.f;put(toolLabel0+i,x,y,half,16,i<valueCount);put(toolValue0+i,x,y+16,half,26,i<valueCount);}
        const float actionWidth=(inner-124-3*gap)/2;
        put(copyRange,left,h-92,actionWidth,26);put(previewTool,left+actionWidth+gap,h-92,actionWidth,26);
        put(panLeft,w-138,h-92,62,26);put(panRight,w-70,h-92,62,26);
      }
    }else if(compact_){
      const float left=10,inner=std::max(1.f,w-20),gap=6,half=(inner-gap)/2,third=(inner-2*gap)/3,quarter=(inner-3*gap)/4;
      const float contentBottom=h-116;
      put(heading,left,8,inner,24);
      for(int i=0;i<4;++i)put(pageTarget+i,left+i*(quarter+gap),38,quarter,28);
      put(targetLabel,left,74,inner,32);
      // Common actions remain reachable even when raw point/formula fields are
      // staged on another page. Each page only changes control geometry.
      for(int i=0;i<3;++i){put(std::array<int,3>{apply,verify,remove}[size_t(i)],left+i*(third+gap),h-108,third,28);put(std::array<int,3>{reload,fromCursor,close}[size_t(i)],left+i*(third+gap),h-74,third,28);}
      put(statusLabel,left,h-40,inner,36);
      canvasVisible_=compactPage_==1;canvas_.viewport={};
      if(compactPage_==0){
        put(pattern,left,114,half,230);put(plugin,left+half+gap,114,half,260);
        put(search,left,148,inner,26);put(parameters,left,182,inner,std::max(60.f,contentBottom-216));
        put(lastTouched,left,contentBottom-28,third,26);put(openRack,left+third+gap,contentBottom-28,third,26);put(absolute,left+2*(third+gap),contentBottom-28,third,26);
      }else if(compactPage_==1){
        put(kind,left,114,inner-204,230);put(snap,w-204,114,94,220);put(enabled,w-104,114,94,26);
        float x=left;for(auto [id,width]:std::initializer_list<std::pair<int,float>>{{panLeft,32.f},{zoomOut,32.f},{fit,40.f},{zoomIn,32.f},{panRight,32.f}}){put(id,x,148,width,26);x+=width+4;}
        put(bank,w-156,148,146,26);
        canvas_.viewport={left,196,inner,std::max(64.f,contentBottom-280)};
        put(rowLabel,left,contentBottom-76,quarter,18);put(valueLabel,left+quarter+gap,contentBottom-76,quarter,18);
        for(int i=0;i<4;++i)put(std::array<int,4>{pointRow,pointValue,setPoint,deletePoint}[size_t(i)],left+i*(quarter+gap),contentBottom-58,quarter,26);
        put(rampUp,left,contentBottom-26,half,26);put(rampDown,left+half+gap,contentBottom-26,half,26);
      }else if(compactPage_==2){
        put(kind,left,114,inner-204,230);put(snap,w-204,114,94,220);put(enabled,w-104,114,94,26);
        put(formulaLabel,left,154,inner,18);put(formula,left,176,inner,26);
        put(expand,left,210,half,28);put(reference,left+half+gap,210,half,28);
        put(rowLabel,left,248,quarter,18);put(valueLabel,left+quarter+gap,248,quarter,18);
        for(int i=0;i<4;++i)put(std::array<int,4>{pointRow,pointValue,setPoint,deletePoint}[size_t(i)],left+i*(quarter+gap),266,quarter,26);
        put(bank,left,306,inner,28);
        set(pageHelp,kind_==8?L"Expand opens the formula workbench. Set point stages these fields; Apply saves the curve.":L"Choose Scripted on the Curve page to edit this point's formula. The reference is available for every curve.");
        put(pageHelp,left,346,inner,std::max(34.f,contentBottom-346));
      }else{
        const float rangeWidth=(inner-112)/2;
        put(rangeLabel,left,114,100,24);put(rangeStart,116,114,rangeWidth,26);put(rangeEnd,122+rangeWidth,114,rangeWidth,26);
        put(tool,left,148,inner,230);put(kind,left,182,inner-110,230);put(snap,w-114,182,104,220);
        for(int i=0;i<4;++i){const float x=left+(i%2)*(half+gap),y=218+(i/2)*52.f;put(toolLabel0+i,x,y,half,18,i<valueCount);put(toolValue0+i,x,y+18,half,26,i<valueCount);}
        put(copyRange,left,324,half,28);put(previewTool,left+half+gap,324,half,28);
        put(bank,left,358,half,26);put(reference,left+half+gap,358,half,26);
      }
    }else{
      canvasVisible_=true;
      put(heading,18,14,w-36,24);put(pattern,18,48,214,220);put(plugin,246,48,w-694,260);put(lastTouched,w-438,48,138,26);put(bank,w-292,48,134,26);put(openRack,w-150,48,132,26);
      put(search,18,90,214,26);put(parameters,18,126,214,std::max(120.f,h-354));put(targetLabel,260,84,w-280,23);put(absolute,18,h-220,214,26);
      put(kind,260,112,214,230);put(snap,482,112,104,220);put(enabled,594,112,104,26);
      float x=706;for(auto [id,width]:std::initializer_list<std::pair<int,float>>{{panLeft,32.f},{zoomOut,32.f},{fit,40.f},{zoomIn,32.f},{panRight,32.f}}){put(id,x,112,width,26);x+=width+4;}
      canvas_.viewport={260,170,w-280,std::max(100.f,h-468)};
      const auto py=h-226;put(rowLabel,260,py-18,92,18);put(pointRow,260,py,92,26);put(valueLabel,360,py-18,92,18);put(pointValue,360,py,92,26);put(setPoint,460,py,98,26);put(deletePoint,566,py,98,26);put(rampUp,672,py,96,26);put(rampDown,776,py,104,26);
      put(formulaLabel,260,h-288,56,20,kind_==8);put(formula,320,h-292,w-548,26,kind_==8);put(expand,w-220,h-292,94,26,kind_==8);put(reference,w-118,h-292,98,26);
      put(rangeLabel,18,h-184,122,20);put(rangeStart,142,h-188,82,26);put(rangeEnd,232,h-188,82,26);put(tool,328,h-188,182,230);put(copyRange,518,h-188,122,26);put(previewTool,648,h-188,140,26);
      for(int i=0;i<4;++i){put(toolLabel0+i,18+194.f*i,h-150,180,20,i<valueCount);put(toolValue0+i,18+194.f*i,h-128,180,26,i<valueCount);}
      x=18;for(auto [id,width]:std::initializer_list<std::pair<int,float>>{{apply,100.f},{verify,94.f},{remove,118.f},{reload,118.f},{fromCursor,124.f}}){put(id,x,h-66,width,28);x+=width+8;}put(close,w-118,h-66,100,28);put(statusLabel,18,h-30,w-36,24);
    }
    if(dragging_&&!canvasVisible_){dragging_=false;ReleaseCapture();schedule();}
    for(auto [id,control]:controls_)if(!shown.contains(id))NativeControls::show(control,false);
    rebuild();EnableWindow(controls_.at(absolute),!pending_&&parameter_.has_value());
    set(enabled,enabled_?L"Enabled":L"Disabled");for(auto [id,control]:controls_)if(id>=pattern&&id<=close)EnableWindow(control,!pending_||id==close||id==search);
    for(int id:{kind,snap,pointRow,pointValue,formula,setPoint,deletePoint,rampUp,rampDown,enabled,apply,verify,bank,expand,openRack})EnableWindow(controls_.at(id),!pending_&&parameter_.has_value());
    for(int id:{formula,expand})EnableWindow(controls_.at(id),!pending_&&parameter_.has_value()&&kind_==8);
    for(int id:{remove,copyRange,previewTool})EnableWindow(controls_.at(id),!pending_&&!laneID_.empty());
    for(int id=pageTarget;id<=pageTools;++id)NativeControls::active(controls_.at(id),compact_&&id-pageTarget==compactPage_);
    // Disabling a focused native control during an API preview clears Win32
    // focus. Restore it once ready unless the user chose another focus target.
    if(pending_&&previousFocus&&IsChild(window_,previousFocus)&&!GetFocus())pendingFocus_=previousFocus;
    if(!pending_&&pendingFocus_){const auto restore=pendingFocus_;pendingFocus_=nullptr;if(!GetFocus()&&IsWindowVisible(restore)&&IsWindowEnabled(restore)&&GetActiveWindow()==GetAncestor(window_,GA_ROOT))SetFocus(restore);}
    // Move focus only if reflow hides its original field. Ordinary refreshes
    // preserve the focused field's selection and insertion point.
    if(previousFocus&&IsChild(window_,previousFocus)&&!IsWindowVisible(previousFocus)&&GetActiveWindow()==GetAncestor(window_,GA_ROOT))focusPage();
  }
  void paint(RenderSurface &s)override{
    const auto [w,h]=size();s.fill(0,0,w,h,0x18222d);if(!canvasVisible_)return;const auto r=canvas_.viewport;s.fill(r.x,r.y,r.w,r.h,0x101923);s.clip(r.x,r.y,r.w,r.h);
    for(int i=0;i<=4;++i){const auto y=r.y+i*r.h/4;s.line(r.x,y,r.x+r.w,y,0x2a3947);}
    const auto step=std::max(256.0,rowsPerBeat_*256.0*std::ceil((canvas_.end-canvas_.start)/(rowsPerBeat_*256)/16));
    for(double p=std::ceil(canvas_.start/step)*step;p<=canvas_.end;p+=step){const auto x=canvas_.screen(p,0).x;s.line(x,r.y,x,r.y+r.h,0x2a3947);}
    for(size_t i=1;i<canvas_.curve.size();++i)s.line(canvas_.curve[i-1].x,canvas_.curve[i-1].y,canvas_.curve[i].x,canvas_.curve[i].y,enabled_?0x6edac5:0x647c89,2);
    for(size_t i=0;i<canvas_.handles.size();++i){const auto p=canvas_.handles[i];s.fill(p.x-4,p.y-4,8,8,int(i)==selected_?0xffd08a:0x6edac5);}s.unclip();s.outline(r.x,r.y,r.w,r.h,GetFocus()==window_?0x6edac5:0x334757);
    wchar_t label[160]{};swprintf_s(label,shortDock_?L"Rows %.2f–%.2f · %.1f–%.1f%%":L"Rows %.2f–%.2f · %.1f–%.1f%% · Ctrl+wheel zooms; Ctrl+Shift zooms values",canvas_.start/256,canvas_.end/256,canvas_.valueLow*100,canvas_.valueHigh*100);s.uiText(label,r.x,r.y-22,r.w,0x93aabd);
  }
public:
  ParameterAutomationWindow(HWND owner,Request request,std::function<Cursor()> context,std::function<void(const std::string &,uint32_t)> inspect,std::function<void(const std::string &,uint32_t)> absoluteEditor):NativeToolWindow(owner),request_(std::move(request)),context_(std::move(context)),inspect_(std::move(inspect)),absolute_(std::move(absoluteEditor)){
    minimumClientWidth_=440;minimumClientHeight_=500;create(L"ScreamSeq.ParameterAutomation",L"Pattern parameter automation",1240,850);
    for(auto [id,name]:std::initializer_list<std::pair<int,const wchar_t *>>{{pageTarget,L"Target"},{pageCurve,L"Curve"},{pageFormula,L"Formula"},{pageTools,L"Tools"}})button(id,name);
    for(int id:{pattern,plugin,kind,snap,tool})combo(id);for(int id:{search,pointRow,pointValue,formula,rangeStart,rangeEnd,toolValue0,toolValue1,toolValue2,toolValue3})edit(id,L"",id==formula?2048:id==search?128:32);
    add(parameters,L"LISTBOX",L"Automation parameters",LBS_NOTIFY|LBS_NOINTEGRALHEIGHT|WS_VSCROLL);
    button(absolute,L"Song automation…");
    for(auto name:{L"Step",L"Linear",L"Smooth",L"Exponential",L"Logarithmic",L"Step at start",L"Exponential reversed",L"Logarithmic reversed",L"Scripted"})ScreamSeq::NativeInputGate::present(controls_.at(kind),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name));choose(kind,1);
    for(auto name:{L"1 row",L"½ row",L"¼ row",L"1/256 row"})ScreamSeq::NativeInputGate::present(controls_.at(snap),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name));choose(snap,0);
    for(auto name:{L"Flip time",L"Flip values",L"Shift",L"Scale",L"Ramp",L"Sine",L"Humanize",L"Paste",L"Insert paste"})ScreamSeq::NativeInputGate::present(controls_.at(tool),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name));choose(tool,0);
    for(auto [id,name]:std::initializer_list<std::pair<int,const wchar_t *>>{{setPoint,L"Set point"},{deletePoint,L"Delete point"},{rampUp,L"Ramp up"},{rampDown,L"Ramp down"},{enabled,L"Enabled"},{apply,L"Apply curve"},{verify,L"Verify"},{remove,L"Remove lane"},{reload,L"Reload"},{fromCursor,L"From cursor"},{bank,L"Envelope bank…"},{expand,L"Expand…"},{reference,L"Reference"},{lastTouched,L"Use last touched"},{openRack,L"Show in rack"},{fit,L"Fit"},{zoomOut,L"−"},{zoomIn,L"+"},{panLeft,L"‹"},{panRight,L"›"},{copyRange,L"Copy range"},{previewTool,L"Preview tool"},{close,L"Close"}})button(id,name);
    for(auto [id,name]:std::initializer_list<std::pair<int,const wchar_t *>>{{heading,L"Pattern parameter automation"},{targetLabel,L"Choose a plugin parameter"},{rowLabel,L"Row / 1⁄256"},{valueLabel,L"Value / %"},{formulaLabel,L"Formula"},{rangeLabel,L"Range / rows"},{statusLabel,L""}})label(id,name);
    for(int id=toolLabel0;id<=toolLabel3;++id)label(id,L"");label(pageHelp,L"");setting_=true;set(rangeStart,L"0");set(rangeEnd,L"64");setting_=false;finish();
  }
  bool retainedDraft()const{return pending_||dragging_||draft()||toolFieldsDirty_||(bank_&&bank_->retainedDraft())||(workbench_&&workbench_->retainedDraft());}
  std::optional<Tracker::DocumentDraft> documentDraft()const override {
    return describeDraft(captured_.document,captured_.revision,Json::array({patternID_,pluginID_,parameter_?Json(*parameter_):Json()}).dump(),
      generation_,draft()||toolFieldsDirty_||dragging_,pending_);
  }
  bool followCursor(){
    if(retainedDraft())return false;const auto now=context_();
    if(now.document==captured_.document&&now.pattern==captured_.pattern&&now.revision==captured_.revision)return true;
    load(true);return true;
  }
  // Prepare only a newly constructed staging window. Layout restoration owns
  // adoption/placement after every requested editor has loaded successfully.
  void initializeHidden(std::string plugin={},std::optional<uint32_t> parameter={}){
    require(!visible()&&!docked()&&captured_.document.empty()&&generation_==0&&!retainedDraft(),"Hidden initialization requires a fresh, undocked automation editor");
    load(true,std::move(plugin),parameter);
  }
  void openAt(std::string plugin={},std::optional<uint32_t> parameter={}){const bool retain=visible()||retainedDraft();show();if(!retain){load(true,std::move(plugin),parameter);if(compact_&&!parameter_){compactPage_=0;layout();}}if(previewNeeded_)SetTimer(window_,3,120,nullptr);focusPage();}
  void openSourceAt(std::string plugin,uint32_t parameter){require(!retainedDraft()&&!pending_,"Apply or Reload the existing parameter curve draft before opening a source");load(true,std::move(plugin),parameter,{ },false,true);show();if(previewNeeded_)SetTimer(window_,3,120,nullptr);focusPage();}
  Json snapshot()const{
    Json handles=Json::array();for(size_t i=0;i<canvas_.handles.size();++i)handles.push_back({{"index",i},{"x",canvas_.handles[i].x},{"y",canvas_.handles[i].y}});const auto r=canvas_.viewport;
    return {{"visible",visible()},{"compact",compact_},{"shortDock",shortDock_},{"page",std::array<const char *,4>{"target","curve","formula","tools"}[size_t(compactPage_)]},{"canvasVisible",canvasVisible_},{"toolFieldDraft",toolFieldsDirty_},{"retainedDraft",retainedDraft()},{"generation",generation_},{"document",captured_.document},{"expectedRevision",captured_.revision},{"pattern",captured_.pattern},{"patternID",patternID_},{"plugin",pluginID_},{"parameter",parameter_?Json(*parameter_):Json()},{"lane",laneID_},{"dirty",dirty_},{"fieldDraft",pointFields_},{"pending",pending_},{"stale",!current()},{"enabled",enabled_},{"points",points_},{"selectedPoint",selected_},{"parameterCount",catalog_.size()},{"filteredCount",filtered_.size()},{"start",canvas_.start},{"end",canvas_.end},{"valueLow",canvas_.valueLow},{"valueHigh",canvas_.valueHigh},{"previewSamples",canvas_.curve.size()},{"handles",handles},{"canvas",{r.x,r.y,r.w,r.h}},{"status",utf8(status_)},{"envelopeBank",bank_?bank_->snapshot():Json{{"visible",false}}},{"formulaWorkbench",workbench_?workbench_->snapshot():Json{{"visible",false}}}};
  }
};
}
