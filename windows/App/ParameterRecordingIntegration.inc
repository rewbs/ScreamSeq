// Application ownership for the native parameter take. The worker owns music,
// timestamps and history; this owner protects presentation and original input.
    std::unique_ptr<ScreamSeq::ParameterRecordingWindow> parameterRecordingWindow;
    std::unique_ptr<ScreamSeq::PluginRecoveryReviewWindow> pluginRecoveryReviewWindow;
    ScreamSeq::Api::CompletedCall stopParameterRecordingPlayback(const Json &params,const std::shared_ptr<ScreamSeq::NativeCallReceipt> &receipt) {
        // Transport belongs to SessionHost, not the document worker dispatcher.
        // Use the same Stop as the public API, publishing its receipt before
        // fallible presentation readback. It never Keep/Discards a take.
        guardDepartureOperation("transport.stop");guardParameterRecordingInput("transport.stop");
        if(busy||recoveryRestoring)throw ScreamSeq::Api::ApiError(-32002,"Wait for the current song operation before stopping playback",Tracker::WriteOutcome{Tracker::CommitOutcome::NotCommitted});
        const auto current=snapshot();
        if(!params.is_object()||params.size()!=1||!params.contains("expectedRevision")||!params.at("expectedRevision").is_string())
            throw ScreamSeq::Api::ApiError(-32602,"Playback Stop requires only expectedRevision",Tracker::WriteOutcome{Tracker::CommitOutcome::NotCommitted});
        if(params.at("expectedRevision")!=current.revision)
            throw ScreamSeq::Api::ApiError(-32001,"Song changed before playback Stop",Tracker::WriteOutcome{Tracker::CommitOutcome::NotCommitted});
        const auto call=std::make_shared<const ScreamSeq::Api::CompletedCall>(ScreamSeq::Api::CompletedCall{"transport.stop",current.documentId,current.revision,{{"playing",false}}});
        stop();if(receipt)receipt->publish(call);
        finishDocumentOperation(call->method,call->result);return *call;
    }
    void openPluginRecoveryReview() {
        if(busy||departureInput||parameterRecordingInput)throw std::runtime_error("Wait for the current song operation before reviewing recovered inputs");
        if(!pluginRecoveryReviewWindow)pluginRecoveryReviewWindow=std::make_unique<ScreamSeq::PluginRecoveryReviewWindow>(window,
            [this](const auto &method,const auto &params){return documentOperation(method,params);},
            [this]{return std::pair(documentId,view->session.revision);});
        pluginRecoveryReviewWindow->show();
    }
    void reconcileParameterRecording(const ScreamSeq::Api::CompletedCall &call) {
        if(call.method=="transport.stop") {
            if(call.document!=documentId||call.revision.empty()||!call.result.is_object()||call.result.value("playing",true))
                throw std::runtime_error("Playback Stop completion belongs to another song or operation");
            refreshDocument();return; // Readback only; never stop a newly started playback.
        }
        if(call.document!=documentId||!parameterRecordingBoundary(call.method)||
            !call.result.is_object()||!call.result.contains("retained"))
            throw std::runtime_error("Recording completion belongs to another song or operation");
        if(!parameterControlDelivery.belongsTo(call.document,call.result.value("take",std::string())))
            throw std::runtime_error("Earlier completion cannot release a later take's native input");
        // Admission is not being repeated: the receipt proves the earlier
        // worker operation. Protect native input during readback only.
        ParameterRecordingInputLease input(*this,"automation.recording.stop");
        const auto current=await(controller->invoke("automation.recording.get",Json::object()));
        if(current.value("retained",false)&&
            (!call.result.at("retained").get<bool>()||current.at("take")!=call.result.at("take")))
            throw std::runtime_error("A later parameter take exists; earlier completion retained for review");
        if(!call.result.at("retained").get<bool>())finishDocumentOperation(call.method,call.result);
        else finishDocumentOperation("automation.recording.get",current);
    }
    void ensureParameterRecordingWindow() {
        if(parameterRecordingWindow)return;
        ScreamSeq::ParameterRecordingWindow::Callbacks callbacks;
        callbacks.context=[this]{return std::pair(documentId,view->session.revision);};
        callbacks.rack=[this]{return view->session.document.at("nativePlugins");};
        callbacks.parameterName=[this](const std::string &plugin,uint32_t parameter){
            const auto values=documentOperation("plugin.parameters.get",{{"plugin",plugin}});
            for(const auto &value:values)if(value.at("id")==parameter)return wide(value.at("name").get<std::string>());
            return L"Parameter "+std::to_wstring(parameter);
        };
        callbacks.read=[this](const auto &method,const auto &params){return documentOperation(method,params);};
        callbacks.write=[this](const auto &method,const auto &params,const auto &receipt){return method=="transport.stop"?
            stopParameterRecordingPlayback(params,receipt):documentOperationWithOutcome(method,params,receipt);};
        callbacks.reconcile=[this](const auto &call){reconcileParameterRecording(call);};
        callbacks.delivery=[this](size_t offset,size_t limit){
            parameterControlDelivery.collect();
            return parameterControlDelivery.snapshot(std::min(offset,parameterControlDelivery.retained()),limit);
        };
        callbacks.saveRecovery=[this]{saveRecovery(view->session.revision,true);};
        callbacks.available=[this]{return !busy&&!recoveryRestoring&&!departureInput&&!parameterRecordingInput;};
        callbacks.recoveredInputs=[this]{openPluginRecoveryReview();};
        callbacks.recordedAutomation=[this](const std::string &plugin,uint32_t parameter){
            // Use stable source navigation: never replace another lane's retained
            // draft, silently choose a different plugin, or move the pattern cursor.
            inspectParameterSource({{"kind","recorded"},{"plugin",plugin},{"parameter",parameter}});
        };
        parameterRecordingWindow=std::make_unique<ScreamSeq::ParameterRecordingWindow>(window,std::move(callbacks));
        connectSampleCaptureTransport(*parameterRecordingWindow);
    }
    void openParameterRecording() {
        if(busy||departureInput||parameterRecordingInput)throw std::runtime_error("Wait for the current song operation before opening parameter recording");
        ensureParameterRecordingWindow();parameterRecordingWindow->show();
    }
    bool protectParameterRecordingTake() {
        // get polls the bounded producer cut and refreshes the cached take.
        // On failure the cached owner still protects a previously seen take.
        try{documentOperation("automation.recording.get",Json::object());}
        catch(...){
            if(parameterRecordingWindow)parameterRecordingWindow->show();
            throw;
        }
        if(!parameterTakeState.value("retained",false)&&!parameterControlDelivery.hasOwner()&&!parameterControlDelivery.retained()&&!parameterControlDelivery.lost()&&
            (!parameterRecordingWindow||!parameterRecordingWindow->protectsClose()))return true;
        ensureParameterRecordingWindow();parameterRecordingWindow->show();
        status=L"Keep or Discard the plugin automation take before leaving this song";frameRequested=true;return false;
    }
