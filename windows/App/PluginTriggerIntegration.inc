// Creation and assignment are separate history entries. Retain each receipt
// before invoking the worker so a failed UI callback cannot append twice.
    ScreamSeq::NativeWriteCompletion pluginTriggerCompletion;
    Json pluginTriggerTarget=Json::object(),pluginTriggerObservation=Json::object();
    bool pluginTriggerPending=false,pluginTriggerReviewed=false;
    uint64_t pluginTriggerGeneration=0;
    void finishPluginTrigger() {
        const auto returned=pluginTriggerCompletion.returned();
        if(!returned||returned->document!=documentId||returned->revision!=view->session.revision)
            throw std::runtime_error("Trigger result needs Review against the current song");
        const auto owner=pluginTriggerTarget.at("plugin").get<std::string>();
        const bool creating=pluginTriggerTarget.at("creating").get<bool>();
        const auto decodedSlot=ScreamSeq::Api::boundedInteger(creating?returned->result.at("instrument"):pluginTriggerTarget.at("instrument"),1,255);
        if(!decodedSlot||(creating&&(returned->result.at("empty")!=true||returned->result.at("dryRun")!=false)))
            throw std::runtime_error("Invalid trigger receipt / result retained");
        const auto slot=unsigned(*decodedSlot);
        const auto &instruments=view->session.document.at("instruments");
        const auto found=std::find_if(instruments.begin(),instruments.end(),[&](const auto &i){return i.at("index")==slot;});
        if(found==instruments.end())throw std::runtime_error("Created instrument is unavailable / result retained");
        const auto identity=found->at("id").get<std::string>();
        if(!creating&&identity!=pluginTriggerTarget.at("instrumentId").get<std::string>())throw std::runtime_error("Trigger identity changed / result retained");
        const auto instrument=documentOperation("instrument.get",{{"instrument",slot}});
        if(creating) {
            const auto &mapping=instrument.at("mapping");
            if(instrument.at("name")!=pluginTriggerTarget.at("name")||!mapping.is_array()||mapping.size()!=128||
               !std::all_of(mapping.begin(),mapping.end(),[](const auto &sample){return sample==0;}))
                throw std::runtime_error("Created trigger does not match its name and empty keymap / result retained");
        } else {
            const auto routes=documentOperation("plugin.instruments.get",{{"plugin",owner}});
            const auto &assignments=routes.at("assignments");
            if(instrument.at("mapping")!=pluginTriggerTarget.at("mapping")||
               !std::any_of(assignments.begin(),assignments.end(),[&](const auto &a){return a.at("instrument")==slot&&a.at("channel")==1;}))
                throw std::runtime_error("Trigger assignment readback differs / result retained");
        }
        if(documentId!=returned->document||view->session.revision!=returned->revision)
            throw std::runtime_error("Song changed during trigger readback / result retained");
        if(creating)pendingPluginTriggers[owner]=identity;else pendingPluginTriggers.erase(owner);
        pluginTriggerCompletion.finish();pluginTriggerReviewed=false;pluginDetailsRevision.clear();
        status=creating?L"Trigger created / Retry assign continues without creating another instrument":L"Trigger assigned / creation and assignment have separate Undo steps";
        frameRequested=true;
    }
    void reviewPluginTrigger() {
        if(pluginTriggerPending)throw std::runtime_error("Wait for the trigger operation");
        if(pluginTriggerReviewed) {
            if(documentId!=pluginTriggerObservation.at("documentId").get<std::string>()||view->session.revision!=pluginTriggerObservation.at("revision").get<std::string>()) {
                pluginTriggerReviewed=false;throw std::runtime_error("Song changed after trigger Review / Review again");
            }
            pluginTriggerCompletion.finish();pluginTriggerReviewed=false;
            status=L"Trigger result acknowledged / no creation or assignment repeated";frameRequested=true;return;
        }
        pluginTriggerPending=true;struct Reset{bool &flag;~Reset(){flag=false;}}reset{pluginTriggerPending};
        documentOperation("synchronizeView",Json::object());
        if(documentId!=pluginTriggerTarget.at("documentId").get<std::string>())throw std::runtime_error("Original trigger document is unavailable / result retained");
        if(const auto returned=pluginTriggerCompletion.returned();returned&&returned->revision==view->session.revision) {
            try{finishPluginTrigger();return;}catch(const std::exception &){
                // A receipt that cannot be reconciled still has an explicit
                // read-only observation/acknowledgement path. Never resend it.
            }
        }
        // synchronizeView above adopts the worker's current immutable catalogue;
        // document.get is a session snapshot read, not a worker operation.
        const auto revision=view->session.revision;const auto observed=view->session.document;
        for(const char *key:{"instruments","nativePlugins"})if(!observed.at(key).is_array())throw std::runtime_error("Invalid trigger catalogue / result retained");
        for(const auto &instrument:observed.at("instruments"))
            if(instrument.at("id").get<std::string>().empty()||instrument.at("index").get<unsigned>()==0||!instrument.at("name").is_string())
                throw std::runtime_error("Invalid trigger identity / result retained");
        for(const auto &plugin:observed.at("nativePlugins"))
            if(plugin.at("instanceID").get<std::string>().empty()||!plugin.at("name").is_string()||!plugin.at("isInstrument").is_boolean())
                throw std::runtime_error("Invalid plugin catalogue / result retained");
        if(documentId!=pluginTriggerTarget.at("documentId").get<std::string>()||view->session.revision!=revision)throw std::runtime_error("Song changed during trigger Review");
        pluginTriggerObservation={{"documentId",documentId},{"revision",revision},{"observed",observed}};pluginTriggerReviewed=true;
        status=L"Current instruments inspected / trigger outcome unverified / Accept result acknowledges without another write";frameRequested=true;
    }
    void createPluginTrigger() {
        if(pluginTriggerPending)throw std::runtime_error("Wait for the trigger operation");
        if(pluginTriggerCompletion.retained()){reviewPluginTrigger();return;}
        if(pluginDraft)throw std::runtime_error("Finish the plugin parameter or program draft before creating a trigger");
        const auto slot=selectedPluginSlot();if(slot<0)throw std::runtime_error("Select an instrument plugin");
        const auto plugin=view->session.document.at("nativePlugins")[slot];
        if(!plugin.value("isInstrument",false))throw std::runtime_error("Select an instrument plugin");
        const auto owner=plugin.at("instanceID").get<std::string>(),document=documentId;
        const auto write=[this](const auto &method,const auto &request,const auto &receipt){return documentOperationWithOutcome(method,request,receipt);};
        pluginTriggerPending=true;struct Reset{bool &flag;~Reset(){flag=false;}}reset{pluginTriggerPending};
        unsigned instrument=0;std::string identity;
        if(auto pending=pendingPluginTriggers.find(owner);pending!=pendingPluginTriggers.end())
            for(const auto &i:view->session.document.at("instruments"))if(i.at("id")==pending->second){instrument=i.at("index").get<unsigned>();identity=pending->second;}
        if(!instrument) {
            const auto name=plugin.at("name").get<std::string>();
            const auto triggerName=name.size()+8<OpenMPT::MAX_INSTRUMENTNAME?name+" trigger":name.size()<OpenMPT::MAX_INSTRUMENTNAME?name:std::string("Plugin trigger");
            pluginTriggerTarget={{"documentId",document},{"expectedRevision",view->session.revision},{"plugin",owner},{"creating",true},{"name",triggerName}};
            pluginTriggerObservation=Json::object();pluginTriggerReviewed=false;
            pluginTriggerCompletion.submit(write,"instrument.create",{{"empty",true},{"name",triggerName},{"expectedRevision",view->session.revision}},document,++pluginTriggerGeneration,pluginTriggerTarget);
            finishPluginTrigger();identity=pendingPluginTriggers.at(owner);
            for(const auto &i:view->session.document.at("instruments"))if(i.at("id")==identity)instrument=i.at("index").get<unsigned>();
        }
        if(documentId!=document||!instrument)throw std::runtime_error("Captured trigger is unavailable");
        const auto revision=view->session.revision;
        const auto mapping=documentOperation("instrument.get",{{"instrument",instrument}}).at("mapping");
        if(documentId!=document||view->session.revision!=revision)throw std::runtime_error("Song changed before trigger assignment");
        pluginTriggerTarget={{"documentId",document},{"expectedRevision",revision},{"plugin",owner},{"creating",false},{"instrument",instrument},{"instrumentId",identity},{"mapping",mapping}};
        pluginTriggerCompletion.submit(write,"instrument.plugin.set",{{"instrument",instrument},{"plugin",owner},{"channel",1},{"expectedRevision",revision}},document,++pluginTriggerGeneration,pluginTriggerTarget);
        finishPluginTrigger();
    }
