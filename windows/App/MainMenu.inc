    std::map<int,std::wstring> menuLabels;
    void menuCommand(int id) {
        const auto focus=GetFocus();wchar_t type[32]{};GetClassNameW(focus,type,32);
        if(!_wcsicmp(type,L"Edit")||!_wcsnicmp(type,L"RichEdit",8)){
            UINT message=0;
            if(id==copyFocusedCommand)message=WM_COPY;
            if(id==cutFocusedCommand)message=WM_CUT;
            if(id==pasteFocusedCommand)message=WM_PASTE;
            if(id==deleteFocusedCommand)message=WM_CLEAR;
            if(id==undoCommand)message=WM_UNDO;
            if(message){SendMessageW(focus,message,0,0);return;}
            if(id==redoCommand||id==redoAlternateCommand){
                if(!_wcsnicmp(type,L"RichEdit",8))SendMessageW(focus,EM_REDO,0,0);
                return; // The standard Edit control has no separate Redo operation.
            }
            if(id==selectAllFocusedCommand){SendMessageW(focus,EM_SETSEL,0,-1);return;}
        }
        runWorkspaceShortcut(id);
    }
    void refreshMenuLabels() {
        const auto menu=GetMenu(window);if(!menu)return;
        for(const auto &[id,label]:menuLabels){
            auto caption=label;const auto shortcut=workspaceShortcutLabel(id);
            if(!shortcut.empty())caption+=L"\t"+shortcut;
            MENUITEMINFOW item{sizeof(item)};item.fMask=MIIM_STRING;item.dwTypeData=caption.data();
            SetMenuItemInfoW(menu,id,FALSE,&item);
        }
        DrawMenuBar(window);
    }
    void installMainMenu() {
        auto menu=CreateMenu();if(!menu)throw std::runtime_error("Cannot create menu bar");
        auto section=[&](const wchar_t *name,std::initializer_list<std::pair<int,const wchar_t *>> entries){
            auto popup=CreatePopupMenu();
            for(const auto &[id,label]:entries){
                if(!id)AppendMenuW(popup,MF_SEPARATOR,0,nullptr);
                else {AppendMenuW(popup,MF_STRING,id,label);menuLabels[id]=label;}
            }
            AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(popup),name);
        };
        section(L"&File",{{newSongCommand,L"&New Song"},{openCommand,L"&Open…"},{recoveryCommand,L"Recover a Song…"},{0,L""},
            {saveCommand,L"&Save"},{saveAsCommand,L"Save &As…"},{0,L""},{sampleImportCommand,L"Import Sample…"},
            {instrumentImportCommand,L"Import Instrument…"},{sampleBrowseCommand,L"Browse Samples…"},{0,L""},{exitCommand,L"E&xit"}});
        section(L"&Edit",{{undoCommand,L"&Undo"},{redoCommand,L"&Redo"},{0,L""},{cutFocusedCommand,L"Cu&t"},
            {copyFocusedCommand,L"&Copy"},{pasteFocusedCommand,L"&Paste"},{deleteFocusedCommand,L"&Delete"},{selectAllFocusedCommand,L"Select &All"},
            {0,L""},{selectChannelCommand,L"Select Channel"},{selectRowCommand,L"Select Row"},{selectPatternCommand,L"Select Pattern"},
            {0,L""},{patternPasteMix,L"Mix Paste (Fill Empty Fields)"},{patternPasteMerge,L"Merge Paste (Skip Empty Source Fields)"},
            {0,L""},{audioSettingsCommand,L"Audio Settings…"}});
        section(L"&View",{{paletteCommand,L"Search &Commands and Shortcuts…"},{layoutsCommand,L"&Layouts…"},
            {composeCommand,L"Compose"},{patternCommand,L"Pattern Focus"},{soundCommand,L"Sound Design"},{0,L""},
            {focusPatternCommand,L"Focus Pattern"},{nextPanelCommand,L"Next Panel"},{dockToggle,L"Toggle Bottom Dock"},
            {0,L""},{notesCommand,L"Precise Notes…"},{samplesCommand,L"Sample and Loops"},{instrumentEnvelopeCommand,L"Instrument…"},
            {effectsCommand,L"Pattern FX"},{pluginsCommand,L"Plugins"},{mixerCommand,L"Mixer"},{graphCommand,L"Routing and Modulation Graph"},
            {parameterAutomationCommand,L"Parameter Automation…"},{arrangementCommand,L"Arrangement…"},{songTimingCommand,L"Tempo and Groove…"}});
        section(L"&Pattern",{{previousInstrumentCommand,L"Previous Instrument"},{nextInstrumentCommand,L"Next Instrument"},
            {octaveDownCommand,L"Octave Down"},{octaveUpCommand,L"Octave Up"},{useInstrumentCommand,L"Use Instrument from Cursor"},{0,L""},
            {newPatternCommand,L"New Pattern…"},{duplicatePatternCommand,L"Duplicate Pattern…"},{0,L""},
            {patternInsertRows,L"Insert Row"},{patternDeleteRows,L"Delete Row"},{patternTransposeUp,L"Transpose Up"},{patternTransposeDown,L"Transpose Down"},
            {patternReverse,L"Reverse Selection"},{patternExpand,L"Expand Selection"},{patternShrink,L"Shrink Selection"}});
        section(L"&Playback",{{togglePlaybackCommand,L"Play / Stop"},{stopCommand,L"Stop"},{followCommand,L"Toggle Follow"},
            {liveKeysCommand,L"Live Musical Keyboard"},{midiRecordingCommand,L"MIDI Recording…"}});
        section(L"&Help",{{keyboardHelpCommand,L"Keyboard &Shortcuts…"},{aboutCommand,L"&About ScreamSeq"}});
        if(!SetMenu(window,menu)){DestroyMenu(menu);throw std::runtime_error("Cannot install menu bar");}
        refreshMenuLabels();
    }
    void selectPatternRegion(int id) {
        if(!trackerWorkspaceVisible()||workspaceState.focus!="pattern"||textOwnsKeyboard()||ScreamSeq::NativeToolWindow::belongsToTool(GetFocus()))throw std::runtime_error("Focus the pattern first");
        const auto firstRow=id==selectRowCommand?row:0,firstChannel=id==selectChannelCommand?channel:0;
        auto next=position();next["row"]=id==selectRowCommand?row:patternRows()-1;
        next["channel"]=id==selectChannelCommand?channel:view->channels-1;next["following"]=false;
        next["column"]=std::min(column,2u+2u*view->effectColumns.at(next["channel"].get<unsigned>()));
        navigate(next);anchorRow=firstRow;anchorChannel=firstChannel;selecting=true;SetFocus(window);frameRequested=true;
    }
    void patternInputCommand(int id) {
        if(!trackerWorkspaceVisible()||workspaceState.focus!="pattern"||textOwnsKeyboard())throw std::runtime_error("Focus the pattern first");
        Json input={{"expectedRevision",view->session.revision},{"expectedContext",documentId+":context:"+std::to_string(contextRevision)+":"+selection().dump()}};
        if(id==octaveDownCommand||id==octaveUpCommand)input["octave"]=unsigned(std::clamp(int(octave)+(id==octaveUpCommand?1:-1),0,8));
        else if(id==useInstrumentCommand){
            unsigned slot=view->cell(patternIndex,row,channel).instrument;
            if(!slot)for(const auto &event:view->notesAt(patternIndex,row,channel))if(event.note.instrument){slot=event.note.instrument;break;}
            if(!slot)throw std::runtime_error("This cell has no instrument number");input["instrument"]=slot;
        } else {
            std::vector<unsigned> slots;for(const auto &entry:typingCatalog(typingSample)){const auto slot=entry.at("index").get<unsigned>();if(slot&&slot<=255)slots.push_back(slot);}
            if(slots.empty())throw std::runtime_error("Import a sample or create an instrument first");
            std::sort(slots.begin(),slots.end());const auto current=inputInstrumentOverride.value_or(typingSound);
            if(id==nextInstrumentCommand){const auto it=std::upper_bound(slots.begin(),slots.end(),current);input["instrument"]=it==slots.end()?slots.back():*it;}
            else {auto it=std::lower_bound(slots.begin(),slots.end(),current);input["instrument"]=it==slots.begin()?slots.front():*--it;}
        }
        workspace("workspace.input",input);
    }
