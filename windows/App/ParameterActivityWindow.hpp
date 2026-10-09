#pragma once
#include "NativeToolWindow.hpp"
#include <deque>

namespace ScreamSeq {
// One retained observer per workspace. Captured processor identities and point
// drafts never follow a new song, prepared engine or external API watch.
class ParameterActivityWindow final : public NativeToolWindow {
  using Json=Api::Json;
public:
  struct Context {std::string document,revision;bool busy=false;};
  using Request=std::function<Json(const std::string &,const Json &)>;
private:
  enum:int {processors=11000,parameters,refresh,freeze,clear,mode,details,items,openSource,openLane,
    previous,next,newPoint,savePoint,removePoint,time,value,zoomOut,zoomIn,fit,close,
    title=11100,targetLabel,ruleLabel,statusLabel,timeLabel,valueLabel,pageLabel};
  Request request_;std::function<Context()> context_;std::function<void(const Json &)> inspect_;
  Context captured_;Json targets_=Json::array(),parameters_=Json::array(),sources_=Json::array(),recorded_=Json::array();
  std::deque<Json> points_;std::vector<size_t> listed_;std::string target_,plugin_,token_,sourceRevision_;
  std::optional<uint32_t> parameter_;uint64_t cursor_=0,dropped_=0,engine_=0,originalFrame_=0;
  uint64_t generation_=0;
  size_t offset_=0,total_=0;int page_=0,traceMode_=0,selected_=-1;double seconds_=8;
  bool setting_=false,pending_=false,frozen_=false,fields_=false,active_=false;
  std::optional<Tracker::DocumentDraft> documentDraft()const override {
    return describeDraft(captured_.document,captured_.revision,Json::array({target_,plugin_,parameter_?Json(*parameter_):Json(),originalFrame_}).dump(),
      generation_,fields_,pending_);
  }
  WorkspaceRect plot_{};
  std::map<int,LRESULT> pendingSelections_;
  void beginPending(){pendingSelections_.clear();for(int id:{processors,parameters,mode,details})pendingSelections_[id]=SendMessageW(controls_.at(id),CB_GETCURSEL,0,0);pendingSelections_[items]=SendMessageW(controls_.at(items),LB_GETCURSEL,0,0);pending_=true;}
  void restorePendingSelection(int id){if(const auto i=pendingSelections_.find(id);i!=pendingSelections_.end())SendMessageW(controls_.at(id),id==items?LB_SETCURSEL:CB_SETCURSEL,i->second,0);}
  void require(bool ok,const char *why)const{if(!ok)throw std::runtime_error(why);}
  bool current()const{const auto c=context_();return c.document==captured_.document&&c.revision==captured_.revision;}
  void status(std::wstring text){status_=std::move(text);set(statusLabel,status_);requestPaint();}
  void error(const std::exception &e)override{status(wide(e.what()));}
  int selection(int id)const{return int(SendMessageW(controls_.at(id),CB_GETCURSEL,0,0));}
  void choose(int id,int i){ScreamSeq::NativeInputGate::present(controls_.at(id),CB_SETCURSEL,i,0);}
  void append(int id,const std::wstring &s){ScreamSeq::NativeInputGate::present(controls_.at(id),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(s.c_str()));}
  void choices(){setting_=true;ScreamSeq::NativeInputGate::present(controls_.at(processors),CB_RESETCONTENT,0,0);int index=-1;for(size_t i=0;i<targets_.size();++i){const auto &t=targets_[i];append(processors,wide(t.at("name"))+L" · "+wide(t.at("key")));if(t.at("key")==target_)index=int(i);}choose(processors,index);
    ScreamSeq::NativeInputGate::present(controls_.at(parameters),CB_RESETCONTENT,0,0);index=-1;for(size_t i=0;i<parameters_.size();++i){const auto &p=parameters_[i];append(parameters,wide(p.at("name"))+L" · "+wide(p.value("unitLabel",std::string())));if(parameter_&&p.at("id")==*parameter_)index=int(i);}choose(parameters,index);setting_=false;}
  void showFields(){setting_=true;if(selected_>=0&&size_t(selected_)<recorded_.size()){const auto &p=recorded_[size_t(selected_)];originalFrame_=p.at("frame");set(time,double(originalFrame_)/48000);set(value,p.at("value"));}else{originalFrame_=0;set(time,L"0");set(value,L"0");}fields_=false;setting_=false;}
  void list(){const auto old=int(SendMessageW(controls_.at(items),LB_GETCURSEL,0,0));SendMessageW(controls_.at(items),WM_SETREDRAW,FALSE,0);ScreamSeq::NativeInputGate::present(controls_.at(items),LB_RESETCONTENT,0,0);listed_.clear();
    const auto add=[&](size_t index,const std::wstring &text){listed_.push_back(index);ScreamSeq::NativeInputGate::present(controls_.at(items),LB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));};
    if(page_==0)for(size_t i=0;i<sources_.size();++i)add(i,wide(sources_[i].value("title",std::string()))+(sources_[i].value("enabled",true)?L"":L" · disabled"));
    else if(page_==1){for(size_t i=points_.size()>512?points_.size()-512:0;i<points_.size();++i){const auto &p=points_[i];add(i,std::to_wstring(p.at("seconds").get<double>())+L" s · "+wide(p.at("source").at("kind"))+L" · "+wide(p.at("value").dump())+(p.at("source").at("kind")=="graph-source"?L" normalized contribution":L" native units"));}}
    else for(size_t i=0;i<recorded_.size();++i){const auto &p=recorded_[i];add(i,std::to_wstring(p.at("frame").get<double>()/48000)+L" s · "+wide(p.at("value").dump()));}
    ScreamSeq::NativeInputGate::present(controls_.at(items),LB_SETCURSEL,page_==2?selected_:old,0);SendMessageW(controls_.at(items),WM_SETREDRAW,TRUE,0);InvalidateRect(controls_.at(items),nullptr,FALSE);
    set(pageLabel,page_==2?std::to_wstring(offset_)+L"–"+std::to_wstring(offset_+recorded_.size())+L" of "+std::to_wstring(total_)+L" recorded points":std::to_wstring(listed_.size())+L" entries");}
  struct RecordedRead {Json points=Json::array();size_t offset=0,total=0;int selected=-1;};
  struct SourcesRead {Json sources=Json::array();std::wstring rule;};
  void guardRead(const Context &c,uint64_t generation)const {
    const auto now=context_();require(c.document==now.document&&c.revision==now.revision,"Song changed while reading / captured fields retained; Refresh");
    require(generation==generation_,"Newer point fields retained; Refresh again when ready");
  }
  RecordedRead readRecorded(const std::string &plugin,std::optional<uint32_t> parameter,size_t offset,const Context &c,uint64_t generation) {
    RecordedRead result; if(plugin.empty()||!parameter)return result;
    auto r=request_("automation.recorded.get",{{"plugin",plugin},{"parameter",*parameter},{"offset",offset},{"limit",512}});guardRead(c,generation);
    require(r.at("sampleRate")==48000,"Unsupported recorded time base");result.total=r.at("total").get<size_t>();result.offset=r.at("offset").get<size_t>();result.points=r.at("points");
    require(result.points.is_array()&&result.points.size()<=512&&result.offset<=result.total&&result.points.size()<=result.total-result.offset,"Incomplete recorded point page");
    for(const auto &p:result.points){(void)p.at("frame").get<uint64_t>();require(std::isfinite(p.at("value").get<double>()),"Invalid recorded value");}
    result.selected=result.points.empty()?-1:0;return result;
  }
  SourcesRead readSources(const std::string &target,std::optional<uint32_t> parameter,const Context &c,uint64_t generation) {
    SourcesRead result;if(!parameter)return result;
    auto r=request_("parameter.activity.sources",{{"target",target},{"parameter",*parameter}});guardRead(c,generation);result.sources=r.at("sources");result.rule=wide(r.at("rule").get<std::string>());
    require(result.sources.is_array(),"Incomplete parameter sources");for(const auto &s:result.sources){(void)s.value("title",std::string());(void)s.value("enabled",true);}return result;
  }
  std::string readWatch(const std::string &target,uint32_t parameter,bool clearCapture,const Context &c,uint64_t generation) {
    auto r=request_("parameter.activity.watch",{{"target",target},{"parameter",parameter},{"clear",clearCapture}});guardRead(c,generation);
    require(r.at("target").at("key")==target&&r.at("parameter")==parameter,"Prepared processor changed while starting observation / Refresh");return r.at("token").get<std::string>();
  }
  void adoptRecorded(RecordedRead r) {recorded_=std::move(r.points);offset_=r.offset;total_=r.total;selected_=r.selected;showFields();}
  void recorded(size_t offset) {const auto c=context_();const auto generation=generation_;auto r=readRecorded(plugin_,parameter_,offset,c,generation);guardRead(c,generation);captured_=c;adoptRecorded(std::move(r));}
  void recorded(){recorded(offset_);}
  void sources(){const auto c=context_();const auto generation=generation_;auto r=readSources(target_,parameter_,c,generation);guardRead(c,generation);sources_=std::move(r.sources);sourceRevision_=c.revision;set(ruleLabel,r.rule);}
  void watch(bool clearCapture){require(parameter_.has_value(),"Choose a prepared processor and parameter");const auto c=context_();auto token=readWatch(target_,*parameter_,clearCapture,c,generation_);token_=std::move(token);cursor_=dropped_=0;points_.clear();}
  void load(std::string wanted={},std::optional<uint32_t> parameter={},bool explicitRefresh=false){
    require(!fields_||explicitRefresh,"Save or Refresh the recorded-point draft before changing target");const auto c=context_();const auto generation=generation_;require(captured_.document.empty()||captured_.document==c.document||explicitRefresh,"Captured song is unavailable / Refresh");
    auto r=request_("parameter.activity.targets",Json::object());guardRead(c,generation);auto targets=r.at("targets");require(targets.is_array(),"Incomplete prepared processor list");const auto engine=r.at("engine").get<uint64_t>();const auto active=r.at("active").get<bool>();
    for(const auto &t:targets){(void)t.at("key").get<std::string>();(void)t.at("name").get<std::string>();(void)t.at("plugin").get<std::string>();}
    const auto desired=wanted.empty()?target_:wanted;auto found=std::find_if(targets.begin(),targets.end(),[&](const auto &t){return t.at("key")==desired||(!wanted.empty()&&t.at("plugin")==wanted);});
    if(found==targets.end()&&!desired.empty()&&!targets.empty())throw std::runtime_error("Captured processor copy is unavailable / reopen after preparing playback");
    if(found==targets.end()&&desired.empty())found=targets.begin();Json catalog=Json::array();std::string target,plugin,token;
    if(found!=targets.end()) {target=found->at("key");plugin=found->at("plugin");catalog=request_("parameter.activity.parameters",{{"target",target}}).at("parameters");guardRead(c,generation);require(catalog.is_array(),"Incomplete parameter catalogue");}
    if(!parameter)parameter=parameter_;std::optional<uint32_t> selected;for(const auto &p:catalog){const auto id=p.at("id").get<uint32_t>();(void)p.at("name").get<std::string>();(void)p.value("unitLabel",std::string());if(parameter&&id==*parameter)selected=id;}if(!selected&&!catalog.empty())selected=catalog.front().at("id").get<uint32_t>();
    auto sourceData=readSources(target,selected,c,generation);auto lane=readRecorded(plugin,selected,0,c,generation);
    // Starting the transient watch is last: all document reads are staged first.
    if(selected)token=readWatch(target,*selected,false,c,generation);guardRead(c,generation);
    targets_=std::move(targets);parameters_=std::move(catalog);target_=std::move(target);plugin_=std::move(plugin);parameter_=selected;engine_=engine;active_=active;captured_=c;
    token_=std::move(token);cursor_=dropped_=0;points_.clear();sources_=std::move(sourceData.sources);sourceRevision_=c.revision;adoptRecorded(std::move(lane));++generation_;
    choices();list();set(ruleLabel,sourceData.rule);set(targetLabel,target_.empty()?L"Choose a prepared processor copy":wide(target_));
    status(parameter_?L"Live processor observation · recorded points use seconds and native parameter units":L"Start playback to prepare processor copies, then Refresh");
  }
  void poll(){if(!visible()||pending_||frozen_||!parameter_||context_().busy)return;beginPending();try{
    const auto c=context_();const auto generation=generation_;require(c.document==captured_.document,"Song changed / captured observer retained; Refresh");
    const auto r=request_("parameter.activity.get",{{"after",cursor_},{"limit",2048}});guardRead(c,generation);
    require(r.at("token")==token_&&!r.at("target").is_null()&&r.at("target").at("key")==target_&&r.at("parameter")==*parameter_,"Prepared processor or API watch changed / Refresh to observe again");
    const auto cursor=r.at("cursor").get<uint64_t>(),dropped=r.at("dropped").get<uint64_t>();const auto active=r.at("active").get<bool>();auto next=points_;
    require(r.at("points").is_array()&&r.at("points").size()<=2048,"Incomplete parameter activity page");
    for(const auto &p:r.at("points")){for(const auto *key:{"seconds","value","minimum","maximum"})require(std::isfinite(p.at(key).get<double>()),"Invalid activity value");(void)p.at("pattern");(void)p.at("order");(void)p.at("position");(void)p.at("source").at("kind").get<std::string>();next.push_back(p);}
    while(next.size()>8192)next.pop_front();std::optional<SourcesRead> sourceData;if(sourceRevision_!=c.revision)sourceData=readSources(target_,parameter_,c,generation);guardRead(c,generation);
    points_=std::move(next);cursor_=cursor;dropped_=dropped;active_=active;if(sourceData){sources_=std::move(sourceData->sources);sourceRevision_=c.revision;set(ruleLabel,sourceData->rule);}if(page_==1)list();requestPaint();pending_=false;
  }catch(const std::exception &e){pending_=false;frozen_=true;set(freeze,L"Resume");error(e);}}
  void commit(bool remove){
    require(parameter_&&!plugin_.empty(),"Recorded points require a rack plugin parameter");require(current(),"Song changed / point fields retained; Refresh before applying");const auto before=captured_;const auto generation=generation_;
    const double t=number(time);require(t>=0&&t<=604800,"Time must be between 0 and 604800 seconds");const auto frame=uint64_t(std::llround(t*48000));Json p={{"expectedRevision",captured_.revision},{"plugin",plugin_},{"parameter",*parameter_},{"frame",selected_>=0?originalFrame_:frame}};
    if(remove){require(selected_>=0,"Select a recorded point to delete");p["remove"]=true;}else{p["value"]=number(value);if(selected_>=0&&originalFrame_!=frame)p["newFrame"]=frame;}
    request_("automation.recorded.edit",p);const auto after=context_();require(after.document==before.document,"Document changed during edit / captured fields retained");
    if(generation!=generation_){status(L"Point saved; newer raw fields retained. Refresh before another edit.");return;}
    auto lane=readRecorded(plugin_,parameter_,offset_,after,generation);auto sourceData=readSources(target_,parameter_,after,generation);guardRead(after,generation);
    captured_=after;adoptRecorded(std::move(lane));sources_=std::move(sourceData.sources);sourceRevision_=after.revision;set(ruleLabel,sourceData.rule);list();++generation_;status(L"Recorded point saved · shared Undo restores the previous lane");
  }
  Json sourceLink(Json source,bool recent)const {
    require(parameter_.has_value(),"Choose a parameter before opening a source");const auto kind=source.value("kind",std::string());
    if(recent&&(kind=="graph-source"||kind=="envelope")) {
      const auto found=std::find_if(sources_.begin(),sources_.end(),[&](const auto &s){return s.value("kind",std::string())==kind&&s.contains("id")&&source.contains("id")&&s.at("id")==source.at("id");});
      if(found!=sources_.end())source=*found;
      else if(kind=="graph-source")source["kind"]="graph";
    }
    if(recent&&source.value("kind",std::string())=="graph") {
      const auto target=std::find_if(targets_.begin(),targets_.end(),[&](const auto &t){return t.at("key")==target_;});require(target!=targets_.end(),"Captured processor is unavailable / Refresh");
      source["graph"]=target->at("graph");source["node"]=target->at("node");
      if(!plugin_.empty()){source["scope"]="song";source["graph"]=nullptr;source.erase("node");}
    }
    source["plugin"]=plugin_;source["parameter"]=*parameter_;return source;
  }
  void action(int id,unsigned notification)override{
    if(setting_)return;if(id==close){hide();return;}if((id==time||id==value)&&notification==EN_CHANGE){fields_=true;++generation_;return;}if(pending_){if(notification==CBN_SELCHANGE||notification==LBN_SELCHANGE)restorePendingSelection(id);return;}
    if(id==freeze&&notification==BN_CLICKED){frozen_=!frozen_;set(freeze,frozen_?L"Resume":L"Freeze");return;}
    beginPending();try{
      if(id==processors&&notification==CBN_SELCHANGE){const auto i=selection(processors);require(i>=0,"Choose a processor");load(targets_.at(size_t(i)).at("key"));}
      else if(id==parameters&&notification==CBN_SELCHANGE){const auto i=selection(parameters);require(i>=0,"Choose a parameter");load(target_,parameters_.at(size_t(i)).at("id").get<uint32_t>());}
      else if(id==mode&&notification==CBN_SELCHANGE){traceMode_=std::max(0,selection(mode));requestPaint();}
      else if(id==details&&notification==CBN_SELCHANGE){page_=std::max(0,selection(details));if(page_==2&&!fields_)recorded();list();layout();}
      else if(id==items&&notification==LBN_SELCHANGE&&page_==2){require(!fields_,"Save or Refresh the point fields before selecting another point");selected_=int(SendMessageW(controls_.at(items),LB_GETCURSEL,0,0));showFields();}
      else if(notification==BN_CLICKED){
        if(id==refresh){load({}, {},true);frozen_=false;set(freeze,L"Freeze");}
        else if(id==clear){watch(true);list();requestPaint();}
        else if(id==openLane){require(current(),"Song changed / Refresh before navigating captured sources");require(parameter_&&!plugin_.empty(),"Choose a rack plugin parameter");inspect_({{"kind","recorded"},{"plugin",plugin_},{"parameter",*parameter_}});}
        else if(id==openSource){require(current(),"Song changed / Refresh before navigating captured sources");const auto i=int(SendMessageW(controls_.at(items),LB_GETCURSEL,0,0));require(i>=0&&size_t(i)<listed_.size(),"Select a source or recent change");if(page_==0)inspect_(sourceLink(sources_.at(listed_[size_t(i)]),false));else if(page_==1)inspect_(sourceLink(points_.at(listed_[size_t(i)]).at("source"),true));else inspect_({{"kind","recorded"},{"plugin",plugin_},{"parameter",*parameter_}});}
        else if(id==previous||id==next){require(!fields_,"Save or Refresh before changing recorded pages");const auto wanted=id==previous?(offset_>=512?offset_-512:0):std::min(offset_+512,total_?((total_-1)/512)*512:0);recorded(wanted);list();}
        else if(id==newPoint){require(!fields_,"Save or Refresh the current point first");selected_=-1;showFields();list();SetFocus(controls_.at(time));}
        else if(id==savePoint||id==removePoint)commit(id==removePoint);
        else if(id==fit||id==zoomIn||id==zoomOut){seconds_=id==fit?std::max(1.,points_.empty()?8.:points_.back().at("seconds").get<double>()-points_.front().at("seconds").get<double>()):std::clamp(seconds_*(id==zoomIn?.5:2),.05,604800.);requestPaint();}
      }
      pending_=false;
    }catch(...){pending_=false;choices();if(page_==2)ScreamSeq::NativeInputGate::present(controls_.at(items),LB_SETCURSEL,selected_,0);throw;}
  }
  void timer(UINT_PTR id)override{if(id==3)poll();}
  bool key(WPARAM key,bool ctrl,bool)override{if(key==VK_ESCAPE){hide();return true;}if(ctrl&&key=='R'){action(refresh,BN_CLICKED);return true;}if(key==VK_RETURN&&(GetFocus()==controls_.at(time)||GetFocus()==controls_.at(value))){action(savePoint,BN_CLICKED);return true;}return false;}
  void layout()override{const auto [w,h]=size();place(title,18,12,w-36,25);place(processors,18,46,w*.55-22,280);place(parameters,w*.55+4,46,w*.45-22,280);place(targetLabel,18,80,w-36,24);
    float x=18;for(auto [id,width]:std::initializer_list<std::pair<int,int>>{{refresh,92},{freeze,84},{clear,112},{zoomOut,38},{fit,46},{zoomIn,38}}){place(id,x,112,width,28);x+=width+8;}place(mode,x,112,200,200);plot_={18,156,w-36,std::max(110.f,h-492)};
    const float y=plot_.y+plot_.h+14;place(details,18,y,200,160);place(openSource,226,y,148,28);place(openLane,382,y,172,28);place(pageLabel,566,y+4,w-584,24);place(items,18,y+38,w-36,102);place(ruleLabel,18,y+146,w-36,36);
    const bool recorded=page_==2;place(timeLabel,18,h-131,156,22,recorded);place(valueLabel,188,h-131,156,22,recorded);place(time,18,h-109,156,28,recorded);place(value,188,h-109,156,28,recorded);x=358;for(auto [id,width]:std::initializer_list<std::pair<int,int>>{{newPoint,78},{savePoint,106},{removePoint,94},{previous,56},{next,56}}){place(id,x,h-109,width,28,recorded);x+=width+8;}place(close,w-112,h-65,94,28);place(statusLabel,18,h-30,w-36,24);}
  struct Trace {std::vector<size_t> indices;size_t contributions=0;double begin=0,end=0,low=0,high=0;};
  Trace trace()const {
    Trace t;for(size_t i=0;i<points_.size();++i){if(points_[i].at("source").at("kind")=="graph-source"){++t.contributions;continue;}t.indices.push_back(i);}if(t.indices.empty())return t;
    if(traceMode_==1){size_t first=t.indices.size()-1;const auto &last=points_[t.indices.back()];while(first){const auto &previous=points_[t.indices[first-1]],&next=points_[t.indices[first]];if(previous.at("pattern")!=last.at("pattern")||previous.at("order")!=last.at("order")||previous.at("position")>next.at("position"))break;--first;}t.indices.erase(t.indices.begin(),t.indices.begin()+first);}
    t.end=points_[t.indices.back()].at("seconds").get<double>();t.begin=traceMode_==1?points_[t.indices.front()].at("seconds").get<double>():std::max(0.,t.end-seconds_);
    std::erase_if(t.indices,[&](size_t i){return points_[i].at("seconds").get<double>()<t.begin;});if(t.indices.empty())return t;
    t.low=points_[t.indices.front()].at("minimum").get<double>();t.high=points_[t.indices.front()].at("maximum").get<double>();for(auto i:t.indices){const auto &p=points_[i];t.low=std::min({t.low,p.at("minimum").get<double>(),p.at("value").get<double>()});t.high=std::max({t.high,p.at("maximum").get<double>(),p.at("value").get<double>()});}return t;
  }
  Json traceSnapshot()const {const auto t=trace();return {{"nativePoints",t.indices.size()},{"normalizedContributions",t.contributions},{"segments",t.indices.empty()?0:t.indices.size()-1},{"minimum",t.indices.empty()?Json(nullptr):Json(t.low)},{"maximum",t.indices.empty()?Json(nullptr):Json(t.high)},{"begin",t.begin},{"end",t.end},{"unit","native parameter units"}};}
  void paint(RenderSurface &s)override{const auto [w,h]=size();s.fill(0,0,w,h,0x18222d);const auto r=plot_;s.fill(r.x,r.y,r.w,r.h,0x101923);for(int i=0;i<=4;++i)s.line(r.x,r.y+r.h*i/4,r.x+r.w,r.y+r.h*i/4,0x2a3947);
    const auto t=trace();if(!t.indices.empty()){const double span=std::max(.001,t.end-t.begin);float px=0,py=0;bool previous=false;s.clip(r.x,r.y,r.w,r.h);for(auto i:t.indices){const auto &p=points_[i];const float x=r.x+float((p.at("seconds").get<double>()-t.begin)/span)*r.w,y=r.y+r.h-float((p.at("value").get<double>()-t.low)/std::max(1e-12,t.high-t.low))*r.h;if(previous){s.line(px,py,x,py,0x6edac5,2);s.line(x,py,x,y,0x6edac5,2);}px=x;py=y;previous=true;}s.unclip();s.uiText(std::to_wstring(t.begin)+L"–"+std::to_wstring(t.end)+L" s · "+std::to_wstring(t.low)+L"–"+std::to_wstring(t.high)+L" native units",r.x+8,r.y+6,r.w-16,0x93aabd);}
    else s.uiText(L"Play the song to capture final parameter values",r.x+12,r.y+12,r.w-24,0x93aabd);s.outline(r.x,r.y,r.w,r.h,0x334757);}

public:
  ParameterActivityWindow(HWND owner,Request request,std::function<Context()> context,std::function<void(const Json &)> inspect):NativeToolWindow(owner),request_(std::move(request)),context_(std::move(context)),inspect_(std::move(inspect)){
    minimumClientWidth_=1020;minimumClientHeight_=680;create(L"ScreamSeq.ParameterActivity",L"Parameter activity",1180,790);clampToOwnerWorkArea();for(int id:{processors,parameters,mode,details})combo(id);append(mode,L"Recent seconds");append(mode,L"Latest pattern pass");choose(mode,0);for(auto s:{L"Sources",L"Recent changes",L"Recorded points"})append(details,s);choose(details,0);
    add(items,L"LISTBOX",L"Parameter activity details",LBS_NOTIFY|LBS_NOINTEGRALHEIGHT|WS_VSCROLL|WS_HSCROLL);edit(time,L"0",40);edit(value,L"0",40);
    for(auto [id,text]:std::initializer_list<std::pair<int,const wchar_t *>>{{refresh,L"Refresh"},{freeze,L"Freeze"},{clear,L"Clear capture"},{openSource,L"Open source"},{openLane,L"Song automation…"},{previous,L"‹"},{next,L"›"},{newPoint,L"New"},{savePoint,L"Save point"},{removePoint,L"Delete"},{zoomOut,L"−"},{zoomIn,L"+"},{fit,L"Fit"},{close,L"Close"}})button(id,text);
    for(auto [id,text]:std::initializer_list<std::pair<int,const wchar_t *>>{{title,L"Parameter activity"},{targetLabel,L"Choose a prepared processor copy"},{ruleLabel,L"Observe the final parameter value and the sources that control it."},{statusLabel,L""},{timeLabel,L"Time / seconds"},{valueLabel,L"Value / native units"},{pageLabel,L""}})label(id,text);finish();SetTimer(window_,3,100,nullptr);}
  void openAt(std::string plugin={},std::optional<uint32_t> parameter={}){const bool retain=visible()||fields_;show();if(!retain){beginPending();try{load(std::move(plugin),parameter);pending_=false;}catch(...){pending_=false;throw;}}SetFocus(controls_.at(processors));}
  Json snapshot()const{return {{"visible",visible()},{"document",captured_.document},{"expectedRevision",captured_.revision},{"target",target_},{"plugin",plugin_},{"parameter",parameter_?Json(*parameter_):Json()},{"engine",engine_},{"token",token_},{"cursor",cursor_},{"pointCount",points_.size()},{"sourceCount",sources_.size()},{"targets",targets_},{"parameters",parameters_},{"sources",sources_},{"recorded",recorded_},{"recordedTotal",total_},{"recordedOffset",offset_},{"selectedPoint",selected_},{"fieldDraft",fields_},{"generation",generation_},{"rawTime",utf8(field(time))},{"rawValue",utf8(field(value))},{"pending",pending_},{"frozen",frozen_},{"active",active_},{"stale",!current()},{"dropped",dropped_},{"page",page_},{"traceMode",traceMode_},{"trace",traceSnapshot()},{"seconds",seconds_},{"status",utf8(status_)}};}
};
}
