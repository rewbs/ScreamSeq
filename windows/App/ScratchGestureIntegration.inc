    std::unique_ptr<ScreamSeq::ScratchGestureWindow> scratchGestureWindow;
    std::optional<ScreamSeq::ScratchPatternTarget> scratchTargetAt(unsigned p,unsigned r,unsigned c,unsigned fx)const{
        if(busy||!view->session.document.value("editable",false)||!view->patterns.contains(p)||r>=view->pattern(p).rows||c>=view->channels||fx>=view->effectColumns.at(c))return {};
        ScreamSeq::ScratchPatternTarget target;target.document=documentId;target.revision=view->session.revision;target.pattern=p;target.row=r;target.channel=c;target.column=fx;target.rows=view->pattern(p).rows;target.rowsPerBeat=view->patternRowsPerBeat.at(p);
        for(const auto &[id,index]:view->nativePattern->patternIndexes)if(index==p){target.patternID=id;break;}
        for(const auto &[id,index]:view->nativePattern->trackChannels)if(index==c){target.trackID=id;break;}
        if(!target.patternID||!target.trackID)return {};
        if(const auto command=view->effect(p,r,c,fx))target.command=ScreamSeq::PatternFields::command(*command);
        return target;
    }
    std::optional<ScreamSeq::ScratchPatternTarget> resolveScratchTarget(const ScreamSeq::ScratchPatternTarget &target)const{
        if(target.document!=documentId)return {};const auto p=view->nativePattern->patternIndexes.find(target.patternID),c=view->nativePattern->trackChannels.find(target.trackID);
        if(p==view->nativePattern->patternIndexes.end()||c==view->nativePattern->trackChannels.end())return {};
        return scratchTargetAt(p->second,target.row,c->second,target.column);
    }
    void openScratchGestures(){
        if(busy)throw std::runtime_error("Wait for the document worker before opening Scratch phrases");
        if(!scratchGestureWindow)scratchGestureWindow=std::make_unique<ScreamSeq::ScratchGestureWindow>(window,
            [this](const auto &method,const auto &params){if(method=="scratch.transport.toggle"){if(device.running()&&!auditionOnly)stop();else command(playCommand);return Json::object();}return documentOperation(method,params);},
            [this]{return std::pair(documentId,view->session.revision);},
            [this]{return scratchTargetAt(patternIndex,row,channel,column>=3?(column-3)/2:0);},
            [this](const auto &target){return resolveScratchTarget(target);},
            [this](const auto &target,bool preview){const auto resolved=resolveScratchTarget(target);if(!resolved)throw std::runtime_error("The captured pattern cell no longer exists");
                if(preview)play({{"pattern",resolved->pattern},{"startRow",resolved->row},{"endRow",resolved->rows},{"cursorRow",resolved->row},{"loop",playbackLoop}});
                else{auto n=position();n["pattern"]=resolved->pattern;n["row"]=resolved->row;n["channel"]=resolved->channel;n["column"]=3+2*resolved->column;n["following"]=false;navigate(n);selecting=false;ensureCursorVisible();SetForegroundWindow(window);SetFocus(window);}});
        scratchGestureWindow->openAt();
    }
    bool scratchDoubleClick(float x,float y){
        if(!patternGridRect().contains(x,y)||!gridClick(x,y,false)||column<3)return false;
        const auto effect=view->effect(patternIndex,row,channel,(column-3)/2);
        if(!effect||effect->kind!=Tracker::PatternCommandKind::Native||effect->native!=Tracker::NativePatternOp::Scratch)return false;
        openScratchGestures();return true;
    }
    bool scratchContextMenu(POINT screen){
        if(screen.x==-1&&screen.y==-1){if(workspaceState.focus!="pattern")return false;const auto r=patternGridRect();screen={LONG((r.x+gutter+20)*GetDpiForWindow(window)/96),LONG((r.y+gridHeader+20)*GetDpiForWindow(window)/96)};ClientToScreen(window,&screen);}
        else{POINT client=screen;ScreenToClient(window,&client);const float scale=96.f/GetDpiForWindow(window);if(!gridClick(client.x*scale,client.y*scale,false))return false;}
        HMENU menu=CreatePopupMenu();if(!menu)return false;AppendMenuW(menu,MF_STRING,scratchGesturesCommand,L"Scratch phrases…");AppendMenuW(menu,MF_STRING,effectsCommand,L"Edit FX…");
        const auto selected=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,screen.x,screen.y,0,window,nullptr);DestroyMenu(menu);if(selected)command(int(selected));return true;
    }
