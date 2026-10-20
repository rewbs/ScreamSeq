void applicationNoteTrackWindows() {
  const auto press=[](HWND window,int id){SendMessageW(window,WM_COMMAND,MAKEWPARAM(id,BN_CLICKED),LPARAM(GetDlgItem(window,id)));};
  {
    RestoreFixture fixture;auto &app=*fixture.app;const auto path=saveDepartureFixture(fixture);
    app.command(patternCommand);app.channel=1;app.anchorChannel=0;app.anchorRow=app.row;app.selecting=true;
    app.openNoteTrackEditor(false);auto *tool=app.groupNoteTrackWindow.get();const auto window=tool->window();ScreamSeq::Tests::ownGuiWindow(window);
    rawDraftText(GetDlgItem(window,7901),L"Captured chords");const auto captured=tool->snapshot().at("captured");
    auto next=app.position();next["channel"]=4;app.navigate(next);tool->hide();app.openNoteTrackEditor(false);
    restoreCheck(app.groupNoteTrackWindow.get()==tool&&tool->snapshot().at("captured")==captured&&restoreText(GetDlgItem(window,7901))==L"Captured chords",
      "Reopening Group replaced its retained owner, captured columns or raw name");
    restoreRejected([&]{app.documentOperation("document.open",departureOpenRequest(app,path));});
    const auto before=app.documentOperation("track.get",RestoreJson::object());const auto revision=app.view->session.revision;
    press(window,7905);restoreCheck(app.view->session.revision==revision&&tool->snapshot().at("preview").is_object(),"Native track preview changed history or lost its result");
    press(window,7906);const auto grouped=app.documentOperation("track.get",RestoreJson::object());
    restoreCheck(grouped.at("noteTracks").size()==1&&grouped.at("noteTracks")[0].at("channels")==RestoreJson::array({0,1})&&
      grouped.at("noteTracks")[0].at("name")=="Captured chords"&&app.channel==4&&tool->snapshot().at("completed")==true,
      "Group applied to the latest cursor instead of captured columns or moved navigation");
    app.edit("history.undo",{{"domain","all"}});restoreCheck(app.documentOperation("track.get",RestoreJson::object())==before,"Native Group needed more than one Undo");
    app.edit("history.redo",{{"domain","all"}});restoreCheck(app.documentOperation("track.get",RestoreJson::object())==grouped,"Native Group Redo changed identities");
    press(window,7904);rawDraftText(GetDlgItem(window,7901),L"Second captured name");app.edit("document.patch",{{"title","Changed elsewhere"}});
    const auto staleSong=songState(app);press(window,7906);
    restoreCheck(songState(app)==staleSong&&tool->snapshot().at("stale")==true&&restoreText(GetDlgItem(window,7901))==L"Second captured name",
      "Stale native Group retargeted a write or discarded its name");
    app.departureChoice=1002;restoreCheck(app.reviewNativeDeparture(),"Track draft was not included in native departure review");
    app.documentOperation("document.open",departureOpenRequest(app,path));
    restoreCheck(!app.groupNoteTrackWindow&&!IsWindow(window),"Accepted replacement left the old track owner alive");
  }
  {
    RestoreFixture fixture;auto &app=*fixture.app;app.openNoteTrackEditor(true);
    auto *tool=app.createNoteTrackWindow.get();const auto window=tool->window();ScreamSeq::Tests::ownGuiWindow(window);
    restoreCheck(ScreamSeq::Tests::accessibleName(GetDlgItem(window,7901))==L"Track name"&&
      ScreamSeq::Tests::accessibleName(GetDlgItem(window,7902))==L"Number of note columns"&&
      ScreamSeq::Tests::accessibleName(GetDlgItem(window,7903))==L"Shared track output","Note-track provider omitted field names");
    rawDraftText(GetDlgItem(window,7901),L"Pads");rawDraftText(GetDlgItem(window,7902),L"-");
    const auto before=app.documentOperation("track.get",RestoreJson::object());const auto revision=app.view->session.revision;
    press(window,7906);restoreCheck(app.view->session.revision==revision&&restoreText(GetDlgItem(window,7902))==L"-","Invalid count changed music or was overwritten");
    rawDraftText(GetDlgItem(window,7902),L"2");unsigned writes=0;
    app.sampleMutationCompletion=[&](const std::string &method){if(method=="track.create"){
      ++writes;rawDraftText(GetDlgItem(window,7901),L"Newer unfinished pad name");throw std::runtime_error("Owned track form completion failure");}};
    press(window,7906);
    restoreCheck(writes==1&&tool->snapshot().at("completion").at("returned")==true&&
      app.documentOperation("track.get",RestoreJson::object()).at("columns").size()==before.at("columns").size()+2,
      "Native Create failed to retain its committed result");
    app.sampleMutationCompletion={};press(window,7906);
    restoreCheck(writes==1&&tool->snapshot().at("completion").is_null()&&tool->snapshot().at("dirty")==true&&
      restoreText(GetDlgItem(window,7901))==L"Newer unfinished pad name","Track result Review replayed Create or discarded later typing");
    app.edit("history.undo",{{"domain","all"}});restoreCheck(app.documentOperation("track.get",RestoreJson::object())==before,"Native Create result recovery changed history cardinality");
    app.openNoteTrackEditor(false);restoreCheck(app.groupNoteTrackWindow&&app.createNoteTrackWindow.get()==tool,
      "Opening Group replaced the separate retained Create draft");
  }
  {
    RestoreFixture fixture;auto &app=*fixture.app;unsigned writes=0;
    ScreamSeq::NoteTrackWindow tool(app.window,true,{
      [&]{return app.noteTrackContext();},[&](const auto &method,const auto &params){return app.documentOperation(method,params);},
      [&](const auto &method,const auto &params,const auto &)->ScreamSeq::Api::CompletedCall{
        ++writes;(void)app.documentOperation(method,params);throw std::runtime_error("Owned track form unknown outcome");},[]{} });
    tool.open();const auto window=tool.window();ScreamSeq::Tests::ownGuiWindow(window);rawDraftText(GetDlgItem(window,7901),L"Unknown create");
    const auto count=app.view->channels;press(window,7906);
    restoreCheck(writes==1&&app.view->channels==count+3&&tool.snapshot().at("completion").at("returned")==false,
      "Unknown track fixture did not lose precisely its completion evidence");
    press(window,7906);restoreCheck(writes==1&&tool.snapshot().at("observation").is_object(),"Unknown track Review repeated the write or failed to observe state");
    app.edit("document.patch",{{"title","Changed after observation"}});press(window,7907);
    restoreCheck(!tool.snapshot().at("completion").is_null(),"Changed revision bypassed track observation admission");
    press(window,7906);press(window,7907);
    restoreCheck(writes==1&&tool.snapshot().at("completion").is_null()&&tool.snapshot().at("dirty")==true&&
      tool.snapshot().at("completed")==true&&app.view->channels==count+3,
      "Accepting observed track state repeated Create or discarded its raw draft");
  }
  {
    RestoreFixture fixture;auto &app=*fixture.app;
    app.edit("mixer.enable",RestoreJson::object());
    app.edit("mixer.bus.add",{{"kind","return"},{"name","Same destination"}});
    app.edit("mixer.bus.add",{{"kind","return"},{"name","Same destination"}});
    app.command(noteTrackCreateCommand);auto *tool=app.createNoteTrackWindow.get();
    restoreCheck(tool!=nullptr,"Native Create command did not open its form");
    const auto window=tool->window();ScreamSeq::Tests::ownGuiWindow(window);
    const auto destinations=tool->snapshot().at("captured").at("layout").at("destinations");
    const auto output=destinations.back().at("id");const auto combo=GetDlgItem(window,7903);
    restoreCheck(SendMessageW(combo,CB_GETCOUNT,0,0)==LRESULT(destinations.size()+1),"Duplicate destination names collapsed native choices");
    SendMessageW(combo,CB_SETCURSEL,destinations.size(),0);
    SendMessageW(window,WM_COMMAND,MAKEWPARAM(7903,CBN_SELCHANGE),LPARAM(combo));
    rawDraftText(GetDlgItem(window,7901),L"Destination identity");rawDraftText(GetDlgItem(window,7902),L"1");
    const auto before=songState(app);unsigned previews=0;
    app.sampleMutationCompletion=[&](const std::string &method){if(method=="track.create"){
      ++previews;rawDraftText(GetDlgItem(window,7901),L"Typed during preview");}};
    press(window,7905);app.sampleMutationCompletion={};
    restoreCheck(previews==1&&songState(app)==before&&tool->snapshot().at("preview").is_null()&&
      restoreText(GetDlgItem(window,7901))==L"Typed during preview","Earlier preview overwrote a newer track draft or changed the song");
    MINMAXINFO minimum{};SendMessageW(window,WM_GETMINMAXINFO,0,reinterpret_cast<LPARAM>(&minimum));
    restoreCheck(SetWindowPos(window,nullptr,0,0,minimum.ptMinTrackSize.x,minimum.ptMinTrackSize.y,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE),"Resize native track form");
    RECT client{};GetClientRect(window,&client);
    for(int id:{7901,7902,7903,7904,7905,7906,7908,7909,7950,7951,7952,7953,7954,7955,7956}) {
      const auto control=GetDlgItem(window,id);RECT bounds{};GetWindowRect(control,&bounds);
      MapWindowPoints(nullptr,window,reinterpret_cast<POINT *>(&bounds),2);
      restoreCheck(IsWindowVisible(control)&&bounds.left>=0&&bounds.top>=0&&bounds.right<=client.right&&bounds.bottom<=client.bottom,
        "Native track controls escaped minimum client bounds");
    }
    SetFocus(GetDlgItem(window,7901));SendMessageW(GetDlgItem(window,7901),WM_KEYDOWN,VK_F6,0);
    restoreCheck(GetFocus()==app.window&&tool->snapshot().at("draft").at("output")==output,"Pattern return lost focus or destination identity");
    app.command(noteTrackCreateCommand);restoreCheck(app.createNoteTrackWindow.get()==tool&&GetFocus()==GetDlgItem(window,7901),"Reopening Create replaced its owner or failed to return focus");
    SendMessageW(GetDlgItem(window,7901),WM_KEYDOWN,VK_ESCAPE,0);
    restoreCheck(!tool->visible()&&GetFocus()==app.window,"Escape did not retain/hide the native track form and return to Pattern");
    app.command(noteTrackCreateCommand);press(window,7906);
    const auto tracks=app.documentOperation("track.get",RestoreJson::object()).at("noteTracks");
    restoreCheck(tracks.size()==1&&tracks[0].at("output")==output&&tracks[0].at("name")=="Typed during preview",
      "Native Create resolved duplicate output labels by name or lost the newer draft");
  }
  {
    RestoreFixture fixture;auto &app=*fixture.app;
    app.edit("mixer.enable",RestoreJson::object());app.edit("mixer.bus.add",{{"kind","return"},{"name","Temporary output"}});
    app.openNoteTrackEditor(true);auto *tool=app.createNoteTrackWindow.get();const auto window=tool->window();
    ScreamSeq::Tests::ownGuiWindow(window);
    const auto destinations=tool->snapshot().at("captured").at("layout").at("destinations");const auto removed=destinations.back().at("id");
    const auto combo=GetDlgItem(window,7903);SendMessageW(combo,CB_SETCURSEL,destinations.size(),0);
    SendMessageW(window,WM_COMMAND,MAKEWPARAM(7903,CBN_SELCHANGE),LPARAM(combo));rawDraftText(GetDlgItem(window,7901),L"Keep my output choice");
    app.edit("mixer.bus.remove",{{"bus",removed}});press(window,7904);
    const auto before=songState(app);press(window,7906);
    restoreCheck(tool->snapshot().at("draft").at("output")==removed&&songState(app)==before&&tool->snapshot().at("completion").is_null(),
      "Recapturing a deleted output silently selected Master, wrote a track or reported uncertain outcome");
    SendMessageW(combo,CB_SETCURSEL,0,0);SendMessageW(window,WM_COMMAND,MAKEWPARAM(7903,CBN_SELCHANGE),LPARAM(combo));press(window,7906);
    const auto layout=app.documentOperation("track.get",RestoreJson::object());
    restoreCheck(layout.at("noteTracks").size()==1&&tool->snapshot().at("completed")==true,
      "Explicit default-output selection failed to recover an unavailable destination draft");
  }
}
