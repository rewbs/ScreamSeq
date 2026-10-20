void applicationMixerInstrumentRouting(){
  withRestoreFixture([](RestoreApplication &app){
    app.edit("mixer.enable",RestoreJson::object());app.command(mixerDetailsCommand);
    const auto document=app.documentId,revision=app.view->session.revision,target=app.mixerTarget;
    const auto actual=app.mixerData;auto menuData=actual;
    menuData["plugins"]=RestoreJson::array({
      {{"id","synth-a"},{"name","Bass & Keys"},{"instrument",true},{"audioBuses",RestoreJson::array({
        {{"direction","output"},{"index",0},{"name","Main"},{"active",true},{"supported",true}},
        {{"direction","output"},{"index",3},{"name","Auxiliary"},{"active",false},{"supported",true}},
        {{"direction","output"},{"index",4},{"name","Unsupported"},{"active",false},{"supported",false}},
        {{"direction","input"},{"index",1},{"name","Input"},{"active",true},{"supported",true}}})}},
      {{"id","missing-synth"},{"name","Unavailable synth"},{"instrument",true},{"audioBuses",RestoreJson::array()}},
      {{"id","effect"},{"name","Effect"},{"instrument",false},{"audioBuses",RestoreJson::array()}}
    });
    menuData["instruments"]=RestoreJson::array({{{"plugin","synth-a"},{"output",3},{"target",target}},{{"plugin","synth-a"},{"output",3},{"target",""}}});
    const auto choices=app.mixerInstrumentChoices(menuData);
    restoreCheck(choices.size()==4&&choices[0].plugin=="synth-a"&&choices[0].output==0&&choices[0].enabled&&choices[0].label.find(L"Master (implicit)")!=std::wstring::npos,
      "Instrument menu omitted main identity or invented an implicit auxiliary route");
    restoreCheck(choices[1].output==3&&choices[1].enabled&&choices[1].label.find(L"enables on playback")!=std::wstring::npos&&choices[1].label.find(L" + Disconnected")!=std::wstring::npos&&
      !choices[2].enabled&&!choices[3].enabled,"Instrument menu misrepresented auxiliary, unsupported or unavailable ports");
    restoreCheck(app.mixerMenuText(L"Bass & Keys\taux")==L"Bass && Keys aux","Instrument names became menu accelerators");
    // This supplies menu data only. Successful musical writes are covered by
    // MixerOperations' real document/history fixture, not fabricated plugins.
    for(unsigned scenario=0;scenario<4;++scenario){
      app.mixerData=menuData;app.mixerTarget=target;app.mixerDirty=false;app.mixerRevision=app.view->session.revision;app.mixerDocument=document;
      const auto before=songState(app);
      app.mixerInstrumentChooser=[&](const auto &destination,const auto &presented){
        restoreCheck(app.mixerPending&&destination.find(wide(target))!=std::wstring::npos&&presented.size()==4,"Native chooser lost its pending destination");
        if(scenario==0)return 0u;
        if(scenario==1)app.mixerTarget="unavailable-target";
        if(scenario==2){rawDraftText(app.controls.at(mixerName),L"New draft during menu");app.mixerFieldChanged();}
        if(scenario==3)return 3u;
        return 1u;
      };
      if(scenario==0)app.mixerControlCommand(mixerRouteInstrument);else restoreRejected([&]{app.mixerControlCommand(mixerRouteInstrument);});
      restoreCheck(songState(app)==before&&!app.mixerPending&&!app.nativeCommandCompletion.retained(),"Canceled or changed menu submitted a route");
      if(scenario==2)restoreCheck(app.mixerDirty&&restoreText(app.controls.at(mixerName))==L"New draft during menu","Menu refusal discarded newer raw fields");
    }
    app.mixerDirty=false;app.mixerTarget=target;app.mixerData=actual;app.mixerRevision="stale";
    restoreRejected([&]{app.mixerControlCommand(mixerRouteInstrument);});
    restoreRejected([&]{app.mixerControlCommand(mixerAddEffect);});
    restoreCheck(app.mixerTarget==target&&app.view->session.revision==revision,"Stale destination action silently selected another bus");
    app.refreshMixer();
    app.mixerData=menuData;const auto routes=actual.at("instruments");
    app.mixerInstrumentChooser=[&](const auto &,const auto &){app.edit("mixer.bus.set",{{"bus",target},{"gainDB",-2}});return 1u;};
    restoreRejected([&]{app.mixerControlCommand(mixerRouteInstrument);});
    const auto afterExternalEdit=app.documentOperation("mixer.get",RestoreJson::object());
    restoreCheck(afterExternalEdit.at("instruments")==routes&&!app.nativeCommandCompletion.retained()&&!app.mixerPending,
      "Menu selection wrote a route after the document revision changed");
    app.refreshMixer();
  });
}
