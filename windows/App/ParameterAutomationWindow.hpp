#pragma once
#include "EnvelopeBankWindow.hpp"
#include <cwctype>

namespace ScreamSeq {
class ParameterAutomationWindow final : public NativeToolWindow {
  using Json=Api::Json;
public:
  struct Cursor {std::string document,revision;unsigned pattern=0;Json patterns,plugins;};
private:
  using Request=std::function<Json(const std::string &,const Json &)>;
  enum : int {pattern=4201,plugin,search,parameters,kind,snap,pointRow,pointValue,formula,setPoint,deletePoint,rampUp,rampDown,enabled,apply,verify,remove,reload,fromCursor,bank,expand,reference,lastTouched,openRack,fit,zoomOut,zoomIn,panLeft,panRight,tool,rangeStart,rangeEnd,toolValue0,toolValue1,toolValue2,toolValue3,copyRange,previewTool,close,
    absolute=4240,heading=4300,targetLabel,rowLabel,valueLabel,formulaLabel,rangeLabel,toolLabel0,toolLabel1,toolLabel2,toolLabel3,statusLabel};
  static constexpr std::array<const char *,9> curves={"step","linear","smooth","exponential","logarithmic","step-next","exponential-reverse","logarithmic-reverse","scripted"};
  static constexpr std::array<const char *,9> operations={"flip-time","flip-values","shift","scale","ramp","sine","humanize","paste","insert"};
  Request request_;std::function<Cursor()> context_;std::function<void(const std::string &,uint32_t)> inspect_;
  std::function<void(const std::string &,uint32_t)> absolute_;
  Cursor captured_;std::string patternID_,pluginID_,laneID_;std::optional<uint32_t> parameter_;
  Json lanes_=Json::array(),plugins_=Json::array(),catalog_=Json::array(),points_=Json::array(),values_=Json::array(),clip_;
  std::vector<size_t> filtered_;unsigned rows_=64,rowsPerBeat_=4,snap_=256;int selected_=-1,kind_=1,tool_=0;
  bool setting_=false,pending_=false,dirty_=false,pointFields_=false,enabled_=true,previewNeeded_=false,dragging_=false,dragDirty_=false;
  uint64_t generation_=0;Json dragBefore_;int dragSelection_=-1;
  AutomationCanvas canvas_;
  std::unique_ptr<EnvelopeBankWindow> bank_;
  std::unique_ptr<FormulaWorkbenchWindow> workbench_,reference_;
  int selection(int id)const{return int(SendMessageW(controls_.at(id),CB_GETCURSEL,0,0));}
  void choose(int id,int index){SendMessageW(controls_.at(id),CB_SETCURSEL,index,0);}
  void require(bool value,const char *message)const{if(!value)throw std::runtime_error(message);}
  bool draft()const{return dirty_||pointFields_;}
  bool current()const{const auto now=context_();return captured_.document==now.document&&captured_.revision==now.revision;}
  void requireCurrent()const{require(current(),"Song changed / captured automation retained; Reload before applying");}
  void status(std::wstring value){status_=std::move(value);set(statusLabel,status_);requestPaint();}
  void error(const std::exception &e)override{status(wide(e.what()));}
  void rebuild(){canvas_.rebuild(points_,values_);requestPaint();}
  void schedule(){previewNeeded_=true;if(visible())SetTimer(window_,3,120,nullptr);}
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
    SendMessageW(controls_.at(parameters),WM_SETREDRAW,FALSE,0);SendMessageW(controls_.at(parameters),LB_RESETCONTENT,0,0);
    for(size_t i=0;i<catalog_.size();++i){auto name=wide(catalog_[i].at("name").get<std::string>()),folded=name;std::transform(folded.begin(),folded.end(),folded.begin(),[](wchar_t c){return wchar_t(std::towlower(c));});if(!term.empty()&&folded.find(term)==std::wstring::npos)continue;
      if(parameter_&&catalog_[i].at("id")==*parameter_)selected=int(filtered_.size());filtered_.push_back(i);SendMessageW(controls_.at(parameters),LB_ADDSTRING,0,reinterpret_cast<LPARAM>(name.c_str()));}
    SendMessageW(controls_.at(parameters),LB_SETCURSEL,selected,0);SendMessageW(controls_.at(parameters),WM_SETREDRAW,TRUE,0);InvalidateRect(controls_.at(parameters),nullptr,FALSE);
  }
  void choices(){
    setting_=true;SendMessageW(controls_.at(pattern),CB_RESETCONTENT,0,0);int i=0;
    for(const auto &p:captured_.patterns){auto name=L"Pattern "+std::to_wstring(p.at("index").get<unsigned>())+L" · "+wide(p.at("name").get<std::string>());SendMessageW(controls_.at(pattern),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name.c_str()));if(p.at("id")==patternID_)choose(pattern,i);++i;}
    SendMessageW(controls_.at(plugin),CB_RESETCONTENT,0,0);i=0;for(const auto &p:plugins_){auto name=wide(p.at("name").get<std::string>());SendMessageW(controls_.at(plugin),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name.c_str()));if(p.at("instanceID")==pluginID_)choose(plugin,i);++i;}
    setting_=false;filter();
  }
  void load(bool follow,std::string wantedPlugin={},std::optional<uint32_t> wantedParameter={},std::optional<unsigned> wantedPattern={},bool allowMissingPlugin=false){
    if(pending_)return;const auto now=context_();require(follow||now.document==captured_.document,"Document changed / use From cursor to capture the new song");
    unsigned index=follow?now.pattern:captured_.pattern;
    if(wantedPattern)index=*wantedPattern;else if(!follow){auto found=std::find_if(now.patterns.begin(),now.patterns.end(),[&](const auto &p){return p.at("id")==patternID_;});require(found!=now.patterns.end(),"Captured pattern was removed / use From cursor");index=found->at("index");}
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
        wanted=chosen->at("instanceID");if(!chosen->value("unavailable",false))try{catalog=request_("plugin.parameters.get",{{"slot",size_t(chosen-plugins.begin())}});}catch(const std::exception &e){catalogueError=wide(e.what());}
        for(const auto &l:data.at("lanes"))if(l.at("plugin")==wanted&&std::none_of(catalog.begin(),catalog.end(),[&](const auto &p){return p.at("id")==l.at("parameter");}))catalog.push_back({{"id",l.at("parameter")},{"name","Unavailable parameter "+std::to_string(l.at("parameter").get<uint32_t>())},{"unavailable",true}});
      }else wanted.clear();
      const auto after=context_();require(after.document==now.document&&after.revision==now.revision&&token==generation_,"Song or draft changed while loading / captured editor retained");
      const bool samePattern=sameSong&&data.at("patternID")==patternID_;captured_=now;captured_.pattern=index;patternID_=data.at("patternID");rows_=data.at("rows");rowsPerBeat_=data.at("rowsPerBeat");plugins_=std::move(plugins);pluginID_=wanted;lanes_=data.at("lanes");catalog_=std::move(catalog);
      if(!samePattern){canvas_.fit(rows_);selectedPosition.reset();setting_=true;set(rangeStart,L"0");set(rangeEnd,std::to_wstring(rows_));setting_=false;}else if(canvas_.end>rows_*256)canvas_.fit(rows_);
      parameter_.reset();if(!catalog_.empty()){auto p=std::find_if(catalog_.begin(),catalog_.end(),[&](const auto &v){return wantedID&&v.at("id")==*wantedID;});if(p==catalog_.end())p=catalog_.begin();selectParameter(p->at("id"),selectedPosition);}
      else {points_=values_=Json::array();laneID_.clear();selected_=-1;dirty_=pointFields_=false;++generation_;set(targetLabel,L"No available parameter");showPoint();rebuild();}
      choices();pending_=false;status(!catalogueError.empty()?catalogueError:catalog_.empty()?L"Add a plugin in the rack, then reload to choose its parameter":L"Click or drag points / Apply saves; the pattern cursor remains independent");
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
      else status(L"The tool leaves this envelope unchanged");
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
    if(notification==EN_CHANGE){if(id==search){filter();return;}if(id==pointRow||id==pointValue||id==formula){pointFields_=true;++generation_;status(L"Point fields pending / Set point before Apply");}return;}
    if(pending_)return;
    if(notification==CBN_SELCHANGE){
      if(id==kind){kind_=std::clamp(selection(kind),0,8);pointFields_=true;++generation_;return;}if(id==snap){snap_=std::array<unsigned,4>{256,128,64,1}.at(size_t(std::max(0,selection(snap))));return;}
      if(id==tool){tool_=std::clamp(selection(tool),0,8);toolFields();return;}
      if(id==pattern||id==plugin){if(draft()){choices();throw std::runtime_error("Apply or Reload the curve draft before changing target");}const auto index=selection(id);if(index<0)return;
        try{if(id==pattern)load(false,{},parameter_,captured_.patterns.at(size_t(index)).at("index").get<unsigned>());else load(false,plugins_.at(size_t(index)).at("instanceID"),std::nullopt);}catch(...){choices();throw;}return;}
    }
    if(id==parameters&&notification==LBN_SELCHANGE){const auto index=SendMessageW(controls_.at(parameters),LB_GETCURSEL,0,0);if(draft()){filter();throw std::runtime_error("Apply or Reload the curve draft before changing parameter");}if(index>=0&&size_t(index)<filtered_.size()){selectParameter(catalog_.at(filtered_[size_t(index)]).at("id"));filter();}return;}
    if(notification!=BN_CLICKED)return;
    if(id==reload)load(false);else if(id==fromCursor)load(true);else if(id==lastTouched)touch();else if(id==bank)openBank();else if(id==expand)openFormula();
    else if(id==reference){if(!reference_)reference_=std::make_unique<FormulaWorkbenchWindow>(window_,L"Envelope formula reference","",Json::object(),-1,request_);reference_->show();}
    else if(id==openRack){requireCurrent();require(parameter_.has_value(),"Choose a parameter");inspect_(pluginID_,*parameter_);}
    else if(id==absolute){requireCurrent();require(parameter_.has_value(),"Choose a parameter");absolute_(pluginID_,*parameter_);}
    else if(id==setPoint)setPointFields();else if(id==apply||id==verify||id==remove)commit(id==verify,id==remove);
    else if(id==copyRange||id==previewTool)transform(id==copyRange);
    else if(id==enabled){enabled_=!enabled_;changed();}
    else if(id==deletePoint){require(!pointFields_,"Set or discard the point fields first");if(selected_>=0){points_.erase(points_.begin()+selected_);selected_=points_.empty()?-1:std::min(selected_,int(points_.size()-1));changed();showPoint();}}
    else if(id==rampUp||id==rampDown){require(parameter_&&!pointFields_,"Choose a parameter and set pending point fields first");points_=Json::array({{{"position",0},{"value",id==rampDown?1:0},{"curve",curves[size_t(kind_)]}},{{"position",rows_*256-1},{"value",id==rampDown?0:1},{"curve",curves[size_t(kind_)]}}});if(kind_==8)for(auto &p:points_)p["formula"]="mix(start,end,t)";selected_=0;changed();showPoint();}
    else if(id==fit||id==zoomIn||id==zoomOut||id==panLeft||id==panRight){if(id==fit)canvas_.fit(rows_);else if(id==panLeft||id==panRight)canvas_.pan((canvas_.end-canvas_.start)*(id==panLeft?-.25:.25),rows_);else canvas_.zoom(id==zoomIn?2:.5,rows_);schedule();rebuild();}
  }
  void cancelDrag(){if(!dragging_)return;dragging_=false;points_=dragBefore_;dirty_=dragDirty_;selected_=dragSelection_;++generation_;values_=Json::array();schedule();showPoint();rebuild();ReleaseCapture();}
  void mouse(UINT message,float x,float y,WPARAM)override{
    if(message==WM_CAPTURECHANGED){cancelDrag();return;}if(message==WM_LBUTTONUP){dragging_=false;ReleaseCapture();schedule();return;}
    if(message==WM_LBUTTONDOWN&&canvas_.viewport.contains(x,y)&&parameter_&&!pending_&&!pointFields_){SetFocus(window_);dragBefore_=points_;dragDirty_=dirty_;dragSelection_=selected_;selected_=canvas_.hit(x,y);
      if(selected_<0){auto position=uint32_t(std::clamp(std::round(canvas_.position(x)/snap_)*snap_,0.0,double(rows_)*256-1));for(size_t i=0;i<points_.size();++i)if(points_[i].at("position")==position)selected_=int(i);
        if(selected_<0){Json p={{"position",position},{"value",canvas_.value(y)},{"curve",curves[size_t(kind_)]}};if(kind_==8)p["formula"]="mix(start,end,t)";replacePoint(std::move(p));}}
      showPoint();dragging_=true;SetCapture(window_);return;}
    if(message==WM_MOUSEMOVE&&dragging_&&selected_>=0){auto p=points_[size_t(selected_)];const auto position=uint32_t(std::clamp(std::round(canvas_.position(x)/snap_)*snap_,0.0,double(rows_)*256-1));for(size_t i=0;i<points_.size();++i)if(int(i)!=selected_&&points_[i].at("position")==position)return;p["position"]=position;p["value"]=canvas_.value(y);replacePoint(std::move(p));}
  }
  bool wheel(UINT message,float x,float y,WPARAM w)override{
    if(!canvas_.viewport.contains(x,y)||dragging_)return false;const double delta=GET_WHEEL_DELTA_WPARAM(w)/120.0;const bool ctrl=GET_KEYSTATE_WPARAM(w)&MK_CONTROL,shift=GET_KEYSTATE_WPARAM(w)&MK_SHIFT;
    if(ctrl){if(shift)canvas_.zoomValues(std::pow(1.25,delta));else canvas_.zoom(std::pow(1.25,delta),rows_);}else if(shift)canvas_.panValues(delta*(canvas_.valueHigh-canvas_.valueLow)*.1);else canvas_.pan((message==WM_MOUSEHWHEEL?1:-1)*delta*(canvas_.end-canvas_.start)*.1,rows_);schedule();rebuild();return true;
  }
  bool key(WPARAM k,bool ctrl,bool shift)override{
    if(k==VK_ESCAPE){if(dragging_)cancelDrag();else if(pointFields_){++generation_;showPoint();}else hide();return true;}
    if(k==VK_F6){SetFocus(GetFocus()==window_?controls_.at(parameters):window_);requestPaint();return true;}
    if(ctrl&&k=='R'){action(reload,BN_CLICKED);return true;}if(ctrl&&k==VK_RETURN){action(apply,BN_CLICKED);return true;}
    if(k==VK_RETURN){const auto id=GetDlgCtrlID(GetFocus());if(id==pointRow||id==pointValue||id==formula){action(setPoint,BN_CLICKED);return true;}wchar_t klass[32]{};GetClassNameW(GetFocus(),klass,32);if(_wcsicmp(klass,L"Button")==0){action(id,BN_CLICKED);return true;}}
    if(GetFocus()!=window_||pending_)return false;
    if(ctrl&&(k==VK_LEFT||k==VK_RIGHT)){action(k==VK_LEFT?panLeft:panRight,BN_CLICKED);return true;}if(ctrl)return false;
    if(k==VK_HOME){action(fit,BN_CLICKED);return true;}if(k==VK_OEM_PLUS||k==VK_ADD||k==VK_OEM_MINUS||k==VK_SUBTRACT){action(k==VK_OEM_PLUS||k==VK_ADD?zoomIn:zoomOut,BN_CLICKED);return true;}
    if(k==VK_DELETE){action(deletePoint,BN_CLICKED);return true;}if(pointFields_)return false;
    if(k==VK_TAB&&!points_.empty()){selected_=(selected_+(shift?-1:1)+int(points_.size()))%int(points_.size());showPoint();requestPaint();return true;}
    if(selected_>=0&&(k==VK_LEFT||k==VK_RIGHT||k==VK_UP||k==VK_DOWN)){auto p=points_[size_t(selected_)];if(k==VK_LEFT||k==VK_RIGHT)p["position"]=uint32_t(std::clamp(p.at("position").get<double>()+(k==VK_LEFT?-1:1)*(shift?1.0:double(snap_)),0.0,double(rows_)*256-1));else p["value"]=std::clamp(p.at("value").get<double>()+(k==VK_UP?1:-1)*(shift?.001:.01),0.0,1.0);replacePoint(std::move(p));return true;}return false;
  }
  void timer(UINT_PTR id)override{if(id!=3)return;KillTimer(window_,3);if(!visible())return;if(pending_||dragging_){SetTimer(window_,3,120,nullptr);return;}if(previewNeeded_)previewNow();}
  void layout()override{
    const auto [w,h]=size();place(heading,18,14,w-36,24);place(pattern,18,48,214,220);place(plugin,246,48,w-694,260);place(lastTouched,w-438,48,138,26);place(bank,w-292,48,134,26);place(openRack,w-150,48,132,26);
    place(search,18,90,214,26);place(parameters,18,126,214,std::max(120.f,h-354));place(targetLabel,260,84,w-280,23);
    place(absolute,18,h-220,214,26);EnableWindow(controls_.at(absolute),!pending_&&parameter_.has_value());
    place(kind,260,112,214,230);place(snap,482,112,104,220);place(enabled,594,112,104,26);
    float x=706;for(auto [id,width]:std::initializer_list<std::pair<int,float>>{{panLeft,32.f},{zoomOut,32.f},{fit,40.f},{zoomIn,32.f},{panRight,32.f}}){place(id,x,112,width,26);x+=width+4;}
    canvas_.viewport={260,170,w-280,std::max(100.f,h-468)};rebuild();
    const auto py=h-226;place(rowLabel,260,py-18,110,18);place(pointRow,260,py,92,26);place(valueLabel,360,py-18,100,18);place(pointValue,360,py,92,26);place(setPoint,460,py,98,26);place(deletePoint,566,py,98,26);place(rampUp,672,py,96,26);place(rampDown,776,py,104,26);
    place(formulaLabel,260,h-288,56,20,kind_==8);place(formula,320,h-292,w-548,26,kind_==8);place(expand,w-220,h-292,94,26,kind_==8);place(reference,w-118,h-292,98,26);
    place(rangeLabel,18,h-184,122,20);place(rangeStart,142,h-188,82,26);place(rangeEnd,232,h-188,82,26);place(tool,328,h-188,182,230);place(copyRange,518,h-188,122,26);place(previewTool,648,h-188,140,26);
    const int valueCount=std::array<int,9>{0,0,1,2,2,4,3,1,1}.at(size_t(tool_));for(int i=0;i<4;++i){place(toolLabel0+i,18+194.f*i,h-150,180,20,i<valueCount);place(toolValue0+i,18+194.f*i,h-128,180,26,i<valueCount);}
    x=18;for(auto [id,width]:std::initializer_list<std::pair<int,float>>{{apply,100.f},{verify,94.f},{remove,118.f},{reload,118.f},{fromCursor,124.f}}){place(id,x,h-66,width,28);x+=width+8;}place(close,w-118,h-66,100,28);place(statusLabel,18,h-30,w-36,24);
    set(enabled,enabled_?L"Enabled":L"Disabled");for(auto [id,control]:controls_)if(id>=pattern&&id<=close)EnableWindow(control,!pending_||id==close||id==search);
    for(int id:{kind,snap,pointRow,pointValue,formula,setPoint,deletePoint,rampUp,rampDown,enabled,apply,verify,bank,expand,openRack})EnableWindow(controls_.at(id),!pending_&&parameter_.has_value());
    for(int id:{remove,copyRange,previewTool})EnableWindow(controls_.at(id),!pending_&&!laneID_.empty());
  }
  void paint(RenderSurface &s)override{
    const auto [w,h]=size();s.fill(0,0,w,h,0x18222d);const auto r=canvas_.viewport;s.fill(r.x,r.y,r.w,r.h,0x101923);s.clip(r.x,r.y,r.w,r.h);
    for(int i=0;i<=4;++i){const auto y=r.y+i*r.h/4;s.line(r.x,y,r.x+r.w,y,0x2a3947);}
    const auto step=std::max(256.0,rowsPerBeat_*256.0*std::ceil((canvas_.end-canvas_.start)/(rowsPerBeat_*256)/16));
    for(double p=std::ceil(canvas_.start/step)*step;p<=canvas_.end;p+=step){const auto x=canvas_.screen(p,0).x;s.line(x,r.y,x,r.y+r.h,0x2a3947);}
    for(size_t i=1;i<canvas_.curve.size();++i)s.line(canvas_.curve[i-1].x,canvas_.curve[i-1].y,canvas_.curve[i].x,canvas_.curve[i].y,enabled_?0x6edac5:0x647c89,2);
    for(size_t i=0;i<canvas_.handles.size();++i){const auto p=canvas_.handles[i];s.fill(p.x-4,p.y-4,8,8,int(i)==selected_?0xffd08a:0x6edac5);}s.unclip();s.outline(r.x,r.y,r.w,r.h,GetFocus()==window_?0x6edac5:0x334757);
    wchar_t label[160]{};swprintf_s(label,L"Rows %.2f–%.2f · %.1f–%.1f%% · Ctrl+wheel zooms; Ctrl+Shift zooms values",canvas_.start/256,canvas_.end/256,canvas_.valueLow*100,canvas_.valueHigh*100);s.uiText(label,r.x,r.y-22,r.w,0x93aabd);
  }
public:
  ParameterAutomationWindow(HWND owner,Request request,std::function<Cursor()> context,std::function<void(const std::string &,uint32_t)> inspect,std::function<void(const std::string &,uint32_t)> absoluteEditor):NativeToolWindow(owner),request_(std::move(request)),context_(std::move(context)),inspect_(std::move(inspect)),absolute_(std::move(absoluteEditor)){
    minimumWidth_=1040;minimumHeight_=760;create(L"ScreamSeq.ParameterAutomation",L"Pattern parameter automation",1240,850);
    for(int id:{pattern,plugin,kind,snap,tool})combo(id);for(int id:{search,pointRow,pointValue,formula,rangeStart,rangeEnd,toolValue0,toolValue1,toolValue2,toolValue3})edit(id,L"",id==formula?2048:id==search?128:32);
    add(parameters,L"LISTBOX",L"Automation parameters",LBS_NOTIFY|LBS_NOINTEGRALHEIGHT|WS_VSCROLL);
    button(absolute,L"Song automation…");
    for(auto name:{L"Step",L"Linear",L"Smooth",L"Exponential",L"Logarithmic",L"Step at start",L"Exponential reversed",L"Logarithmic reversed",L"Scripted"})SendMessageW(controls_.at(kind),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name));choose(kind,1);
    for(auto name:{L"1 row",L"½ row",L"¼ row",L"1/256 row"})SendMessageW(controls_.at(snap),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name));choose(snap,0);
    for(auto name:{L"Flip time",L"Flip values",L"Shift",L"Scale",L"Ramp",L"Sine",L"Humanize",L"Paste",L"Insert paste"})SendMessageW(controls_.at(tool),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name));choose(tool,0);
    for(auto [id,name]:std::initializer_list<std::pair<int,const wchar_t *>>{{setPoint,L"Set point"},{deletePoint,L"Delete point"},{rampUp,L"Ramp up"},{rampDown,L"Ramp down"},{enabled,L"Enabled"},{apply,L"Apply curve"},{verify,L"Verify"},{remove,L"Remove lane"},{reload,L"Reload"},{fromCursor,L"From cursor"},{bank,L"Envelope bank…"},{expand,L"Expand…"},{reference,L"Reference"},{lastTouched,L"Use last touched"},{openRack,L"Show in rack"},{fit,L"Fit"},{zoomOut,L"−"},{zoomIn,L"+"},{panLeft,L"‹"},{panRight,L"›"},{copyRange,L"Copy range"},{previewTool,L"Preview tool"},{close,L"Close"}})button(id,name);
    for(auto [id,name]:std::initializer_list<std::pair<int,const wchar_t *>>{{heading,L"Pattern parameter automation"},{targetLabel,L"Choose a plugin parameter"},{rowLabel,L"Row / 1⁄256"},{valueLabel,L"Value / %"},{formulaLabel,L"Formula"},{rangeLabel,L"Range / rows"},{statusLabel,L""}})label(id,name);
    for(int id=toolLabel0;id<=toolLabel3;++id)label(id,L"");set(rangeStart,L"0");set(rangeEnd,L"64");finish();
  }
  void openAt(std::string plugin={},std::optional<uint32_t> parameter={}){const bool retain=visible()||draft()||(bank_&&bank_->retainedDraft())||(workbench_&&workbench_->retainedDraft());show();if(!retain)load(true,std::move(plugin),parameter);if(previewNeeded_)SetTimer(window_,3,120,nullptr);SetFocus(controls_.at(parameters));}
  Json snapshot()const{
    Json handles=Json::array();for(size_t i=0;i<canvas_.handles.size();++i)handles.push_back({{"index",i},{"x",canvas_.handles[i].x},{"y",canvas_.handles[i].y}});const auto r=canvas_.viewport;
    return {{"visible",visible()},{"document",captured_.document},{"expectedRevision",captured_.revision},{"pattern",captured_.pattern},{"patternID",patternID_},{"plugin",pluginID_},{"parameter",parameter_?Json(*parameter_):Json()},{"lane",laneID_},{"dirty",dirty_},{"fieldDraft",pointFields_},{"pending",pending_},{"stale",!current()},{"enabled",enabled_},{"points",points_},{"selectedPoint",selected_},{"parameterCount",catalog_.size()},{"filteredCount",filtered_.size()},{"start",canvas_.start},{"end",canvas_.end},{"valueLow",canvas_.valueLow},{"valueHigh",canvas_.valueHigh},{"previewSamples",canvas_.curve.size()},{"handles",handles},{"canvas",{r.x,r.y,r.w,r.h}},{"status",utf8(status_)},{"envelopeBank",bank_?bank_->snapshot():Json{{"visible",false}}},{"formulaWorkbench",workbench_?workbench_->snapshot():Json{{"visible",false}}}};
  }
};
}
