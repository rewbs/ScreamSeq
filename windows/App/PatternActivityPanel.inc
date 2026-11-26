    std::future<ScreamSeq::Api::CompletedCall> activityFuture;
    std::string activityRequested,activityReady,activityAttempted,activityObserved,activityDocument,activityRevision;
    ULONGLONG activityChangedAt=0;
    Json activityData=Json::object(),activityLinks=Json::array();
    bool patternActivityVisible()const {return workspaceState.contextPanelVisible&&(contextPanelEditor=="pattern"||contextPanelEditor=="notes"||contextPanelEditor=="preciseNotes"||contextPanelEditor=="effects");}
    std::string activityKey()const {return currentRulerKey()+Json::array({row,channel}).dump();}
    void fillActivityList(const Json &data) {
        const auto list=controls.at(activityListCommand);const auto old=SendMessageW(list,LB_GETCURSEL,0,0),top=SendMessageW(list,LB_GETTOPINDEX,0,0);
        const auto prior=old>=0&&size_t(old)<activityLinks.size()?activityLinks[size_t(old)]:Json();
        activityLinks=Json::array();SendMessageW(list,WM_SETREDRAW,FALSE,0);ScreamSeq::NativeInputGate::present(list,LB_RESETCONTENT,0,0);
        auto line=[&](const std::wstring &text,Json link=Json()) {ScreamSeq::NativeInputGate::present(list,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));activityLinks.push_back(std::move(link));};
        line(L"FROM SONG START · first visit");
        if(!data.value("limitation",std::string()).empty())line(wide(data.at("limitation")));
        if(data.contains("items"))for(const auto *category:{"Sounds","Plugins","Pattern FX","Envelopes"}){
            line(wide(std::string(category))+(std::string(category)=="Plugins"?L" · signal path":L""));bool any=false;
            for(const auto &item:data.at("items"))if(item.at("category")==category){line(L"  "+wide(item.at("label")),item);any=true;}
            if(!any)line(L"  None");
        }
        if(data.value("pending",false))line(L"Analysing…");
        int selected=-1;if(!prior.is_null())for(size_t i=0;i<activityLinks.size();++i)if(activityLinks[i]==prior){selected=int(i);break;}
        ScreamSeq::NativeInputGate::present(list,LB_SETCURSEL,selected,0);ScreamSeq::NativeInputGate::present(list,LB_SETTOPINDEX,std::max<LRESULT>(0,top),0);
        SendMessageW(list,LB_SETHORIZONTALEXTENT,900,0);SendMessageW(list,WM_SETREDRAW,TRUE,0);InvalidateRect(list,nullptr,TRUE);
    }
    void servicePatternActivity() {
        if(!view||!controller||!controls.contains(activityListCommand))return;
        layoutChannelButtons();const auto key=activityKey();
        if(activityObserved!=key){activityObserved=key;activityChangedAt=GetTickCount64();activityReady.clear();activityData=Json::object();fillActivityList({{"pending",true}});}
        if(activityFuture.valid()&&activityFuture.wait_for(std::chrono::milliseconds(0))==std::future_status::ready){
            try{auto result=activityFuture.get();if(activityRequested==key&&result.document==activityDocument&&result.revision==activityRevision){activityData=std::move(result.result);activityReady=key;fillActivityList(activityData);frameRequested=true;}}
            catch(const std::exception &e){if(activityRequested==key){activityData={{"limitation",e.what()}};fillActivityList(activityData);}}
        }
        if(activityFuture.valid()||!patternActivityVisible()||busy||recoveryRestoring||departureInput||activityAttempted==key||GetTickCount64()-activityChangedAt<180)return;
        activityAttempted=activityRequested=key;activityDocument=documentId;activityRevision=view->session.revision;
        if(const auto order=rulerOrder())activityFuture=controller->invokeCompleted("pattern.activity.get",{{"pattern",patternIndex},{"order",*order},{"row",row},{"channel",channel}});
        else {activityData={{"limitation","Pattern is not in the selected sequence"}};fillActivityList(activityData);}
    }
    Json patternActivitySnapshot()const{return {{"ready",activityReady==activityKey()},{"pending",activityFuture.valid()},{"data",activityReady==activityKey()?activityData:Json::object()}};}
    void openActivityLink() {
        if(activityReady!=activityKey())throw std::runtime_error("Wait for the current row activity");
        const auto index=SendMessageW(controls.at(activityListCommand),LB_GETCURSEL,0,0);if(index<0||size_t(index)>=activityLinks.size()||activityLinks[size_t(index)].is_null())return;
        const auto link=activityLinks[size_t(index)];const auto kind=link.at("kind").get<std::string>();
        if(kind=="plugin"){
            const auto plugin=link.at("plugin").get<std::string>();const auto &rack=view->session.document.at("nativePlugins");
            const auto found=std::find_if(rack.begin(),rack.end(),[&](const auto &p){return p.at("instanceID")==plugin;});if(found==rack.end())throw std::runtime_error("Plugin is no longer available");
            if(found->at("format")=="VST3")edit("plugin.editor.open",{{"slot",found->at("slot")}});else openGraphPluginParameters(plugin);return;
        }
        if(kind=="graphPlugin"){edit("graph.plugin.editor.open",{{"graph",link.at("graph")},{"node",link.at("node")}});return;}
        if(kind=="sample"){openSampleDetail();sampleDetailWindow->openSourceAt(link.at("slot").get<unsigned>());return;}
        if(kind=="instrument"){
            if(instrumentEnvelopeWindow&&instrumentEnvelopeWindow->retainedDraft())throw std::runtime_error("Finish the instrument draft before opening another source");
            const auto slot=link.at("slot").get<unsigned>();const auto &catalog=view->session.document.at("instruments");
            const auto found=std::find_if(catalog.begin(),catalog.end(),[&](const auto &i){return i.at("index")==slot;});if(found==catalog.end())throw std::runtime_error("Instrument is no longer available");
            const auto identity=found->at("id").get<std::string>();openInstrumentEnvelope();instrumentEnvelopeWindow->openSourceAt(identity,link.value("envelope",std::string()));workspaceEditors[1].pinned=true;return;
        }
        if(kind=="graph"&&link.at("category")=="Envelopes"){
            if(!graphCurveWindow)graphCurveWindow=makeGraphCurveWindow();configureWorkspaceEditor("graphCurve");
            graphCurveWindow->openTarget({link.at("graph"),link.at("node"),workspacePlaybackPatternID(link.at("pattern").get<unsigned>()),link.at("pattern").get<unsigned>()});
            workspaceEditors[workspaceEditorIndex("graphCurve")].pinned=true;finishWorkspaceEditorOpen("graphCurve");return;
        }
        auto source=link;
        if(kind=="effect"){source["kind"]="pattern-commands";source["position"]=link.at("row").get<unsigned>()*65536;}
        if(kind=="automation"){source["kind"]="envelope";source["position"]=0;}
        if(source.contains("node")&&source.at("node").is_null())source["node"]="";
        inspectParameterSource(source);
    }
