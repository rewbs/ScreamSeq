// Sole precise-row ownership lives in PreciseNoteWindow. Main retains only
// published context, selection serial, shell placement and compatibility entry points.
    std::unique_ptr<ScreamSeq::PreciseNoteWindow> preciseNoteWindow;
    Json preciseNoteSelectionIdentity=Json::array();
    uint64_t preciseNoteSelectionSerial=0;
    ScreamSeq::PreciseNoteWindow::Context preciseNoteContext()const {
        const auto source=view; // Immutable catalogues and arbitrary-cell reads share this owner.
        ScreamSeq::PreciseNoteWindow::Context value;
        value.document=source->session.documentId;value.revision=source->session.revision;
        value.patterns=source->session.document.at("patterns");value.tracks=source->session.document.at("tracks");
        value.noteNames.assign(source->noteNames.begin(),source->noteNames.end());
        value.selectionGeneration=preciseNoteSelectionSerial;value.busy=busy||recoveryRestoring;
        value.cell=[source](unsigned pattern,unsigned row,unsigned channel){const auto cell=source->cell(pattern,row,channel);
            return ScreamSeq::PreciseNoteWindow::SeedCell{cell.note,cell.instrument,cell.volumeCommand,cell.volume,cell.effect,cell.parameter};};
        if(source->patterns.contains(patternIndex)&&row<source->pattern(patternIndex).rows&&channel<source->channels){
            std::string patternID,trackID;
            for(const auto &item:value.patterns)if(item.at("index")==patternIndex)patternID=item.at("id").get<std::string>();
            for(const auto &item:value.tracks)if(item.at("index")==channel)trackID=item.at("id").get<std::string>();
            if(!patternID.empty()&&!trackID.empty())value.selected=ScreamSeq::PreciseNoteWindow::Target{patternID,trackID,patternIndex,row,channel};
        }return value;
    }
    Json preciseNoteSelectionGuard()const {
        const auto context=preciseNoteContext();Json target=nullptr;
        if(context.selected)target={{"patternID",context.selected->patternID},{"trackID",context.selected->trackID},
            {"pattern",context.selected->pattern},{"row",context.selected->row},{"channel",context.selected->channel}};
        return {{"document",context.document},{"revision",context.revision},{"selectionGeneration",context.selectionGeneration},{"target",std::move(target)}};
    }
    void notePreciseNoteSelection(){
        const auto context=preciseNoteContext();auto identity=Json::array({context.document,patternIndex,row,channel});
        identity.push_back(context.selected?Json(context.selected->patternID):Json());identity.push_back(context.selected?Json(context.selected->trackID):Json());
        if(identity!=preciseNoteSelectionIdentity){preciseNoteSelectionIdentity=std::move(identity);++preciseNoteSelectionSerial;}
        if(preciseNoteWindow)preciseNoteWindow->observeContext();
    }
    void connectPreciseNoteTyping(){
        if(!preciseNoteWindow)return;const auto owner=preciseNoteWindow->window();
        preciseNoteWindow->musicalTyping([this,owner](HWND focus,WPARAM key,bool repeat){
            if(!liveKeyboard||(GetKeyState(VK_CONTROL)&0x8000)||(GetKeyState(VK_MENU)&0x8000)||typingTextControl(focus))return false;
            const auto offset=musicalOffset(key);if(offset<0)return false;if(repeat||typedNotes.contains(key))return true;
            beginTypedNote(key,typingTarget(typingSample,typingSound,std::min(120u,1+octave*12+unsigned(offset)),owner));return true;
        },[this](WPARAM key){return releaseTypedKey(key);},[this,owner]{releaseTypedNotes(owner);});
    }
    std::unique_ptr<ScreamSeq::PreciseNoteWindow> makePreciseNoteWindow(std::shared_ptr<bool> preparing={}){
        return std::make_unique<ScreamSeq::PreciseNoteWindow>(window,
            [this,preparing](const auto &method,const auto &params){return preparing&&*preparing?workspacePreparationRead(method,params):documentOperation(method,params);},
            [this]{return preciseNoteContext();},[this]{workspaceEditorRequest({{"panel","preciseNotes"},{"return",true}});});
    }
    void openNoteEditor(){
        if(busy||recoveryRestoring||preciseNotePending())throw std::runtime_error("Wait for the current document operation");
        notePreciseNoteSelection();if(!preciseNoteWindow){preciseNoteWindow=makePreciseNoteWindow();connectPreciseNoteTyping();}
        configureWorkspaceEditor("preciseNotes");EditorPlacementGuard placement(changingEditorPlacement);
        const auto &state=workspaceEditors[workspaceEditorIndex("preciseNotes")];
        if(preciseNoteWindow->capturedTarget()&&!state.origin.empty()&&state.pinned){preciseNoteWindow->show();SetFocus(preciseNoteWindow->window());}
        else preciseNoteWindow->openAt();finishWorkspaceEditorOpen("preciseNotes");
    }
    bool preciseNotePending()const{return preciseNoteWindow&&preciseNoteWindow->pending();}
    bool preciseNoteVisible()const{return preciseNoteWindow&&preciseNoteWindow->visible();}
    Json preciseNoteOperationGuard()const{return preciseNoteWindow?preciseNoteWindow->operationGuard():Json::object();}
    Json preciseNoteSnapshot()const{
        if(!preciseNoteWindow)return {{"visible",false},{"pending",false},{"captured",false},{"draftCount",0},{"selected",-1},{"selectedEvent",nullptr}};
        auto result=preciseNoteWindow->snapshot(false); // Never materialize the large row array for workspace polling.
        result["captured"]=preciseNoteWindow->capturedTarget().has_value();result["coordinateSpace"]="ownerClientDIP";return result;
    }
    bool noteCommand(int id){if(id<noteAdd||id>noteRepeat)return false;if(!preciseNoteWindow)openNoteEditor();return preciseNoteWindow->dispatchLegacyAction(id);}
