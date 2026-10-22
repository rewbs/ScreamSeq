// The real retained window and worker, with failures at the native call boundary.
// A busy code must not turn an uncertain write into an automatic second edit.
void applicationScratchWriteAdmission(){
  using Json=RestoreJson;using Outcome=Tracker::CommitOutcome;
  for(const auto outcome:std::vector<std::optional<Outcome>>{Outcome::NotCommitted,Outcome::NoChange,Outcome::Committed,Outcome::Unknown,std::nullopt}){
    RestoreFixture fixture;auto &app=*fixture.app;app.edit("scratch.gestures.set",{{"preset","baby"}});
    const auto before=app.documentOperation("scratch.gestures.get",Json::object());
    unsigned attempts=0;bool refuse=true;HWND nameField=nullptr;
    {
      ScreamSeq::ScratchGestureWindow tool(app.window,[&](const std::string &method,const Json &params){
        if(method=="scratch.gestures.set"){
          ++attempts;
          restoreCheck(IsWindowEnabled(nameField),"Pending scratch write disabled its focused field");
          SendMessageW(nameField,WM_SETTEXT,0,LPARAM(L"Pumped input must wait"));
          SendMessageW(nameField,EM_SETSEL,0,0);
          MSG queued{};queued.hwnd=nameField;queued.message=WM_KEYUP;queued.wParam=VK_SPACE;
          restoreCheck(app.filterDepartureMessage(queued),"Scratch request leaked queued input into global shortcuts");
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
      const auto field=GetDlgItem(window,3102);nameField=field;SetActiveWindow(GetAncestor(window,GA_ROOT));SetFocus(field);
      SetWindowTextW(field,L"Retained busy phrase");SendMessageW(field,EM_SETSEL,2,7);
      SendMessageW(window,WM_TIMER,4,0);
      restoreCheck(attempts==1&&tool.snapshot().at("dirty")==true,"Scratch failure discarded the draft or invoked multiple writes");
      DWORD first=0,last=0;SendMessageW(field,EM_GETSEL,WPARAM(&first),LPARAM(&last));
      restoreCheck(restoreText(field)==L"Retained busy phrase","Scratch refusal lost raw text or admitted pumped input");
      restoreCheck(first==2&&last==7,"Scratch refusal lost the original caret selection");
      restoreCheck(GetFocus()==field,"Scratch refusal lost keyboard focus");
      const bool safe=outcome==Outcome::NotCommitted;
      const bool uncertain=!outcome||*outcome==Outcome::Committed||*outcome==Outcome::Unknown;
      restoreCheck(tool.snapshot().at("saveQueued")==safe&&tool.snapshot().at("writeNeedsReview")==uncertain,"Scratch retry/review classification differs from the actual outcome");
      refuse=false;SendMessageW(window,WM_TIMER,4,0);
      restoreCheck(attempts==(safe?2u:1u),"Scratch automatically replayed a non-retryable busy write");
      if(safe){
        restoreCheck(!tool.snapshot().at("dirty").get<bool>(),"Admitted retry did not accept its original draft");
        SendMessageW(field,EM_GETSEL,WPARAM(&first),LPARAM(&last));
        restoreCheck(GetFocus()==field&&first==2&&last==7&&restoreText(field)==L"Retained busy phrase","Successful scratch retry/readback lost text/caret/focus");
        MSG queued{};queued.hwnd=field;queued.message=WM_KEYUP;queued.wParam=VK_SPACE;
        restoreCheck(!app.filterDepartureMessage(queued),"Scratch completion retained the input lease");
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

void applicationScratchExternalRefresh(){
  using Json=RestoreJson;RestoreFixture fixture;auto &app=*fixture.app;
  app.edit("scratch.gestures.set",{{"preset","baby"}});app.openScratchGestures();
  auto &tool=*app.scratchGestureWindow;const auto window=tool.window();ScreamSeq::Tests::ownGuiWindow(window);
  const auto before=tool.snapshot();const auto id=before.at("selected");
  const auto list=GetDlgItem(window,3110);SetActiveWindow(GetAncestor(window,GA_ROOT));SetFocus(list);
  SendMessageW(list,LB_SETCURSEL,0,0);SendMessageW(window,WM_COMMAND,MAKEWPARAM(3110,LBN_SELCHANGE),LPARAM(list));
  app.edit("scratch.gestures.set",{{"id",id},{"name","Changed elsewhere"}});
  const auto revision=app.view->session.revision;
  SendMessageW(window,WM_TIMER,5,0);
  restoreCheck(tool.snapshot().at("draft").at("name")=="Changed elsewhere"&&!tool.snapshot().at("stale").get<bool>(),"Clean scratch editor did not follow an external edit");
  restoreCheck(tool.snapshot().at("lanes")[0].at("selected")==0&&GetFocus()==list,"Scratch refresh moved selection or keyboard focus");
  restoreCheck(app.view->session.revision==revision,"Scratch refresh wrote to song history");
  app.edit("history.undo",{{"domain","all"}});SendMessageW(window,WM_TIMER,5,0);
  restoreCheck(tool.snapshot().at("draft")==before.at("draft"),"Main-window Undo left the clean scratch canvas stale");
  restoreCheck(tool.snapshot().at("target").at("row")==before.at("target").at("row"),"External refresh recaptured the cursor");

  const auto field=GetDlgItem(window,3112);SetFocus(field);SetWindowTextW(field,L"-.");SendMessageW(field,EM_SETSEL,0,2);
  const auto draft=tool.snapshot();app.edit("document.patch",{{"title","Keep my incomplete point"}});SendMessageW(window,WM_TIMER,5,0);
  DWORD first=0,last=0;SendMessageW(field,EM_GETSEL,WPARAM(&first),LPARAM(&last));
  restoreCheck(tool.snapshot().at("fields")==draft.at("fields")&&tool.snapshot().at("expectedRevision")==draft.at("expectedRevision"),"External refresh replaced or rebased an unfinished scratch edit");
  restoreCheck(GetFocus()==field&&first==0&&last==2,"Retained scratch refresh lost focus or caret");
  SendMessageW(window,WM_COMMAND,MAKEWPARAM(3108,BN_CLICKED),LPARAM(GetDlgItem(window,3108)));SetFocus(list);
  app.edit("scratch.gestures.set",{{"preset","chirp"}});SendMessageW(window,WM_TIMER,5,0);
  app.edit("scratch.gestures.remove",{{"id",id}});SendMessageW(window,WM_TIMER,5,0);
  restoreCheck(tool.snapshot().at("selected")==0&&tool.snapshot().at("draft").empty(),"Removing the selected phrase silently retargeted another phrase");
  app.edit("document.patch",{{"title","Keep empty selection"}});SendMessageW(window,WM_TIMER,5,0);
  restoreCheck(tool.snapshot().at("selected")==0,"Later refresh redirected a removed phrase to another entry");
  fixture.close();
  std::cout<<"PASS scratch external refresh: Undo, stable selection, unchanged history, retained raw fields/caret and removed target\n";
}
