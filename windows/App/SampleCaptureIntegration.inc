    std::unique_ptr<ScreamSeq::SampleRecordingWindow> sampleRecordingWindow;
    std::unique_ptr<ScreamSeq::PatternSampleRenderWindow> patternSampleRenderWindow;
    bool recordingTakeKnown=false;
    bool directSampleRenderPending=false;
    uint64_t directSampleRenderGeneration=0;
    ScreamSeq::NativeWriteCompletion directSampleRenderCompletion;
    ScreamSeq::NativeAssetObservation directSampleRenderObservation;
    Json directSampleRenderTarget=Json::object(),directSampleRenderReport=Json::object();
    void connectSampleCaptureTransport(ScreamSeq::NativeToolWindow &tool){
        tool.musicalTyping([this](HWND focus,WPARAM key,bool repeat){
            if(key!=VK_SPACE||(GetKeyState(VK_CONTROL)&0x8000)||(GetKeyState(VK_MENU)&0x8000)||typingTextControl(focus)||typingButtonControl(focus))return false;
            if(!repeat){if(device.running()&&!auditionOnly)stop();else play();}return true;
        },{},{});
    }
    void revealCapturedSample(const std::string &document,const Json &result){
        if(document!=documentId)return;
        const auto sample=result.at("sample").get<unsigned>();
        if(!view->samples.contains(sample))return;
        const auto instrument=result.value("instrument",0u);
        if(instrument||!view->instruments){
            const auto target=typingTarget(!instrument,instrument?instrument:sample,49,window);
            typingDocument=documentId;typingSample=!instrument;typingSound=target.slot;typingSoundId=target.identity;refreshTypingSounds();
        }
        frameRequested=true;
        if(sampleDraft||dragging==4)return;
        workspaceState.show("samples",position(),cursorSample(),false);
        auto &panel=workspaceState.panel("samples");panel.sample=sample;panel.pinned=true;sampleDraft=false;
        // Keep focus in the modeless tool; the new sample is visible behind it.
        updateInspector();layoutControls();frameRequested=true;
    }
    void ensureSampleRecordingWindow(){
        if(!sampleRecordingWindow){sampleRecordingWindow=std::make_unique<ScreamSeq::SampleRecordingWindow>(window,
            [this](const auto &method,const auto &params){if(method=="document.get")return view->session.document;return documentOperation(method,params);},
            [this]{return std::pair(documentId,view->session.revision);},
            [this](const auto &document,const auto &result){revealCapturedSample(document,result);},
            [this](const auto &method,const auto &params,const auto &receipt){return documentOperationWithOutcome(method,params,receipt);});
            connectSampleCaptureTransport(*sampleRecordingWindow);}
    }
    void openSampleRecording(){
        if(busy)throw std::runtime_error("Wait for the document worker before opening sample recording");
        ensureSampleRecordingWindow();
        sampleRecordingWindow->openAt();
    }
    bool protectSampleRecordingTake(){
        Json state;
        try{state=documentOperation("sample.recording.get",Json::object());}
        catch(...){if(sampleRecordingWindow&&sampleRecordingWindow->hasRetainedTake()){recordingTakeKnown=true;sampleRecordingWindow->protectTake();return false;}throw;}
        recordingTakeKnown=!state.at("take").is_null()&&state.at("take")!="";if(!recordingTakeKnown)return true;
        ensureSampleRecordingWindow();sampleRecordingWindow->protectTake(state);
        status=L"Keep or Discard the microphone take before leaving this song";frameRequested=true;return false;
    }
    bool protectRecordingTake(){
        // Each session owns its take independently. Show one review at a time;
        // resolving it must never allow the other take to be discarded.
        if(!protectMidiRecordingTake())return false;
        if(!protectSampleRecordingTake())return false;
        // Reading sample state pumps UI/API messages on the document worker
        // boundary, so MIDI capture may have begun since the first check.
        return protectMidiRecordingTake();
    }
    ScreamSeq::PatternSampleRenderWindow::Target patternSampleTarget()const{
        const auto s=selection();return {documentId,view->session.revision,patternIndex,s.at("startRow").get<unsigned>(),s.at("endRow").get<unsigned>(),s.at("startChannel").get<unsigned>(),s.at("endChannel").get<unsigned>()};
    }
    void renderPatternSample(bool instrument,std::optional<ScreamSeq::PatternSampleRenderWindow::Target> captured={}){
        if(busy||directSampleRenderPending)throw std::runtime_error("Wait for the document worker before rendering a selection");
        if(directSampleRenderCompletion.retained()){reviewDirectSampleRender();return;}
        if(patternSampleRenderWindow&&patternSampleRenderWindow->hasUnresolvedResult()){
            patternSampleRenderWindow->show();throw std::runtime_error("Review the retained Render options result before rendering another selection");
        }
        const auto target=captured?std::move(*captured):patternSampleTarget();
        if(target.document!=documentId||target.revision!=view->session.revision)throw std::runtime_error("Song changed while choosing Render / choose the selection again");
        const Json params={{"pattern",target.pattern},{"firstRow",target.firstRow},{"lastRow",target.lastRow},{"firstChannel",target.firstChannel},{"lastChannel",target.lastChannel},
            {"name","Rendered selection"},{"tailSeconds",0},{"createInstrument",instrument},{"dryRun",false},{"expectedRevision",target.revision}};
        directSampleRenderTarget=params;directSampleRenderTarget["documentId"]=target.document;directSampleRenderReport=Json::object();
        ++directSampleRenderGeneration;directSampleRenderObservation.clear();
        directSampleRenderPending=true;struct Guard{bool &value;~Guard(){value=false;}} guard{directSampleRenderPending};
        directSampleRenderCompletion.submit([this](const auto &method,const auto &request,const auto &receipt){return documentOperationWithOutcome(method,request,receipt);},
            "sample.renderSelection",params,target.document,directSampleRenderGeneration,directSampleRenderTarget);
        finishDirectSampleRender();
    }
    void finishDirectSampleRender(){
        const auto returned=directSampleRenderCompletion.returned();
        if(!returned)throw std::runtime_error("Render outcome is still unknown / captured selection is retained; do not render it again until reconciled");
        directSampleRenderReport=returned->result;
        const bool current=returned->document==documentId&&returned->revision==view->session.revision;
        if(current)revealCapturedSample(returned->document,directSampleRenderReport);
        auto text=L"Rendered P"+std::to_wstring(directSampleRenderTarget.at("pattern").get<unsigned>())+L" rows "+
            std::to_wstring(directSampleRenderTarget.at("firstRow").get<unsigned>())+L"–"+std::to_wstring(directSampleRenderTarget.at("lastRow").get<unsigned>())+
            L" / sample "+std::to_wstring(directSampleRenderReport.at("sample").get<unsigned>());
        if(const auto slot=directSampleRenderReport.value("instrument",0u))text+=L" / instrument "+std::to_wstring(slot);
        text+=current?L" / one Undo":L" / song changed since completion; selection left unchanged";
        status=std::move(text);frameRequested=true;directSampleRenderCompletion.finish();
    }
    void reviewDirectSampleRender(){
        if(busy||directSampleRenderPending)throw std::runtime_error("Wait for the render request before reviewing its result");
        if(!directSampleRenderCompletion.retained())return;
        directSampleRenderPending=true;struct Guard{bool &value;~Guard(){value=false;}} guard{directSampleRenderPending};
        documentOperation("synchronizeView",Json::object());
        if(directSampleRenderCompletion.returned()){finishDirectSampleRender();return;}
        directSampleRenderObservation.read([this](const std::string &method,const Json &params){if(method=="document.get")return view->session.document;return documentOperation(method,params);},
          [this]{return std::pair(documentId,view->session.revision);},[this]{return directSampleRenderGeneration;},directSampleRenderTarget.at("documentId").get<std::string>(),
          Json{{"completion",directSampleRenderCompletion.snapshot()},{"params",directSampleRenderTarget}});
        directSampleRenderReport=directSampleRenderObservation.report();
        auto prompt=directSampleRenderReport;prompt["submission"]["domain"]="render";
        const auto choice=chooseNativeCommandAcknowledgement(prompt);
        if(choice!=IDYES){status=L"Unverified render retained / no write repeated";return;}
        directSampleRenderObservation.check({documentId,view->session.revision},directSampleRenderGeneration);
        directSampleRenderCompletion.finish();status=L"Unverified render acknowledged / no selection changed and no render repeated";frameRequested=true;
    }
    void openPatternSampleRender(bool instrument,std::optional<ScreamSeq::PatternSampleRenderWindow::Target> captured={}){
        if(busy)throw std::runtime_error("Wait for the document worker before opening selection rendering");
        if(directSampleRenderCompletion.retained()){reviewDirectSampleRender();return;}
        if(!view->session.document.value("editable",false))throw std::runtime_error("The current song cannot be edited");
        if(!patternSampleRenderWindow){patternSampleRenderWindow=std::make_unique<ScreamSeq::PatternSampleRenderWindow>(window,
            [this](const auto &method,const auto &params){if(method=="document.get")return view->session.document;return documentOperation(method,params);},
            [this]{return patternSampleTarget();},
            [this](const auto &document,const auto &result){revealCapturedSample(document,result);},
            [this](const auto &method,const auto &params,const auto &receipt){return documentOperationWithOutcome(method,params,receipt);});
            connectSampleCaptureTransport(*patternSampleRenderWindow);}
        patternSampleRenderWindow->openAt(instrument,captured?std::move(*captured):patternSampleTarget());
    }
    bool patternContextClick(float x,float y){
        const auto g=patternGridRect();
        if(selecting&&g.contains(x,y)&&x>=g.x+gutter&&y>=g.y+gridHeader){
            const auto r=firstRow+unsigned((y-g.y-gridHeader)/rowHeight);
            const float content=x-g.x-gutter+horizontalScroll;unsigned c=firstChannel();while(c<view->channels&&channelStart(c+1)<=content)++c;
            const auto s=selection();
            if(r>=s.at("startRow").get<unsigned>()&&r<=s.at("endRow").get<unsigned>()&&c>=s.at("startChannel").get<unsigned>()&&c<=s.at("endChannel").get<unsigned>()){
                workspaceState.focus="pattern";SetFocus(window);return true;
            }
        }
        return gridClick(x,y,false);
    }
    bool sampleCaptureContextMenu(HWND source,POINT screen){
        bool inSamples=source==controls.at(sampleCommandBase);
        if(screen.x==-1&&screen.y==-1){
            inSamples=inSamples||workspaceState.focus=="samples";if(!inSamples)return false;
            RECT r{};GetWindowRect(controls.at(sampleCommandBase),&r);screen={r.left+12,r.top+12};
        }else if(!inSamples){
            POINT point=screen;ScreenToClient(window,&point);const float scale=96.f/GetDpiForWindow(window);
            inSamples=sampleEditorVisible()&&sampleEditorRect().contains(point.x*scale,point.y*scale);
            if(!inSamples)return false;
        }
        HMENU menu=CreatePopupMenu();if(!menu)return false;
        AppendMenuW(menu,MF_STRING,sampleRecordCommand,L"Record a sample…");AppendMenuW(menu,MF_STRING,sampleBrowseCommand,L"Browse samples…");
        AppendMenuW(menu,MF_STRING,sampleImportCommand,L"Import audio files…");AppendMenuW(menu,MF_STRING,sampleDetailCommand,L"Sample detail…");
        const auto selected=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,screen.x,screen.y,0,window,nullptr);DestroyMenu(menu);if(selected)command(int(selected));return true;
    }
