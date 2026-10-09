#pragma once
#include "NativeToolWindow.hpp"
#include "NativeAssetObservation.hpp"
#include <iomanip>
#include <sstream>

namespace ScreamSeq {
// Capture belongs to the session. Hiding this view stops its microphone, but
// leaves the stopped take available until an explicit Keep or Discard.
class SampleRecordingWindow final : public NativeToolWindow {
public:
  using Json=Api::Json;
  using Request=std::function<Json(const std::string &,const Json &)>;
  using Context=std::function<std::pair<std::string,std::string>()>;
  using Committed=std::function<void(const std::string &,const Json &)>;
private:
  enum:int {device=6501,channels,refresh,record,stop,keep,discard,name,output,close,discardSetup,reviewTake,acceptState,
    heading=6550,deviceLabel,channelLabel,nameLabel,outputLabel,permissionLabel,takeLabel,statusLabel,helpLabel};
  Request request_;Context context_;Committed committed_;
  NativeWriteCompletion::Write write_;NativeWriteCompletion completion_;NativeAssetObservation observation_;Json submitted_;
  std::string completionTake_;
  struct Lifecycle {std::string method,document,revision,take;Json params;};
  std::optional<Lifecycle> lifecycle_;
  Json devices_=Json::array(),takeState_=Json::object(),report_=Json::object();
  std::string selectedDevice_,document_,baseRevision_,take_,permission_;
  struct Input {unsigned first=0,count=1;};
  std::vector<Input> inputs_;Input input_;
  bool pending_=false,setting_=false,loaded_=false,draft_=false;uint64_t generation_=0;
  std::pair<std::string,std::string> draftContext_,pendingContext_;
  Json baseline_=Json::array();
  Json raw()const{return Json::array({utf8(field(name)),choice(output)});}
  std::optional<Tracker::DocumentDraft> documentDraft()const override {
    // Takes have their own session guard. Name/output intent belongs to a song;
    // idle endpoint/channel choices are global capture configuration.
    const auto captured=lifecycle_?std::pair(lifecycle_->document,lifecycle_->revision):draft_?draftContext_:pending_?pendingContext_:draftContext_;
    return describeDraft(captured.first,captured.second,Json::array({"sample-recording",take_,selectedDevice_,input_.first,input_.count}).dump(),generation_,draft_,pending_,!pending_&&(completion_.retained()||bool(lifecycle_)));
  }
  void requireResolved()const{require(!completion_.retained(),"Review the Keep result before changing the retained take or setup");require(!lifecycle_,"Review current take before another recording operation; the earlier operation will not be repeated");}
  void requireSetupDocument()const{require(!draft_||draftContext_.first==context_().first,"Sample setup belongs to the previous song / Discard setup before recording into another song");}
  int choice(int id)const{return int(SendMessageW(controls_.at(id),CB_GETCURSEL,0,0));}
  bool capturing()const{return !take_.empty()&&takeState_.value("capturing",false);}
  bool sameDocument()const{return take_.empty()||context_().first==document_;}
  unsigned inputChannels()const{for(const auto &d:devices_)if(d.at("id")==selectedDevice_)return d.at("channels").get<unsigned>();return 0;}
  bool inputAvailable()const{const auto count=inputChannels();return count<=64&&!selectedDevice_.empty()&&input_.first<count&&input_.count<=count-input_.first;}
  void require(bool ok,const char *why)const{if(!ok)throw std::runtime_error(why);}
  void status(std::wstring value){status_=std::move(value);set(statusLabel,status_);requestPaint();}
  void error(const std::exception &e)override{auto message=wide(e.what());if(lifecycle_)message+=L" / Review current take; the earlier operation will not be repeated";status(std::move(message));}
  void schedule(){if(visible()&&capturing()&&!lifecycle_)SetTimer(window_,3,150,nullptr);else KillTimer(window_,3);}
  template<typename F> void perform(F operation){
    require(!pending_,"A recording request is still running");pendingContext_=context_();pending_=true;layout();
    try{operation();}catch(...){pending_=false;layout();schedule();throw;}
    pending_=false;layout();schedule();requestPaint();
  }
  template<typename F> void lifecycleRequest(const std::string &method,Json params,F finish){
    requireResolved();const auto captured=context_();
    lifecycle_=Lifecycle{method,captured.first,captured.second,take_,std::move(params)};
    bool returned=false;
    try{auto result=request_(method,lifecycle_->params);returned=true;finish(result);lifecycle_.reset();}
    catch(const Api::ApiError &e){
      // A native completion callback cannot classify the original request.
      if(!returned&&e.outcome&&e.outcome->state==Tracker::CommitOutcome::NotCommitted)lifecycle_.reset();
      throw;
    }
  }
  void reviewLifecycle(){
    if(!lifecycle_||pending_)return;
    perform([&]{
      const auto current=request_("sample.recording.get",Json::object());
      const auto &identity=current.at("take");require(identity.is_null()||identity.is_string(),"Invalid take readback / review is still required");
      const auto currentTake=identity.is_null()?std::string():identity.get<std::string>();
      std::wstring text=L"Current take inspected / earlier recording operation was not repeated";
      if(currentTake.empty())text+=L" / the session holds no take";
      else if(!lifecycle_->take.empty()&&currentTake!=lifecycle_->take)text+=L" / a different take is now retained";
      else text+=L" / choose Stop, Keep or Discard for this observed take";
      // This adopts an observation, not a claim about the old operation's
      // success. A concurrent API client may have consumed or replaced it.
      accept(current,true);status(std::move(text));lifecycle_.reset();
    });
  }
  void deviceChoices(){
    setting_=true;ScreamSeq::NativeInputGate::present(controls_.at(device),CB_RESETCONTENT,0,0);int selected=-1;
    for(size_t i=0;i<devices_.size();++i){const auto &d=devices_[i];const auto text=wide(d.at("name").get<std::string>())+L" · "+std::to_wstring(d.at("channels").get<unsigned>())+L" ch";
      ScreamSeq::NativeInputGate::present(controls_.at(device),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));if(d.at("id")==selectedDevice_)selected=int(i);}
    if(selected<0&&!take_.empty()){
      const wchar_t *text=selectedDevice_.empty()?L"Default input used for this take":L"Recorded input is unavailable";
      selected=int(ScreamSeq::NativeInputGate::present(controls_.at(device),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text)));
    }
    ScreamSeq::NativeInputGate::present(controls_.at(device),CB_SETCURSEL,selected,0);setting_=false;
  }
  void inputChoices(bool fallback=false){
    // The native capture format accepts up to 64 source channels. Retain the
    // exact choice on refresh, even when that input temporarily disappears.
    const auto nativeChannels=inputChannels(),count=nativeChannels<=64?nativeChannels:0;inputs_.clear();
    for(unsigned first=0;first<count;++first)inputs_.push_back({first,1});
    for(unsigned first=0;first+1<count;++first)inputs_.push_back({first,2});
    auto selected=std::find_if(inputs_.begin(),inputs_.end(),[&](const auto &v){return v.first==input_.first&&v.count==input_.count;});
    if(selected==inputs_.end()&&fallback&&!inputs_.empty()){input_=inputs_.front();selected=inputs_.begin();}
    int index=selected==inputs_.end()?-1:int(selected-inputs_.begin());
    if(index<0&&!take_.empty()){index=int(inputs_.size());inputs_.push_back(input_);}
    setting_=true;ScreamSeq::NativeInputGate::present(controls_.at(channels),CB_RESETCONTENT,0,0);
    for(const auto &v:inputs_){const auto text=v.count==1?L"Mono / input "+std::to_wstring(v.first+1):L"Stereo / inputs "+std::to_wstring(v.first+1)+L"–"+std::to_wstring(v.first+2);
      ScreamSeq::NativeInputGate::present(controls_.at(channels),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));}
    ScreamSeq::NativeInputGate::present(controls_.at(channels),CB_SETCURSEL,index,0);setting_=false;
  }
  void clearTake(){take_.clear();takeState_=Json::object();baseRevision_.clear();document_.clear();deviceChoices();inputChoices();describe();}
  void describe(){
    set(permissionLabel,permission_=="authorized"?L"Microphone access allowed":permission_=="denied"?L"Microphone access denied":L"Access is checked when you choose Record");
    if(take_.empty()){set(takeLabel,L"No take / Record opens the selected input for up to 60 seconds");return;}
    std::wostringstream text;text<<std::fixed<<std::setprecision(2)
      <<(capturing()?L"Recording":L"Stopped")<<L" · "<<takeState_.value("seconds",0.0)<<L" / "<<takeState_.value("maxSeconds",60.0)<<L" s · "
      <<takeState_.value("frames",uint64_t(0))<<L" frames · "<<takeState_.value("sampleRate",0u)
      <<L" Hz · input "<<input_.first+1;if(input_.count==2)text<<L"–"<<input_.first+2;
    if(const auto clipped=takeState_.value("clipped",uint64_t(0)))text<<L" · "<<clipped<<L" clipped samples";
    if(takeState_.value("limitReached",false))text<<L" · recording limit reached";
    if(!sameDocument())text<<L"\nOriginal song is no longer open / take retained";
    set(takeLabel,text.str());
  }
  void accept(const Json &value,bool adopt=false){
    if(value.at("take").is_null()||value.at("take")==""){require(adopt,"Recording response has no take / retained take unchanged");clearTake();return;}
    const auto identity=value.at("take").get<std::string>();
    const auto document=value.at("documentId").get<std::string>(),revision=value.at("baseRevision").get<std::string>();
    const auto endpoint=value.at("device").get<std::string>();const Input input{value.at("firstChannel").get<unsigned>(),value.at("channels").get<unsigned>()};
    require(adopt||((take_.empty()||identity==take_)&&(document_.empty()||document==document_)),"Recording response belongs to another take / retained take unchanged");
    if(identity!=take_)report_=Json::object();
    take_=identity;document_=document;baseRevision_=revision;takeState_=value;permission_="authorized";
    if(adopt||selectedDevice_!=endpoint||input_.first!=input.first||input_.count!=input.count){selectedDevice_=endpoint;input_=input;deviceChoices();inputChoices();}
    describe();
    if(value.contains("error")&&value.at("error").is_string()&&!value.at("error").get<std::string>().empty())status(wide(value.at("error").get<std::string>()));
  }
  void loadDevices(){
    perform([&]{
      const auto result=request_("sample.recording.devices",Json::object());
      const auto next=result.at("devices");const auto permission=result.value("permission",std::string("unknown"));
      std::string selected=selectedDevice_;
      if(!loaded_&&take_.empty()){for(const auto &d:next)if(d.value("default",false)){selected=d.at("id").get<std::string>();break;}if(selected.empty()&&!next.empty())selected=next.front().at("id").get<std::string>();}
      devices_=next;permission_=permission;selectedDevice_=std::move(selected);loaded_=true;
      deviceChoices();inputChoices();describe();status(devices_.empty()?L"No input devices available / Refresh after connecting a microphone":inputChannels()==0?L"The selected input is unavailable / choose an input device":L"Ready / choose Record to open the microphone");
    });
  }
  void loadTake(){
    perform([&]{
      // An explicit opening reconciles API-created, committed or discarded
      // takes. Polls below remain identity-guarded once a take is known.
      const bool retained=!take_.empty();accept(request_("sample.recording.get",Json::object()),true);
      if(retained&&take_.empty())status(L"The session no longer holds that take / ready to record");
      if(!take_.empty()&&!takeState_.contains("error"))status(!sameDocument()?L"Take belongs to another song / Stop and Discard remain available":capturing()?L"Recording take restored / Stop retains it":L"Retained take restored / Keep adds it to this song");
    });
  }
  void begin(){
    requireResolved();
    requireSetupDocument();
    require(take_.empty(),"Keep or discard the retained take before recording again");
    require(inputAvailable(),"Choose an available input and channel range");
    const auto first=input_.first,count=input_.count;
    const auto captured=context_();const auto endpoint=selectedDevice_;
    perform([&]{
      lifecycleRequest("sample.recording.start",{{"device",endpoint},{"firstChannel",first},{"channels",count},{"maxSeconds",60},{"expectedRevision",captured.second}},[&](const Json &result){
      // A document switch while the worker pumps messages must never adopt
      // the new song as the destination of this take.
      document_=captured.first;baseRevision_=captured.second;permission_="authorized";report_=Json::object();accept(result);
      if(!result.contains("error"))status(L"Recording / Stop retains the take / Close also stops the microphone");
      });
    });
  }
  void end(){requireResolved();require(!take_.empty(),"There is no retained take");perform([&]{lifecycleRequest("sample.recording.stop",{{"take",take_}},[&](const Json &result){accept(result);if(!takeState_.contains("error"))status(L"Take stopped / Keep adds a new sample with one Undo");});});}
  void commit(){
    if(completion_.retained()){reviewCommit();return;}
    requireResolved();
    requireSetupDocument();
    require(!take_.empty()&&!capturing(),"Stop the take before keeping it");
    require(takeState_.value("frames",uint64_t(0))>0,"The take contains no audio frames");
    const auto target=context_();require(target.first==document_,"Original song is no longer open / take retained");
    const auto identity=take_,title=utf8(field(name));const bool instrument=choice(output)==1;const auto generation=generation_;const auto fields=raw();
    require(!title.empty()&&title.size()<=128,"Enter a sample name using 1–128 UTF-8 bytes");
    perform([&]{
      completionTake_=identity;observation_.clear();submitted_={{"take",identity},{"name",title},{"createInstrument",instrument},{"expectedRevision",target.second},{"dryRun",false}};
      completion_.submit(write_,"sample.recording.commit",submitted_,target.first,generation,fields);
      finishCommit();
    });
  }
  void finishCommit(){
    const auto returned=completion_.returned();
    require(bool(returned),"Keep outcome is still unknown / the take cannot be kept again until the operation is reconciled");
    const auto &result=returned->result;
    require(result.at("take")==completionTake_,"Keep result belongs to another take / retained result needs review");
    auto text=L"Created sample "+std::to_wstring(result.at("sample").get<unsigned>());
    if(const auto instrument=result.value("instrument",0u))text+=L" + mapped instrument "+std::to_wstring(instrument);
    // A new take may have been created externally after this Keep. Read it,
    // never clear/discard it to complete presentation of the original result.
    const auto current=request_("sample.recording.get",Json::object());
    require(current.at("take")!=completionTake_,"Committed result still has its original take / retained result needs review");
    const auto now=context_();
    if(now.first==returned->document&&now.second==returned->revision)committed_(returned->document,result);
    else text+=L" / song changed since completion; selection left unchanged";
    accept(current,true);report_=result;
    baseline_=completion_.fields();draft_=generation_!=completion_.generation();
    status(text+L" / result reviewed without repeating Keep");completion_.finish();completionTake_.clear();
  }
  void reviewCommit(){perform([&]{request_("synchronizeView",Json::object());if(completion_.returned())finishCommit();else{
    observation_.read(request_,context_,[this]{return generation_;},completion_.snapshot().at("documentId").get<std::string>(),Json{{"completion",completion_.snapshot()},{"params",submitted_}},true);
    status(L"Current assets and take inspected / earlier Keep unverified; Accept state acknowledges without keeping again");
  }});}
  void acknowledgeKeep(){
    require(completion_.retained(),"No Keep result to acknowledge");perform([&]{
      observation_.check(context_(),generation_);auto current=request_("sample.recording.get",Json::object());observation_.checkTake(current);observation_.check(context_(),generation_);
      auto report=observation_.report();accept(current,true);report_=std::move(report);
      status(L"Unverified Keep acknowledged / current take and raw setup retained / no write repeated");completion_.finish();completionTake_.clear();observation_.clear();
    });
  }
  void discardTake(){requireResolved();require(!take_.empty(),"There is no retained take");perform([&]{lifecycleRequest("sample.recording.discard",{{"take",take_}},[&](const Json &){clearTake();status(L"Take discarded / ready to record");});});}
  void action(int id,unsigned notification)override{
    if(setting_)return;
    if((id==name&&notification==EN_CHANGE)||(id==output&&notification==CBN_SELCHANGE)){
      if(!draft_)draftContext_=pending_?pendingContext_:context_();++generation_;draft_=raw()!=baseline_;return;
    }
    if((id==device||id==channels)&&notification==CBN_SELCHANGE&&(pending_||lifecycle_||!take_.empty())){deviceChoices();inputChoices();return;}
    if(pending_)return;
    if(id==device&&notification==CBN_SELCHANGE){const auto index=choice(device);if(index>=0&&size_t(index)<devices_.size()){selectedDevice_=devices_[size_t(index)].at("id").get<std::string>();inputChoices(true);}return;}
    if(id==channels&&notification==CBN_SELCHANGE){const auto index=choice(channels);if(index>=0&&size_t(index)<inputs_.size())input_=inputs_[size_t(index)];return;}
    if(notification!=BN_CLICKED)return;
    if(id==discardSetup){requireResolved();setting_=true;set(name,L"Recording");ScreamSeq::NativeInputGate::present(controls_.at(output),CB_SETCURSEL,0,0);setting_=false;baseline_=raw();draft_=false;draftContext_=context_();++generation_;status(L"Sample name and output reset / retained take unchanged");return;}
    if(id==acceptState){acknowledgeKeep();return;}
    if(id==reviewTake)reviewLifecycle();else if(id==refresh){requireResolved();loadDevices();}else if(id==record)begin();else if(id==stop)end();else if(id==keep)commit();else if(id==discard)discardTake();else if(id==close)hide();
  }
  bool key(WPARAM value,bool,bool)override{
    if(value==VK_ESCAPE){hide();return true;}
    if(value==VK_RETURN){wchar_t type[32]{};GetClassNameW(GetFocus(),type,32);if(_wcsicmp(type,L"Button")==0&&IsWindowEnabled(GetFocus()))action(GetDlgCtrlID(GetFocus()),BN_CLICKED);return true;}
    return false;
  }
  void timer(UINT_PTR id)override{
    if(id!=3)return;KillTimer(window_,3);if(!visible()||take_.empty()||completion_.retained()||lifecycle_)return;if(pending_){schedule();return;}
    try{perform([&]{accept(request_("sample.recording.get",{{"take",take_}}));});}
    catch(const Api::ApiError &e){
      if(e.code==-32001){try{loadTake();}catch(const Api::ApiError &next){if(next.code!=-32002)error(next);}catch(const std::exception &next){error(next);}}
      else if(e.code!=-32002)error(e);schedule();
    }
    catch(const std::exception &e){error(e);schedule();}
  }
  void layout()override{
    if(!ready_)return;const auto [w,h]=size();
    place(heading,18,12,w-186,24);place(discardSetup,w-158,12,140,26);place(deviceLabel,18,46,w-36,20);place(device,18,70,w-146,240);place(refresh,w-118,70,100,27);
    place(channelLabel,18,112,150,20);place(channels,18,136,210,180);place(permissionLabel,242,139,w-260,24);
    place(nameLabel,18,180,90,20);place(name,18,204,w-272,27);place(outputLabel,w-240,180,222,20);place(output,w-240,204,222,180);
    place(takeLabel,18,250,w-36,50);place(helpLabel,18,332,w-36,46);place(statusLabel,18,388,w-36,std::max(48.f,h-446));
    place(acceptState,206,302,204,26,completion_.retained());EnableWindow(controls_.at(acceptState),!pending_&&observation_.ready());
    place(reviewTake,18,302,180,26);ShowWindow(controls_.at(reviewTake),lifecycle_?SW_SHOWNA:SW_HIDE);EnableWindow(controls_.at(reviewTake),!pending_&&bool(lifecycle_));
    place(record,18,h-46,92,28);place(stop,118,h-46,80,28);place(keep,206,h-46,110,28);place(discard,324,h-46,100,28);place(close,w-118,h-46,100,28);
    for(int id:{device,channels,refresh})EnableWindow(controls_.at(id),!pending_&&!completion_.retained()&&!lifecycle_&&take_.empty());
    EnableWindow(controls_.at(record),!pending_&&!completion_.retained()&&!lifecycle_&&take_.empty()&&inputAvailable());
    EnableWindow(controls_.at(stop),!pending_&&!lifecycle_&&capturing());
    set(keep,completion_.retained()?L"Review result":L"Keep take");
    EnableWindow(controls_.at(keep),!pending_&&!lifecycle_&&(completion_.retained()||(!take_.empty()&&!capturing()&&sameDocument()&&takeState_.value("frames",uint64_t(0))>0)));
    EnableWindow(controls_.at(discard),!pending_&&!completion_.retained()&&!lifecycle_&&!take_.empty());
    for(int id:{name,output,close,discardSetup})EnableWindow(controls_.at(id),!pending_);
    EnableWindow(controls_.at(discardSetup),!pending_&&!completion_.retained()&&!lifecycle_);
  }
  void paint(RenderSurface &s)override{
    const auto [w,h]=size();s.fill(0,0,w,h,0x18222d);s.fill(18,310,w-36,12,0x0c141c);
    const auto peak=std::clamp(takeState_.value("peak",0.0),0.0,1.0);s.fill(18,310,float(peak)*(w-36),12,takeState_.value("clipped",uint64_t(0))>0?0xe27a73:0x72dcc6);
  }
public:
  SampleRecordingWindow(HWND owner,Request request,Context context,Committed committed,NativeWriteCompletion::Write write):NativeToolWindow(owner),request_(std::move(request)),context_(std::move(context)),committed_(std::move(committed)),write_(std::move(write)){
    minimumWidth_=600;minimumHeight_=530;create(L"ScreamSeq.SampleRecording",L"Record a sample",660,560);button(acceptState,L"Accept observed state");
    for(auto [id,text]:std::initializer_list<std::pair<int,const wchar_t *>>{{heading,L"RECORD A SAMPLE"},{deviceLabel,L"Input device"},{channelLabel,L"Input channels"},{nameLabel,L"Sample name"},{outputLabel,L"Keep as"},{permissionLabel,L""},{takeLabel,L""},{statusLabel,L""},{helpLabel,L"Record opens the selected microphone. No input monitoring.\nStop or Close retains the take; Keep adds it to the original song."}})label(id,text);
    for(auto [id,text]:std::initializer_list<std::pair<int,const wchar_t *>>{{refresh,L"Refresh"},{record,L"Record"},{stop,L"Stop"},{keep,L"Keep take"},{discard,L"Discard take"},{close,L"Close"},{discardSetup,L"Discard setup"},{reviewTake,L"Review current take"}})button(id,text);
    combo(device);combo(channels);combo(output);edit(name,L"Recording",128);
    for(const auto text:{L"Sample",L"Sample + mapped instrument"})ScreamSeq::NativeInputGate::present(controls_.at(output),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text));
    ScreamSeq::NativeInputGate::present(controls_.at(output),CB_SETCURSEL,0,0);baseline_=raw();draftContext_=context_();finish();
  }
  ~SampleRecordingWindow()override{ready_=false;if(window_)KillTimer(window_,3);}
  bool hasRetainedTake()const{return !take_.empty();}
  void protectTake(const Json &value){require(!pending_,"Wait for the recording request before leaving this song");requireResolved();accept(value,true);protectTake();}
  void protectTake(){show();status(capturing()?L"Stop, then Keep or Discard this take before leaving the song":L"Keep or Discard this take before leaving the song");layout();schedule();}
  void documentChanged(){if(ready_&&!pending_){describe();layout();requestPaint();}}
  void openAt(){show();if(pending_)return;if(lifecycle_){status(L"Review current take before another operation / the microphone may still be active");layout();return;}if(completion_.retained()){status(L"Review the retained Keep result before recording again");layout();return;}if(!loaded_)try{loadDevices();}catch(const std::exception &e){error(e);}try{loadTake();}catch(const std::exception &e){error(e);}describe();layout();schedule();}
  void hide()override{
    if(pending_){status(L"Wait for the recording request to finish before closing");return;}
    require(!lifecycle_,"Review current take before closing / the microphone may still be active");
    if(capturing())end();KillTimer(window_,3);NativeToolWindow::hide();
  }
  Json snapshot()const{Json inputs=Json::array();for(const auto &v:inputs_)inputs.push_back({{"firstChannel",v.first},{"channels",v.count}});return {{"visible",visible()},{"pending",pending_},{"completion",completion_.snapshot()},{"observation",observation_.report()},{"lifecycleReview",lifecycle_?Json{{"method",lifecycle_->method},{"documentId",lifecycle_->document},{"revision",lifecycle_->revision},{"take",lifecycle_->take},{"params",lifecycle_->params}}:Json()},{"draft",draft_},{"draftDocument",draftContext_.first},{"draftRevision",draftContext_.second},{"generation",generation_},{"document",document_},{"baseRevision",baseRevision_},{"take",take_},{"capturing",capturing()},{"staleDocument",!sameDocument()},{"device",selectedDevice_},{"devices",devices_},{"permission",permission_},{"firstChannel",input_.first},{"channels",input_.count},{"inputChoices",inputs},{"name",utf8(field(name))},{"createInstrument",choice(output)==1},{"state",takeState_},{"report",report_},{"status",utf8(status_)}};}
};
}
