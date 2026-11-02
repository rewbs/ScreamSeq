void applicationPatternActivityAndChannelControls(){
  RestoreFixture fixture;auto &app=*fixture.app;
  resizeRestoreClient(app,1440,900);app.command(patternCommand);
  auto pos=app.position();pos["pattern"]=0;pos["row"]=1;pos["channel"]=0;pos["column"]=0;pos["following"]=false;app.navigate(pos);app.layoutControls();
  restoreCheck(IsWindowVisible(app.controls.at(channelMuteFirst))&&IsWindowVisible(app.controls.at(channelSoloFirst)),"Channel header omitted M/S controls");
  restoreCheck(restoreText(app.controls.at(channelMuteFirst)).find(L"Mute channel 1")!=std::wstring::npos,"Channel mute has no accessible name");
  restoreCheck(app.defaultShortcutKeys(noteColumnMuteCommand)==std::vector<std::string>{"ctrl+shift+m"}&&app.defaultShortcutKeys(noteColumnSoloCommand)==std::vector<std::string>{"ctrl+shift+l"},"Current channel shortcuts missing");
  for(const auto id:{channelMuteFirst,channelSoloFirst}){
    SetFocus(app.controls.at(id));const auto mutes=app.channelMuteState();app.status.clear();
    restoreCheck(app.handleKey(VK_SPACE)&&app.status.find(L"Inspection mode")!=std::wstring::npos,"Space on M/S did not reach transport");
    restoreCheck(app.channelMuteState()==mutes&&GetFocus()==app.controls.at(id),"Transport key toggled M/S or stole focus");
  }
  SetFocus(app.window);
  const auto original=app.channelMuteState();
  app.toggleNoteColumnMute(2);const auto before=app.channelMuteState();
  SendMessageW(app.controls.at(channelSoloFirst),BM_CLICK,0,0);
  const auto solo=app.channelMuteState();
  restoreCheck(app.channelSoloed(0)&&solo[0].at("mute")==false&&solo[1].at("mute")==true,"Header Solo did not isolate its channel");
  restoreCheck(app.channel==0&&app.row==1,"Header Solo moved the editing cursor");
  app.edit("history.undo",{{"domain","all"}});restoreCheck(app.channelMuteState()==before,"Solo was not one Undo");
  app.edit("history.redo",{{"domain","all"}});restoreCheck(app.channelMuteState()==solo,"Solo Redo failed");
  SendMessageW(app.controls.at(channelSoloFirst),BM_CLICK,0,0);restoreCheck(app.channelMuteState()==before,"Solo did not restore prior mutes");
  app.toggleNoteColumnSolo(0);app.toggleNoteColumnSolo(1);app.toggleNoteColumnSolo(1);restoreCheck(app.channelMuteState()==before,"Switching solo lost the original mute combination");
  const auto revision=app.view->session.revision;
  auto invalid=before;invalid[1]["column"]=invalid[0]["column"];
  bool rejected=false;try{app.documentOperation("track.columns.set",{{"columns",invalid},{"expectedRevision",revision}});}catch(const std::exception &){rejected=true;}
  restoreCheck(rejected&&app.view->session.revision==revision&&app.channelMuteState()==before,"Invalid mute batch partially changed the song");
  app.documentOperation("track.columns.set",{{"columns",original},{"expectedRevision",revision}});
  const auto data=app.documentOperation("pattern.activity.get",{{"pattern",0},{"order",0},{"row",1},{"channel",0}});
  restoreCheck(data.at("complete")==true&&data.at("reachable")==true,"Engine activity query did not complete");
  app.activityReady=app.activityKey();app.activityData=data;app.fillActivityList(data);
  restoreCheck(SendMessageW(app.controls.at(activityListCommand),LB_GETCOUNT,0,0)>4,"Activity list omitted its sections");
  for(size_t i=0;i<app.activityLinks.size();++i)if(app.activityLinks[i].is_object()&&app.activityLinks[i].at("kind")=="sample"){
    const auto sample=app.activityLinks[i].at("slot");SendMessageW(app.controls.at(activityListCommand),LB_SETCURSEL,i,0);app.openActivityLink();
    restoreCheck(app.sampleDetailWindow&&app.sampleDetailWindow->musicalTarget().at("slot")==sample,"Sample context link opened the wrong sample");ScreamSeq::Tests::ownGuiWindow(app.sampleDetailWindow->window());break;
  }
  pos["row"]=2;app.navigate(pos);bool stale=false;try{app.openActivityLink();}catch(const std::exception &){stale=true;}
  restoreCheck(stale,"Stale activity link opened a previous cursor target");
  std::cout<<"PASS pattern activity, native M/S controls, solo restore/switch, atomic rejection, Undo/Redo and stale links\n";
}
