    std::vector<ScreamSeq::WorkspaceCommand> workspaceCommands;
    std::unique_ptr<ScreamSeq::WorkspaceShortcuts> workspaceShortcuts;
    ScreamSeq::WorkspaceShortcutHandler workspaceShortcutHandler;
    std::string shortcutContextIdentity;
    uint64_t shortcutContextSerial=0;
    std::wstring shortcutHint,shortcutPreviousStatus;
    static std::string shortcutCommandID(int id){return "windows.command."+std::to_string(id);}
    static std::vector<std::string> defaultShortcutKeys(int id) {
        switch(id) {
        case paletteCommand:return {"ctrl+k"};case layoutsCommand:return {"ctrl+alt+w"};
        case liveKeysCommand:return {"ctrl+alt+l"};case dockToggle:return {"ctrl+j"};
        case openCommand:return {"ctrl+o"};case saveCommand:return {"ctrl+s"};case saveAsCommand:return {"ctrl+shift+s"};
        case undoCommand:return {"ctrl+z"};case redoCommand:return {"ctrl+y"};case redoAlternateCommand:return {"ctrl+shift+z"};
        case copyFocusedCommand:return {"ctrl+c"};case pasteFocusedCommand:return {"ctrl+v"};case cutFocusedCommand:return {"ctrl+x"};
        case deleteFocusedCommand:return {"delete"};case selectAllFocusedCommand:return {"ctrl+a"};case patternPasteMix:return {"ctrl+shift+v"};
        case sampleImportCommand:return {"ctrl+i"};case composeCommand:return {"ctrl+1"};case patternCommand:return {"ctrl+2"};case soundCommand:return {"ctrl+3"};
        case pinCommand:return {"ctrl+p"};case cursorCommand:return {"ctrl+home"};case returnCommand:return {"alt+left"};
        case nextPanelCommand:return {"f6"};case followCommand:return {"f"};case togglePlaybackCommand:return {"space"};case stopCommand:return {"escape"};
        case widerCommand:return {"ctrl+alt+left"};case narrowerCommand:return {"ctrl+alt+right"};
        case tallerCommand:return {"ctrl+alt+up"};case shorterCommand:return {"ctrl+alt+down"};
        default:return {};
        }
    }
    void configureWorkspaceCommands() {
        workspaceCommands=defaultWorkspaceCommands();
        workspaceCommands.insert(workspaceCommands.begin(),{
            {paletteCommand,L"Workspace / Search commands and shortcuts",L""},
            {copyFocusedCommand,L"Edit / Copy focused selection",L""},
            {pasteFocusedCommand,L"Edit / Paste into focused selection",L""},
            {cutFocusedCommand,L"Edit / Cut focused sample selection",L""},
            {deleteFocusedCommand,L"Edit / Delete focused selection",L""},
            {selectAllFocusedCommand,L"Edit / Select all sample frames",L""},
            {togglePlaybackCommand,L"Playback / Play or stop",L""},
            {redoAlternateCommand,L"Edit / Redo (alternate shortcut)",L""},
            {reloadShortcutsCommand,L"Workspace / Reload saved shortcuts",L""}});
        std::vector<ScreamSeq::WorkspaceShortcuts::Definition> definitions;
        for(auto &entry:workspaceCommands) {
            auto keys=defaultShortcutKeys(entry.id);
            // Context hints describe intrinsic local navigation, not a second
            // global binding that can silently survive Clear or reassignment.
            if(keys.empty()&&entry.id!=playCommand&&entry.id!=copyCommand&&entry.id!=pasteCommand&&entry.id!=clearCommand&&entry.id!=sampleCopyCommand&&entry.id!=samplePasteCommand&&entry.id!=sampleCutCommand&&entry.id!=sampleClearCommand&&entry.id!=sampleAllCommand)
                entry.contextHint=entry.shortcut;
            entry.shortcut.clear();
            if(entry.id==playCommand)entry.label=L"Playback / Play";
            if(entry.id==focusPatternCommand||entry.id==graphLanesFocus)entry.contextHint.clear();
            definitions.push_back({shortcutCommandID(entry.id),std::move(keys)});
        }
        workspaceShortcuts=std::make_unique<ScreamSeq::WorkspaceShortcuts>(std::move(definitions));
        workspaceShortcutHandler=[this](WPARAM key,bool repeat,bool prefixOnly){return workspaceShortcutKey(key,repeat,prefixOnly);};
        if(!SetPropW(window,ScreamSeq::workspaceShortcutProperty,reinterpret_cast<HANDLE>(&workspaceShortcutHandler)))
            throw std::system_error(GetLastError(),std::system_category(),"Install workspace shortcuts");
    }
    void connectPaletteShortcuts() {
        auto change=[this](int id,auto update){
            try{update();}
            catch(const ScreamSeq::Api::ApiError &error){
                const std::string message=error.what(),prefix="Shortcut prefix conflict between ";
                if(error.code==-32602&&message.starts_with(prefix)){
                    const auto requested=shortcutCommandID(id);
                    for(const auto &entry:workspaceCommands)if(entry.id!=id){
                        const auto other=shortcutCommandID(entry.id);
                        if(message==prefix+requested+" and "+other||message==prefix+other+" and "+requested)
                            throw std::runtime_error("Conflicts with \""+utf8Path(entry.label)+"\". Choose another shortcut.");
                    }
                    throw std::runtime_error("This shortcut overlaps another command. Choose another shortcut.");
                }
                throw;
            }
            workspaceShortcutChanged();
        };
        palette->configureShortcuts([this](int id){return workspaceShortcuts->keys(shortcutCommandID(id));},
            [this,change](int id,const auto &keys){change(id,[&]{workspaceShortcuts->set(shortcutCommandID(id),keys);});},
            [this,change](int id){change(id,[&]{workspaceShortcuts->reset(shortcutCommandID(id));});});
    }
    void workspaceShortcutChanged() {
        shortcutTimer();
        if(palette)palette->refreshShortcuts();
        refreshShortcutControlLabels();
    }
    void refreshShortcutControlLabels() {
        if(!workspaceShortcuts||controls.empty())return;
        auto caption=[&](int id,const wchar_t *label){auto keys=workspaceShortcutLabel(id);std::wstring value=label;if(!keys.empty())value+=L" ["+keys+L"]";ScreamSeq::NativeControls::text(controls.at(id),value);};
        caption(paletteCommand,L"Commands");caption(followCommand,follow?L"Follow on":L"Detached");
    }
    std::wstring workspaceShortcutLabel(int id)const {
        const auto keys=workspaceShortcuts?workspaceShortcuts->keys(shortcutCommandID(id)):defaultShortcutKeys(id);
        std::wstring result;for(const auto &key:keys){if(!result.empty())result+=L" → ";auto label=wide(key);bool first=true;for(auto &c:label){if(first&&c>=L'a'&&c<=L'z')c=wchar_t(c-L'a'+L'A');first=c==L'+';}result+=label;}return result;
    }
    void loadWorkspaceShortcuts(const std::filesystem::path &path) {
        if(!workspaceShortcuts->load(path))status=wide(workspaceShortcuts->diagnostic());
        workspaceShortcutChanged();
    }
    Json workspaceCommandSnapshot()const {
        Json result=Json::array();
        for(const auto &entry:workspaceCommands)result.push_back({{"id",shortcutCommandID(entry.id)},{"name",utf8Path(entry.label)},
            {"keys",workspaceShortcuts->keys(shortcutCommandID(entry.id))},{"defaults",defaultShortcutKeys(entry.id)},
            {"customized",workspaceShortcuts->overridden(shortcutCommandID(entry.id))},{"contextHint",utf8Path(entry.contextHint)}});
        return {{"commands",result}};
    }
    Json setWorkspaceShortcut(const Json &p) {
        if(p.size()!=2||!p.contains("command")||!p["command"].is_string()||!p.contains("keys")||!p["keys"].is_array()||p["keys"].size()>4)
            throw ScreamSeq::Api::ApiError(-32602,"Supply a known command ID and zero to four shortcut strings");
        std::vector<std::string> keys;for(const auto &key:p["keys"]){if(!key.is_string())throw ScreamSeq::Api::ApiError(-32602,"Shortcut strokes must be strings");keys.push_back(key.get<std::string>());}
        const auto id=p["command"].get<std::string>();workspaceShortcuts->set(id,keys);workspaceShortcutChanged();
        return {{"command",id},{"keys",workspaceShortcuts->keys(id)}};
    }
    uint64_t workspaceShortcutContext() {
        const auto identity=documentId+"|"+view->session.revision+"|"+std::to_string(contextRevision)+"|"+selection().dump()+"|"+workspaceState.focus+"|"+std::to_string(reinterpret_cast<uintptr_t>(GetFocus()))+"|"+std::to_string(reinterpret_cast<uintptr_t>(GetActiveWindow()));
        if(identity!=shortcutContextIdentity){shortcutContextIdentity=identity;++shortcutContextSerial;}
        return shortcutContextSerial;
    }
    void shortcutTimer() {
        if(!workspaceShortcuts)return;
        workspaceShortcuts->expire(workspaceShortcutContext(),GetTickCount64());
        if(workspaceShortcuts->pending()) {
            if(shortcutHint.empty())shortcutPreviousStatus=status;
            shortcutHint=wide(workspaceShortcuts->hint())+L" / Esc cancels";status=shortcutHint;SetTimer(window,8,100,nullptr);
        } else {
            KillTimer(window,8);if(!shortcutHint.empty()&&status==shortcutHint)status=shortcutPreviousStatus;shortcutHint.clear();
        }
        frameRequested=true;
    }
    void cancelWorkspaceShortcut(){if(workspaceShortcuts){workspaceShortcuts->cancel();shortcutTimer();}}
    bool nativeOwnsShortcut(WPARAM key,bool ctrl,bool alt)const {
        const auto focus=GetFocus();wchar_t klass[64]{};GetClassNameW(focus,klass,64);
        const bool edit=_wcsicmp(klass,L"Edit")==0||_wcsnicmp(klass,L"RichEdit",8)==0,combo=_wcsicmp(klass,L"ComboBox")==0,list=_wcsicmp(klass,L"ListBox")==0||_wcsicmp(klass,L"SysListView32")==0;
        if(combo&&SendMessageW(focus,CB_GETDROPPEDSTATE,0,0))return true;
        if(edit||combo||list) {
            // Open selectors and editor-local Escape already took priority.
            // Closed main controls still allow configured Stop/return and
            // function-key workspace navigation; ordinary text stays native.
            if(!ctrl&&!alt)return key!=VK_ESCAPE&&!(key>=VK_F1&&key<=VK_F24);
            if(ctrl&&!alt&&(key=='A'||key=='C'||key=='V'||key=='X'||key=='Z'||key=='Y'||key==VK_SPACE||key==VK_INSERT||key==VK_LEFT||key==VK_RIGHT||key==VK_UP||key==VK_DOWN||key==VK_HOME||key==VK_END||key==VK_BACK||key==VK_DELETE))return true;
            if(alt&&!ctrl&&(key==VK_DOWN||key==VK_UP))return true;
        }
        return false;
    }
    bool workspaceShortcutKey(WPARAM key,bool repeat,bool prefixOnly=false) {
        if(!workspaceShortcuts)return false;
        const auto context=workspaceShortcutContext();workspaceShortcuts->expire(context,GetTickCount64());
        if(prefixOnly&&!workspaceShortcuts->pending()){shortcutTimer();return false;}
        const auto encoded=ScreamSeq::workspaceStrokeForKey(key);
        if(!encoded){
            const bool modifier=key==VK_CONTROL||key==VK_LCONTROL||key==VK_RCONTROL||key==VK_MENU||key==VK_LMENU||key==VK_RMENU||key==VK_SHIFT||key==VK_LSHIFT||key==VK_RSHIFT;
            if(!modifier)cancelWorkspaceShortcut();return false;
        }
        const bool ctrl=(GetKeyState(VK_CONTROL)&0x8000)!=0,alt=(GetKeyState(VK_MENU)&0x8000)!=0;
        if(!workspaceShortcuts->pending()&&nativeOwnsShortcut(key,ctrl,alt))return false;
        // Unmodified F is a hexadecimal digit outside the note column.
        if(!workspaceShortcuts->pending()&&!ctrl&&!alt&&key=='F'&&GetFocus()==window&&workspaceState.focus=="pattern"&&column!=0)return false;
        ScreamSeq::WorkspaceShortcuts::Stroke stroke;
        try{stroke=ScreamSeq::WorkspaceShortcuts::parseStroke(*encoded);}
        catch(const ScreamSeq::Api::ApiError &){cancelWorkspaceShortcut();return false;} // Reserved system chord.
        const auto decision=workspaceShortcuts->handle(stroke,repeat,context,GetTickCount64());
        shortcutTimer();
        if(!decision.command.empty()) {
            const auto found=std::find_if(workspaceCommands.begin(),workspaceCommands.end(),[&](const auto &c){return shortcutCommandID(c.id)==decision.command;});
            if(found!=workspaceCommands.end())runWorkspaceShortcut(found->id);
        }
        return decision.consumed;
    }
    void runWorkspaceShortcut(int id) {
        const auto focus=GetFocus();const auto panel=workspaceState.focus;
        if(id==patternPasteMix&&(!trackerWorkspaceVisible()||panel!="pattern"||ScreamSeq::NativeToolWindow::belongsToTool(focus))){
            status=L"Focus the pattern selection before mix paste";return;
        }
        const bool preserve=id==liveKeysCommand||id==togglePlaybackCommand||id==stopCommand||id==saveCommand||id==saveAsCommand||id==undoCommand||id==redoCommand||id==redoAlternateCommand;
        command(id);
        if(id==stopCommand&&!ScreamSeq::NativeToolWindow::belongsToTool(focus))command(focusPatternCommand);
        if(preserve&&ScreamSeq::NativeToolWindow::belongsToTool(focus)&&workspaceFocusVisible(focus)){workspaceState.focus=panel;SetFocus(focus);}
    }
    bool focusedEditingCommand(int id) {
        if(id==togglePlaybackCommand){if(device.running()&&!auditionOnly)stopAndFinishRecording();else play();return true;}
        if(id==redoAlternateCommand){command(redoCommand);return true;}
        if(id==nextPanelCommand){
            if(!trackerWorkspaceVisible())focusVisibleWorkspaceCanvas();
            else if(workspaceState.focus=="pattern"&&!view->graphPattern->lanes.empty())command(graphLanesFocus);
            else {workspaceState.focus=workspaceState.focus=="pattern"&&workspaceState.visible()?workspaceState.active:"pattern";SetFocus(window);layoutControls();}
            return true;
        }
        if(id<copyFocusedCommand||id>selectAllFocusedCommand)return false;
        if(!trackerWorkspaceVisible()||ScreamSeq::NativeToolWindow::belongsToTool(GetFocus()))throw std::runtime_error("Focus the tracker or sample selection first");
        if(workspaceState.focus=="samples")command(id==copyFocusedCommand?sampleCopyCommand:id==pasteFocusedCommand?samplePasteCommand:id==cutFocusedCommand?sampleCutCommand:id==deleteFocusedCommand?sampleClearCommand:sampleAllCommand);
        else if(workspaceState.focus=="pattern"&&(id==copyFocusedCommand||id==pasteFocusedCommand||id==deleteFocusedCommand))command(id==copyFocusedCommand?copyCommand:id==pasteFocusedCommand?pasteCommand:clearCommand);
        else throw std::runtime_error("Focus a matching pattern or sample selection first");
        return true;
    }
