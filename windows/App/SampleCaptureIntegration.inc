    std::unique_ptr<ScreamSeq::SampleRecordingWindow> sampleRecordingWindow;
    std::unique_ptr<ScreamSeq::PatternSampleRenderWindow> patternSampleRenderWindow;
    bool recordingTakeKnown=false;
    bool directSampleRenderPending=false;
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
            [this](const auto &method,const auto &params){return documentOperation(method,params);},
            [this]{return std::pair(documentId,view->session.revision);},
            [this](const auto &document,const auto &result){revealCapturedSample(document,result);});
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
        const auto target=captured?std::move(*captured):patternSampleTarget();
        if(target.document!=documentId||target.revision!=view->session.revision)throw std::runtime_error("Song changed while choosing Render / choose the selection again");
        const Json params={{"pattern",target.pattern},{"firstRow",target.firstRow},{"lastRow",target.lastRow},{"firstChannel",target.firstChannel},{"lastChannel",target.lastChannel},
            {"name","Rendered selection"},{"tailSeconds",0},{"createInstrument",instrument},{"dryRun",false},{"expectedRevision",target.revision}};
        directSampleRenderTarget=params;directSampleRenderTarget["documentId"]=target.document;directSampleRenderReport=Json::object();
        directSampleRenderPending=true;struct Guard{bool &value;~Guard(){value=false;}} guard{directSampleRenderPending};
        directSampleRenderReport=documentOperation("sample.renderSelection",params);
        if(target.document!=documentId)return;
        revealCapturedSample(target.document,directSampleRenderReport);
        status=L"Rendered P"+std::to_wstring(target.pattern)+L" rows "+std::to_wstring(target.firstRow)+L"–"+std::to_wstring(target.lastRow)+L" / sample "+std::to_wstring(directSampleRenderReport.at("sample").get<unsigned>());
        if(const auto slot=directSampleRenderReport.value("instrument",0u))status+=L" / instrument "+std::to_wstring(slot)+L" selected";
        status+=L" / one Undo";frameRequested=true;
    }
    void openPatternSampleRender(bool instrument,std::optional<ScreamSeq::PatternSampleRenderWindow::Target> captured={}){
        if(busy)throw std::runtime_error("Wait for the document worker before opening selection rendering");
        if(!view->session.document.value("editable",false))throw std::runtime_error("The current song cannot be edited");
        if(!patternSampleRenderWindow){patternSampleRenderWindow=std::make_unique<ScreamSeq::PatternSampleRenderWindow>(window,
            [this](const auto &method,const auto &params){return documentOperation(method,params);},
            [this]{return patternSampleTarget();},
            [this](const auto &document,const auto &result){revealCapturedSample(document,result);});
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
