#pragma once
#include "NativeToolWindow.hpp"
#include "NativeWriteCompletion.hpp"
#include "NativeReportList.hpp"
#include <iomanip>
#include <sstream>
#include <map>

namespace ScreamSeq {
// Retained native presentation for the worker-owned take. This window never
// remaps time, replays input, edits manual plugin state or owns musical history.
class ParameterRecordingWindow final:public NativeToolWindow {
public:
  using Json=Api::Json;
  struct Callbacks {
    std::function<std::pair<std::string,std::string>()> context;
    std::function<Json()> rack;
    std::function<std::wstring(const std::string &,uint32_t)> parameterName;
    std::function<Json(const std::string &,const Json &)> read;
    NativeWriteCompletion::Write write;
    std::function<void(const Api::CompletedCall &)> reconcile;
    std::function<Json(size_t,size_t)> delivery;
    std::function<void()> saveRecovery;
    std::function<bool()> available;
    std::function<void()> recoveredInputs;
    std::function<void(const std::string &,uint32_t)> recordedAutomation;
    std::function<bool()> inputProtected;
  };
private:
  enum:int {scope=9720,start,stop,keep,discard,review,saveCopy,close,target,previous,next,acknowledge,
    events,heading,summary,help,page,statusLabel,recovered,inputDetails,stopPlayback,openKept,restoreSaved};
  static constexpr UINT_PTR notificationSubclass=0x50525731;
  static constexpr UINT_PTR listSubclass=0x50525732;
  Callbacks callbacks_;
  std::string document_,targetKey_,error_,displayedTarget_,displayedTake_,targetCatalogueTake_;
  std::map<std::pair<std::string,uint32_t>,std::wstring> parameterNames_;
  Json state_={{"retained",false}},delivery_=Json::object(),rack_=Json::array(),rows_=Json::array(),targets_=Json::array(),lossEvidence_;
  NativeWriteCompletion completion_;
  Json keptTarget_,keepCandidate_;
  struct QueuedAction {std::string document,revision,take,method;Json params,lossEvidence,navigationTarget;bool capturing,completed;};
  std::optional<QueuedAction> queuedAction_;
  struct QueuedTarget {std::string document,revision,take,key;size_t offset=0;};
  std::optional<QueuedTarget> queuedTarget_;
  struct QueuedKept {std::pair<std::string,std::string> context;Json target;};
  std::optional<QueuedKept> queuedKept_;
  bool refreshing_=false;
  size_t offset_=0,total_=0,displayedOffset_=0;uint64_t generation_=0;
  bool pending_=false,setting_=false,presentingRows_=false,lossAcknowledged_=false,targetChosen_=false;
  std::optional<Tracker::DocumentDraft> documentDraft()const override {
    return describeDraft(document_,state_.value("baseRevision",callbacks_.context().second),take(),generation_,
      retained(),pending_||queuedAction_.has_value(),completion_.retained()&&!pending_);
  }
  static constexpr size_t pageSize=128;
  bool retained()const{return state_.value("retained",false);}
  bool inputProtected()const{return (callbacks_.inputProtected&&callbacks_.inputProtected())||
    delivery_.value("pending",false)||delivery_.value("needsReview",false)||
    delivery_.value("retained",size_t(0))||delivery_.value("lost",uint64_t(0));}
  bool capturing()const{return state_.value("capturing",false);}
  bool playbackRecovery()const{return retained()&&(!capturing()||!state_.value("compatible",false));}
  bool needsPlaybackStop()const{return playbackRecovery()&&state_.value("transportPlaying",false);}
  bool deliveryView()const{return targetKey_.empty();}
  std::string take()const{return state_.value("take",std::string());}
  bool loss()const {
    if(delivery_.value("needsReview",false)||state_.value("failure",0u)||state_.value("rejected",uint64_t(0)))return true;
    for(const auto &source:state_.value("sources",Json::array()))if(source.value("lost",uint64_t(0)))return true;
    for(const auto &entry:state_.value("targets",Json::array()))if(entry.value("unmapped",uint64_t(0))||entry.value("overflow",uint64_t(0)))return true;
    return false;
  }
  Json lossEvidence()const {
    Json sources=Json::array(),targets=Json::array();
    for(const auto &source:state_.value("sources",Json::array()))sources.push_back({source.at("plugin"),source.value("lost",uint64_t(0))});
    for(const auto &entry:state_.value("targets",Json::array()))targets.push_back({entry.at("plugin"),entry.at("parameter"),entry.value("unmapped",uint64_t(0)),entry.value("overflow",uint64_t(0))});
    return Json::array({take(),state_.value("failure",0u),state_.value("rejected",uint64_t(0)),sources,targets,
      delivery_.value("needsReview",false),delivery_.value("lost",uint64_t(0)),delivery_.value("retained",size_t(0)),delivery_.value("pending",false)});
  }
  void requireContext()const{if(callbacks_.context().first!=document_)throw std::runtime_error("This recording window belongs to the previous song");}
  bool keptTargetAvailable()const {
    return callbacks_.recordedAutomation&&!keptTarget_.is_null()&&!retained()&&
      !queuedAction_&&!completion_.retained()&&callbacks_.context().first==document_&&
      std::any_of(rack_.begin(),rack_.end(),[&](const auto &plugin){return plugin.at("instanceID")==keptTarget_.at("plugin");});
  }
  bool canOpenKept()const{return keptTargetAvailable()&&!pending_&&!queuedKept_;}
  void cancelKeptNavigation(){queuedKept_.reset();KillTimer(window_,10);}
  void drainKeptNavigation(){
    if(!queuedKept_||pending_||(callbacks_.available&&!callbacks_.available()))return;
    const auto navigation=std::exchange(queuedKept_,{});KillTimer(window_,10);
    if(!visible()||callbacks_.context()!=navigation->context||!keptTargetAvailable()||keptTarget_!=navigation->target)
      throw std::runtime_error("Kept automation context changed / choose Song automation again");
    callbacks_.recordedAutomation(navigation->target.at("plugin").get<std::string>(),navigation->target.at("parameter").get<uint32_t>());
  }
  void openKeptAutomation(){
    requireContext();if(!keptTargetAvailable()||(pending_&&!refreshing_))return;
    if(!queuedKept_)queuedKept_=QueuedKept{callbacks_.context(),keptTarget_};
    if(pending_||(callbacks_.available&&!callbacks_.available())){SetTimer(window_,10,20,nullptr);if(!pending_)layout();}
    else drainKeptNavigation();
  }
  void rememberKept(const Api::CompletedCall &call) {
    if(call.document!=document_)throw std::runtime_error("Kept automation belongs to another song");
    if(call.method=="automation.recording.commit"&&(call.result.value("completed",false)||!call.result.value("retained",true))){
      keptTarget_=keepCandidate_;
      if(!call.result.value("retained",true)&&!keptTarget_.is_null())set(summary,L"Take kept · "+targetName(keptTarget_)+L" · Open Song automation to edit its points.");
    }
    else if(call.method=="automation.recording.start")keptTarget_=Json();
  }
  std::wstring pluginName(const std::string &plugin)const {
    for(const auto &entry:rack_)if(entry.at("instanceID")==plugin)return wide(entry.at("name").get<std::string>());
    return wide(plugin);
  }
  std::wstring targetName(const Json &target)const {
    const auto plugin=target.at("plugin").get<std::string>();const auto id=target.at("parameter").get<uint32_t>();
    const auto found=parameterNames_.find({plugin,id});
    return pluginName(plugin)+L" / "+(found!=parameterNames_.end()?found->second:L"Parameter "+std::to_wstring(id));
  }
  void rememberTarget(const Json &target) {
    if(!callbacks_.parameterName)return;
    const auto key=std::pair(target.at("plugin").get<std::string>(),target.at("parameter").get<uint32_t>());
    if(!parameterNames_.contains(key)){auto name=callbacks_.parameterName(key.first,key.second);requireContext();parameterNames_.emplace(key,std::move(name));}
  }
  void updateRack() {
    if(retained()||SendMessageW(controls_.at(scope),CB_GETDROPPEDSTATE,0,0))return;
    auto current=callbacks_.rack();if(current==rack_)return;
    const auto old=SendMessageW(controls_.at(scope),CB_GETCURSEL,0,0);
    std::string selected;if(old>0&&size_t(old)<=rack_.size())selected=rack_[size_t(old-1)].at("instanceID");
    rack_=std::move(current);setting_=true;
    struct Reset{bool &value;~Reset(){value=false;}}reset{setting_};
    NativeInputGate::present(controls_.at(scope),CB_RESETCONTENT,0,0);
    NativeInputGate::present(controls_.at(scope),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"All rack plugins"));
    int selection=0;for(size_t i=0;i<rack_.size();++i){const auto name=wide(rack_[i].at("name").get<std::string>());
      NativeInputGate::present(controls_.at(scope),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name.c_str()));if(rack_[i].at("instanceID")==selected)selection=int(i+1);}
    NativeInputGate::present(controls_.at(scope),CB_SETCURSEL,selection,0);
  }
  void updateTargets() {
    auto targets=state_.value("targets",Json::array());
    // Counts grow while playing. Rebuild only if identities changed, preserving
    // the native popup, selection and focus through ordinary recording polls.
    Json identities=Json::array();for(const auto &entry:targets)identities.push_back({{"plugin",entry.at("plugin")},{"parameter",entry.at("parameter")}});
    if(identities==targets_&&targetCatalogueTake_==take())return;
    if(SendMessageW(controls_.at(target),CB_GETDROPPEDSTATE,0,0))return;
    for(const auto &entry:identities)rememberTarget(entry);
    // A healthy take should immediately show captured music, rather than an
    // empty delivery-error page. Respect an explicit target/delivery choice.
    if(!targetChosen_&&targetKey_.empty()&&!identities.empty()&&!delivery_.value("needsReview",false))
      targetKey_=identities.front().dump();
    targets_=std::move(identities);targetCatalogueTake_=take();setting_=true;
    struct Reset{bool &value;~Reset(){value=false;}}reset{setting_};
    NativeInputGate::present(controls_.at(target),CB_RESETCONTENT,0,0);
    NativeInputGate::present(controls_.at(target),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Native input delivery"));
    int selection=0;for(size_t i=0;i<targets_.size();++i){const auto &entry=targets_[i];
      const auto label=targetName(entry);
      NativeInputGate::present(controls_.at(target),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));if(entry.dump()==targetKey_)selection=int(i+1);}
    if(!selection&&!targetKey_.empty()){targetKey_.clear();offset_=0;}
    NativeInputGate::present(controls_.at(target),CB_SETCURSEL,selection,0);
  }
  void presentTargetSelection() {
    int selected=0;for(size_t i=0;i<targets_.size();++i)if(targets_[i].dump()==targetKey_)selected=int(i+1);
    NativeInputGate::present(controls_.at(target),CB_SETCURSEL,selected,0);
  }
  void cancelTargetSelection(){queuedTarget_.reset();KillTimer(window_,9);}
  void changeTarget(std::string key,size_t offset=0) {
    const auto previousKey=targetKey_;const auto previousOffset=offset_;const auto previousChosen=targetChosen_;
    const auto previousTotal=total_;
    targetChosen_=true;targetKey_=std::move(key);offset_=offset;
    try{refresh();presentTargetSelection();}
    catch(...){
      // Failed reads retain the old event page. If its take/catalogue is still
      // current, restore its selector too; do not relabel those old inputs as
      // belonging to the attempted target or reset their selected row/caret.
      if(displayedTake_==take()&&displayedTarget_==previousKey&&displayedOffset_==previousOffset&&
         (previousKey.empty()||std::any_of(targets_.begin(),targets_.end(),[&](const auto &entry){return entry.dump()==previousKey;}))){
        targetKey_=previousKey;offset_=previousOffset;total_=previousTotal;targetChosen_=previousChosen;presentTargetSelection();
      }
      throw;
    }
  }
  void selectTarget() {
    const auto selected=SendMessageW(controls_.at(target),CB_GETCURSEL,0,0);
    if(selected<0||size_t(selected)>targets_.size())return;
    const auto key=selected?targets_[size_t(selected-1)].dump():std::string();
    const bool unavailable=callbacks_.available&&!callbacks_.available();
    if(pending_||queuedAction_||unavailable){
      // Reads pump native messages. Keep the displayed target tied to its
      // current rows until the stable choice can be loaded without reentry.
      if(!queuedAction_&&(refreshing_||!pending_)){
        queuedTarget_=QueuedTarget{document_,callbacks_.context().second,take(),key};SetTimer(window_,9,20,nullptr);
      }
      presentTargetSelection();return;
    }
    cancelTargetSelection();changeTarget(key);
  }
  void selectPage(int id) {
    if(queuedAction_)return;
    const auto context=callbacks_.context();
    const auto base=queuedTarget_&&queuedTarget_->key==targetKey_&&queuedTarget_->take==take()&&
      context==std::pair(queuedTarget_->document,queuedTarget_->revision)?queuedTarget_->offset:offset_;
    const auto last=total_?((total_-1)/pageSize)*pageSize:0;
    const auto wanted=id==previous?(base>=pageSize?base-pageSize:0):base>=last?last:std::min(last,base+pageSize);
    if(pending_||(callbacks_.available&&!callbacks_.available())){
      // Paging is relative to the displayed target, not a different selector
      // choice which has not loaded yet. Multiple clicks retain their bounded
      // destination without reentering a read or queuing any musical write.
      if(refreshing_||!pending_){queuedTarget_=QueuedTarget{document_,context.second,take(),targetKey_,wanted};SetTimer(window_,9,20,nullptr);}
      return;
    }
    cancelTargetSelection();if(wanted!=offset_)changeTarget(targetKey_,wanted);
  }
  void drainTargetSelection() {
    if(pending_||!queuedTarget_)return;
    auto choice=std::move(*queuedTarget_);queuedTarget_.reset();requireContext();
    if(!visible())return;
    if(callbacks_.context()!=std::pair(choice.document,choice.revision)||take()!=choice.take||
       (!choice.key.empty()&&std::none_of(targets_.begin(),targets_.end(),[&](const auto &entry){return entry.dump()==choice.key;})))
      throw std::runtime_error("Song or recording target changed during refresh; choose the input target again");
    changeTarget(std::move(choice.key),choice.offset);
  }
  std::wstring cell(size_t row,unsigned column)const {
    if(row>=rows_.size())return {};const auto &value=rows_[row];
    const bool native=value.contains("plugin");
    if(column==0){if(native)return L"—";if(value.at("frame").is_null())return L"Unmapped";
      std::wostringstream seconds;seconds<<std::fixed<<std::setprecision(4)<<double(std::stoull(value.at("frame").get<std::string>()))/48000.<<L" s";return seconds.str();}
    if(column==1)return wide(value.at("value").dump());
    if(column==2)return native?targetName(value):L"Selected parameter";
    return native?wide(value.value("state",std::string())+" / "+value.value("error",std::string())):
      value.at("frame").is_null()?L"Original input retained; timing unavailable":L"Captured";
  }
  void selectionDetails() {
    const auto row=ListView_GetNextItem(controls_.at(events),-1,LVNI_SELECTED);
    if(row<0||size_t(row)>=rows_.size()){
      set(inputDetails,L"Select an input to read its complete status. Tab enters these read-only details; Ctrl+C copies selected text.");return;
    }
    const auto &event=rows_[size_t(row)];std::wstring name;
    if(event.contains("plugin"))name=targetName(event);
    else for(const auto &entry:targets_)if(entry.dump()==targetKey_){name=targetName(entry);break;}
    auto text=name+L" · Value: "+cell(size_t(row),1);
    if(!event.contains("plugin"))text+=L" · Song time: "+cell(size_t(row),0);
    // Both live delivery and captured-event pages expose the original input as
    // timestamp. hostTime belongs only to the detached recovery archive schema.
    const auto timestamp=event.value("timestamp",std::string());
    text+=L"\r\nOriginal host time: "+(timestamp.empty()?std::wstring(L"Unavailable"):wide(timestamp));
    text+=L"\r\n"+cell(size_t(row),3);
    set(inputDetails,text);
  }
  void loadPage() {
    Json data;
    if(deliveryView())data=callbacks_.delivery(offset_,pageSize);
    else {
      const auto selected=std::find_if(targets_.begin(),targets_.end(),[&](const auto &entry){return entry.dump()==targetKey_;});
      if(selected==targets_.end())throw std::runtime_error("Choose a retained recording target");
      auto params=*selected;params["take"]=take();params["offset"]=offset_;params["limit"]=pageSize;
      data=callbacks_.read("automation.recording.events",params);
    }
    total_=data.at(deliveryView()?"retained":"total").get<size_t>();
    auto rows=data.at("events");if(deliveryView())for(const auto &entry:rows)rememberTarget(entry);
    const bool pageChanged=displayedTake_!=take()||displayedTarget_!=targetKey_||displayedOffset_!=offset_;
    displayedTake_=take();displayedTarget_=targetKey_;displayedOffset_=offset_;
    if(rows!=rows_||pageChanged){
      presentingRows_=true;struct PresentRows{bool &value;~PresentRows(){value=false;}}present{presentingRows_};
      const auto list=controls_.at(events);
      int selected=ListView_GetNextItem(list,-1,LVNI_SELECTED);
      const bool first=pageChanged||rows_.empty();
      if(first)selected=rows.empty()?-1:0;
      else if(deliveryView()&&selected>=0&&size_t(selected)<rows_.size()) {
        // Accepted deliveries leave this queue. Row numbers are not identities:
        // keep the original input through removal/status changes, or clear an
        // expired/ambiguous selection rather than display a different input.
        const auto identity=[](Json event){event.erase("state");event.erase("error");return event;};
        const auto wanted=identity(rows_[size_t(selected)]);
        const auto oldCount=std::count_if(rows_.begin(),rows_.end(),[&](const auto &event){return identity(event)==wanted;});
        int found=-1;unsigned matches=0;
        for(size_t i=0;i<rows.size();++i)if(identity(rows[i])==wanted){found=int(i);++matches;}
        selected=oldCount==1&&matches==1?found:-1;
      }
      if(selected>=0&&size_t(selected)>=rows.size())selected=-1;
      rows_=std::move(rows);NativeReportList::setItemCount(controls_.at(events),int(rows_.size()),LVSICF_NOSCROLL|LVSICF_NOINVALIDATEALL);
      NativeReportList::setItemState(list,-1,0,LVIS_SELECTED|LVIS_FOCUSED);
      if(selected>=0)NativeReportList::setItemState(list,selected,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
      if(first&&selected>=0)NativeReportList::ensureVisible(list,selected,FALSE);
      InvalidateRect(controls_.at(events),nullptr,FALSE);
    }
    selectionDetails();
    set(page,total_?std::to_wstring(offset_+1)+L"–"+std::to_wstring(offset_+rows_.size())+L" of "+std::to_wstring(total_):L"No inputs on this page");
  }
  void refresh() {
    if(pending_)return;requireContext();pending_=refreshing_=true;
    bool succeeded=false;
    struct Reset{ParameterRecordingWindow &self;bool &succeeded;~Reset(){
      self.pending_=self.refreshing_=false;
      // An incomplete refresh cannot authorize a later write. The normal
      // error path presents the read failure; no command has been submitted.
      if(!succeeded){self.queuedAction_.reset();self.cancelTargetSelection();self.cancelKeptNavigation();}
      if(self.queuedAction_)SetTimer(self.window_,8,1,nullptr);
      if(self.queuedTarget_)SetTimer(self.window_,9,1,nullptr);
      self.layout();}}reset{*this,succeeded};
    const auto old=take();auto state=callbacks_.read("automation.recording.get",Json::object());requireContext();
    state_=std::move(state);delivery_=callbacks_.delivery(0,1);
    if(take()!=old){if(retained())keptTarget_=Json();offset_=0;targetKey_.clear();targetChosen_=false;lossAcknowledged_=false;NativeInputGate::present(controls_.at(acknowledge),BM_SETCHECK,BST_UNCHECKED,0);}
    const auto evidence=lossEvidence();if(evidence!=lossEvidence_){lossEvidence_=evidence;lossAcknowledged_=false;NativeInputGate::present(controls_.at(acknowledge),BM_SETCHECK,BST_UNCHECKED,0);}
    updateRack();updateTargets();
    if(deliveryView()&&offset_&&offset_>=delivery_.at("retained").get<size_t>())offset_=0;
    loadPage();
    set(summary,retained()?(state_.value("completed",false)?L"Take kept · finish restoring the native controls":capturing()?
      (state_.value("transportPlaying",true)?(state_.value("compatible",false)?L"Recording · playback continues":
        L"Recording context lost · stop playback, then save recovery or discard"):
        L"Playback stopped · original input retained for review or discard"):
      state_.value("compatible",false)?L"Stopped take · review before keeping or discarding":L"Take no longer matches the song · save a recovery copy or discard"):
      inputProtected()?L"Original parameter input is retained. Refresh / review its outcome before recording again.":
      !keptTarget_.is_null()?L"Take kept · "+targetName(keptTarget_)+L" · Open Song automation to edit its points.":
      L"Start playback, then record native controls or plugin editor gestures.");
    const auto counts=L"Captured "+std::to_wstring(state_.value("eventCount",size_t(0)))+L" inputs · "+
      std::to_wstring(delivery_.value("retained",size_t(0)))+L" delivery results retained · "+std::to_wstring(delivery_.value("lost",uint64_t(0)))+L" native inputs lost";
    set(help,state_.value("completed",false)?L"Kept automation remains. Restore saved state abandons later unsaved plugin input; stop playback and close recorded plugin editors first.":
      counts+(loss()?L" · Review loss before keeping accepted points.":L" · Keep take creates one Undo step."));
    if(state_.contains("intakeError"))error_=state_.at("intakeError").get<std::string>();
    set(statusLabel,wide(error_));
    succeeded=true;
  }
  void queueAction(int id) {
    if(queuedAction_||completion_.retained())return;
    Json params=Json::object();std::string method;
    if(id==start){
      if(retained())throw std::runtime_error("Finish the retained take before recording again");
      if(inputProtected())throw std::runtime_error("Original parameter input is retained; Refresh / review its outcome before recording again");
      const auto selected=SendMessageW(controls_.at(scope),CB_GETCURSEL,0,0);
      if(selected<0||size_t(selected)>rack_.size())throw std::runtime_error("Choose available rack plugins");
      auto plugins=Json::array();
      if(selected)plugins.push_back(rack_[size_t(selected-1)].at("instanceID"));
      else for(const auto &entry:rack_)plugins.push_back(entry.at("instanceID"));
      params["plugins"]=std::move(plugins);method="automation.recording.start";
    }else if(id==stopPlayback){
      if(!needsPlaybackStop())throw std::runtime_error("Playback no longer needs stopping; review the retained take");
      method="transport.stop";
    }else{
      if(!retained())throw std::runtime_error("No retained recording take");
      params["take"]=take();
      if(id==keep)params["acknowledgeLoss"]=lossAcknowledged_;
      if(id==restoreSaved) {
        if(!state_.value("completed",false)||capturing()||!state_.contains("transportPlaying")||state_.at("transportPlaying")!=false)
          throw std::runtime_error("Keep the take and stop playback before restoring saved plugin state");
        params["restoreSavedState"]=true;
      }
      method=id==stop?"automation.recording.stop":(id==keep||id==restoreSaved)?"automation.recording.commit":"automation.recording.discard";
    }
    Json navigationTarget;
    if(method=="automation.recording.commit") {
      if(state_.value("completed",false))navigationTarget=keptTarget_;
      if(navigationTarget.is_null()&&queuedTarget_&&!queuedTarget_->key.empty()&&queuedTarget_->take==take()&&
         callbacks_.context()==std::pair(queuedTarget_->document,queuedTarget_->revision))
        for(const auto &entry:targets_)if(entry.dump()==queuedTarget_->key)navigationTarget=entry;
      if(navigationTarget.is_null())for(const auto &entry:targets_)if(entry.dump()==targetKey_)navigationTarget=entry;
      if(navigationTarget.is_null()&&!targets_.empty())navigationTarget=targets_.front();
    }
    queuedAction_=QueuedAction{document_,callbacks_.context().second,take(),std::move(method),std::move(params),lossEvidence(),
      std::move(navigationTarget),capturing(),state_.value("completed",false)};
  }
  void drainAction() {
    if(pending_||!queuedAction_)return;
    auto action=std::move(*queuedAction_);queuedAction_.reset();
    requireContext();
    if(callbacks_.context()!=std::pair(action.document,action.revision)||take()!=action.take||capturing()!=action.capturing||state_.value("completed",false)!=action.completed)
      throw std::runtime_error("Song or recording changed during refresh; review it before choosing the action again");
    if(action.method=="automation.recording.start"&&inputProtected())
      throw std::runtime_error("Original parameter input is retained; Refresh / review its outcome before recording again");
    if(action.method=="transport.stop"&&!needsPlaybackStop())
      throw std::runtime_error("Playback changed during refresh; review the retained take before choosing the action again");
    if(action.params.value("restoreSavedState",false)&&(!state_.value("completed",false)||!state_.contains("transportPlaying")||state_.at("transportPlaying")!=false))
      throw std::runtime_error("Playback or take changed before saved-state recovery; review it again");
    if(action.method=="automation.recording.discard"&&state_.value("completed",false))
      throw std::runtime_error("This take is already kept. Finish restoring the controls; use Undo to restore the earlier music.");
    if(action.method=="automation.recording.commit"&&action.lossEvidence!=lossEvidence())
      throw std::runtime_error("Recording input loss changed during refresh; review it before keeping the take");
    // Submit once, with the original scope/acknowledgement. Neither uncertain
    // completion nor worker admission failure automatically retries the write.
    submit(action.method,std::move(action.params),std::move(action.navigationTarget));
  }
  void submit(const std::string &method,Json params,Json navigationTarget) {
    requireContext();if(pending_||completion_.retained())throw std::runtime_error("Review the previous recording result first");
    if(method=="automation.recording.commit") {
      keepCandidate_=std::move(navigationTarget);
    }
    // These boundaries can replace/remove the take. Keep already captured its
    // navigation destination; a delayed page read must not report a stale
    // selection error after the successful lifecycle result.
    if(method=="automation.recording.commit"||method=="automation.recording.discard"||method=="automation.recording.start")cancelTargetSelection();
    params["expectedRevision"]=callbacks_.context().second;pending_=true;
    try{completion_.submit(callbacks_.write,method,params,document_,++generation_,params);pending_=false;
      if(!completion_.returned())throw std::runtime_error("Recording result is uncertain; use Refresh / review");
      error_=completion_.returned()->result.value("restorationError",std::string());refresh();rememberKept(*completion_.returned());completion_.finish();layout();
    }catch(...){pending_=false;layout();throw;}
  }
  void action(int id,unsigned notification)override {
    if(setting_)return;
    if(id==close&&notification==BN_CLICKED){hide();return;}
    if(id==openKept&&notification==BN_CLICKED){openKeptAutomation();return;}
    if(id==target&&notification==CBN_SELCHANGE){selectTarget();return;}
    if((id==previous||id==next)&&notification==BN_CLICKED){selectPage(id);return;}
    if(pending_){
      if(refreshing_&&notification==BN_CLICKED&&(id==start||id==stop||id==keep||id==discard||id==stopPlayback||id==restoreSaved))queueAction(id);
      return;
    }
    if(queuedAction_)return;
    if(id==acknowledge&&notification==BN_CLICKED){lossAcknowledged_=SendMessageW(controls_.at(id),BM_GETCHECK,0,0)==BST_CHECKED;layout();return;}
    if(notification!=BN_CLICKED)return;
    if(id==recovered&&callbacks_.recoveredInputs){callbacks_.recoveredInputs();return;}
    if(id==review){
      const auto returned=completion_.returned();if(returned&&callbacks_.reconcile){requireContext();callbacks_.reconcile(*returned);}
      refresh();if(completion_.retained()){
        error_=returned?"Completed action re-read; no request was resent":"Current take re-read; earlier action outcome remains unverified";
        // An empty current take does not prove whether Keep/Discard/Start
        // completed. Retain the original receipt and close protection until
        // its matching completion can be reconciled; never enable a repeat.
        if(returned){rememberKept(*returned);completion_.finish();}set(statusLabel,wide(error_));layout();}return;
    }
    if(id==saveCopy){pending_=true;try{callbacks_.saveRecovery();pending_=false;error_="Recovery copy saved";refresh();}catch(...){pending_=false;throw;}return;}
    if(id==start||id==stop||id==keep||id==discard||id==stopPlayback||id==restoreSaved){queueAction(id);drainAction();}
  }
  bool key(WPARAM key,bool ctrl,bool)override {
    if(key==VK_ESCAPE){hide();return true;}if(ctrl&&key=='R'){action(review,BN_CLICKED);return true;}
    if(ctrl&&(key==VK_PRIOR||key==VK_NEXT)){action(key==VK_PRIOR?previous:next,BN_CLICKED);return true;}
    if(ctrl&&key=='A'&&GetFocus()==controls_.at(inputDetails)){NativeInputGate::present(controls_.at(inputDetails),EM_SETSEL,0,-1);return true;}
    if(key==VK_RETURN){wchar_t kind[20]{};GetClassNameW(GetFocus(),kind,20);if(!_wcsicmp(kind,L"Button")){action(GetDlgCtrlID(GetFocus()),BN_CLICKED);return true;}}return false;
  }
  void timer(UINT_PTR id)override{
    if(id==10){if(pending_||(callbacks_.available&&!callbacks_.available()))return;drainKeptNavigation();layout();return;}
    if(id==9){
      if(queuedTarget_&&(pending_||(callbacks_.available&&!callbacks_.available()))){SetTimer(window_,9,20,nullptr);return;}
      KillTimer(window_,9);drainTargetSelection();return;
    }
    if(id==8){
      if(queuedAction_&&(pending_||(callbacks_.available&&!callbacks_.available()))){SetTimer(window_,8,20,nullptr);return;}
      KillTimer(window_,8);drainAction();return;
    }
    if(id==7&&visible()&&!pending_&&!queuedAction_&&!queuedTarget_&&!completion_.retained()&&(!callbacks_.available||callbacks_.available()))refresh();
  }
  void error(const std::exception &error)override{error_=error.what();set(statusLabel,wide(error_));layout();}
  void layout()override {
    if(!ready_)return;const auto [w,h]=size();place(heading,12,10,w-(callbacks_.recoveredInputs?218:24),24);place(recovered,w-198,8,186,26,bool(callbacks_.recoveredInputs));place(summary,12,40,w-24,40);
    const float recoveryWidth=playbackRecovery()?126.f:0.f;
    place(scope,12,84,w-246-recoveryWidth,240);place(start,w-226-recoveryWidth,84,100,26);place(stop,w-118-recoveryWidth,84,106,26);
    place(stopPlayback,w-130,84,118,26,playbackRecovery());
    place(help,12,118,w-24,42);place(target,12,168,w-252,240);place(previous,w-232,168,68,26);place(next,w-156,168,68,26);place(close,w-80,168,68,26);
    place(events,12,204,w-24,std::max(80.f,h-416));place(inputDetails,12,h-204,w-24,62);place(page,12,h-134,w-222,22);
    place(openKept,w-202,h-138,190,26,bool(callbacks_.recordedAutomation));EnableWindow(controls_.at(openKept),canOpenKept());
    const bool completed=state_.value("completed",false);
    place(acknowledge,12,h-108,w-24,24);place(keep,12,h-76,132,28);place(discard,152,h-76,112,28,!completed);
    place(restoreSaved,152,h-76,150,28,completed);
    place(saveCopy,completed?310.f:272.f,h-76,completed?138.f:150.f,28);place(review,w-190,h-76,178,28);place(statusLabel,12,h-40,w-24,32);
    const bool ready=!pending_&&!queuedAction_&&!completion_.retained();
    EnableWindow(controls_.at(scope),!retained());EnableWindow(controls_.at(start),ready&&!retained()&&!inputProtected()&&!rack_.empty());
    EnableWindow(controls_.at(stop),ready&&retained()&&capturing());
    EnableWindow(controls_.at(stopPlayback),ready&&needsPlaybackStop());
    EnableWindow(controls_.at(keep),ready&&retained()&&!capturing()&&(completed||state_.value("compatible",false))&&!delivery_.value("pending",false)&&(completed||!loss()||lossAcknowledged_));
    EnableWindow(controls_.at(discard),ready&&retained()&&state_.value("discardable",!capturing())&&!state_.value("completed",false)&&!delivery_.value("pending",false));
    EnableWindow(controls_.at(restoreSaved),ready&&retained()&&completed&&!capturing()&&state_.contains("transportPlaying")&&state_.at("transportPlaying")==false&&!delivery_.value("pending",false));
    set(keep,completed?L"Finish restoring":L"Keep take");
    EnableWindow(controls_.at(acknowledge),ready&&retained()&&!completed&&!capturing()&&loss());
    EnableWindow(controls_.at(saveCopy),!pending_&&retained());EnableWindow(controls_.at(review),!pending_);
    EnableWindow(controls_.at(previous),!pending_&&offset_>0);EnableWindow(controls_.at(next),!pending_&&offset_+rows_.size()<total_);
  }
  LRESULT notify(NMHDR *header) {
    if(header->hwndFrom!=controls_.at(events))return 0;
    if(header->code==LVN_GETDISPINFOW){auto &info=*reinterpret_cast<NMLVDISPINFOW *>(header);if((info.item.mask&LVIF_TEXT)&&info.item.pszText&&info.item.cchTextMax>0){const auto value=info.item.iItem>=0?cell(size_t(info.item.iItem),unsigned(info.item.iSubItem)):std::wstring();lstrcpynW(info.item.pszText,value.c_str(),info.item.cchTextMax);}return 0;}
    if(header->code==LVN_ITEMCHANGED||header->code==NM_CLICK){if(!presentingRows_)selectionDetails();return 0;}
    if(header->code==NM_CUSTOMDRAW)return NativeReportList::customDraw(*reinterpret_cast<NMLVCUSTOMDRAW *>(header),[this](size_t row,unsigned column){return cell(row,column);});return 0;
  }
  static LRESULT CALLBACK notifications(HWND window,UINT message,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR data) {
    auto &self=*reinterpret_cast<ParameterRecordingWindow *>(data);
    if(message==WM_NCDESTROY)RemoveWindowSubclass(window,notifications,id);
    if(self.ready_&&NativeReportList::themeMessage(message))NativeReportList::refresh(self.controls_.at(events));
    if(message==WM_NOTIFY&&self.ready_)try{return self.notify(reinterpret_cast<NMHDR *>(l));}catch(const std::exception &error){self.error(error);return 0;}
    return DefSubclassProc(window,message,w,l);
  }
  static LRESULT CALLBACK listNotifications(HWND window,UINT message,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR) {
    if(message==WM_NCDESTROY)RemoveWindowSubclass(window,listNotifications,id);
    if(message==WM_NOTIFY){const auto header=reinterpret_cast<NMHDR *>(l);
      if(header->hwndFrom==ListView_GetHeader(window)&&header->code==NM_CUSTOMDRAW)return NativeReportList::headerDraw(*reinterpret_cast<NMCUSTOMDRAW *>(header));}
    return DefSubclassProc(window,message,w,l);
  }
public:
  bool protectsClose()const{return retained()||pending_||queuedAction_.has_value()||completion_.retained();}
  // The API can finish a take while this review is hidden (its poll timer is
  // stopped). Adopt confirmed worker boundaries immediately for departure
  // admission. Do not pump reads or acknowledge this window's own unresolved
  // write here; its receipt/generation still owns that reconciliation.
  void observeBoundary(const std::string &document,const std::string &method,const Json &state) {
    if(document!=document_||callbacks_.context().first!=document_||pending_||completion_.retained())return;
    if(method!="automation.recording.start"&&method!="automation.recording.stop"&&
       method!="automation.recording.commit"&&method!="automation.recording.discard")return;
    if(!state.is_object()||!state.contains("retained")||!state.contains("take"))return;
    // A late completion must not replace a newer take shown in this window.
    if(method!="automation.recording.start"&&retained()&&state.at("take")!=take())return;
    if(method=="automation.recording.start"){
      // A hidden review may see the entire next take through API boundaries,
      // without a poll. Its previous Keep destination must not reappear after
      // that newer take is discarded.
      keptTarget_=Json();cancelKeptNavigation();
    }
    if(state.at("take")!=take()){
      offset_=0;targetKey_.clear();targetChosen_=false;lossAcknowledged_=false;
    }
    state_=state;
  }
  ParameterRecordingWindow(HWND owner,Callbacks callbacks):NativeToolWindow(owner),callbacks_(std::move(callbacks)) {
    document_=callbacks_.context().first;minimumClientWidth_=650;minimumClientHeight_=500;
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_LISTVIEW_CLASSES};check(InitCommonControlsEx(&common),"Initialize parameter recording review");
    create(L"ScreamSeq.ParameterRecording",L"Plugin automation recording",820,650);
    combo(scope);combo(target);NativeInputGate::present(controls_.at(target),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Native input delivery"));NativeInputGate::present(controls_.at(target),CB_SETCURSEL,0,0);
    for(auto [id,text]:std::initializer_list<std::pair<int,const wchar_t *>>{{start,L"Record"},{stop,L"Stop recording"},{keep,L"Keep take"},{discard,L"Discard take"},
      {review,L"Refresh / review (Ctrl+R)"},{saveCopy,L"Save recovery copy"},{close,L"Close"},{previous,L"< Prev"},{next,L"Next >"},{recovered,L"Recovered inputs…"},{stopPlayback,L"Stop playback"},{openKept,L"Song automation…"},{restoreSaved,L"Restore saved state"}})button(id,text);
    add(acknowledge,L"BUTTON",L"Keep accepted points despite reported input loss",BS_AUTOCHECKBOX);
    for(int id:{heading,summary,help,page,statusLabel})label(id,id==heading?L"PLUGIN AUTOMATION RECORDING":L"");
    accessibleName(scope,L"Plugins to record");accessibleName(target,L"Recording input target");accessibleName(acknowledge,L"Keep accepted points despite reported input loss");
    accessibleName(previous,L"Previous recorded input page (Ctrl+Page Up)");accessibleName(next,L"Next recorded input page (Ctrl+Page Down)");
    add(inputDetails,L"EDIT",L"",ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL);
    SendMessageW(controls_.at(inputDetails),EM_SETLIMITTEXT,262144,0);
    accessibleName(inputDetails,L"Selected recording input details, read only");
    const auto list=add(events,WC_LISTVIEWW,L"Original recorded inputs",LVS_REPORT|LVS_OWNERDATA|LVS_SINGLESEL|LVS_SHOWSELALWAYS|WS_BORDER);
    ListView_SetExtendedListViewStyle(list,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);NativeReportList::install(list);
    if(const auto theme=LoadLibraryW(L"uxtheme.dll")){using Theme=HRESULT(WINAPI *)(HWND,LPCWSTR,LPCWSTR);if(const auto setTheme=reinterpret_cast<Theme>(GetProcAddress(theme,"SetWindowTheme")))setTheme(list,L"",L"");FreeLibrary(theme);}
    const wchar_t *names[]={L"Song time",L"Value",L"Parameter",L"Status"};
    for(int i=0;i<4;++i){LVCOLUMNW column{};column.mask=LVCF_TEXT|LVCF_WIDTH;column.pszText=const_cast<wchar_t *>(names[i]);column.cx=MulDiv(i==0?190:i==1?100:220,GetDpiForWindow(list),96);ListView_InsertColumn(list,i,&column);}
    check(SetWindowSubclass(window_,notifications,notificationSubclass,reinterpret_cast<DWORD_PTR>(this))&&
      SetWindowSubclass(list,listNotifications,listSubclass,reinterpret_cast<DWORD_PTR>(this)),"Install parameter recording notifications");
    for(int id:{scope,start,stop,stopPlayback,recovered,target,previous,next,close,events,inputDetails,openKept,acknowledge,keep,discard,restoreSaved,saveCopy,review})
      check(SetWindowPos(controls_.at(id),HWND_BOTTOM,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE),"Order recording controls for keyboard navigation");
    finish();refresh();
  }
  ~ParameterRecordingWindow()override{if(window_)RemoveWindowSubclass(window_,notifications,notificationSubclass);if(controls_.contains(events))RemoveWindowSubclass(controls_.at(events),listNotifications,listSubclass);}
  void show(){const auto focused=GetFocus();NativeToolWindow::show();SetTimer(window_,7,250,nullptr);if(!owns(focused))SetFocus(controls_.at(retained()?target:scope));}
  void hide()override{KillTimer(window_,7);cancelTargetSelection();cancelKeptNavigation();NativeToolWindow::hide();}
  Json snapshot()const{return {{"document",document_},{"visible",visible()},{"pending",pending_},{"queuedAction",queuedAction_?Json(queuedAction_->method):Json()},{"take",state_},{"delivery",delivery_},
    {"queuedTarget",queuedTarget_?Json(queuedTarget_->key):Json()},
    {"queuedOffset",queuedTarget_?Json(queuedTarget_->offset):Json()},
    {"queuedKept",queuedKept_?queuedKept_->target:Json()},
    {"target",targetKey_},{"offset",offset_},{"total",total_},{"events",rows_},{"inputDetails",utf8(field(inputDetails))},
    {"keptTarget",keptTarget_},{"canOpenKept",canOpenKept()},{"acknowledgeLoss",lossAcknowledged_},{"completion",completion_.snapshot()},{"error",error_}};}
};
}
