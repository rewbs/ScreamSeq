// Real Application/native controls with failure injected only after a genuine
// worker read. Snapshot exclusions are transient pending/status/currentness;
// every retained target, range, waveform, loop and drawing stays compared.
void applicationSampleReadbackRecovery(){
  using Json=RestoreJson;
  const auto press=[](HWND window,int id){SendMessageW(window,WM_COMMAND,id,reinterpret_cast<LPARAM>(GetDlgItem(window,id)));};
  const auto state=[](ScreamSeq::SampleDetailWindow &tool){auto value=tool.snapshot();for(const auto *key:{"status","pending","stale"})value.erase(key);value["loops"].erase("stale");return value;};
  const auto raw=[](HWND window){Json fields=Json::object();for(const auto id:{4856,4815,4819,4820,5007,5021})fields[std::to_string(id)]=restoreUtf8(restoreText(GetDlgItem(window,id)));return fields;};
  for(const int command:{4803,4802,5018})for(const std::string method:{"sample.get","sample.waveform.get"})for(int fault=0;fault<4;++fault){
    RestoreFixture fixture;auto &app=*fixture.app;app.openSampleDetail();auto *tool=app.sampleDetailWindow.get();const auto window=tool->window();ScreamSeq::Tests::ownGuiWindow(window);
    rawDraftText(GetDlgItem(window,4822),L"0");rawDraftText(GetDlgItem(window,4823),L"0.25");press(window,4824);
    rawDraftText(GetDlgItem(window,4856),L"Original raw name");rawDraftText(GetDlgItem(window,4815),L"007");
    rawDraftText(GetDlgItem(window,4819),L"004");rawDraftText(GetDlgItem(window,5007),L"-.");rawDraftText(GetDlgItem(window,5021),L"-6.00");
    if(command==4802)app.workspaceState.panel("samples").sample=2;
    auto expected=state(*tool),fields=raw(window),song=songState(app);bool injected=false;
    app.sampleReadResult=[&](const auto &read,Json &result){if(read!=method||injected)return;injected=true;
      if(fault==2)rawDraftText(GetDlgItem(window,4856),L"Newer raw name during read");
      if(fault==3)app.edit("document.patch",{{"title","Newer song revision during read"}});
      SetFocus(app.window);expected=state(*tool);fields=raw(window);song=songState(app);
      if(fault==0)throw std::runtime_error("Owned sample read failure");
      if(fault==1){if(method=="sample.get")result["frames"]="broken";else result["peaks"]=Json::array({0.0});}
    };
    press(window,command);app.sampleReadResult={};
    restoreCheck(injected&&state(*tool)==expected&&raw(window)==fields,"Failed/stale sample read changed captured target, waveform or draft");
    restoreCheck(songState(app)==song&&GetFocus()==app.window&&tool->snapshot().at("mutationCompletion").is_null(),"Read failure wrote, retained a write result or stole newer focus");
    press(window,command);const auto completed=tool->snapshot();restoreCheck(songState(app)==song,"Successful sample Reload changed music/history");
    if(command==5018){
      restoreCheck(!completed.at("loops").at("dirty").get<bool>()&&completed.at("points")==expected.at("points")&&
        restoreText(GetDlgItem(window,4856))==wide(fields.at("4856").get<std::string>())&&restoreText(GetDlgItem(window,4819))==L"004"&&restoreText(GetDlgItem(window,5021))==L"-6.00",
        "Loop-only Reload discarded another raw field or drawing");
    }else{
      restoreCheck(completed.at("sample")==unsigned(command==4802?2:1)&&completed.at("points").empty()&&!completed.at("fieldDraft").get<bool>()&&completed.at("expectedRevision")==app.view->session.revision,
        "Successful explicit Reload did not adopt requested target/discard submitted draft");
    }
    app.sampleDetailWindow.reset();fixture.close();std::cout<<"PASS sample read "<<command<<" / "<<method<<" / fault "<<fault<<"\n";
  }
  // Native channel/viewport choices must agree with their old waveform after a
  // failure; a changed combo selection is not a successful sample adoption.
  for(const bool channel:{false,true})for(int fault=0;fault<3;++fault){
    RestoreFixture fixture;auto &app=*fixture.app;app.openSampleDetail();auto *tool=app.sampleDetailWindow.get();const auto window=tool->window();ScreamSeq::Tests::ownGuiWindow(window);
    rawDraftText(GetDlgItem(window,4819),L"004");rawDraftText(GetDlgItem(window,4820),L"012");rawDraftText(GetDlgItem(window,4856),L"Keep viewport name");
    auto expected=state(*tool),fields=raw(window);const auto song=songState(app);bool injected=false;
    app.sampleReadResult=[&](const auto &method,Json &result){if(method!="sample.waveform.get"||injected)return;injected=true;
      if(fault==2)rawDraftText(GetDlgItem(window,4819),L"006");SetFocus(app.window);expected=state(*tool);fields=raw(window);
      if(fault==0)throw std::runtime_error("View read failed");if(fault==1)result["start"]=999999;
    };
    const auto change=[&]{if(channel){SendMessageW(GetDlgItem(window,4813),CB_SETCURSEL,1,0);SendMessageW(window,WM_COMMAND,MAKEWPARAM(4813,CBN_SELCHANGE),reinterpret_cast<LPARAM>(GetDlgItem(window,4813)));}else press(window,4821);};
    change();app.sampleReadResult={};
    restoreCheck(injected&&state(*tool)==expected&&raw(window)==fields&&songState(app)==song&&GetFocus()==app.window,"Waveform navigation failure changed retained presentation/draft");
    if(channel)restoreCheck(SendMessageW(GetDlgItem(window,4813),CB_GETCURSEL,0,0)==0,"Failed channel switch left misleading combo selection");
    change();restoreCheck(songState(app)==song&&restoreText(GetDlgItem(window,4856))==L"Keep viewport name","View retry changed music or settings draft");
    const auto current=tool->snapshot();restoreCheck(channel?current.at("channels")=="left":current.at("viewStart")==unsigned(fault==2?6:4),"View retry did not adopt the explicit current request");
    app.sampleDetailWindow.reset();fixture.close();
  }
  {
    RestoreFixture fixture;auto &app=*fixture.app;app.openSampleDetail();auto *tool=app.sampleDetailWindow.get();const auto window=tool->window();ScreamSeq::Tests::ownGuiWindow(window);const auto original=state(*tool);
    bool fail=true;app.sampleReadResult=[&](const auto &method,Json &){if(fail&&method=="sample.waveform.get")throw std::runtime_error("Sample selection read failed");};
    const auto select=[&]{SendMessageW(GetDlgItem(window,4801),CB_SETCURSEL,1,0);SendMessageW(window,WM_COMMAND,MAKEWPARAM(4801,CBN_SELCHANGE),reinterpret_cast<LPARAM>(GetDlgItem(window,4801)));};
    select();restoreCheck(state(*tool)==original&&SendMessageW(GetDlgItem(window,4801),CB_GETCURSEL,0,0)==0,"Failed sample switch changed target/cache or left misleading selection");
    fail=false;select();app.sampleReadResult={};restoreCheck(tool->snapshot().at("sample")==2,"Sample switch retry failed");
    app.sampleDetailWindow.reset();fixture.close();
  }
}
