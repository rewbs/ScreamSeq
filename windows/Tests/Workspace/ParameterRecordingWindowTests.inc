// Real native review controls; worker/timing/state publication has its separate
// native lifecycle fixture. This fixture controls receipts and read failures.
void applicationParameterRecordingReview() {
    using Json=RestoreJson;using Call=ScreamSeq::Api::CompletedCall;
    RestoreFixture fixture;auto &app=*fixture.app;const auto plugin=addRestoreGain(app);const auto music=songState(app);
    Json take={{"retained",false},{"capturing",false},{"take",""}},delivery={{"retained",0},{"pending",false},{"needsReview",false},{"lost",0},{"events",Json::array()}};
    std::vector<std::pair<std::string,Json>> writes;unsigned saves=0,reconciled=0,nameReads=0;bool failAfterKeep=false;
    bool deferKeepReceipt=false,failReconcile=false,refuseStart=false;
    std::shared_ptr<ScreamSeq::NativeCallReceipt> lateReceipt;
    std::shared_ptr<const Call> lateCompletion;
    ScreamSeq::ParameterRecordingWindow::Callbacks callbacks;
    callbacks.context=[&]{return std::pair(app.documentId,app.view->session.revision);};
    callbacks.rack=[&]{return app.view->session.document.at("nativePlugins");};
    callbacks.parameterName=[&](const std::string &id,uint32_t parameter){restoreCheck(id==plugin&&parameter==1,"Review looked up another parameter name");++nameReads;return L"Gain";};
    callbacks.available=[]{return true;};callbacks.saveRecovery=[&]{++saves;};
    callbacks.reconcile=[&](const Call &call){restoreCheck(call.method=="automation.recording.commit"&&call.result.at("retained")==false,
      "Native review reconciled the wrong completion");
      if(std::exchange(failReconcile,false))throw std::runtime_error("Owned readback failure");++reconciled;};
    callbacks.delivery=[&](size_t offset,size_t limit){auto page=delivery;page["events"]=Json::array();
      for(size_t i=offset;i<std::min(delivery.at("events").size(),offset+limit);++i)page["events"].push_back(delivery.at("events")[i]);return page;};
    callbacks.read=[&](const std::string &method,const Json &params)->Json {
      if(method=="automation.recording.get")return take;
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
      }else {
        restoreCheck(params.at("take")==take.at("take"),"Recording action targeted a different retained take");
        if(method=="automation.recording.stop")take["capturing"]=false;
        else if(method=="automation.recording.commit"){
          restoreCheck(params.at("acknowledgeLoss")==true,"Keep recording failed to pass explicit loss acknowledgement");take={{"retained",false},{"capturing",false},{"take",""}};
        }else if(method=="automation.recording.discard")take={{"retained",false},{"capturing",false},{"take",""}};
        else throw std::runtime_error("Unexpected review write");
      }
      const Call result{method,app.documentId,app.view->session.revision,take};
      if(method=="automation.recording.commit"&&std::exchange(deferKeepReceipt,false)){
        lateReceipt=receipt;lateCompletion=std::make_shared<const Call>(result);
        throw std::runtime_error("Owned missing native completion");
      }
      receipt->publish(std::make_shared<const Call>(result));
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
    SendMessageW(control(9728),CB_SETCURSEL,0,0);SendMessageW(window,WM_COMMAND,MAKEWPARAM(9728,CBN_SELCHANGE),reinterpret_cast<LPARAM>(control(9728)));
    SendMessageW(window,WM_TIMER,7,0);restoreCheck(tool.snapshot().at("target")=="",
        "Recording poll redirected an explicit native delivery review choice");
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
      {"transportPlaying",false},{"discardable",true},{"targets",Json::array()},{"sources",Json::array()}};
    SendMessageW(window,WM_TIMER,7,0);
    restoreCheck(IsWindowEnabled(control(9724))&&!IsWindowEnabled(control(9723))&&
      restoreText(control(9734)).find(L"Playback stopped")!=std::wstring::npos,
      "Stopped transport with a failed Stop cut trapped the retained take behind disabled Discard");
    click(9724);restoreCheck(writes.size()==4&&writes.back().first=="automation.recording.discard"&&
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
