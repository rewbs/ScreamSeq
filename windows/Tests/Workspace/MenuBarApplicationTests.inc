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
