    std::unique_ptr<ScreamSeq::NativeMenuBar> menuBar;
    HWND menuFocus{};
    Json menuGuard;
    bool menuArmed=false;
    std::wstring menuPreviousStatus,menuHint;
    static bool menuTextControl(HWND focus){wchar_t type[64]{};GetClassNameW(focus,type,64);return !_wcsicmp(type,L"EDIT")||!_wcsnicmp(type,L"RichEdit",8);}
    Json menuTarget() {
        Json result={{"document",documentId},{"revision",view->session.revision},{"context",contextRevision},
            {"position",position()},{"selection",selection()},{"panel",workspaceState.focus},{"inspector",inspectorCanvasFocus},
            {"loop",playbackLoop},{"live",liveKeyboard},{"playing",device.running()&&!auditionOnly},
            {"rack",{selectedPlugin,selectedParameter,pluginDetailsTarget,pluginDetailsRevision,pluginDraftGeneration}}};
        if(menuTextControl(menuFocus)){
            const auto length=GetWindowTextLengthW(menuFocus);std::wstring text(size_t(length)+1,0);GetWindowTextW(menuFocus,text.data(),int(text.size()));text.resize(size_t(length));
            DWORD first=0,last=0;SendMessageW(menuFocus,EM_GETSEL,reinterpret_cast<WPARAM>(&first),reinterpret_cast<LPARAM>(&last));
            result["text"]=utf8Path(text);result["caret"]={first,last};
        }
        if(!menuTextControl(menuFocus)){
            if(workspaceMainCanvasFocused("samples"))result["sample"]=workspaceMenuTarget("sample");
            else if(workspaceState.focus=="pattern")result["pattern"]=workspaceMenuTarget("pattern");
            else if(workspaceState.focus=="graph")result["graph"]=workspaceMenuTarget("graph");
        }
        return result;
    }
    ScreamSeq::NativeMenuBar::State menuCommandState(int id)const {
        using State=ScreamSeq::NativeMenuBar::State;
        const auto found=std::find_if(workspaceCommands.begin(),workspaceCommands.end(),[&](const auto &entry){return entry.id==id;});
        if(found==workspaceCommands.end())throw std::runtime_error("Application menu references an unknown command");
        const auto slash=found->label.find(L" / ");State result;
        result.label=slash==std::wstring::npos?found->label:found->label.substr(slash+3);result.shortcut=workspaceShortcutLabel(id);
        if(id==exitCommand&&result.shortcut.empty())result.shortcut=L"Alt+F4";
        const auto refuse=[&](const wchar_t *reason){result.enabled=false;result.reason=reason;};
        const auto focus=menuArmed?menuFocus:GetFocus();
        const bool text=menuTextControl(focus),focusedEdit=id>=copyFocusedCommand&&id<=selectAllFocusedCommand;
        const bool localText=text&&(focusedEdit||id==undoCommand||id==redoCommand);
        if(preparingWorkspaceLayout||documentDrafts.departing()||(!localText&&(busy||recoveryRestoring)))refuse(L"Finish the current document operation first");
        if((id==pluginAutomate||id==pluginActivity)&&!hasPluginParameterTarget())refuse(L"Select an available plugin parameter in the rack first");
        // USER32 captures this owner while tracking the native menu. That is
        // not an editor drag; retain the guard for real drags and other capture.
        const auto capture=GetCapture();GUITHREADINFO gui{sizeof(gui)};
        const bool nativeMenuCapture=capture==window&&GetGUIThreadInfo(GetCurrentThreadId(),&gui)&&
            (gui.flags&GUI_INMENUMODE)&&gui.hwndMenuOwner==window;
        if(dragging||(capture&&!nativeMenuCapture))refuse(L"Finish the current mouse gesture first");
        if(text&&(focusedEdit||id==undoCommand||id==redoCommand)){
            const bool writable=IsWindowEnabled(focus)&&!(GetWindowLongPtrW(focus,GWL_STYLE)&ES_READONLY);
            DWORD first=0,last=0;SendMessageW(focus,EM_GETSEL,reinterpret_cast<WPARAM>(&first),reinterpret_cast<LPARAM>(&last));
            wchar_t type[64]{};GetClassNameW(focus,type,64);const bool rich=!_wcsnicmp(type,L"RichEdit",8);
            if(id==undoCommand&&(!writable||!SendMessageW(focus,EM_CANUNDO,0,0)))refuse(L"No text edit to undo");
            if(id==redoCommand&&(!writable||!rich||!SendMessageW(focus,EM_CANREDO,0,0)))refuse(L"No text edit to redo");
            if((id==copyFocusedCommand||id==cutFocusedCommand||id==deleteFocusedCommand)&&first==last)refuse(L"Select text first");
            if((id==cutFocusedCommand||id==deleteFocusedCommand||id==pasteFocusedCommand)&&!writable)refuse(L"This text field is read-only");
            if(id==pasteFocusedCommand&&!IsClipboardFormatAvailable(CF_UNICODETEXT)&&!IsClipboardFormatAvailable(CF_TEXT))refuse(L"The clipboard has no text");
        }else{
            if(focusedEdit&&!(focus==window&&(workspaceState.focus=="pattern"&&trackerWorkspaceVisible()||workspaceMainCanvasFocused("samples"))))refuse(L"Focus a text field, pattern or sample selection first");
            if(id==undoCommand&&!view->session.document.value("canUndo",false))refuse(L"No song edit to undo");
            if(id==redoCommand&&!view->session.document.value("canRedo",false))refuse(L"No song edit to redo");
        }
        if(id==patternRowsLargerCommand&&rowHeight>=34)refuse(L"Pattern rows are already at their largest size");
        if(id==patternRowsSmallerCommand&&rowHeight<=18)refuse(L"Pattern rows are already at their smallest size");
        if(id==followCommand)result.checked=follow;
        else if(id==playbackLoopCommand)result.checked=playbackLoop;
        else if(id==liveKeysCommand)result.checked=liveKeyboard;
        else if(id==dockToggle)result.checked=workspaceState.lowerVisible;
        else if(id==composeCommand)result.checked=workspaceState.layout=="Compose";
        else if(id==patternCommand)result.checked=workspaceState.layout=="Pattern focus";
        else if(id==soundCommand)result.checked=workspaceState.layout=="Sound design";
        if(id==patternRenderOptionsCommand)result.label=directSampleRenderCompletion.retained()?L"Review selection render result":L"Render selection options…";
        return result;
    }
    void refreshMenuBar(){if(menuBar)menuBar->refresh([this](int id){return menuCommandState(id);});}
    void installMenuBar(){
        using Item=ScreamSeq::NativeMenuBar::Item;
        const auto commands=[](std::initializer_list<int> ids){std::vector<Item> result;for(const auto id:ids)result.push_back({id,id?L"Command":L""});return result;};
        std::vector<Item> menus={
            {0,L"&File",true,false,commands({newDocumentCommand,openCommand,openDemoCommand,0,saveCommand,saveAsCommand,0,recoveryCommand,documentLoadReportCommand,0,exitCommand})},
            {0,L"&Edit",true,false,commands({undoCommand,redoCommand,0,cutFocusedCommand,copyFocusedCommand,pasteFocusedCommand,deleteFocusedCommand,selectAllFocusedCommand,0,patternToolsCommand})},
            {0,L"&Song",true,false,commands({arrangementCommand,arrangementMatrixCommand,songTimingCommand,songPropertiesCommand,0,noteTrackCreateCommand,noteTrackGroupCommand,noteTrackUngroupCommand,0,newPatternCommand,duplicatePatternCommand,editSectionCommand})},
            {0,L"&Playback",true,false,commands({togglePlaybackCommand,stopCommand,0,playCursorCommand,playSelectionCommand,playSelectionCursorCommand,0,playbackLoopCommand,followCommand,liveKeysCommand,0,auditionCommand,midiRecordingCommand})},
            {0,L"&View",true,false,commands({paletteCommand,0,composeCommand,patternCommand,soundCommand,connectedWorkspaceCommand,graphEditingWorkspaceCommand,0,layoutsCommand,saveLayoutCommand,restoreLayoutCommand,dockToggle})},
            {0,L"&Tools",true,false,{
                {0,L"Pattern",true,false,commands({effectPickerCommand,effectsCommand,notesCommand,graphCommandsCommand,scratchGesturesCommand,patternRenderOptionsCommand,0,positionRulerCommand,patternRowsLargerCommand,patternRowsSmallerCommand})},
                {0,L"Sound",true,false,commands({sampleBrowseCommand,sampleImportCommand,sampleDetailCommand,sampleRecordCommand,instrumentEnvelopeCommand,instrumentImportCommand,pluginsCommand,pluginBrowse,0,mixerCommand,mixerRouting,graphCommand,graphWorkflowCommand})},
                {0,L"Automation",true,false,commands({parameterAutomationCommand,absoluteAutomationCommand,parameterActivityCommand,0,pluginAutomate,pluginActivity,0,curveReference})},
                {0,L"Preferences",true,false,commands({audioSettingsCommand,keyboardSettingsCommand,reloadShortcutsCommand})}}}};
        menuBar=std::make_unique<ScreamSeq::NativeMenuBar>(window,std::move(menus));refreshMenuBar();
    }
    void beginMenuBar(){
        if(!menuBar)return;menuArmed=false;menuGuard=nullptr;cancelWorkspaceShortcut();releaseTypedNotes();
        menuFocus=GetFocus();menuGuard=menuTarget();menuArmed=true;menuPreviousStatus=status;menuHint.clear();refreshMenuBar();
    }
    void endMenuBar(){if(!menuHint.empty()&&status==menuHint)status=menuPreviousStatus;menuHint.clear();frameRequested=true;}
    void menuBarHint(unsigned wire){if(!menuBar)return;const auto id=menuBar->command(wire);if(!id)return;
        const auto state=menuCommandState(id);menuHint=state.label;if(!state.reason.empty())menuHint+=L" / "+state.reason;status=menuHint;frameRequested=true;}
    bool menuTargetCurrent(int id){
        const auto current=menuTarget();if(current.at("document")!=menuGuard.at("document"))return false;
        const bool edit=id>=copyFocusedCommand&&id<=selectAllFocusedCommand;
        if(menuTextControl(menuFocus)&&(edit||id==undoCommand||id==redoCommand))
            return current.at("text")==menuGuard.at("text")&&current.at("caret")==menuGuard.at("caret");
        // Playback-follow movement must not prevent saving, stopping, opening
        // preferences or choosing a view. Selection edits keep the full guard.
        switch(id){
        case stopCommand:case exitCommand:case paletteCommand:case layoutsCommand:case audioSettingsCommand:case keyboardSettingsCommand:case reloadShortcutsCommand:
        case composeCommand:case patternCommand:case soundCommand:case connectedWorkspaceCommand:case graphEditingWorkspaceCommand:return true;
        case playbackLoopCommand:return current.at("loop")==menuGuard.at("loop");
        case liveKeysCommand:return current.at("live")==menuGuard.at("live");
        case followCommand:return current.at("position").at("following")==menuGuard.at("position").at("following");
        case togglePlaybackCommand:return current.at("playing")==menuGuard.at("playing");
        case newDocumentCommand:case openCommand:case openDemoCommand:case saveCommand:case saveAsCommand:case recoveryCommand:case documentLoadReportCommand:
        case undoCommand:case redoCommand:case songPropertiesCommand:case songTimingCommand:return current.at("revision")==menuGuard.at("revision");
        default:return current==menuGuard;
        }
    }
    bool dispatchMenuBar(unsigned wire){
        if(!menuBar)return false;const auto id=menuBar->command(wire);if(!id)return false;
        const bool captured=menuArmed;menuArmed=false;const auto state=menuCommandState(id);endMenuBar();
        if(!captured||!IsWindow(menuFocus)||GetFocus()!=menuFocus||!menuTargetCurrent(id)){status=L"Menu target changed / reopen the menu before editing";return true;}
        if(!state.enabled){status=state.reason;return true;}
        rejectDepartureInput();
        if(menuTextControl(menuFocus)){
            UINT message=0;switch(id){case undoCommand:message=WM_UNDO;break;case redoCommand:message=EM_REDO;break;case copyFocusedCommand:message=WM_COPY;break;
                case pasteFocusedCommand:message=WM_PASTE;break;case cutFocusedCommand:message=WM_CUT;break;case deleteFocusedCommand:message=WM_CLEAR;break;
                case selectAllFocusedCommand:SendMessageW(menuFocus,EM_SETSEL,0,-1);return true;default:break;}
            if(message){SendMessageW(menuFocus,message,0,0);return true;}
        }
        runWorkspaceShortcut(id);return true;
    }
