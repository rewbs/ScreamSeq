struct MenuEntry {std::wstring text;UINT state{};};
MenuEntry menuEntry(HMENU menu,unsigned command){
    for(int i=0;i<GetMenuItemCount(menu);++i){
        if(const auto child=GetSubMenu(menu,i)){
            try{return menuEntry(child,command);}catch(const std::out_of_range &){}
        }else if(GetMenuItemID(menu,i)==ScreamSeq::NativeMenuBar::commandBase+command){
            wchar_t text[512]{};MENUITEMINFOW item{sizeof(item)};item.fMask=MIIM_STRING|MIIM_STATE;item.dwTypeData=text;item.cch=unsigned(std::size(text));
            restoreCheck(GetMenuItemInfoW(menu,UINT(i),TRUE,&item),"Read actual native menu item");return {text,item.fState};
        }
    }
    throw std::out_of_range("Missing application menu command");
}
void menuBegin(RestoreApplication &app){SendMessageW(app.window,WM_ENTERMENULOOP,FALSE,0);SendMessageW(app.window,WM_INITMENU,reinterpret_cast<WPARAM>(GetMenu(app.window)),0);}
void menuChoose(RestoreApplication &app,int id){SendMessageW(app.window,WM_EXITMENULOOP,FALSE,0);SendMessageW(app.window,WM_COMMAND,ScreamSeq::NativeMenuBar::commandBase+id,0);}
void applicationMenuBar(){
    {
        RestoreFixture fixture;auto &app=*fixture.app;
        app.restoreLayoutConfiguration(layoutFor(app,"notes"));
        app.workspaceEditorRequest({{"panel","preciseNotes"},{"focus",true}});
        const auto owner=app.preciseNoteWindow->window();ScreamSeq::Tests::ownGuiWindow(owner);
        SendMessageW(GetDlgItem(owner,9201),BM_CLICK,0,0); // Hit page owns list focus.
        const auto list=GetDlgItem(owner,ScreamSeq::PreciseNoteWindow::list);
        restoreCheck(GetFocus()==list,"Precise Hit page must focus its list");
        const auto before=app.documentOperation("pattern.notes.get",{{"pattern",0}});
        const auto pattern=app.view->pattern(0);
        auto key=[&](WPARAM code,bool ctrl=false,bool shift=false){
            struct Keys{BYTE saved[256]{};Keys(){restoreCheck(GetKeyboardState(saved)!=FALSE,"Read private Hit-list modifiers");}~Keys(){SetKeyboardState(saved);}}keys;
            BYTE modifiers[256]{};std::copy(std::begin(keys.saved),std::end(keys.saved),std::begin(modifiers));
            modifiers[VK_CONTROL]=ctrl?0x80:0;modifiers[VK_SHIFT]=shift?0x80:0;modifiers[VK_MENU]=0;
            restoreCheck(SetKeyboardState(modifiers)!=FALSE,"Set private Hit-list modifiers");
            // Match the app message pump: EDIT consumes translated Ctrl+Z
            // (WM_CHAR 26), while an editor-handled chord suppresses that char.
            // Direct WM_KEYDOWN alone does not exercise Windows text Undo.
            MSG message{};message.hwnd=GetFocus();message.message=WM_KEYDOWN;message.wParam=code;
            message.lParam=1|(LPARAM(MapVirtualKeyW(UINT(code),MAPVK_VK_TO_VSC))<<16);
            TranslateMessage(&message);DispatchMessageW(&message);
            MSG character{};
            while(PeekMessageW(&character,message.hwnd,WM_CHAR,WM_DEADCHAR,PM_REMOVE))DispatchMessageW(&character);
        };
        key(VK_BACK);const auto removed=app.documentOperation("pattern.notes.get",{{"pattern",0}});
        restoreCheck(app.preciseNoteWindow->snapshot().at("draftCount")==0&&GetFocus()==list,"Hit-list Backspace must delete and retain list focus");
        key('Z',true);
        restoreCheck(app.documentOperation("pattern.notes.get",{{"pattern",0}})==before&&app.view->pattern(0).cells==pattern.cells&&
            app.preciseNoteWindow->snapshot().at("draftCount")==1&&GetFocus()==list,"Hit-list Ctrl+Z must undo the deletion through shared history");
        key('Y',true);
        restoreCheck(app.documentOperation("pattern.notes.get",{{"pattern",0}})==removed&&app.preciseNoteWindow->snapshot().at("draftCount")==0&&GetFocus()==list,"Hit-list Ctrl+Y must redo the deletion");
        key('Z',true);key('Z',true,true);
        restoreCheck(app.documentOperation("pattern.notes.get",{{"pattern",0}})==removed&&GetFocus()==list,"Hit-list Ctrl+Shift+Z must use configured Redo");
        key('Z',true);
        const auto field=GetDlgItem(owner,ScreamSeq::PreciseNoteWindow::velocity);
        rawDraftText(field,L"-");const auto beforeTextUndo=songState(app);key('Z',true);
        restoreCheck(restoreText(field)!=L"-"&&songState(app)==beforeTextUndo&&GetFocus()==field,"Velocity Ctrl+Z must remain native text Undo");
    }
    {
        RestoreFixture fixture;auto &app=*fixture.app;saveDepartureFixture(fixture);
        app.restoreLayoutConfiguration(layoutFor(app,"notes"));
        // The fixture root is sized in pixels. At 200% DPI this is a compact
        // tabbed workspace: restoring a placement does not select its tab.
        // Enter the actual editor before sending text or capturing File Save.
        app.workspaceEditorRequest({{"panel","preciseNotes"},{"focus",true}});
        const auto owner=app.preciseNoteWindow->window();ScreamSeq::Tests::ownGuiWindow(owner);
        const auto field=GetDlgItem(owner,ScreamSeq::PreciseNoteWindow::velocity);
        rawDraftText(field,L"94");SetFocus(field);SendMessageW(field,EM_SETSEL,1,2);
        restoreCheck(GetFocus()==field&&IsWindowVisible(field)&&IsWindowEnabled(field),"Save fixture must begin in the visible enabled velocity field");
        restoreCheck(app.preciseNoteWindow->queuedMusicalEdit(),"Save fixture must begin with an unsubmitted musical edit");
        menuBegin(app);menuChoose(app,saveCommand);
        DWORD first=0,last=0;SendMessageW(field,EM_GETSEL,reinterpret_cast<WPARAM>(&first),reinterpret_cast<LPARAM>(&last));
        const auto evidence=RestoreJson{{"focus",reinterpret_cast<uintptr_t>(GetFocus())},{"field",reinterpret_cast<uintptr_t>(field)},
            {"selection",{first,last}},{"raw",utf8Path(restoreText(field))},{"dirty",app.view->dirty},
            {"status",utf8Path(app.status)},{"preciseNotes",app.preciseNoteWindow->snapshot()}}.dump();
        restoreCheck(GetFocus()==field,("Native File Save lost field focus: "+evidence).c_str());
        restoreCheck(first==1&&last==2,("Native File Save lost text selection: "+evidence).c_str());
        restoreCheck(restoreText(field)==L"94",("Native File Save changed raw text: "+evidence).c_str());
        restoreCheck(!app.preciseNoteWindow->retainedDraft(),("Native File Save retained the queued draft: "+evidence).c_str());
        restoreCheck(!app.view->dirty,("Native File Save did not save the project: "+evidence).c_str());
        restoreCheck(app.preciseNoteWindow->snapshot().at("selectedEvent").at("velocity")==94,
            ("Native File Save omitted the queued note: "+evidence).c_str());
    }
    {
        RestoreFixture fixture;auto &app=*fixture.app;
        app.restoreLayoutConfiguration(layoutFor(app,"notes"));
        app.workspaceEditorRequest({{"panel","preciseNotes"},{"focus",true}});
        const auto owner=app.preciseNoteWindow->window();ScreamSeq::Tests::ownGuiWindow(owner);
        const auto beforeNotes=app.documentOperation("pattern.notes.get",{{"pattern",0}});
        // pattern.get is served by the API snapshot adapter, not the worker's
        // documentOperation dispatcher. Compare the same immutable source here.
        const auto beforePattern=app.view->pattern(0);
        restoreCheck(!app.view->session.document.value("canUndo",false),"Queued-note menu fixture needs empty document history");
        rawDraftText(GetDlgItem(owner,ScreamSeq::PreciseNoteWindow::velocity),L"94");SetFocus(owner);
        restoreCheck(app.preciseNoteWindow->queuedMusicalEdit()&&app.canUndoSong()&&!app.canRedoSong(),"Queued note did not expose shared Undo availability");
        menuBegin(app);restoreCheck(!(menuEntry(GetMenu(app.window),undoCommand).state&MFS_DISABLED),"Edit menu disabled Undo for the first queued note edit");
        menuChoose(app,undoCommand);
        restoreCheck(app.documentOperation("pattern.notes.get",{{"pattern",0}})==beforeNotes&&
            app.view->pattern(0).cells==beforePattern.cells&&app.view->pattern(0).rows==beforePattern.rows&&
            app.view->pattern(0).channels==beforePattern.channels&&
            !app.preciseNoteWindow->retainedDraft()&&app.view->session.document.value("canRedo",false),
            "Native menu Undo did not finish and undo the latest queued precise note");
        SetFocus(owner);menuBegin(app);menuChoose(app,redoCommand);
        restoreCheck(app.preciseNoteWindow->snapshot().at("selectedEvent").at("velocity")==94&&!app.preciseNoteWindow->retainedDraft(),
            "Native menu Redo failed to restore the precise edit and current editor");
        const auto after=songState(app);SendMessageW(owner,WM_TIMER,0x5345,0);
        restoreCheck(songState(app)==after,"Consumed precise timer replayed after native menu history");
    }
    {
        RestoreFixture fixture;auto &app=*fixture.app;const auto before=songState(app);
        app.openSongProperties();const auto title=GetDlgItem(app.songPropertiesWindow->window(),8601);
        const auto original=restoreText(title);rawDraftText(title,L"Palette local text");
        app.palette->show();const auto popup=app.palette->window();ScreamSeq::Tests::ownGuiWindow(popup);
        const auto search=GetDlgItem(popup,101),list=GetDlgItem(popup,102),run=GetDlgItem(popup,103);
        SetWindowTextW(search,L"edit undo");
        restoreCheck(SendMessageW(list,LB_GETCOUNT,0,0)==1&&IsWindowEnabled(run),"Palette Undo did not use the originating text history");
        SendMessageW(search,WM_KEYDOWN,VK_RETURN,0);
        restoreCheck(!IsWindowVisible(popup)&&restoreText(title)==original&&GetFocus()==title&&songState(app)==before,
            "Palette text Undo edited the song, query or wrong field");
        app.songPropertiesWindow->hide();SetFocus(app.window);app.palette->show();SetWindowTextW(search,L"edit undo");
        restoreCheck(!IsWindowEnabled(run),"Palette song Undo ignored empty shared history");
        SetWindowTextW(search,L"selected plugin parameter controls");
        restoreCheck(SendMessageW(list,LB_GETCOUNT,0,0)==1&&!IsWindowEnabled(run),"Palette plugin action ignored its missing rack target");
        SetWindowTextW(search,L"new song");
        SetCapture(run);SendMessageW(popup,WM_TIMER,1,0);
        const bool buttonAvailable=IsWindowEnabled(run);ReleaseCapture();
        restoreCheck(buttonAvailable,"Palette Run button capture was mistaken for an editor drag");
        app.busy=true;SendMessageW(popup,WM_TIMER,1,0);
        restoreCheck(!IsWindowEnabled(run)&&GetFocus()==search&&restoreText(search)==L"new song","Palette busy refresh lost query focus or enabled New");
        SendMessageW(search,WM_KEYDOWN,VK_RETURN,0);app.busy=false;
        restoreCheck(IsWindowVisible(popup)&&songState(app)==before,"Unavailable palette command changed the document or dismissed search");
        SendMessageW(popup,WM_TIMER,1,0);restoreCheck(IsWindowEnabled(run),"Palette did not recover availability when the worker became free");
        SendMessageW(search,WM_KEYDOWN,VK_ESCAPE,0);
    }
    {
        RestoreFixture fixture;auto &app=*fixture.app;const auto menu=GetMenu(app.window);
        restoreCheck(menu&&GetMenuItemCount(menu)==6,"Six native application menu categories are missing");
        const auto before=songState(app);menuBegin(app);
        restoreCheck(menuEntry(menu,newDocumentCommand).text.find(L"Ctrl+N")!=std::wstring::npos,"Native File menu omitted its configured New shortcut");
        restoreCheck(menuEntry(menu,undoCommand).state&MFS_DISABLED,"Initial song Undo menu is not disabled");
        SendMessageW(app.window,WM_EXITMENULOOP,FALSE,0);
        app.workspaceShortcuts->set(app.shortcutCommandID(saveCommand),{"ctrl+alt+s"});menuBegin(app);
        const auto save=menuEntry(menu,saveCommand).text;
        restoreCheck(save.find(L"Ctrl+Alt+S")!=std::wstring::npos&&save.find(L"\tCtrl+S")==std::wstring::npos,"Menu retained a superseded shortcut");
        SendMessageW(app.window,WM_EXITMENULOOP,FALSE,0);restoreCheck(songState(app)==before,"Opening menus or changing their binding labels edited the song");
        app.openSongProperties();const auto title=GetDlgItem(app.songPropertiesWindow->window(),8601);const auto original=restoreText(title);
        rawDraftText(title,L"Text edit only");menuBegin(app);restoreCheck(!(menuEntry(menu,undoCommand).state&MFS_DISABLED),"Text Undo incorrectly uses song history availability");
        menuChoose(app,undoCommand);restoreCheck(restoreText(title)==original&&songState(app)==before&&GetFocus()==title,"Edit menu Undo changed music or lost native field focus");
        rawDraftText(title,L"Raw title retained");menuBegin(app);menuChoose(app,selectAllFocusedCommand);
        DWORD first=0,last=0;SendMessageW(title,EM_GETSEL,reinterpret_cast<WPARAM>(&first),reinterpret_cast<LPARAM>(&last));
        restoreCheck(first==0&&last==18&&songState(app)==before,"Edit menu Select All did not stay inside the focused field");
        menuBegin(app);rawDraftText(title,L"Newer raw title");menuChoose(app,deleteFocusedCommand);
        restoreCheck(restoreText(title)==L"Newer raw title"&&songState(app)==before&&app.status.find(L"Menu target changed")!=std::wstring::npos,"Stale menu modified newer native text");
    }
    {
        RestoreFixture fixture;auto &app=*fixture.app;menuBegin(app);app.edit("document.patch",{{"title","Changed during menu"}});const auto changed=songState(app);
        menuChoose(app,undoCommand);restoreCheck(songState(app)==changed&&app.status.find(L"Menu target changed")!=std::wstring::npos,"Menu silently undid a newer document revision");
        SetFocus(app.window);menuBegin(app);menuChoose(app,undoCommand);restoreCheck(app.view->session.document.at("title")!="Changed during menu","Fresh native Edit Undo did not reach shared history");
        SetFocus(app.window);menuBegin(app);auto next=app.position();next["row"]=7;app.navigate(next);
        menuChoose(app,songPropertiesCommand);restoreCheck(app.songPropertiesWindow&&app.songPropertiesWindow->visible(),"Playback/cursor movement prevented opening document properties");
        app.songPropertiesWindow->hide();SetFocus(app.window);menuBegin(app);next=app.position();next["row"]=9;app.navigate(next);const auto before=songState(app);
        menuChoose(app,patternToolsCommand);restoreCheck(!app.patternToolsWindow&&songState(app)==before,"Stale selection menu opened an editor on a different target");
        SetFocus(app.window);menuBegin(app);next=app.position();next["row"]=11;app.navigate(next);
        menuChoose(app,mixingGraphPanelCommand);
        restoreCheck(app.songRoutingWindow&&app.songRoutingWindow->visible()&&app.songRoutingWindow->docked()&&songState(app)==before,
            "Cursor movement blocked whole-song graph navigation or changed the song");
        // Invoke from the actual graph owner. Focusing Main while its logical
        // canvas is still the graph creates an invalid focus that layout must
        // repair, correctly invalidating a menu captured before that repair.
        SetFocus(app.songRoutingWindow->window());menuBegin(app);next=app.position();next["row"]=12;app.navigate(next);
        menuChoose(app,navigationPatternCommand);
        restoreCheck(GetFocus()==app.window&&app.workspaceState.focus=="pattern","Pattern menu did not focus the visible tracker");
        restoreCheck(app.position().at("row")==12&&songState(app)==before,"Pattern menu restored an old position or changed music");
        const auto layout=app.workspaceDockSnapshot().at("configuration");
        for(const auto id:{parameterRecordingCommand,pluginRecoveryReviewCommand}){
            SetFocus(app.window);menuBegin(app);next=app.position();next["row"]=next.at("row").get<int>()+1;app.navigate(next);
            menuChoose(app,id);
            const bool visible=id==parameterRecordingCommand?app.parameterRecordingWindow&&app.parameterRecordingWindow->visible():
                app.pluginRecoveryReviewWindow&&app.pluginRecoveryReviewWindow->visible();
            restoreCheck(visible,"Playback/cursor movement blocked document-global recording review navigation");
            const auto tool=id==parameterRecordingCommand?app.parameterRecordingWindow->window():app.pluginRecoveryReviewWindow->window();
            ScreamSeq::Tests::ownGuiWindow(tool);
            if(id==parameterRecordingCommand)app.parameterRecordingWindow->hide();else app.pluginRecoveryReviewWindow->hide();
            restoreCheck(app.workspaceDockSnapshot().at("configuration")==layout&&songState(app)==before,
                "Recording review navigation replaced the dock layout or edited music");
        }
        SetFocus(app.window);SetCapture(app.window);menuBegin(app);
        const bool capturedDisabled=(menuEntry(GetMenu(app.window),newDocumentCommand).state&MFS_DISABLED)!=0;
        SendMessageW(app.window,WM_EXITMENULOOP,FALSE,0);ReleaseCapture();
        restoreCheck(capturedDisabled,"Non-menu mouse capture no longer protects document commands");
        app.dragging=1;menuBegin(app);
        const bool draggingDisabled=(menuEntry(GetMenu(app.window),newDocumentCommand).state&MFS_DISABLED)!=0;
        SendMessageW(app.window,WM_EXITMENULOOP,FALSE,0);app.dragging=0;
        restoreCheck(draggingDisabled,"Active editor drag no longer protects document commands");
        app.busy=true;SetFocus(app.window);menuBegin(app);restoreCheck(menuEntry(GetMenu(app.window),newDocumentCommand).state&MFS_DISABLED,"Busy native File command remained enabled");
        SendMessageW(app.window,WM_EXITMENULOOP,FALSE,0);app.busy=false;
    }
}
