// Application ownership for the native parameter take. The worker owns music,
// timestamps and history; this owner protects presentation and original input.
    std::unique_ptr<ScreamSeq::ParameterRecordingWindow> parameterRecordingWindow;
    std::unique_ptr<ScreamSeq::PluginRecoveryReviewWindow> pluginRecoveryReviewWindow;
    void openPluginRecoveryReview() {
        if(busy||departureInput||parameterRecordingInput)throw std::runtime_error("Wait for the current song operation before reviewing recovered inputs");
        if(!pluginRecoveryReviewWindow)pluginRecoveryReviewWindow=std::make_unique<ScreamSeq::PluginRecoveryReviewWindow>(window,
            [this](const auto &method,const auto &params){return documentOperation(method,params);},
            [this]{return std::pair(documentId,view->session.revision);});
        pluginRecoveryReviewWindow->show();
    }
    void reconcileParameterRecording(const ScreamSeq::Api::CompletedCall &call) {
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
        callbacks.write=[this](const auto &method,const auto &params,const auto &receipt){return documentOperationWithOutcome(method,params,receipt);};
        callbacks.reconcile=[this](const auto &call){reconcileParameterRecording(call);};
        callbacks.delivery=[this](size_t offset,size_t limit){
            parameterControlDelivery.collect();
            return parameterControlDelivery.snapshot(std::min(offset,parameterControlDelivery.retained()),limit);
        };
        callbacks.saveRecovery=[this]{saveRecovery(view->session.revision,true);};
        callbacks.available=[this]{return !busy&&!recoveryRestoring&&!departureInput&&!parameterRecordingInput;};
        callbacks.recoveredInputs=[this]{openPluginRecoveryReview();};
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
