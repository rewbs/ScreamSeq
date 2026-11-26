void applicationGateWriteAdmission(){
  using Json=RestoreJson;using Outcome=Tracker::CommitOutcome;
  {
    RestoreFixture fixture;auto &app=*fixture.app;
    auto cursor=app.position();cursor["row"]=6;cursor["column"]=3;cursor["following"]=false;app.navigate(cursor);
    app.key('G');app.key('T');auto &tool=*app.gateInspectorWindow;const auto window=tool.window();ScreamSeq::Tests::ownGuiWindow(window);
    const auto before=app.documentOperation("pattern.effects.get",{{"pattern",0}});
    SetWindowTextW(GetDlgItem(window,6200),L"25");SetWindowTextW(GetDlgItem(window,6201),L"25");
    restoreCheck(tool.snapshot().at("automaticPending")==true,"Gate value change did not schedule automatic save");
    SendMessageW(window,WM_TIMER,0x5345,0);
    const auto after=app.documentOperation("pattern.effects.get",{{"pattern",0}});
    restoreCheck(!tool.retainedDraft()&&after!=before,"Automatic gate edit did not commit");
    app.edit("history.undo",{{"domain","all"}});restoreCheck(app.documentOperation("pattern.effects.get",{{"pattern",0}})==before,"Coalesced gate fields were not one Undo");
    app.edit("history.redo",{{"domain","all"}});
    SendMessageW(GetDlgItem(window,6110),BM_CLICK,0,0);
    SetWindowTextW(GetDlgItem(window,6202),L"-");SendMessageW(window,WM_TIMER,0x5345,0);
    restoreCheck(tool.retainedDraft()&&app.documentOperation("pattern.effects.get",{{"pattern",0}})==after,"Invalid automatic field changed the gate");
    SetWindowTextW(GetDlgItem(window,6202),L"3/8");tool.hide();
    restoreCheck(!tool.retainedDraft()&&app.view->effect(0,6,0,0)->arguments[3]==.375,"Closing a valid pending field did not finish the automatic gate edit");
  }
  // Real typed GT and real Application::await: an ordinary read pumps Apply.
  {
    RestoreFixture fixture;auto &app=*fixture.app;
    auto cursor=app.position();cursor["row"]=6;cursor["column"]=3;cursor["following"]=false;app.navigate(cursor);
    restoreCheck(app.key('G')&&app.key('T')&&app.gateInspectorWindow,"Typed GT did not open its inspector");
    auto &tool=*app.gateInspectorWindow;const auto window=tool.window();ScreamSeq::Tests::ownGuiWindow(window);
    const auto before=app.documentOperation("pattern.effects.get",{{"pattern",0}});
    app.targetRead="mixer.get";app.pumpOrdinaryReads=true;
    app.postedInput=[&]{
      restoreCheck(app.busy,"Gate fixture did not enter a genuine worker wait");
      SendMessageW(GetDlgItem(window,6107),BM_CLICK,0,0);
      restoreCheck(tool.snapshot().at("applyQueued")==true&&tool.retainedDraft(),"Busy Apply lost the gate instead of retaining its exact write");
    };
    app.documentOperation("mixer.get",Json::object());app.pumpOrdinaryReads=false;
    cursor["row"]=9;app.navigate(cursor); // Queued write must stay on row 6.
    SendMessageW(window,WM_TIMER,3,0);
    const auto command=app.view->effect(0,6,0,0);
    restoreCheck(command&&command->native==Tracker::NativePatternOp::Gate&&app.effectText(command).first==L"GT",
      "Admitted gate did not reach the pattern grid's actual command and display code");
    restoreCheck(!tool.snapshot().at("applyQueued").get<bool>()&&!tool.retainedDraft()&&!app.view->effect(0,9,0,0),"Gate retry changed the newer cursor cell or remained pending");
    const auto after=app.documentOperation("pattern.effects.get",{{"pattern",0}});
    SendMessageW(window,WM_TIMER,3,0);
    app.edit("history.undo",{{"domain","all"}});restoreCheck(app.documentOperation("pattern.effects.get",{{"pattern",0}})==before,"Gate retry did not create exactly one Undo");
    app.edit("history.redo",{{"domain","all"}});restoreCheck(app.documentOperation("pattern.effects.get",{{"pattern",0}})==after,"Gate retry Redo changed the saved command");
  }
  for(const auto outcome:std::vector<std::optional<Outcome>>{Outcome::NotCommitted,Outcome::NoChange,Outcome::Committed,Outcome::Unknown,std::nullopt}){
    RestoreFixture fixture;auto &app=*fixture.app;unsigned attempts=0;bool refuse=true;
    ScreamSeq::GateInspectorWindow tool(app.window,[&](const std::string &method,const Json &args){
      if(method=="pattern.effects.set"){
        ++attempts;if(refuse){if(outcome==Outcome::Committed)app.documentOperation(method,args);
          throw ScreamSeq::Api::ApiError(-32002,"Injected gate admission",outcome?std::optional(Tracker::WriteOutcome{*outcome}):std::nullopt);}
      }
      return app.documentOperation(method,args);
    },[&]{return std::pair(app.documentId,app.view->session.revision);},[&]{return app.scratchTargetAt(0,0,0,0);},[&]{return app.view->session.document.at("nativePlugins");});
    tool.open();const auto window=tool.window();ScreamSeq::Tests::ownGuiWindow(window);
    const auto press=[&](int id){SendMessageW(GetDlgItem(window,id),BM_CLICK,0,0);};
    press(6107);const bool safe=outcome==Outcome::NotCommitted;
    restoreCheck(attempts==1&&tool.retainedDraft()&&tool.snapshot().at("applyQueued")==safe,"Gate admission classified an uncertain outcome as retryable");
    refuse=false;SendMessageW(window,WM_TIMER,3,0);
    restoreCheck(attempts==(safe?2u:1u),"Gate automatically replayed an unclassified or committed write");
    if(!safe)continue;
    // New text, stale songs, and closing the inspector cancel unsent intent.
    for(unsigned mode=0;mode<3;++mode){
      press(6110);refuse=true;press(6108);const auto count=attempts;refuse=false;
      restoreCheck(tool.snapshot().at("applyQueued")==true,"Stop gate did not retain busy admission");
      if(mode==0)SetWindowTextW(GetDlgItem(window,6202),L"3/8");
      else if(mode==1)app.edit("document.patch",{{"title","Changed while gate waited"}});
      else tool.hide();
      SendMessageW(window,WM_TIMER,3,0);
      restoreCheck(attempts==count&&!tool.snapshot().at("applyQueued").get<bool>()&&tool.retainedDraft(),"Canceled gate intent was replayed or lost its draft");
      if(mode==2)tool.open();
    }
  }
  std::cout<<"PASS gate typed Apply: real worker contention, GT grid, one Undo/Redo, strict outcome retry, newer/stale/hidden cancellation\n";
}
