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
        RestoreFixture fixture;auto &app=*fixture.app;saveDepartureFixture(fixture);
        app.restoreLayoutConfiguration(layoutFor(app,"notes"));
        const auto owner=app.preciseNoteWindow->window();ScreamSeq::Tests::ownGuiWindow(owner);
        const auto field=GetDlgItem(owner,ScreamSeq::PreciseNoteWindow::velocity);
        rawDraftText(field,L"94");SetFocus(field);SendMessageW(field,EM_SETSEL,1,2);
        menuBegin(app);menuChoose(app,saveCommand);
        DWORD first=0,last=0;SendMessageW(field,EM_GETSEL,reinterpret_cast<WPARAM>(&first),reinterpret_cast<LPARAM>(&last));
        restoreCheck(GetFocus()==field&&first==1&&last==2&&restoreText(field)==L"94"&&
            !app.preciseNoteWindow->retainedDraft()&&!app.view->dirty&&
            app.preciseNoteWindow->snapshot().at("selectedEvent").at("velocity")==94,
            "Native File Save lost precise-note field focus/caret or omitted its queued edit");
    }
    {
        RestoreFixture fixture;auto &app=*fixture.app;
        app.restoreLayoutConfiguration(layoutFor(app,"notes"));
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
