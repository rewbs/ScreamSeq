#pragma once
#include "NativeToolWindow.hpp"
#include "NativeTextSearch.hpp"
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
    previous,next,newPoint,savePoint,removePoint,time,value,zoomOut,zoomIn,fit,close,search,
    title=11100,targetLabel,ruleLabel,statusLabel,timeLabel,valueLabel,pageLabel};
  Request request_;std::function<Context()> context_;std::function<void(const Json &)> inspect_;
  Context captured_;Json targets_=Json::array(),parameters_=Json::array(),sources_=Json::array(),recorded_=Json::array();
  std::deque<Json> points_;std::vector<size_t> listed_;std::string target_,plugin_,token_,sourceRevision_;
  std::vector<std::wstring> listedText_;std::vector<uint64_t> listedSequences_;int listedPage_=-1;std::string listedToken_;
  std::optional<uint64_t> selectedTrace_;
  std::optional<uint32_t> parameter_;uint64_t cursor_=0,dropped_=0,engine_=0,originalFrame_=0;
  uint64_t generation_=0;
  size_t offset_=0,total_=0;int page_=0,traceMode_=0,selected_=-1;double seconds_=8;
  bool setting_=false,pending_=false,polling_=false,frozen_=false,fields_=false,active_=false;
  bool explicitPointAction_=false;
  std::string recordedRefreshFailure_;
  std::vector<uint32_t> parameterChoices_;
  size_t searchMatches_=0;
  bool searchPending_=false;
  std::function<void()> pendingNavigation_;
  std::function<void()> pendingHistory_;
  std::optional<uint64_t> pendingPointSelectionFrame_;
  bool pendingPointDelete_=false;
  void cancelNavigation(){pendingNavigation_={};pendingPointSelectionFrame_.reset();pendingPointDelete_=false;KillTimer(window_,4);}
  void deferRefresh(){
    const auto c=context_();const auto generation=generation_;
    cancelNavigation();
    pendingNavigation_=[this,c,generation]{
      // Refresh explicitly discards the captured fields, but never input typed
      // after that request or a different revision reached while it waited.
      guardRead(c,generation);require(visible(),"Activity refresh was closed");
      action(refresh,BN_CLICKED);
    };
    SetTimer(window_,4,100,nullptr);
  }
  void deferTargetSelection(int id){
    const auto index=selection(id);if(index<0)return;
    const auto wanted=id==processors?targets_.at(size_t(index)).at("key").get<std::string>():target_;
    const auto parameter=id==parameters?selectedParameter():parameter_;
    if(id==parameters&&!parameter)return;
    const auto c=context_();const auto generation=generation_;
    cancelNavigation();
    pendingNavigation_=[this,id,wanted,parameter,c,generation]{
      guardRead(c,generation);require(visible(),"Activity navigation was closed");
      finishPointEditForNavigation();
      beginPending();try{load(wanted,parameter,false,id==parameters);pending_=false;}catch(...){pending_=false;throw;}
    };
    SetTimer(window_,4,100,nullptr);
  }
  void finishPointEditForNavigation(){
    if(automaticEdit_)flushAutomaticEdit();
    require(!automaticEdit_&&!fields_,"Finish or Refresh the retained point fields before navigating");
  }
  void selectRecordedPoint(uint64_t frame,bool samePoint){
    if(!samePoint){const auto found=std::find_if(recorded_.begin(),recorded_.end(),[&](const auto &p){return p.at("frame")==frame;});
      require(found!=recorded_.end(),"The chosen point is no longer on this page; choose it again");selected_=int(found-recorded_.begin());}
    showFields();list();selectedTrace_.reset();requestPaint();
  }
  void deferPointSelection(){
    const auto i=int(SendMessageW(controls_.at(items),LB_GETCURSEL,0,0));
    if(i<0||size_t(i)>=recorded_.size())return;
    const auto frame=recorded_[size_t(i)].at("frame").get<uint64_t>();const bool samePoint=i==selected_;
    const auto c=context_();const auto generation=generation_;
    cancelNavigation();pendingPointSelectionFrame_=frame;
    pendingNavigation_=[this,frame,samePoint,c,generation]{
      const bool remove=std::exchange(pendingPointDelete_,false);pendingPointSelectionFrame_.reset();
      guardRead(c,generation);require(visible()&&page_==2,"Recorded point navigation was closed");
      finishPointEditForNavigation();selectRecordedPoint(frame,samePoint);
      if(remove)action(removePoint,BN_CLICKED);
    };
    SetTimer(window_,4,100,nullptr);
  }
  std::optional<Tracker::DocumentDraft> documentDraft()const override {
    return describeDraft(captured_.document,captured_.revision,Json::array({target_,plugin_,parameter_?Json(*parameter_):Json(),originalFrame_}).dump(),
      generation_,fields_,pending_||bool(automaticEdit_)||pendingPointDelete_);
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
  std::optional<uint32_t> selectedParameter()const {
    const auto row=selection(parameters);
    if(row<0||size_t(row)>=parameterChoices_.size())return {};
    return parameterChoices_[size_t(row)];
  }
  void filterParameters(){
    const auto query=field(search);std::vector<uint32_t> ids;std::vector<std::wstring> labels;
    int selected=-1;searchMatches_=0;
    for(const auto &p:parameters_){
      const auto id=p.at("id").get<uint32_t>();const bool current=parameter_&&id==*parameter_;
      auto label=wide(p.at("name"))+L" · "+wide(p.value("unitLabel",std::string()))+L" · "+std::to_wstring(id);
      const bool match=nativeTextSearchMatches(label,query);if(match)++searchMatches_;
      if(!match&&!current)continue;
      if(!match)label+=L" [observed; outside search]";
      if(current)selected=int(ids.size());ids.push_back(id);labels.push_back(std::move(label));
    }
    // The query only changes presentation. Keep the observed target visible
    // even when excluded, and never start a watch or save a retained draft.
    const auto combo=controls_.at(parameters);const bool wasSetting=setting_;setting_=true;
    struct Restore{bool &value;bool old;~Restore(){value=old;}}restore{setting_,wasSetting};
    SendMessageW(combo,WM_SETREDRAW,FALSE,0);
    ScreamSeq::NativeInputGate::present(combo,CB_RESETCONTENT,0,0);
    for(const auto &label:labels)append(parameters,label);
    if(labels.empty())append(parameters,parameters_.empty()?L"No prepared parameters":L"No matching parameters");
    parameterChoices_=std::move(ids);choose(parameters,selected);
    SendMessageW(combo,WM_SETREDRAW,TRUE,0);InvalidateRect(combo,nullptr,FALSE);
    searchPending_=false;KillTimer(window_,5);
  }
  void choices(){setting_=true;ScreamSeq::NativeInputGate::present(controls_.at(processors),CB_RESETCONTENT,0,0);int index=-1;for(size_t i=0;i<targets_.size();++i){const auto &t=targets_[i];append(processors,wide(t.at("name"))+L" · "+wide(t.at("key")));if(t.at("key")==target_)index=int(i);}choose(processors,index);
    filterParameters();setting_=false;}
  void showFields(){setting_=true;if(selected_>=0&&size_t(selected_)<recorded_.size()){const auto &p=recorded_[size_t(selected_)];originalFrame_=p.at("frame");set(time,double(originalFrame_)/48000);set(value,p.at("value"));}else{originalFrame_=0;set(time,L"0");set(value,L"0");}fields_=false;set(savePoint,selected_>=0?L"Save now":L"Add point");setting_=false;}
  void list(){const auto list=controls_.at(items);const auto old=int(SendMessageW(list,LB_GETCURSEL,0,0)),oldTop=int(SendMessageW(list,LB_GETTOPINDEX,0,0));
    const auto previousSequence=[&](int row)->std::optional<uint64_t>{if(page_==1&&listedPage_==1&&listedToken_==token_&&row>=0&&size_t(row)<listedSequences_.size())return listedSequences_[size_t(row)];return {};};
    const auto selectedSequence=previousSequence(old),topSequence=previousSequence(oldTop);
    std::vector<std::wstring> text;std::vector<uint64_t> sequences;listed_.clear();
    const auto add=[&](size_t index,const std::wstring &row){listed_.push_back(index);text.push_back(row);if(page_==1)sequences.push_back(points_[index].at("sequence").get<uint64_t>());};
    if(page_==0)for(size_t i=0;i<sources_.size();++i)add(i,wide(sources_[i].value("title",std::string()))+(sources_[i].value("enabled",true)?L"":L" · disabled"));
    else if(page_==1){for(size_t i=points_.size()>512?points_.size()-512:0;i<points_.size();++i){const auto &p=points_[i];add(i,std::to_wstring(p.at("seconds").get<double>())+L" s · "+wide(p.at("source").at("kind"))+L" · "+wide(p.at("value").dump())+(p.at("source").at("kind")=="graph-source"?L" normalized contribution":L" native units"));}}
    else for(size_t i=0;i<recorded_.size();++i){const auto &p=recorded_[i];add(i,std::to_wstring(p.at("frame").get<double>()/48000)+L" s · "+wide(p.at("value").dump()));}
    const auto rowFor=[&](std::optional<uint64_t> sequence){if(sequence){const auto found=std::find(sequences.begin(),sequences.end(),*sequence);if(found!=sequences.end())return int(found-sequences.begin());}return -1;};
    const auto selected=page_==2?selected_:page_==1?rowFor(selectedSequence):listedPage_==page_?old:-1;
    const auto top=page_==1?std::max(0,rowFor(topSequence)):listedPage_==page_?oldTop:0;
    // Repeated observation with no new rows needs no list reset. When rows do
    // change, retain event identity and scroll; never reinterpret a row number
    // as a different source after the bounded recent-event page rolls forward.
    const bool changed=text!=listedText_||page_!=listedPage_;
    if(changed){SendMessageW(list,WM_SETREDRAW,FALSE,0);ScreamSeq::NativeInputGate::present(list,LB_RESETCONTENT,0,0);
      for(const auto &row:text)ScreamSeq::NativeInputGate::present(list,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(row.c_str()));}
    if(changed||old!=selected)ScreamSeq::NativeInputGate::present(list,LB_SETCURSEL,selected,0);
    if(changed){SendMessageW(list,LB_SETTOPINDEX,top,0);SendMessageW(list,WM_SETREDRAW,TRUE,0);InvalidateRect(list,nullptr,FALSE);}
    listedText_=std::move(text);listedSequences_=std::move(sequences);listedPage_=page_;listedToken_=token_;
    set(pageLabel,page_==2?std::to_wstring(offset_)+L"–"+std::to_wstring(offset_+recorded_.size())+L" of "+std::to_wstring(total_)+L" recorded points":std::to_wstring(listed_.size())+L" entries");}
  struct RecordedRead {Json points=Json::array();size_t offset=0,total=0;int selected=-1;};
  struct SourcesRead {Json sources=Json::array();std::wstring rule;};
  void guardRead(const Context &c,std::optional<uint64_t> generation)const {
    const auto now=context_();require(c.document==now.document&&c.revision==now.revision,"Song changed while reading / captured fields retained; Refresh");
    require(!generation||*generation==generation_,"Newer point fields retained; Refresh again when ready");
  }
  RecordedRead readRecorded(const std::string &plugin,std::optional<uint32_t> parameter,size_t offset,const Context &c,uint64_t generation) {
    RecordedRead result; if(plugin.empty()||!parameter)return result;
    auto r=request_("automation.recorded.get",{{"plugin",plugin},{"parameter",*parameter},{"offset",offset},{"limit",512}});guardRead(c,generation);
    require(r.at("sampleRate")==48000,"Unsupported recorded time base");result.total=r.at("total").get<size_t>();result.offset=r.at("offset").get<size_t>();result.points=r.at("points");
    require(result.points.is_array()&&result.points.size()<=512&&result.offset<=result.total&&result.points.size()<=result.total-result.offset,"Incomplete recorded point page");
    for(const auto &p:result.points){(void)p.at("frame").get<uint64_t>();require(std::isfinite(p.at("value").get<double>()),"Invalid recorded value");}
    result.selected=result.points.empty()?-1:0;return result;
  }
  SourcesRead readSources(const std::string &target,std::optional<uint32_t> parameter,const Context &c,std::optional<uint64_t> generation) {
    SourcesRead result;if(!parameter)return result;
    auto r=request_("parameter.activity.sources",{{"target",target},{"parameter",*parameter}});guardRead(c,generation);result.sources=r.at("sources");result.rule=wide(r.at("rule").get<std::string>());
    require(result.sources.is_array(),"Incomplete parameter sources");for(const auto &s:result.sources){(void)s.value("title",std::string());(void)s.value("enabled",true);}return result;
  }
  std::string readWatch(const std::string &target,uint32_t parameter,bool clearCapture,const Context &c,uint64_t generation) {
    auto r=request_("parameter.activity.watch",{{"target",target},{"parameter",parameter},{"clear",clearCapture}});guardRead(c,generation);
    require(r.at("target").at("key")==target&&r.at("parameter")==parameter,"Prepared processor changed while starting observation / Refresh");return r.at("token").get<std::string>();
  }
  void adoptRecorded(RecordedRead r) {recorded_=std::move(r.points);offset_=r.offset;total_=r.total;selected_=r.selected;showFields();}
  bool cleanRecordedPage()const {
    const auto focus=GetFocus();
    return page_==2&&!fields_&&!automaticEdit_&&!pendingNavigation_&&!pendingHistory_&&
      focus!=controls_.at(time)&&focus!=controls_.at(value);
  }
  void refreshCleanRecordedPage(const Context &c){
    if(c.revision==captured_.revision||c.revision==recordedRefreshFailure_||!cleanRecordedPage())return;
    const auto generation=generation_;const auto selected=selected_;
    const auto frame=selected>=0?std::optional<uint64_t>(originalFrame_):std::nullopt;
    try{
      auto lane=readRecorded(plugin_,parameter_,offset_,c,generation);
      // Reads pump messages. A new field or navigation owns its original
      // revision; do not rebase it onto the newly observed history state.
      if(!visible()||!cleanRecordedPage())return;
      std::optional<SourcesRead> sourceData;
      if(sourceRevision_!=c.revision)sourceData=readSources(target_,parameter_,c,generation);
      if(!visible()||!cleanRecordedPage())return;
      guardRead(c,generation);
      const auto found=std::find_if(lane.points.begin(),lane.points.end(),[&](const auto &point){return frame&&point.at("frame")==*frame;});
      lane.selected=found==lane.points.end()?-1:int(found-lane.points.begin());
      captured_=c;adoptRecorded(std::move(lane));list();++generation_;recordedRefreshFailure_.clear();
      if(sourceData){sources_=std::move(sourceData->sources);sourceRevision_=c.revision;set(ruleLabel,sourceData->rule);}
      status(L"Recorded points updated from song history · Shared Undo/Redo");
    }catch(const std::exception &e){
      const auto now=context_();
      if(now.document!=c.document||now.revision!=c.revision)throw;
      if(generation!=generation_||!cleanRecordedPage())return;
      // A failed lane read retains its old fields without stopping observation
      // or hammering the worker every timer tick. Explicit Refresh retries it.
      recordedRefreshFailure_=c.revision;error(e);
    }
  }
  void recorded(size_t offset) {const auto c=context_();const auto generation=generation_;auto r=readRecorded(plugin_,parameter_,offset,c,generation);guardRead(c,generation);captured_=c;adoptRecorded(std::move(r));}
  void recorded(){recorded(offset_);}
  void sources(){const auto c=context_();const auto generation=generation_;auto r=readSources(target_,parameter_,c,generation);guardRead(c,generation);sources_=std::move(r.sources);sourceRevision_=c.revision;set(ruleLabel,r.rule);}
  void watch(bool clearCapture){require(parameter_.has_value(),"Choose a prepared processor and parameter");const auto c=context_();auto token=readWatch(target_,*parameter_,clearCapture,c,generation_);token_=std::move(token);cursor_=dropped_=0;points_.clear();selectedTrace_.reset();}
  void load(std::string wanted={},std::optional<uint32_t> parameter={},bool explicitRefresh=false,bool strictTarget=false){
    require(!fields_||explicitRefresh,"Save or Refresh the recorded-point draft before changing target");const auto c=context_();const auto generation=generation_;require(captured_.document.empty()||captured_.document==c.document||explicitRefresh,"Captured song is unavailable / Refresh");
    auto r=request_("parameter.activity.targets",Json::object());guardRead(c,generation);auto targets=r.at("targets");require(targets.is_array(),"Incomplete prepared processor list");const auto engine=r.at("engine").get<uint64_t>();const auto active=r.at("active").get<bool>();
    for(const auto &t:targets){(void)t.at("key").get<std::string>();(void)t.at("name").get<std::string>();(void)t.at("plugin").get<std::string>();}
    const auto desired=wanted.empty()?target_:wanted;auto found=std::find_if(targets.begin(),targets.end(),[&](const auto &t){return t.at("key")==desired||(!wanted.empty()&&t.at("plugin")==wanted);});
    if(found==targets.end()&&!desired.empty()&&!targets.empty())throw std::runtime_error("Captured processor copy is unavailable / reopen after preparing playback");
    if(strictTarget)require(!wanted.empty()&&parameter&&found!=targets.end(),"This processor is not prepared / start playback, then inspect activity again");
    if(found==targets.end()&&desired.empty())found=targets.begin();Json catalog=Json::array();std::string target,plugin,token;
    if(found!=targets.end()) {target=found->at("key");plugin=found->at("plugin");catalog=request_("parameter.activity.parameters",{{"target",target}}).at("parameters");guardRead(c,generation);require(catalog.is_array(),"Incomplete parameter catalogue");}
    if(!parameter)parameter=parameter_;std::optional<uint32_t> selected;for(const auto &p:catalog){const auto id=p.at("id").get<uint32_t>();(void)p.at("name").get<std::string>();(void)p.value("unitLabel",std::string());if(parameter&&id==*parameter)selected=id;}if(strictTarget)require(selected.has_value(),"The requested parameter is unavailable in this prepared processor");if(!selected&&!catalog.empty())selected=catalog.front().at("id").get<uint32_t>();
    auto sourceData=readSources(target,selected,c,generation);auto lane=readRecorded(plugin,selected,0,c,generation);
    // Starting the transient watch is last: all document reads are staged first.
    if(selected)token=readWatch(target,*selected,false,c,generation);guardRead(c,generation);
    targets_=std::move(targets);parameters_=std::move(catalog);target_=std::move(target);plugin_=std::move(plugin);parameter_=selected;engine_=engine;active_=active;captured_=c;
    token_=std::move(token);cursor_=dropped_=0;points_.clear();selectedTrace_.reset();sources_=std::move(sourceData.sources);sourceRevision_=c.revision;adoptRecorded(std::move(lane));++generation_;
    recordedRefreshFailure_.clear();choices();list();set(ruleLabel,sourceData.rule);set(targetLabel,target_.empty()?L"Choose a prepared processor copy":wide(target_));
    status(parameter_?L"Live observation · click trace to inspect · F6 focuses trace · Enter opens source":L"Start playback to prepare processor copies, then Refresh");
  }
  void poll(){if(!visible()||pending_||!parameter_||context_().busy)return;beginPending();polling_=true;
    struct Reset{bool &value;~Reset(){value=false;}}reset{polling_};try{
    const auto c=context_();require(c.document==captured_.document,"Song changed / captured observer retained; Refresh");
    // Freezing the trace does not freeze a clean recorded list after Undo/Redo.
    if(frozen_){refreshCleanRecordedPage(c);pending_=false;return;}
    // Typing during a pumped observation read must not freeze the trace. Only
    // a clean recorded page may adopt a changed revision, under its own guard.
    const auto r=request_("parameter.activity.get",{{"after",cursor_},{"limit",2048}});guardRead(c,std::nullopt);
    // A click can freeze the currently displayed trace while this read pumps
    // native messages. Keep that exact view/cursor; Resume can consume later data.
    if(frozen_){pending_=false;return;}
    require(r.at("token")==token_&&!r.at("target").is_null()&&r.at("target").at("key")==target_&&r.at("parameter")==*parameter_,"Prepared processor or API watch changed / Refresh to observe again");
    const auto cursor=r.at("cursor").get<uint64_t>(),dropped=r.at("dropped").get<uint64_t>();const auto active=r.at("active").get<bool>();auto next=points_;
    require(r.at("points").is_array()&&r.at("points").size()<=2048,"Incomplete parameter activity page");
    for(const auto &p:r.at("points")){for(const auto *key:{"seconds","value","minimum","maximum"})require(std::isfinite(p.at(key).get<double>()),"Invalid activity value");(void)p.at("sequence").get<uint64_t>();(void)p.at("pattern");(void)p.at("order");(void)p.at("position");(void)p.at("source").at("kind").get<std::string>();next.push_back(p);}
    while(next.size()>8192)next.pop_front();std::optional<SourcesRead> sourceData;if(sourceRevision_!=c.revision)sourceData=readSources(target_,parameter_,c,std::nullopt);guardRead(c,std::nullopt);
    if(frozen_){pending_=false;return;}
    points_=std::move(next);cursor_=cursor;dropped_=dropped;active_=active;if(sourceData){sources_=std::move(sourceData->sources);sourceRevision_=c.revision;set(ruleLabel,sourceData->rule);}if(page_==1)list();refreshCleanRecordedPage(c);requestPaint();pending_=false;
  }catch(const std::exception &e){pending_=false;frozen_=true;set(freeze,L"Resume");error(e);}}
  void commit(bool remove){
    require(parameter_&&!plugin_.empty(),"Recorded points require a rack plugin parameter");require(current(),"Song changed / point fields retained; Refresh before applying");const auto before=captured_;const auto generation=generation_;
    const double t=number(time);require(t>=0&&t<=604800,"Time must be between 0 and 604800 seconds");const auto frame=uint64_t(std::llround(t*48000));Json p={{"expectedRevision",captured_.revision},{"plugin",plugin_},{"parameter",*parameter_},{"frame",selected_>=0?originalFrame_:frame}};
    if(remove){require(selected_>=0,"Select a recorded point to delete");p["remove"]=true;}else{p["value"]=number(value);if(selected_>=0&&originalFrame_!=frame)p["newFrame"]=frame;}
    request_("automation.recorded.edit",p);const auto after=context_();require(after.document==before.document,"Document changed during edit / captured fields retained");
    if(generation!=generation_){status(L"Point saved; newer raw fields retained. Refresh before another edit.");return;}
    auto lane=readRecorded(plugin_,parameter_,offset_,after,generation);auto sourceData=readSources(target_,parameter_,after,generation);guardRead(after,generation);
    // Row positions change when an edit moves a point through the time-sorted
    // page. Keep the accepted point, never silently redirect the next edit to
    // the first row. Deleted points and moves outside this page leave no selection.
    const auto selected=std::find_if(lane.points.begin(),lane.points.end(),[&](const auto &point){return !remove&&point.at("frame")==frame;});
    lane.selected=selected==lane.points.end()?-1:int(selected-lane.points.begin());
    captured_=after;adoptRecorded(std::move(lane));sources_=std::move(sourceData.sources);sourceRevision_=after.revision;set(ruleLabel,sourceData.rule);list();++generation_;status(L"Recorded point saved · shared Undo restores the previous lane");
  }
  void deferPointAction(int id,uint64_t generation){
    explicitPointAction_=true;
    queueAutomaticEdit([this,id,generation]{
      if(generation!=generation_)explicitPointAction_=false;
      require(generation==generation_,"Point fields changed while waiting; review and save the current fields");
      if(pending_||context_().busy){deferPointAction(id,generation);return;}
      explicitPointAction_=false;
      action(id,BN_CLICKED);
    },20);
  }
  void queuePointEdit(uint64_t generation){
    if(explicitPointAction_)return;
    queueAutomaticEdit([this,generation]{
      require(generation==generation_,"Point selection or fields changed; review the retained fields");
      // Navigation owns the ordering when its selection arrived inside a read.
      // Its drain removes that intent before flushing this original edit. This
      // avoids a debounce timer invalidating the navigation's revision guard.
      if(pending_||context_().busy||pendingNavigation_){queuePointEdit(generation);return;}
      if(fields_)action(savePoint,BN_CLICKED);
    });
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
  Json selectedSource(bool lane)const {
    require(current(),"Song changed / Refresh before navigating captured sources");
    if(lane){require(parameter_&&!plugin_.empty(),"Choose a rack plugin parameter");return {{"kind","recorded"},{"plugin",plugin_},{"parameter",*parameter_}};}
    if(selectedTrace_){const auto *point=traceSelection();require(point!=nullptr,"The selected trace event has expired; select a current source");return sourceLink(point->at("source"),true);}
    const auto i=int(SendMessageW(controls_.at(items),LB_GETCURSEL,0,0));
    require(i>=0&&size_t(i)<listed_.size(),"Select a source, recent change or trace point");
    if(page_==2){require(parameter_&&!plugin_.empty(),"Choose a rack plugin parameter");return {{"kind","recorded"},{"plugin",plugin_},{"parameter",*parameter_}};}
    return page_==0?sourceLink(sources_.at(listed_[size_t(i)]),false):sourceLink(points_.at(listed_[size_t(i)]).at("source"),true);
  }
  void deferSourceNavigation(bool lane){
    const auto source=selectedSource(lane);const auto c=context_();const auto generation=generation_;
    cancelNavigation();
    pendingNavigation_=[this,source,c,generation]{
      guardRead(c,generation);require(visible(),"Activity source navigation was closed");
      finishPointEditForNavigation();
      beginPending();try{inspect_(source);pending_=false;}catch(...){pending_=false;throw;}
    };
    SetTimer(window_,4,100,nullptr);
  }
  void action(int id,unsigned notification)override{
    if(setting_)return;if(id==close){hide();return;}
    // A list focus notification is not a new navigation intent. Cancelling a
    // queued selection here would let Delete target the old selected point.
    if(id==items&&(notification==LBN_SETFOCUS||notification==LBN_KILLFOCUS))return;
    if(id==refresh&&notification==BN_CLICKED&&(polling_||(!pending_&&context_().busy))){deferRefresh();return;}
    if(id==removePoint&&notification==BN_CLICKED&&pendingNavigation_){
      require(pendingPointSelectionFrame_.has_value(),"Finish the requested Activity navigation before deleting");
      // The list has already reported the musician's new stable timestamp.
      // Keep one Delete intent behind that selection; never apply it to the
      // previous row or replay a duplicate key after the point disappears.
      pendingPointDelete_=true;return;
    }
    if(id==search&&notification==EN_CHANGE){
      cancelNavigation();
      if(pending_){searchPending_=true;SetTimer(window_,5,20,nullptr);}else filterParameters();
      return;
    }
    if((id==time||id==value)&&notification==EN_CHANGE){fields_=true;++generation_;if(page_==2&&selected_>=0)queuePointEdit(generation_);return;}if(pending_){
      // Polling pumps native messages. Retain one explicit edit instead of
      // silently losing an enabled Save/Delete click during that read. Never
      // queue/replay a second mutation while another mutation is in flight.
      if(polling_&&notification==BN_CLICKED&&(id==savePoint||id==removePoint)&&!explicitPointAction_){cancelAutomaticEdit();deferPointAction(id,generation_);}
      // A live observer must not swallow a processor/parameter selection just
      // because its read pumps messages. Capture stable identities, then load
      // once the owner is idle; never reinterpret a later catalogue row index.
      if(polling_&&notification==CBN_SELCHANGE&&(id==processors||id==parameters))deferTargetSelection(id);
      if(polling_&&notification==LBN_SELCHANGE&&id==items&&page_==2)deferPointSelection();
      // Source buttons stay usable during observation. Capture the actual
      // source now; a later trace/list update must not reinterpret the click.
      if(polling_&&notification==BN_CLICKED&&(id==openSource||id==openLane))deferSourceNavigation(id==openLane);
      if(notification==CBN_SELCHANGE||notification==LBN_SELCHANGE)restorePendingSelection(id);return;}
    cancelNavigation();
    if(notification==BN_CLICKED&&(id==refresh||id==savePoint||id==removePoint)){cancelAutomaticEdit();explicitPointAction_=false;}
    if(id==freeze&&notification==BN_CLICKED){frozen_=!frozen_;set(freeze,frozen_?L"Resume":L"Freeze");return;}
    try{
      // Finish an already requested edit before idle navigation. Capture stable
      // destinations first: saving a time edit can reorder the point list, and
      // saving refreshes the parameter selector. Never turn an invalid/new or
      // previously uncertain draft into an implicit retry.
      std::string wantedTarget;std::optional<uint32_t> wantedParameter;
      std::optional<uint64_t> wantedFrame;bool samePoint=false;
      const bool processorChoice=id==processors&&notification==CBN_SELCHANGE;
      const bool parameterChoice=id==parameters&&notification==CBN_SELCHANGE;
      const bool pointChoice=id==items&&notification==LBN_SELCHANGE&&page_==2;
      const bool sourceNavigation=notification==BN_CLICKED&&(id==openSource||id==openLane);
      const auto wantedSource=sourceNavigation?selectedSource(id==openLane):Json();
      if(processorChoice){const auto i=selection(processors);require(i>=0,"Choose a processor");wantedTarget=targets_.at(size_t(i)).at("key");}
      if(parameterChoice){wantedParameter=selectedParameter();require(wantedParameter.has_value(),"Choose a parameter");}
      if(pointChoice){const auto i=int(SendMessageW(controls_.at(items),LB_GETCURSEL,0,0));
        require(i>=0&&size_t(i)<recorded_.size(),"Choose a recorded point");
        wantedFrame=recorded_[size_t(i)].at("frame").get<uint64_t>();samePoint=i==selected_;}
      const bool pointNavigation=processorChoice||parameterChoice||pointChoice||
        (notification==BN_CLICKED&&(id==previous||id==next||id==newPoint));
      if(pointNavigation||sourceNavigation)finishPointEditForNavigation();
      beginPending();
      if(processorChoice)load(wantedTarget);
      else if(parameterChoice)load(target_,wantedParameter,false,true);
      else if(id==mode&&notification==CBN_SELCHANGE){traceMode_=std::max(0,selection(mode));requestPaint();}
      else if(id==details&&notification==CBN_SELCHANGE){page_=std::max(0,selection(details));selectedTrace_.reset();if(page_==2&&!fields_)recorded();list();layout();if(page_==2)status(L"Edits save automatically · Backspace/Delete: selected list point · New, then Add point · Shared Undo");}
      else if(id==items&&notification==LBN_SELCHANGE){if(pointChoice)selectRecordedPoint(*wantedFrame,samePoint);else{selectedTrace_.reset();requestPaint();}}
      else if(notification==BN_CLICKED){
        if(id==refresh){load({}, {},true);frozen_=false;set(freeze,L"Freeze");}
        else if(id==clear){watch(true);list();requestPaint();}
        else if(id==openLane||id==openSource)inspect_(wantedSource);
        else if(id==previous||id==next){require(!fields_,"Save or Refresh before changing recorded pages");const auto wanted=id==previous?(offset_>=512?offset_-512:0):std::min(offset_+512,total_?((total_-1)/512)*512:0);recorded(wanted);list();}
        else if(id==newPoint){require(!fields_,"Save or Refresh the current point first");selected_=-1;showFields();list();SetFocus(controls_.at(time));}
        else if(id==savePoint||id==removePoint)commit(id==removePoint);
        else if(id==fit||id==zoomIn||id==zoomOut){seconds_=id==fit?std::max(1.,points_.empty()?8.:points_.back().at("seconds").get<double>()-points_.front().at("seconds").get<double>()):std::clamp(seconds_*(id==zoomIn?.5:2),.05,604800.);requestPaint();}
      }
      pending_=false;
    }catch(...){pending_=false;choices();if(page_==2)ScreamSeq::NativeInputGate::present(controls_.at(items),LB_SETCURSEL,selected_,0);throw;}
  }
  void timer(UINT_PTR id)override{
    if(id==6){if(pending_||context_().busy)return;KillTimer(window_,6);auto history=std::exchange(pendingHistory_,{});if(history)history();return;}
    if(id==5){if(pending_)return;if(searchPending_)filterParameters();else KillTimer(window_,5);return;}
    if(id==4){if(pending_||context_().busy)return;KillTimer(window_,4);auto navigate=std::exchange(pendingNavigation_,{});if(navigate)navigate();return;}
    if(id==3)poll();
  }
  bool key(WPARAM key,bool ctrl,bool shift)override{
    if(ctrl&&key=='F'){SetFocus(controls_.at(search));SendMessageW(controls_.at(search),EM_SETSEL,0,-1);return true;}
    if(!ctrl&&!shift&&(key==VK_BACK||key==VK_DELETE)&&page_==2&&GetFocus()==controls_.at(items)){
      if(selected_>=0||pendingPointSelectionFrame_)action(removePoint,BN_CLICKED);return true;
    }
    if(key==VK_ESCAPE){hide();return true;}
    if(ctrl&&key=='R'){action(refresh,BN_CLICKED);return true;}
    if(key==VK_F6){SetFocus(GetFocus()==window_?controls_.at(items):window_);requestPaint();return true;}
    if(key==VK_TAB&&!ctrl&&GetFocus()==window_){SetFocus(controls_.at(shift?mode:details));return true;}
    if(key==VK_RETURN&&(GetFocus()==controls_.at(time)||GetFocus()==controls_.at(value))){action(savePoint,BN_CLICKED);return true;}
    if(GetFocus()==window_&&!ctrl&&!pending_){if(key==VK_RETURN&&selectedTrace_){action(openSource,BN_CLICKED);return true;}if(key==VK_LEFT||key==VK_RIGHT||key==VK_HOME||key==VK_END){
      const auto t=trace();if(t.indices.empty())return true;size_t at=key==VK_LEFT||key==VK_END?t.indices.size()-1:0;
      if(key==VK_LEFT||key==VK_RIGHT)for(size_t i=0;i<t.indices.size();++i)if(selectedTrace_&&points_[t.indices[i]].at("sequence")==*selectedTrace_){at=key==VK_LEFT?(i?i-1:0):std::min(i+1,t.indices.size()-1);break;}
      selectTrace(t.indices[at]);return true;}}
    return false;}
  void layout()override{const auto [w,h]=size();place(title,18,12,w-36,25);place(processors,18,46,w*.55-22,280);place(search,w*.55+4,46,w*.45-22,28);place(parameters,w*.55+4,80,w*.45-22,280);place(targetLabel,18,80,w*.55-22,24);
    float x=18;for(auto [id,width]:std::initializer_list<std::pair<int,int>>{{refresh,92},{freeze,84},{clear,112},{zoomOut,38},{fit,46},{zoomIn,38}}){place(id,x,112,width,28);x+=width+8;}place(mode,x,112,200,200);plot_={18,156,w-36,std::max(110.f,h-492)};
    const float y=plot_.y+plot_.h+14;place(details,18,y,200,160);place(openSource,226,y,148,28);place(openLane,382,y,172,28);place(pageLabel,566,y+4,w-584,24);place(items,18,y+38,w-36,102);place(ruleLabel,18,y+146,w-36,36);
    const bool recorded=page_==2;place(timeLabel,18,h-131,156,22,recorded);place(valueLabel,188,h-131,156,22,recorded);place(time,18,h-109,156,28,recorded);place(value,188,h-109,156,28,recorded);x=358;for(auto [id,width]:std::initializer_list<std::pair<int,int>>{{newPoint,78},{savePoint,106},{removePoint,94},{previous,56},{next,56}}){place(id,x,h-109,width,28,recorded);x+=width+8;}place(close,w-112,h-65,94,28);place(statusLabel,18,h-30,w-36,24);}
  struct Trace {std::vector<size_t> indices;size_t contributions=0;double begin=0,end=0,low=0,high=0;};
  const Json *traceSelection()const {if(selectedTrace_)for(const auto &point:points_)if(point.at("sequence")==*selectedTrace_)return &point;return nullptr;}
  void selectTrace(size_t index){
    require(!fields_,"Save or Refresh the recorded-point draft before inspecting the trace");
    const auto &point=points_.at(index);selectedTrace_=point.at("sequence").get<uint64_t>();frozen_=true;set(freeze,L"Resume");
    status(L"Frozen · "+std::to_wstring(point.at("seconds").get<double>())+L" s · "+wide(point.at("value").dump())+L" · "+wide(point.at("source").at("kind"))+L" · Enter: source · Left/Right: neighbours");requestPaint();
  }
  void mouse(UINT message,float x,float y,WPARAM)override{
    if(message!=WM_LBUTTONDOWN||(pending_&&!polling_)||!plot_.contains(x,y))return;
    const auto t=trace();if(t.indices.empty())return;const auto seconds=t.begin+std::clamp((x-plot_.x-2)/std::max(1.f,plot_.w-4),0.f,1.f)*(t.end-t.begin);
    const auto nearest=std::min_element(t.indices.begin(),t.indices.end(),[&](size_t a,size_t b){return std::abs(points_[a].at("seconds").get<double>()-seconds)<std::abs(points_[b].at("seconds").get<double>()-seconds);});
    selectTrace(*nearest);SetFocus(window_);
  }
  Trace trace()const {
    Trace t;for(size_t i=0;i<points_.size();++i){if(points_[i].at("source").at("kind")=="graph-source"){++t.contributions;continue;}t.indices.push_back(i);}if(t.indices.empty())return t;
    if(traceMode_==1){size_t first=t.indices.size()-1;const auto &last=points_[t.indices.back()];while(first){const auto &previous=points_[t.indices[first-1]],&next=points_[t.indices[first]];if(previous.at("pattern")!=last.at("pattern")||previous.at("order")!=last.at("order")||previous.at("position")>next.at("position"))break;--first;}t.indices.erase(t.indices.begin(),t.indices.begin()+first);}
    t.end=points_[t.indices.back()].at("seconds").get<double>();t.begin=traceMode_==1?points_[t.indices.front()].at("seconds").get<double>():std::max(0.,t.end-seconds_);
    std::erase_if(t.indices,[&](size_t i){return points_[i].at("seconds").get<double>()<t.begin;});if(t.indices.empty())return t;
    t.low=points_[t.indices.front()].at("minimum").get<double>();t.high=points_[t.indices.front()].at("maximum").get<double>();for(auto i:t.indices){const auto &p=points_[i];t.low=std::min({t.low,p.at("minimum").get<double>(),p.at("value").get<double>()});t.high=std::max({t.high,p.at("maximum").get<double>(),p.at("value").get<double>()});}return t;
  }
  struct TracePoint {float x,y;};
  TracePoint traceLocation(const Trace &t,double seconds,double value)const {
    const auto r=plot_;const auto span=std::max(.001,t.end-t.begin);
    // Keep both extrema inside the stroke clip. A constant parameter belongs
    // at the centre, not on an invisible bottom border (zero observed range).
    const auto fraction=t.high>t.low?(value-t.low)/(t.high-t.low):.5;
    return {r.x+2+float(std::clamp((seconds-t.begin)/span,0.,1.))*std::max(0.f,r.w-4),
      r.y+2+float(1-std::clamp(fraction,0.,1.))*std::max(0.f,r.h-4)};
  }
  Json traceSnapshot()const {const auto t=trace();Json last=nullptr;if(!t.indices.empty()){const auto &p=points_[t.indices.back()];const auto at=traceLocation(t,p.at("seconds").get<double>(),p.at("value").get<double>());last=Json::array({at.x,at.y});}return {{"nativePoints",t.indices.size()},{"normalizedContributions",t.contributions},{"segments",t.indices.empty()?0:t.indices.size()-1},{"minimum",t.indices.empty()?Json(nullptr):Json(t.low)},{"maximum",t.indices.empty()?Json(nullptr):Json(t.high)},{"begin",t.begin},{"end",t.end},{"plot",Json::array({plot_.x,plot_.y,plot_.w,plot_.h})},{"lastPoint",last},{"coordinateSpace","ownerClientDIP"},{"selectedSequence",selectedTrace_?Json(*selectedTrace_):Json()},{"unit","native parameter units"}};}
  void paint(RenderSurface &s)override{
    NativeToolWindow::paint(s);const auto colors=NativeControls::curveColors();const auto r=plot_;
    s.fill(r.x,r.y,r.w,r.h,colors.background);
    for(int i=0;i<=4;++i)s.line(r.x,r.y+r.h*i/4,r.x+r.w,r.y+r.h*i/4,colors.grid);
    const auto t=trace();
    if(!t.indices.empty()){
      float px=0,py=0;bool previous=false;s.clip(r.x,r.y,r.w,r.h);
      for(auto i:t.indices){const auto &p=points_[i];const auto at=traceLocation(t,p.at("seconds").get<double>(),p.at("value").get<double>());
        const float x=at.x,y=at.y;
        if(previous){s.line(px,py,x,py,colors.curve,2);s.line(x,py,x,y,colors.curve,2);}
        else s.fill(x-2,y-2,4,4,colors.curve);
        px=x;py=y;previous=true;
      }
      for(auto index:t.indices)if(selectedTrace_&&points_[index].at("sequence")==*selectedTrace_){
        const auto &p=points_[index];const auto at=traceLocation(t,p.at("seconds").get<double>(),p.at("value").get<double>());
        paintCurveHandle(s,at.x,at.y,true,colors,colors.curve);break;
      }
      s.unclip();
      s.uiText(std::to_wstring(t.begin)+L"–"+std::to_wstring(t.end)+L" s · "+std::to_wstring(t.low)+L"–"+std::to_wstring(t.high)+L" native units",r.x+8,r.y+6,r.w-16,colors.text);
    }else s.uiText(L"Play the song to capture final parameter values",r.x+12,r.y+12,r.w-24,colors.text);
    paintCurveBorder(s,r,GetFocus()==window_,colors);
  }

public:
  bool recordedPointListFocused()const{return page_==2&&GetFocus()==controls_.at(items);}
  bool deferHistoryIfObserving(std::function<void()> perform){
    if(recordedPointListFocused())require(!pendingHistory_,"Wait for the queued Activity history command");
    if(!pending_||!polling_||!recordedPointListFocused())return false;
    require(!pendingHistory_&&!pendingNavigation_,"Wait for the queued Activity action before another history command");
    const auto c=context_();const auto generation=generation_;const auto selected=selected_;const auto frame=originalFrame_;
    pendingHistory_=[this,perform=std::move(perform),c,generation,selected,frame]{
      guardRead(c,generation);
      require(visible()&&recordedPointListFocused()&&selected_==selected&&originalFrame_==frame,
        "Activity selection or focus changed / request history again");
      perform();
    };
    SetTimer(window_,6,20,nullptr);return true;
  }
  bool queuedMusicalEdit()const{return bool(automaticEdit_)||bool(pendingHistory_)||pendingPointDelete_;}
  void flushForProjectSave(){
    // Finish an already queued typing or explicit Save/Delete intent. Invalid,
    // rejected or uncertain fields are never retried by project Save/history.
    if(pendingPointDelete_){require(!pending_&&!context_().busy,"Wait for the selected point before saving");timer(4);}
    if(automaticEdit_)flushAutomaticEdit();
    require(!automaticEdit_&&!pending_&&!pendingHistory_&&!pendingPointDelete_,"Wait for the requested point edit before continuing");
  }
  void flushForHistory(){flushForProjectSave();}
  void hide()override{
    if(pendingPointDelete_)flushForProjectSave();
    cancelNavigation();
    pendingHistory_={};KillTimer(window_,6);
    if(automaticEdit_)flushForProjectSave();
    NativeToolWindow::hide();
  }
  ParameterActivityWindow(HWND owner,Request request,std::function<Context()> context,std::function<void(const Json &)> inspect):NativeToolWindow(owner),request_(std::move(request)),context_(std::move(context)),inspect_(std::move(inspect)){
    minimumClientWidth_=1020;minimumClientHeight_=680;create(L"ScreamSeq.ParameterActivity",L"Parameter activity",1180,790);clampToOwnerWorkArea();for(int id:{processors,parameters,mode,details})combo(id);append(mode,L"Recent seconds");append(mode,L"Latest pattern pass");choose(mode,0);for(auto s:{L"Sources",L"Recent changes",L"Recorded points"})append(details,s);choose(details,0);
    edit(search,L"",256);SendMessageW(controls_.at(search),EM_SETCUEBANNER,FALSE,reinterpret_cast<LPARAM>(L"Find parameter by name, unit or ID (Ctrl+F)"));
    add(items,L"LISTBOX",L"Parameter activity details",LBS_NOTIFY|LBS_NOINTEGRALHEIGHT|WS_VSCROLL|WS_HSCROLL);edit(time,L"0",40);edit(value,L"0",40);
    for(auto [id,text]:std::initializer_list<std::pair<int,const wchar_t *>>{{refresh,L"Refresh"},{freeze,L"Freeze"},{clear,L"Clear capture"},{openSource,L"Open source"},{openLane,L"Song automation…"},{previous,L"‹"},{next,L"›"},{newPoint,L"New"},{savePoint,L"Save now"},{removePoint,L"Delete"},{zoomOut,L"−"},{zoomIn,L"+"},{fit,L"Fit"},{close,L"Close"}})button(id,text);
    for(auto [id,text]:std::initializer_list<std::pair<int,const wchar_t *>>{{title,L"Parameter activity"},{targetLabel,L"Choose a prepared processor copy"},{ruleLabel,L"Observe the final parameter value and the sources that control it."},{statusLabel,L""},{timeLabel,L"Time / seconds"},{valueLabel,L"Value / native units"},{pageLabel,L""}})label(id,text);
    for(auto [id,name]:std::initializer_list<std::pair<int,const wchar_t *>>{
        {processors,L"Parameter activity processor copy"},{search,L"Search activity parameters by name, unit or ID"},{parameters,L"Parameter activity parameter"},
        {mode,L"Activity trace time range"},{details,L"Parameter activity details page"},
        {items,L"Parameter sources, changes or recorded points"},{time,L"Recorded point time, seconds"},
        {value,L"Recorded point value, native units"},{previous,L"Previous recorded page"},
        {next,L"Next recorded page"},{zoomOut,L"Zoom activity trace out"},{zoomIn,L"Zoom activity trace in"}})
      accessibleName(id,name);
    HWND previousControl=HWND_TOP;
    for(const auto id:{processors,search,parameters,refresh,freeze,clear,zoomOut,fit,zoomIn,mode,
        details,openSource,openLane,items,time,value,newPoint,savePoint,removePoint,previous,next,close}){
      const auto control=controls_.at(id);SetWindowPos(control,previousControl,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);previousControl=control;
    }
    finish();SetTimer(window_,3,100,nullptr);}
  void openAt(std::string plugin={},std::optional<uint32_t> parameter={}){cancelNavigation();const bool retain=visible()||fields_;show();if(!retain){beginPending();try{load(std::move(plugin),parameter);pending_=false;}catch(...){pending_=false;throw;}}SetFocus(controls_.at(processors));}
  void openSourceAt(std::string plugin,uint32_t parameter) {
    cancelNavigation();
    const auto c=context_();const bool same=captured_.document==c.document&&plugin_==plugin&&parameter_&&*parameter_==parameter;
    require(!pending_&&!c.busy,"Finish the current activity read or document operation first");
    if(same){show();SetFocus(controls_.at(parameters));return;}
    require(!fields_&&!frozen_,"The activity editor retains a point draft or frozen trace. Save/Refresh it before choosing another parameter.");
    beginPending();try{load(std::move(plugin),parameter,false,true);pending_=false;}catch(...){pending_=false;throw;}
    show();SetFocus(controls_.at(parameters));
  }
  Json snapshot()const{return {{"visible",visible()},{"document",captured_.document},{"expectedRevision",captured_.revision},{"target",target_},{"plugin",plugin_},{"parameter",parameter_?Json(*parameter_):Json()},{"engine",engine_},{"token",token_},{"cursor",cursor_},{"pointCount",points_.size()},{"sourceCount",sources_.size()},{"targets",targets_},{"parameters",parameters_},{"search",utf8(field(search))},{"searchMatches",searchMatches_},{"parameterChoices",parameterChoices_},{"searchPending",searchPending_},{"sources",sources_},{"recorded",recorded_},{"recordedTotal",total_},{"recordedOffset",offset_},{"selectedPoint",selected_},{"fieldDraft",fields_},{"generation",generation_},{"rawTime",utf8(field(time))},{"rawValue",utf8(field(value))},{"pending",pending_},{"frozen",frozen_},{"active",active_},{"stale",!current()},{"dropped",dropped_},{"page",page_},{"traceMode",traceMode_},{"trace",traceSnapshot()},{"seconds",seconds_},{"status",utf8(status_)}};}
};
}
