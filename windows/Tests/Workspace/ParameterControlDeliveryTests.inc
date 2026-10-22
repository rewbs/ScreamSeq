// Deterministic UI-owner tests: actual production delivery class, controlled
// worker futures/receipts. No device, delay, message pumping or compilation here.
void parameterControlDeliveryCases() {
    using Delivery=ScreamSeq::ParameterControlDelivery;using Call=ScreamSeq::Api::CompletedCall;
    struct Submitted {
        RestoreJson params;std::shared_ptr<ScreamSeq::NativeCallReceipt> receipt;std::promise<Call> completion;
    };
    Delivery delivery;std::deque<Submitted> submitted;
    const Delivery::Input original{"doc","revision","take","plugin",17,.25,9007199254740993ull};
    const auto submit=[&](const RestoreJson &p,const auto &receipt){
        submitted.emplace_back();auto &entry=submitted.back();entry.params=p;entry.receipt=receipt;return entry.completion.get_future();
    };
    const auto result=[&](bool accepted){return Call{"automation.recording.capture",original.document,original.revision,
        {{"take",original.take},{"accepted",accepted}}};};
    for(unsigned i=0;i<3;++i){auto input=original;input.timestamp+=i;input.value=.25+i*.1;delivery.enqueue(input,submit);}
    restoreCheck(submitted.size()==3&&delivery.pending()&&delivery.retained()==3&&!delivery.needsReview(),
        "Original inputs were coalesced, pumped or marked delivered before completion");
    for(unsigned i=0;i<3;++i)restoreCheck(submitted[i].params.at("timestamp")==std::to_string(original.timestamp+i)&&
        submitted[i].params.at("value")==.25+i*.1&&submitted[i].params.at("parameter")==17&&
        !submitted[i].params.contains("documentId"),"Native input timestamp/value/identity changed before worker submission");
    restoreRejected([&]{delivery.release();});
    const auto recovery=delivery.recovery(original.document,original.revision,original.take);
    restoreCheck(recovery.at("events").size()==3&&recovery.at("events")[0].at("hostTime")==std::to_string(original.timestamp)&&
        recovery.at("events")[0].at("state")=="pending"&&delivery.retained()==3,
        "Native recovery capture consumed or retimed pending input");
    restoreRejected([&]{delivery.recovery("other document",original.revision,original.take);});
    // A later ready input must be collected without waiting on an earlier one.
    submitted[1].completion.set_value(result(true));delivery.collect();
    restoreCheck(delivery.pending()&&delivery.accepted()==1&&delivery.retained()==2&&submitted.size()==3,
        "Delivery waited for an unrelated completion or resent an original input");
    // The worker receipt proves acceptance even when the native callback failed.
    submitted[0].receipt->publish(std::make_shared<const Call>(result(true)));
    submitted[0].completion.set_exception(std::make_exception_ptr(std::runtime_error("Owned lost callback")));
    submitted[2].completion.set_value(result(false));delivery.collect();
    auto review=delivery.snapshot();
    const auto failedRecovery=delivery.recovery(original.document,original.revision,original.take);
    restoreCheck(failedRecovery.at("events").size()==1&&failedRecovery.at("events")[0].at("state")=="refused"&&
        failedRecovery.at("accepted")=="2"&&recovery.at("events").size()==3,
        "Completed delivery mutated an earlier immutable recovery snapshot");
    restoreCheck(!delivery.pending()&&delivery.accepted()==2&&delivery.needsReview()&&delivery.retained()==1&&
        review.at("events")[0].at("state")=="refused"&&review.at("events")[0].at("timestamp")==std::to_string(original.timestamp+2),
        "Lost completion was resent or worker refusal discarded its original input");
    delivery.collect();restoreCheck(submitted.size()==3,"Review silently retried refused input");delivery.release();
    restoreCheck(!delivery.needsReview()&&delivery.retained()==0&&delivery.accepted()==0,"Explicit take release retained old delivery counts");
    // Neither a successful response for another take nor an unclassified
    // submission exception is proof that this input was rejected or accepted.
    delivery.enqueue(original,submit);auto wrong=result(true);wrong.result["take"]="another take";
    submitted.back().completion.set_value(wrong);delivery.collect();
    restoreCheck(delivery.snapshot().at("events")[0].at("state")=="unknown"&&delivery.retained()==1,
        "Mismatched completion consumed a different take's input");delivery.release();
    unsigned attempts=0;std::shared_ptr<ScreamSeq::NativeCallReceipt> lateReceipt;
    restoreRejected([&]{delivery.enqueue(original,[&](const auto &,const auto &receipt)->std::future<Call>{
        ++attempts;lateReceipt=receipt;throw std::runtime_error("Owned uncertain submission");});});
    delivery.collect();restoreCheck(attempts==1&&delivery.snapshot().at("events")[0].at("state")=="unknown",
        "Uncertain submission was retried or treated as definitely rejected");
    lateReceipt->publish(std::make_shared<const Call>(result(true)));delivery.collect();
    restoreCheck(delivery.accepted()==1&&!delivery.needsReview()&&delivery.retained()==0&&attempts==1,
        "Late receipt failed to reconcile the original request without resubmission");delivery.release();
    restoreRejected([&]{delivery.enqueue(original,[&](const auto &,const auto &)->std::future<Call>{
        ++attempts;throw ScreamSeq::Api::ApiError(-32002,"Not queued",Tracker::WriteOutcome{Tracker::CommitOutcome::NotCommitted});});});
    restoreCheck(delivery.snapshot().at("events")[0].at("state")=="refused","Explicit not-queued result became an uncertain retry");delivery.release();
    // Keep every original bounded entry and report overflow separately; never
    // overwrite the oldest event to conceal loss.
    for(size_t i=0;i<Delivery::capacity;++i){auto input=original;input.timestamp+=i;delivery.enqueue(input,submit);}
    const auto count=submitted.size();restoreRejected([&]{delivery.enqueue(original,submit);});
    restoreCheck(delivery.retained()==Delivery::capacity&&delivery.lost()==1&&delivery.needsReview()&&submitted.size()==count,
        "Full native queue overwrote or submitted an unretained event");
    const auto tail=delivery.snapshot(Delivery::capacity-1,1);
    restoreCheck(tail.at("events").size()==1&&tail.at("events")[0].at("timestamp")==std::to_string(original.timestamp+Delivery::capacity-1),
        "Native queue review lost exact raw timestamps or pagination");
    restoreRejected([&]{delivery.snapshot(0,513);});restoreRejected([&]{delivery.release();});
    for(size_t i=count-Delivery::capacity;i<count;++i)submitted[i].completion.set_value(result(true));
    delivery.collect();restoreCheck(!delivery.pending()&&delivery.retained()==0&&delivery.lost()==1&&delivery.needsReview(),
        "Successful later completion concealed overflow");delivery.release();
}

void applicationRecordedParameterControls() {
    RestoreFixture fixture;auto &app=*fixture.app;const auto plugin=addRestoreGain(app);
    app.openGraphPluginParameters(plugin);auto &editor=*app.graphPluginParameters.at(plugin);const auto window=editor.window();
    ScreamSeq::Tests::ownGuiWindow(window);
    struct Event{uint32_t parameter;double value;uint64_t timestamp;};std::vector<Event> events;
    bool failCapture=false,held=false;
    editor.recording([&]{if(held)throw std::runtime_error("Retained take review");return true;},
        [&](uint32_t id,double value,uint64_t time){if(failCapture)throw std::runtime_error("Owned queue refusal");events.push_back({id,value,time});});
    const auto music=songState(app),state=app.documentOperation("plugin.state.get",{{"plugin",plugin}});
    const auto value=GetDlgItem(window,4012),slider=GetDlgItem(window,4011);
    const auto before=ScreamSeq::hostTime100ns();SetFocus(value);rawDraftText(value,L"-12.00");
    const auto after=ScreamSeq::hostTime100ns();SendMessageW(value,EM_SETSEL,1,4);
    restoreCheck(events.size()==1&&events.back().parameter==1&&events.back().value==-12&&events.back().timestamp>=before&&events.back().timestamp<=after&&
        restoreText(value)==L"-12.00"&&GetFocus()==value&&!editor.snapshot().at("dirty").get<bool>(),
        "Recorded typed control lost original input time, stable identity, raw formatting or focus");
    const auto field=controlState(value);SendMessageW(window,WM_TIMER,7,0);
    restoreCheck(controlState(value)==field,"Background parameter refresh reformatted recorded text or moved its caret");
    for(const auto position:{5000,6000,7000}){
        SendMessageW(slider,TBM_SETPOS,TRUE,position);
        SendMessageW(window,WM_HSCROLL,MAKEWPARAM(TB_THUMBTRACK,position),reinterpret_cast<LPARAM>(slider));
    }
    restoreCheck(events.size()==4&&events[1].value!=events[2].value&&events[2].value!=events[3].value,
        "Recorded drag coalesced intermediate native control positions");
    SendMessageW(window,WM_HSCROLL,MAKEWPARAM(TB_ENDTRACK,0),reinterpret_cast<LPARAM>(slider));
    restoreCheck(events.size()==4,"Recorded slider release duplicated its final event");
    SendMessageW(window,WM_COMMAND,MAKEWPARAM(4004,BN_CLICKED),reinterpret_cast<LPARAM>(GetDlgItem(window,4004)));
    restoreCheck(events.size()==5&&events.back().parameter==0&&events.back().value==0,"Recorded native switch was not captured exactly once");
    rawDraftText(value,L"-.");
    SendMessageW(window,WM_COMMAND,MAKEWPARAM(4015,BN_CLICKED),0);
    restoreCheck(events.size()==5&&restoreText(value)==L"-."&&editor.snapshot().at("dirty")==true,
        "Partial recorded text was lost or retried at a new time");
    rawDraftText(value,L"-8");restoreCheck(events.size()==6&&!editor.snapshot().at("dirty").get<bool>(),"Correcting partial recording text never resumed capture");
    failCapture=true;rawDraftText(value,L"-7.500");
    restoreCheck(events.size()==6&&restoreText(value)==L"-7.500"&&editor.snapshot().at("dirty")==true,
        "Failed native capture lost its raw field");
    held=true;failCapture=false;SendMessageW(window,WM_COMMAND,MAKEWPARAM(4015,BN_CLICKED),0);
    restoreCheck(events.size()==6&&restoreText(value)==L"-7.500","Held take converted Retry into a new recorded/manual write");
    restoreCheck(songState(app)==music&&app.documentOperation("plugin.state.get",{{"plugin",plugin}})==state,
        "Native recording controls wrote manual opaque state or per-event Undo history");
    editor.recording({},{});
    restoreCheck(editor.retainedDraft(),"Partial recorded text was not protected at take admission");
    const auto retained=controlState(value);editor.takeFinished();SendMessageW(window,WM_TIMER,7,0);
    restoreCheck(controlState(value)==retained&&editor.retainedDraft(),"Take completion erased a partial recorded value");
    SendMessageW(window,WM_COMMAND,MAKEWPARAM(3955,BN_CLICKED),0);
    restoreCheck(!editor.retainedDraft()&&restoreText(value)==L"0","Explicit post-take reload failed to restore accepted manual gain");
    editor.recording([]{return true;},[&](uint32_t,double,uint64_t){});
    SetFocus(value);rawDraftText(value,L"-3.00");const auto revision=app.view->session.revision;
    editor.recording({},{});editor.takeFinished();SendMessageW(window,WM_TIMER,7,0);
    restoreCheck(!editor.retainedDraft()&&restoreText(value)==L"0"&&GetFocus()==value&&app.view->session.revision==revision,
        "Discard at the same revision left transient recorded gain visible or lost native field focus");
}

void applicationRecordedRackControls() {
    using Call=ScreamSeq::Api::CompletedCall;
    RestoreFixture fixture;auto &app=*fixture.app;resizeRestoreClient(app,1440,800);const auto plugin=addRestoreGain(app);
    app.restoreLayoutConfiguration(layoutFor(app,"plugins"));app.selectedPlugin=plugin;app.selectedParameter=1;
    app.refreshPluginDetails();app.layoutControls();
    const auto music=songState(app),opaque=app.documentOperation("plugin.state.get",{{"plugin",plugin}});
    app.parameterTakeDocument=app.documentId;
    app.parameterTakeState={{"take","owned-native-controls"},{"baseRevision",app.view->session.revision},{"retained",true},
        {"capturing",true},{"compatible",true},{"failure",0},{"sources",RestoreJson::array({{{"plugin",plugin}}})}};
    std::deque<std::promise<Call>> completions;std::vector<RestoreJson> submitted;
    app.parameterControlSubmit=[&](const auto &params,const auto &){submitted.push_back(params);completions.emplace_back();return completions.back().get_future();};
    const auto field=app.controls.at(pluginValue);SetFocus(field);const auto firstTime=ScreamSeq::hostTime100ns();
    rawDraftText(field,L"-12.000");const auto lastTime=ScreamSeq::hostTime100ns();SendMessageW(field,EM_SETSEL,1,3);
    const auto raw=controlState(field);
    restoreCheck(submitted.size()==1&&submitted[0].at("plugin")==plugin&&submitted[0].at("parameter")==1&&submitted[0].at("value")==-12&&
        std::stoull(submitted[0].at("timestamp").get<std::string>())>=firstTime&&std::stoull(submitted[0].at("timestamp").get<std::string>())<=lastTime&&
        !app.pluginDraft&&!app.pluginAutoQueued&&GetFocus()==field&&app.parameterControlDelivery.pending(),
        "Rack typed recording was debounced, retargeted or awaited before returning native focus");
    app.pluginCommand(pluginApply);restoreCheck(submitted.size()==1&&controlState(field)==raw,
        "Enter/Retry duplicated a valid recorded rack input or reformatted its text");
    rawDraftText(field,L"-.");restoreCheck(submitted.size()==1&&app.pluginDraft&&!app.pluginAutoQueued,"Partial rack text was submitted as a number");
    restoreRejected([&]{app.pluginCommand(pluginApply);});rawDraftText(field,L"-8.5");
    restoreCheck(submitted.size()==2&&!app.pluginDraft,"Corrected rack recording text did not resume capture");
    app.selectedParameter=0;app.setPluginParameterField();app.layoutControls();
    const auto toggle=app.controls.at(pluginValueToggle);SendMessageW(toggle,BM_SETCHECK,BST_UNCHECKED,0);app.pluginCommand(pluginValueToggle);
    restoreCheck(submitted.size()==3&&submitted.back().at("parameter")==0&&submitted.back().at("value")==0&&!app.pluginDraft,
        "Rack switch's hidden numeric EN_CHANGE duplicated or durably committed its recording input");
    restoreRejected([&]{Application::ParameterRecordingInputLease lease(app,"automation.recording.commit");});
    restoreRejected([&]{Application::ParameterRecordingInputLease lease(app,"automation.recording.discard");});
    for(auto &completion:completions)completion.set_value(Call{"automation.recording.capture",app.documentId,app.view->session.revision,
        {{"take","owned-native-controls"},{"accepted",true}}});
    app.serviceParameterControlDelivery();
    restoreCheck(!app.parameterControlDelivery.pending()&&app.parameterControlDelivery.accepted()==3&&
        app.parameterControlDelivery.retained()==0,"Native timer did not reconcile rack input without resubmission");
    app.parameterTakeState["capturing"]=false;app.selectedParameter=1;app.setPluginParameterField();app.layoutControls();rawDraftText(field,L"-9.000");
    restoreCheck(submitted.size()==3&&app.pluginDraft&&restoreText(field)==L"-9.000"&&!app.pluginAutoQueued,
        "Stopped take silently used the manual edit path or erased raw text");
    restoreCheck(songState(app)==music&&app.documentOperation("plugin.state.get",{{"plugin",plugin}})==opaque,
        "Recorded rack input changed manual plugin state or chronological history");
    app.parameterControlSubmit={};app.parameterControlDelivery.release();app.parameterTakeState={{"retained",false}};app.pluginDraft=false;
    const auto source=ScreamSeq::ParameterControlDelivery::Input{app.documentId,app.view->session.revision,"review-loss",plugin,1,-6,ScreamSeq::hostTime100ns()};
    restoreRejected([&]{app.parameterControlDelivery.enqueue(source,[](const auto &,const auto &)->std::future<Call>{
        throw ScreamSeq::Api::ApiError(-32002,"Owned refused input",Tracker::WriteOutcome{Tracker::CommitOutcome::NotCommitted});});});
    const auto evidence=app.parameterControlDelivery.snapshot();
    for(const auto &acknowledgement:{RestoreJson(),RestoreJson(false),RestoreJson(1),RestoreJson("true")}) {
        bool rejected=false;try{Application::ParameterRecordingInputLease lease(app,"automation.recording.commit",{{"acknowledgeLoss",acknowledgement}});}
        catch(const ScreamSeq::Api::ApiError &error){rejected=error.outcome&&!error.outcome->needsReconciliation();}
        restoreCheck(rejected,"Unacknowledged native loss was admitted or classified as an uncertain write");
    }
    {Application::ParameterRecordingInputLease lease(app,"automation.recording.commit",{{"acknowledgeLoss",true}});
        restoreCheck(app.parameterControlDelivery.snapshot()==evidence,"Loss consent replayed or discarded original input before worker commit");}
    app.parameterControlDelivery.release();
}
