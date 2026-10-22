// The real retained window and worker, with failures at the native call boundary.
// A busy code must not turn an uncertain write into an automatic second edit.
void applicationScratchWriteAdmission(){
  using Json=RestoreJson;using Outcome=Tracker::CommitOutcome;
  for(const auto outcome:std::vector<std::optional<Outcome>>{Outcome::NotCommitted,Outcome::NoChange,Outcome::Committed,Outcome::Unknown,std::nullopt}){
    RestoreFixture fixture;auto &app=*fixture.app;app.edit("scratch.gestures.set",{{"preset","baby"}});
    const auto before=app.documentOperation("scratch.gestures.get",Json::object());
    unsigned attempts=0;bool refuse=true;
    {
      ScreamSeq::ScratchGestureWindow tool(app.window,[&](const std::string &method,const Json &params){
        if(method=="scratch.gestures.set"){
          ++attempts;
          if(refuse){
            // Committed errors model a lost completion after an actual edit.
            if(outcome==Outcome::Committed)app.documentOperation(method,params);
            throw ScreamSeq::Api::ApiError(-32002,"Injected busy completion",outcome?std::optional(Tracker::WriteOutcome{*outcome}):std::nullopt);
          }
        }
        return app.documentOperation(method,params);
      },[&]{return std::pair(app.documentId,app.view->session.revision);},
      []{return std::optional<ScreamSeq::ScratchPatternTarget>{};},
      [](const auto &){return std::optional<ScreamSeq::ScratchPatternTarget>{};},[](const auto &,bool){});
      tool.openAt();const auto window=tool.window();ScreamSeq::Tests::ownGuiWindow(window);
      const auto field=GetDlgItem(window,3102);SetActiveWindow(GetAncestor(window,GA_ROOT));SetFocus(field);
      SetWindowTextW(field,L"Retained busy phrase");SendMessageW(field,EM_SETSEL,2,7);
      SendMessageW(window,WM_TIMER,4,0);
      restoreCheck(attempts==1&&tool.snapshot().at("dirty")==true,"Scratch failure discarded the draft or invoked multiple writes");
      DWORD first=0,last=0;SendMessageW(field,EM_GETSEL,WPARAM(&first),LPARAM(&last));
      restoreCheck(GetFocus()==field&&first==2&&last==7&&restoreText(field)==L"Retained busy phrase","Scratch refusal lost text/caret/focus");
      const bool safe=outcome==Outcome::NotCommitted;
      const bool uncertain=!outcome||*outcome==Outcome::Committed||*outcome==Outcome::Unknown;
      restoreCheck(tool.snapshot().at("saveQueued")==safe&&tool.snapshot().at("writeNeedsReview")==uncertain,"Scratch retry/review classification differs from the actual outcome");
      refuse=false;SendMessageW(window,WM_TIMER,4,0);
      restoreCheck(attempts==(safe?2u:1u),"Scratch automatically replayed a non-retryable busy write");
      if(safe){
        restoreCheck(!tool.snapshot().at("dirty").get<bool>(),"Admitted retry did not accept its original draft");
        app.edit("history.undo",{{"domain","all"}});
        restoreCheck(app.documentOperation("scratch.gestures.get",Json::object())==before,"Admitted scratch retry did not create exactly one Undo");
      }else{
        // Hide/show and later typing must not clear an uncertain operation.
        tool.hide();tool.show();
        if(uncertain){SetWindowTextW(field,L"Newer retained phrase");SendMessageW(window,WM_TIMER,4,0);restoreCheck(attempts==1,"Later text or reopening replayed an unresolved write");}
        const auto changed=app.documentOperation("scratch.gestures.get",Json::object());
        restoreCheck((changed!=before)==(outcome==Outcome::Committed),"Refused scratch edit changed the document");
        SendMessageW(window,WM_COMMAND,MAKEWPARAM(3108,BN_CLICKED),LPARAM(GetDlgItem(window,3108)));
        restoreCheck(!tool.snapshot().at("writeNeedsReview").get<bool>()&&!tool.snapshot().at("dirty").get<bool>(),"Explicit Reload did not reconcile the current phrase");
        if(outcome==Outcome::Committed){app.edit("history.undo",{{"domain","all"}});restoreCheck(app.documentOperation("scratch.gestures.get",Json::object())==before,"Committed scratch refusal repeated or lost its Undo");}
      }
    }
    fixture.close();
  }
  std::cout<<"PASS scratch busy admission: only classified pre-write retry, retained uncertainty, one Undo, text/focus, reopen and explicit readback\n";
}
