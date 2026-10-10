void applicationSongProperties() {
    const auto press=[](HWND window,int id){SendMessageW(window,WM_COMMAND,id,reinterpret_cast<LPARAM>(GetDlgItem(window,id)));};
    {
        RestoreFixture fixture;auto &app=*fixture.app;
        MINMAXINFO mainLimits{};SendMessageW(app.window,WM_GETMINMAXINFO,0,reinterpret_cast<LPARAM>(&mainLimits));
        SetWindowPos(app.window,nullptr,0,0,mainLimits.ptMinTrackSize.x,mainLimits.ptMinTrackSize.y,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        app.layoutControls();RECT mainClient{};GetClientRect(app.window,&mainClient);
        for(int id:{songPropertiesCommand,documentLoadReportCommand,saveAsCommand,playbackLoopCommand}){RECT bounds{};const auto control=app.controls.at(id);GetWindowRect(control,&bounds);MapWindowPoints(nullptr,app.window,reinterpret_cast<POINT *>(&bounds),2);
            restoreCheck(IsWindowVisible(control)&&bounds.left>=0&&bounds.top>=0&&bounds.right<=mainClient.right&&bounds.bottom<=mainClient.bottom,"Song/file entry point clips at the main-window minimum size");}
        app.command(songPropertiesCommand);auto *tool=app.songPropertiesWindow.get();
        restoreCheck(tool!=nullptr,"Song settings command did not create its native owner");
        const auto window=tool->window();ScreamSeq::Tests::ownGuiWindow(window);
        restoreCheck(ScreamSeq::Tests::accessibleName(GetDlgItem(window,8601))==L"Song title"&&
            ScreamSeq::Tests::accessibleName(GetDlgItem(window,8602))==L"Channel count","Song fields lack native accessible names");
        const auto original=app.view->session.document;const auto count=app.view->channels;
        rawDraftText(GetDlgItem(window,8601),L"Retained title & channels");rawDraftText(GetDlgItem(window,8602),L"-");
        const auto before=songState(app);press(window,8605);
        restoreCheck(songState(app)==before&&restoreText(GetDlgItem(window,8602))==L"-","Invalid channel text changed the song or was discarded");
        SetFocus(GetDlgItem(window,8602));app.updateSongTools();app.command(songPropertiesCommand);
        restoreCheck(GetFocus()==GetDlgItem(window,8602)&&restoreText(GetDlgItem(window,8602))==L"-","Refresh/reopen overwrote a raw song draft or stole focus");
        rawDraftText(GetDlgItem(window,8602),std::to_wstring(original.at("formatLimits").at("channelsMax").get<unsigned>()+1).c_str());press(window,8605);
        restoreCheck(songState(app)==before,"Out-of-format channel count reached musical mutation");
        rawDraftText(GetDlgItem(window,8602),std::to_wstring(count+1).c_str());
        app.edit("document.patch",{{"title","External title"}});const auto changed=songState(app);press(window,8605);
        restoreCheck(songState(app)==changed&&tool->snapshot().at("stale")==true,"Stale song form silently rebased its draft");
        press(window,8603);press(window,8605);
        const auto applied=app.view->session.document;
        restoreCheck(applied.at("title")=="Retained title & channels"&&app.view->channels==count+1&&tool->snapshot().at("completed")==true,"Native title/count Apply did not use one captured patch");
        app.edit("history.undo",{{"domain","all"}});
        restoreCheck(app.view->channels==count&&app.view->session.document.at("title")=="External title"&&app.view->session.document.at("tracks")==original.at("tracks"),"One Undo did not restore title, count and stable channel identities");
        app.edit("history.redo",{{"domain","all"}});
        restoreCheck(app.view->channels==count+1&&app.view->session.document.at("tracks")==applied.at("tracks"),"Redo changed channel identities");
        const auto path=fixture.folder/L"song-properties.screamseq";
        app.documentOperation("document.save",{{"expectedRevision",app.view->session.revision},{"path",utf8Path(path)}});
        app.documentOperation("document.open",{{"expectedRevision",app.view->session.revision},{"path",utf8Path(path)}});
        restoreCheck(!app.songPropertiesWindow&&!IsWindow(window)&&app.view->channels==count+1&&
            app.view->session.document.at("title")==applied.at("title")&&app.view->session.document.at("tracks")==applied.at("tracks"),"Save/reopen lost settings or retained their old owner");
        app.openSongProperties();tool=app.songPropertiesWindow.get();const auto next=tool->window();ScreamSeq::Tests::ownGuiWindow(next);
        rawDraftText(GetDlgItem(next,8601),L"Unsaved raw properties");tool->hide();const auto retained=songState(app);
        const auto refused=restoreRejected([&]{app.documentOperation("document.new",{{"expectedRevision",app.view->session.revision},{"discard",true}});});
        restoreCheck(!refused.empty()&&songState(app)==retained&&!tool->visible(),"Hidden song settings draft did not protect document replacement");
        app.openSongProperties();press(next,8604);restoreCheck(tool->snapshot().at("dirty")==false&&restoreText(GetDlgItem(next,8601))==L"Retained title & channels","Explicit Reload did not restore stored song values");
        MINMAXINFO limits{};SendMessageW(next,WM_GETMINMAXINFO,0,reinterpret_cast<LPARAM>(&limits));
        SetWindowPos(next,nullptr,0,0,limits.ptMinTrackSize.x,limits.ptMinTrackSize.y,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);RECT client{};GetClientRect(next,&client);
        for(int id:{8601,8602,8603,8604,8605,8607,8608,8650,8651,8652,8653,8654}){RECT bounds{};GetWindowRect(GetDlgItem(next,id),&bounds);MapWindowPoints(nullptr,next,reinterpret_cast<POINT *>(&bounds),2);
            restoreCheck(IsWindowVisible(GetDlgItem(next,id))&&bounds.left>=0&&bounds.top>=0&&bounds.right<=client.right&&bounds.bottom<=client.bottom,"Song properties control clips at its minimum size");}
        SetFocus(GetDlgItem(next,8601));SendMessageW(GetDlgItem(next,8601),WM_KEYDOWN,VK_ESCAPE,0);
        restoreCheck(!tool->visible()&&GetFocus()==app.window,"Escape did not retain the properties form and return to the pattern");
    }
    {
        RestoreFixture fixture;auto &app=*fixture.app;app.openSongProperties();auto *tool=app.songPropertiesWindow.get();const auto window=tool->window();ScreamSeq::Tests::ownGuiWindow(window);
        rawDraftText(GetDlgItem(window,8601),L"Committed title");unsigned writes=0;
        app.sampleMutationCompletion=[&](const std::string &method){if(method=="document.patch"){++writes;rawDraftText(GetDlgItem(window,8601),L"Newer unfinished title");throw std::runtime_error("Owned song properties completion failure");}};
        press(window,8605);app.sampleMutationCompletion={};
        restoreCheck(writes==1&&tool->snapshot().at("completion").at("returned")==true&&app.view->session.document.at("title")=="Committed title","Song form did not retain its committed receipt");
        press(window,8605);restoreCheck(writes==1&&tool->snapshot().at("completion").is_null()&&tool->snapshot().at("dirty")==true&&restoreText(GetDlgItem(window,8601))==L"Newer unfinished title","Song result Review replayed the patch or overwrote newer text");
    }
    {
        RestoreFixture fixture;auto &app=*fixture.app;unsigned writes=0;
        ScreamSeq::SongPropertiesWindow tool(app.window,{
            [&]{return app.songPropertiesContext();},[&](const auto &method,const auto &params){return app.documentOperation(method,params);},
            [&](const auto &method,const auto &params,const auto &)->ScreamSeq::Api::CompletedCall{++writes;(void)app.documentOperation(method,params);throw std::runtime_error("Owned unknown settings outcome");},[]{} });
        tool.open();const auto window=tool.window();ScreamSeq::Tests::ownGuiWindow(window);rawDraftText(GetDlgItem(window,8601),L"Unknown settings outcome");press(window,8605);
        restoreCheck(writes==1&&tool.snapshot().at("completion").at("returned")==false,"Song settings fixture did not lose its completion evidence");
        press(window,8605);restoreCheck(writes==1&&tool.snapshot().at("observation").is_object(),"Song settings Review did not read current state without repeating its write");
        app.edit("document.patch",{{"title","Changed after review"}});press(window,8606);
        restoreCheck(tool.snapshot().at("completion").is_object(),"Stale settings observation was accepted");
        press(window,8605);press(window,8606);
        restoreCheck(writes==1&&tool.snapshot().at("completion").is_null()&&tool.snapshot().at("dirty")==true,"Accept observed settings discarded intent or replayed the patch");
    }
}
