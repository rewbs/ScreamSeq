// Real native review controls; worker/timing/state publication has its separate
// native lifecycle fixture. This fixture controls receipts and read failures.
void applicationParameterRecordingReview() {
    using Json=RestoreJson;using Call=ScreamSeq::Api::CompletedCall;
    RestoreFixture fixture;auto &app=*fixture.app;const auto plugin=addRestoreGain(app);const auto music=songState(app);
    Json take={{"retained",false},{"capturing",false},{"take",""}},delivery={{"retained",0},{"pending",false},{"needsReview",false},{"lost",0},{"events",Json::array()}};
    std::vector<std::pair<std::string,Json>> writes;unsigned saves=0,reconciled=0,nameReads=0;bool failAfterKeep=false;
    bool deferKeepReceipt=false,failReconcile=false,refuseStart=false,failAfterPlaybackStop=false;
    unsigned playbackReconciled=0;
    std::function<void()> duringRead;
    std::string contextRevision;
    bool available=true;
    std::shared_ptr<ScreamSeq::NativeCallReceipt> lateReceipt;
    std::shared_ptr<const Call> lateCompletion;
    ScreamSeq::ParameterRecordingWindow::Callbacks callbacks;
    callbacks.context=[&]{return std::pair(app.documentId,contextRevision.empty()?app.view->session.revision:contextRevision);};
    callbacks.rack=[&]{return app.view->session.document.at("nativePlugins");};
    callbacks.parameterName=[&](const std::string &id,uint32_t parameter){restoreCheck(id==plugin&&parameter==1,"Review looked up another parameter name");++nameReads;return L"Gain";};
    callbacks.available=[&]{return available;};callbacks.saveRecovery=[&]{++saves;};
    callbacks.reconcile=[&](const Call &call){if(call.method=="transport.stop"){
      restoreCheck(call.result.at("playing")==false,"Review accepted an invalid playback Stop receipt");++playbackReconciled;return;}
      restoreCheck(call.method=="automation.recording.commit"&&call.result.at("retained")==false,
      "Native review reconciled the wrong completion");
      if(std::exchange(failReconcile,false))throw std::runtime_error("Owned readback failure");++reconciled;};
    callbacks.delivery=[&](size_t offset,size_t limit){auto page=delivery;page["events"]=Json::array();
      for(size_t i=offset;i<std::min(delivery.at("events").size(),offset+limit);++i)page["events"].push_back(delivery.at("events")[i]);return page;};
    callbacks.read=[&](const std::string &method,const Json &params)->Json {
      if(method=="automation.recording.get"){
        if(auto hook=std::exchange(duringRead,{}))hook();
        return take;
      }
      restoreCheck(method=="automation.recording.events"&&params.at("take")==take.at("take")&&params.at("plugin")==plugin&&params.at("parameter")==1,
        "Review read a different recording take or target");
      Json rows=Json::array();const auto offset=params.at("offset").get<size_t>(),limit=params.at("limit").get<size_t>();
      for(size_t i=offset;i<std::min(size_t(130),offset+limit);++i)rows.push_back({{"timestamp",std::to_string(9007199254740993ull+i)},{"value",double(i)},
        {"frame",i==129?Json():Json(std::to_string(i*48))}});
      return {{"events",rows},{"total",130}};
    };
    callbacks.write=[&](const std::string &method,const Json &params,const std::shared_ptr<ScreamSeq::NativeCallReceipt> &receipt){
      restoreCheck(params.at("expectedRevision")==app.view->session.revision,"Recording UI dropped its revision guard");writes.emplace_back(method,params);
      if(method=="automation.recording.start") {
        restoreCheck(params.at("plugins")==Json::array({plugin}),"Recording scope silently selected another plugin");
        if(refuseStart)throw ScreamSeq::Api::ApiError(-32002,"Playback timing is not available yet",
          Tracker::WriteOutcome{Tracker::CommitOutcome::NotCommitted});
        take={{"retained",true},{"capturing",true},{"take","review-owned-take"},{"compatible",true},{"eventCount",130},{"failure",0},
          {"targets",Json::array({{{"plugin",plugin},{"parameter",1},{"unmapped",1},{"overflow",0}}})},{"sources",Json::array()}};
      }else if(method=="transport.stop"){
        restoreCheck(!params.contains("take")&&params.size()==1,"Playback Stop leaked recording-only arguments");
        take["transportPlaying"]=false;take["discardable"]=true;
      }else {
        restoreCheck(params.at("take")==take.at("take"),"Recording action targeted a different retained take");
        if(method=="automation.recording.stop")take["capturing"]=false;
        else if(method=="automation.recording.commit"){
          restoreCheck(params.at("acknowledgeLoss")==true,"Keep recording failed to pass explicit loss acknowledgement");take={{"retained",false},{"capturing",false},{"take",""}};
        }else if(method=="automation.recording.discard")take={{"retained",false},{"capturing",false},{"take",""}};
        else throw std::runtime_error("Unexpected review write");
      }
      const Call result{method,app.documentId,app.view->session.revision,method=="transport.stop"?Json{{"playing",false}}:take};
      if(method=="automation.recording.commit"&&std::exchange(deferKeepReceipt,false)){
        lateReceipt=receipt;lateCompletion=std::make_shared<const Call>(result);
        throw std::runtime_error("Owned missing native completion");
      }
      receipt->publish(std::make_shared<const Call>(result));
      if(method=="transport.stop"&&std::exchange(failAfterPlaybackStop,false))throw std::runtime_error("Owned post-Stop presentation failure");
      if(method=="automation.recording.commit"&&std::exchange(failAfterKeep,false))throw std::runtime_error("Owned post-commit presentation failure");return result;
    };
    ScreamSeq::ParameterRecordingWindow tool(app.window,std::move(callbacks));tool.show();const auto window=tool.window();ScreamSeq::Tests::ownGuiWindow(window);
    const auto control=[&](int id){return GetDlgItem(window,id);};const auto click=[&](int id){SendMessageW(window,WM_COMMAND,MAKEWPARAM(id,BN_CLICKED),reinterpret_cast<LPARAM>(control(id)));};
    restoreCheck(GetNextDlgTabItem(window,control(9720),FALSE)==control(9721),"Recording scope and Record are not adjacent in Tab order");
    SendMessageW(control(9720),CB_SETCURSEL,1,0);click(9721);
    restoreCheck(writes.size()==1&&tool.snapshot().at("take").at("capturing")==true&&IsWindowEnabled(control(9722))&&!IsWindowEnabled(control(9723)),
        "Record did not enter the retained capture state or allowed Keep while capturing");
    restoreCheck(!tool.snapshot().at("target").get<std::string>().empty()&&tool.snapshot().at("events").size()==128,
        "New captured music stayed hidden behind an empty native delivery page");
    restoreCheck(tool.snapshot().at("inputDetails").get<std::string>().find("Gain")!=std::string::npos&&
      tool.snapshot().at("inputDetails").get<std::string>().find("Original host time: 9007199254740993")!=std::string::npos&&
      (GetWindowLongPtrW(control(9739),GWL_STYLE)&ES_READONLY)&&
      GetNextDlgTabItem(window,control(9732),FALSE)==control(9739),
      "Recording details are not readable, read-only or adjacent to the list in Tab order");
    const std::string refusal="Disable this parameter's pattern automation before recording absolute automation. The original input remains available for review.";
    // Use the live producer's wire shape, not a hand-written recovery archive:
    // the two intentionally have different timestamp field names.
    ScreamSeq::ParameterControlDelivery originalInput;
    const auto retainFailure=[&](double value,uint64_t timestamp,bool refused) {
      bool caught=false;
      try {
        originalInput.enqueue({app.documentId,app.view->session.revision,take.at("take").get<std::string>(),plugin,1,value,timestamp},
          [&](const Json &,const std::shared_ptr<ScreamSeq::NativeCallReceipt> &)->std::future<Call> {
            if(refused)throw ScreamSeq::Api::ApiError(-32002,refusal,Tracker::WriteOutcome{Tracker::CommitOutcome::NotCommitted});
            throw std::runtime_error("Original delivery is uncertain; inspect current state before retrying.");
          });
      }catch(const ScreamSeq::Api::ApiError &){caught=true;restoreCheck(refused,"Unknown submission became a known refusal");}
       catch(const std::runtime_error &){caught=true;restoreCheck(!refused,"Known refusal lost its outcome");}
      restoreCheck(caught,"Original input fixture unexpectedly accepted a failed submission");
    };
    retainFailure(-6.5,9007199254740993ull,true);retainFailure(-12.5,9007199254740994ull,false);
    delivery=originalInput.snapshot();
    restoreCheck(delivery.at("retained")==2&&delivery.at("needsReview")==true&&
      delivery.at("events")[0].at("state")=="refused"&&delivery.at("events")[1].at("state")=="unknown",
      "Original input fixture did not retain both delivery outcomes");
    SendMessageW(control(9728),CB_SETCURSEL,0,0);SendMessageW(window,WM_COMMAND,MAKEWPARAM(9728,CBN_SELCHANGE),reinterpret_cast<LPARAM>(control(9728)));
    restoreCheck(tool.snapshot().at("inputDetails").get<std::string>().find(refusal)!=std::string::npos&&
      tool.snapshot().at("inputDetails").get<std::string>().find("Original host time: 9007199254740993")!=std::string::npos,
      "Recording review truncated the actionable refusal reason");
    SetFocus(control(9732));SendMessageW(control(9732),WM_KEYDOWN,VK_DOWN,0);
    restoreCheck(GetFocus()==control(9732)&&tool.snapshot().at("inputDetails").get<std::string>().find("Original delivery is uncertain")!=std::string::npos&&
      tool.snapshot().at("inputDetails").get<std::string>().find("Original host time: 9007199254740994")!=std::string::npos,
      "Arrow-key selection did not update the complete recording input details");
    SendMessageW(control(9732),WM_KEYDOWN,VK_UP,0);
    SetFocus(control(9739));SendMessageW(control(9739),EM_SETSEL,2,9);
    SendMessageW(window,WM_TIMER,7,0);restoreCheck(tool.snapshot().at("target")=="",
        "Recording poll redirected an explicit native delivery review choice");
    DWORD first=0,last=0;SendMessageW(control(9739),EM_GETSEL,reinterpret_cast<WPARAM>(&first),reinterpret_cast<LPARAM>(&last));
    restoreCheck(GetFocus()==control(9739)&&first==2&&last==9,
      "Recording poll stole details focus or reset text selected for copying");
    SendMessageW(control(9739),WM_CHAR,L'x',0);
    restoreCheck(tool.snapshot().at("inputDetails").get<std::string>().find(refusal)!=std::string::npos,
      "Typing changed the read-only recording evidence");
    delivery["events"]=Json::array();delivery["retained"]=0;delivery["needsReview"]=false;
    SendMessageW(control(9728),CB_SETCURSEL,1,0);SendMessageW(window,WM_COMMAND,MAKEWPARAM(9728,CBN_SELCHANGE),reinterpret_cast<LPARAM>(control(9728)));
    restoreCheck(tool.snapshot().at("events").size()==128&&tool.snapshot().at("events")[0].at("timestamp")=="9007199254740993",
        "Native review changed exact timestamps or failed bounded pagination");click(9730);
    restoreCheck(tool.snapshot().at("offset")==128&&tool.snapshot().at("events").size()==2&&tool.snapshot().at("events")[1].at("frame").is_null(),
        "Second page hid unmapped events or reused first-page offsets");
    SetFocus(control(9732));const auto focus=GetFocus();SendMessageW(window,WM_TIMER,7,0);
    restoreCheck(GetFocus()==focus&&tool.snapshot().at("offset")==128&&nameReads==1,"Live recording refresh stole focus, reset its page or repeatedly queried parameter metadata");
    tool.hide();restoreCheck(take.at("capturing")==true&&writes.size()==1,"Closing review stopped or discarded the recording");tool.show();click(9722);
    restoreCheck(writes.size()==2&&!IsWindowEnabled(control(9723))&&IsWindowEnabled(control(9731)),"Stopped lossy take did not require explicit review");
    click(9726);restoreCheck(saves==1&&writes.size()==2,"Save recovery copy changed the recording take");
    SendMessageW(control(9731),BM_SETCHECK,BST_CHECKED,0);click(9731);restoreCheck(IsWindowEnabled(control(9723)),"Explicit loss acknowledgement did not enable Keep");
    take["targets"][0]["unmapped"]=2;SendMessageW(window,WM_TIMER,7,0);
    restoreCheck(!tool.snapshot().at("acknowledgeLoss").get<bool>()&&!IsWindowEnabled(control(9723)),"New loss reused acknowledgement of an earlier review");
    SendMessageW(control(9731),BM_SETCHECK,BST_CHECKED,0);click(9731);
    take["completed"]=true;SendMessageW(window,WM_TIMER,7,0);
    restoreCheck(!IsWindowEnabled(control(9724))&&restoreText(control(9723))==L"Finish keeping",
        "Already committed take offered Discard as if it could undo accepted music");
    take["completed"]=false;SendMessageW(window,WM_TIMER,7,0);
    failAfterKeep=true;click(9723);restoreCheck(writes.size()==3&&!tool.snapshot().at("completion").is_null(),"Post-commit error lost its original receipt");
    click(9723);restoreCheck(writes.size()==3,"Retry duplicated an uncertain recording commit");click(9725);
    restoreCheck(writes.size()==3&&reconciled==1&&tool.snapshot().at("completion").is_null()&&tool.snapshot().at("take").at("retained")==false,
        "Review resent a completed action or retained the old take after receipt reconciliation");
    take={{"retained",true},{"capturing",true},{"take","failed-clock-take"},{"compatible",false},{"completed",false},
      {"transportPlaying",true},{"discardable",false},{"targets",Json::array()},{"sources",Json::array()}};
    SendMessageW(window,WM_TIMER,7,0);
    restoreCheck(!IsWindowEnabled(control(9723))&&!IsWindowEnabled(control(9724))&&IsWindowEnabled(control(9740))&&IsWindowVisible(control(9740))&&
      restoreText(control(9734)).find(L"stop playback")!=std::wstring::npos,
      "Failed live clock still claimed to record or hid the safe recovery action");
    MINMAXINFO minimum{};SendMessageW(window,WM_GETMINMAXINFO,0,reinterpret_cast<LPARAM>(&minimum));
    restoreCheck(SetWindowPos(window,nullptr,0,0,minimum.ptMinTrackSize.x,minimum.ptMinTrackSize.y,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE),"Resize compact recording recovery");
    RECT client{},previousBounds{};GetClientRect(window,&client);
    for(int id:{9721,9722,9740}) {
      RECT bounds{};GetWindowRect(control(id),&bounds);MapWindowPoints(nullptr,window,reinterpret_cast<POINT *>(&bounds),2);
      restoreCheck(bounds.left>=previousBounds.right&&bounds.right<=client.right&&bounds.top>=0&&bounds.bottom<=client.bottom,
        "Compact recording and playback buttons overlap or leave the client");previousBounds=bounds;
    }
    restoreCheck(GetNextDlgTabItem(window,control(9722),FALSE)==control(9740),"Playback recovery is missing from adjacent keyboard navigation");
    // A stop arriving during a polling read is retained once; completion may
    // only stop playback, never implicitly Keep or Discard the original input.
    const auto beforePlaybackStop=writes.size();
    duringRead=[&]{click(9740);click(9740);restoreCheck(writes.size()==beforePlaybackStop,
      "Playback Stop reentered the worker during a recording refresh");};
    SendMessageW(window,WM_TIMER,7,0);
    restoreCheck(tool.snapshot().at("queuedAction")=="transport.stop","Refresh lost explicit playback Stop");
    failAfterPlaybackStop=true;SendMessageW(window,WM_TIMER,8,0);SendMessageW(window,WM_TIMER,8,0);
    restoreCheck(writes.size()==beforePlaybackStop+1&&!tool.snapshot().at("completion").is_null()&&
      !IsWindowEnabled(control(9740))&&tool.protectsClose(),"Uncertain playback Stop lost its receipt or allowed an immediate retry");
    click(9740);click(9724);restoreCheck(writes.size()==beforePlaybackStop+1,"Uncertain playback Stop allowed a second write");
    SetFocus(control(9725));click(9725);
    restoreCheck(playbackReconciled==1&&tool.snapshot().at("completion").is_null()&&GetFocus()==control(9725),
      "Playback Stop review did not reconcile its receipt while preserving keyboard focus");
    restoreCheck(writes.size()==beforePlaybackStop+1&&writes.back().first=="transport.stop"&&take.at("retained")==true&&
      take.at("capturing")==true&&take.at("take")=="failed-clock-take"&&tool.protectsClose()&&
      IsWindowEnabled(control(9724))&&!IsWindowEnabled(control(9723))&&!IsWindowEnabled(control(9740))&&
      restoreText(control(9734)).find(L"Playback stopped")!=std::wstring::npos,
      "Explicit playback Stop lost the failed take, duplicated a write or failed to offer safe Discard");
    click(9740);restoreCheck(writes.size()==beforePlaybackStop+1,"Disabled playback Stop submitted again");
    // Eligibility can change without changing the take or musical revision.
    take["transportPlaying"]=true;take["discardable"]=false;SendMessageW(window,WM_TIMER,7,0);
    duringRead=[&]{click(9740);take["transportPlaying"]=false;take["discardable"]=true;};
    SendMessageW(window,WM_TIMER,7,0);SendMessageW(window,WM_TIMER,8,0);
    restoreCheck(writes.size()==beforePlaybackStop+1&&tool.snapshot().at("queuedAction").is_null()&&
      tool.snapshot().at("error").get<std::string>().find("Playback changed during refresh")!=std::string::npos,
      "Queued playback Stop ignored changed transport eligibility");
    click(9724);restoreCheck(writes.size()==5&&writes.back().first=="automation.recording.discard"&&
      writes.back().second.at("take")=="failed-clock-take"&&!tool.protectsClose(),
      "Discard of a stopped failed-context take lost its identity or retained a false close barrier");
    // A read of the present take is not a receipt for an earlier write. Even
    // when Keep made it empty, retain the original callback and block repeats.
    click(9721);click(9722);SendMessageW(control(9731),BM_SETCHECK,BST_CHECKED,0);click(9731);
    deferKeepReceipt=true;click(9723);const auto submitted=writes.size();
    restoreCheck(lateReceipt&&lateCompletion&&!tool.snapshot().at("completion").at("returned").get<bool>(),
      "Unclassified completion failure failed to retain its unresolved receipt");
    const auto unresolved=tool.snapshot();
    tool.observeBoundary(app.documentId,"automation.recording.discard",{{"take","review-owned-take"},{"retained",false}});
    restoreCheck(tool.snapshot()==unresolved&&tool.protectsClose(),
      "External take completion acknowledged this window's unresolved write");
    SetFocus(control(9725));click(9725);
    restoreCheck(!tool.snapshot().at("take").at("retained").get<bool>()&&tool.protectsClose()&&
      !tool.snapshot().at("completion").is_null()&&!IsWindowEnabled(control(9721))&&GetFocus()==control(9725),
      "Empty readback discarded unresolved Keep or re-enabled Record/stole review focus");
    click(9721);click(9723);restoreCheck(writes.size()==submitted,"Unresolved review allowed a second write");
    auto foreign=*lateCompletion;foreign.document="another song";lateReceipt->publish(std::make_shared<const Call>(foreign));click(9725);
    restoreCheck(tool.protectsClose()&&!tool.snapshot().at("completion").at("returned").get<bool>()&&reconciled==1,
      "Foreign receipt released the retained take completion");
    lateReceipt->publish(lateCompletion);failReconcile=true;click(9725);
    restoreCheck(tool.protectsClose()&&!tool.snapshot().at("completion").is_null()&&reconciled==1,
      "Failed receipt reconciliation discarded the original operation");
    click(9725);
    restoreCheck(!tool.protectsClose()&&tool.snapshot().at("completion").is_null()&&IsWindowEnabled(control(9721))&&
      writes.size()==submitted&&reconciled==2&&GetFocus()==control(9725),
      "Late matching receipt did not release review exactly once without a repeated write");
    restoreCheck(songState(app)==music,"Native review controls changed unrelated musical state");
    const auto beforeRefusal=writes.size();refuseStart=true;SetFocus(control(9721));click(9721);
    restoreCheck(writes.size()==beforeRefusal+1&&!tool.protectsClose()&&
      tool.snapshot().at("completion").is_null()&&!tool.snapshot().at("take").at("retained").get<bool>()&&
      IsWindowEnabled(control(9721))&&GetFocus()==control(9721),
      "Typed start refusal blocked Record/close or lost keyboard focus");
    click(9725);
    restoreCheck(writes.size()==beforeRefusal+1&&!tool.protectsClose(),"Review retried a rejected start or protected an absent take");
    refuseStart=false;click(9721);
    restoreCheck(writes.size()==beforeRefusal+2&&tool.snapshot().at("take").at("capturing").get<bool>(),
      "Explicit Record after a pure refusal could not start a fresh take");
    click(9722);click(9724);
    restoreCheck(!tool.protectsClose()&&songState(app)==music,"Refusal/retry left native protection or changed music");
    // Background reads pump native messages. A real Stop click during that
    // read must survive exactly once, and never target a changed take/revision.
    click(9721);const auto beforeQueuedStop=writes.size();
    duringRead=[&]{click(9722);click(9722);restoreCheck(writes.size()==beforeQueuedStop,
      "Recording Stop reentered the worker during a background read");};
    SendMessageW(window,WM_TIMER,7,0);
    restoreCheck(tool.snapshot().at("queuedAction")=="automation.recording.stop"&&tool.protectsClose(),
      "Background refresh lost the original Stop intent");
    available=false;SendMessageW(window,WM_TIMER,8,0);
    restoreCheck(writes.size()==beforeQueuedStop&&!tool.snapshot().at("queuedAction").is_null(),
      "Another editor's worker read dropped or prematurely submitted the retained Stop");available=true;
    SendMessageW(window,WM_TIMER,8,0);SendMessageW(window,WM_TIMER,8,0);
    restoreCheck(writes.size()==beforeQueuedStop+1&&!take.at("capturing").get<bool>()&&tool.snapshot().at("queuedAction").is_null(),
      "Queued Stop was lost or submitted more than once");click(9724);
    click(9721);const auto beforeChangedTake=writes.size();
    duringRead=[&]{click(9722);take["take"]="replacement-during-read";};
    SendMessageW(window,WM_TIMER,7,0);SendMessageW(window,WM_TIMER,8,0);
    restoreCheck(writes.size()==beforeChangedTake&&take.at("capturing").get<bool>()&&
      tool.snapshot().at("error").get<std::string>().find("changed during refresh")!=std::string::npos,
      "Deferred Stop retargeted a different recording");click(9722);click(9724);
    click(9721);const auto beforeChangedRevision=writes.size();
    duringRead=[&]{click(9722);contextRevision="newer-song-revision";};
    SendMessageW(window,WM_TIMER,7,0);SendMessageW(window,WM_TIMER,8,0);
    restoreCheck(writes.size()==beforeChangedRevision&&take.at("capturing").get<bool>(),
      "Deferred recording action silently rebased to a later song revision");
    contextRevision.clear();click(9722);click(9724);
    click(9721);const auto beforeFailedRefresh=writes.size();
    duringRead=[&]{click(9722);throw std::runtime_error("Owned interrupted recording refresh");};
    SendMessageW(window,WM_TIMER,7,0);SendMessageW(window,WM_TIMER,8,0);
    restoreCheck(writes.size()==beforeFailedRefresh&&tool.snapshot().at("queuedAction").is_null()&&take.at("capturing").get<bool>(),
      "Failed read authorized a deferred recording write");click(9722);click(9724);
    click(9721);click(9722);SendMessageW(control(9731),BM_SETCHECK,BST_CHECKED,0);click(9731);
    const auto beforeNewLoss=writes.size();
    duringRead=[&]{click(9723);take["targets"][0]["unmapped"]=3;};
    SendMessageW(window,WM_TIMER,7,0);SendMessageW(window,WM_TIMER,8,0);
    restoreCheck(writes.size()==beforeNewLoss&&!tool.snapshot().at("acknowledgeLoss").get<bool>()&&
      tool.snapshot().at("error").get<std::string>().find("input loss changed")!=std::string::npos,
      "Queued Keep reused an acknowledgement after new input loss");click(9724);
    // API completion must update hidden departure protection without relying
    // on the disabled poll timer, fetching data or stealing another editor's focus.
    tool.hide();SetFocus(app.window);const auto hiddenFocus=GetFocus();const auto boundaryWrites=writes.size();
    Json external={{"take","api-take"},{"retained",true},{"capturing",true}};
    tool.observeBoundary(app.documentId,"automation.recording.start",external);
    restoreCheck(tool.protectsClose()&&tool.snapshot().at("take")==external,
      "Hidden review did not observe API Start");
    external["capturing"]=false;tool.observeBoundary(app.documentId,"automation.recording.stop",external);
    const auto stoppedReview=tool.snapshot();
    const Json finished={{"take","api-take"},{"retained",false},{"capturing",false}};
    tool.observeBoundary("another song","automation.recording.discard",finished);
    tool.observeBoundary(app.documentId,"automation.recording.get",finished);
    tool.observeBoundary(app.documentId,"automation.recording.discard",{{"take","older-take"},{"retained",false}});
    restoreCheck(tool.snapshot()==stoppedReview&&tool.protectsClose(),
      "Foreign, read-only or older completion cleared the hidden review");
    tool.observeBoundary(app.documentId,"automation.recording.discard",finished);
    restoreCheck(!tool.protectsClose()&&!tool.visible()&&tool.snapshot().at("take")==finished&&
      writes.size()==boundaryWrites&&GetFocus()==hiddenFocus,
      "Confirmed API Discard retained a hidden departure barrier, repeated a write or stole focus");
}
