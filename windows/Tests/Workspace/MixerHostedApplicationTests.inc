// Real provider, Application command, document worker, history and project codec.
// Only the popup's selection is supplied; native menu presentation is a separate gate.
void applicationHostedMixerInstrumentRouting(){
  const auto environment=[](const wchar_t *name){
    wchar_t value[32768]{};const auto count=GetEnvironmentVariableW(name,value,DWORD(std::size(value)));
    restoreCheck(count&&count<std::size(value),"Required owned VST3 fixture path unavailable");
    return std::filesystem::path(value);
  };
  const auto scanner=environment(L"SCREAMSEQ_WORKSPACE_VST3_SCANNER");
  const auto module=environment(L"SCREAMSEQ_WORKSPACE_VST3_FIXTURE");
  RestoreFixture fixture;auto &app=*fixture.app;
  Tracker::WindowsVST3::configure(utf8Path(scanner),utf8Path(fixture.folder/L"hosted-mixer-cache.json"));
  restoreCheck(!Tracker::WindowsVST3::rescan(utf8Path(module)).empty(),"Owned VST3 scan returned no descriptors");
  const auto write=[&](const std::string &method,RestoreJson params){params["expectedRevision"]=app.view->session.revision;return app.documentOperation(method,params);};
  const auto readMixer=[&]{return app.documentOperation("mixer.get",RestoreJson::object());};
  const auto catalog=app.documentOperation("plugin.discover",{{"format","VST3"}});
  const auto descriptor=std::find_if(catalog.begin(),catalog.end(),[](const auto &entry){return entry.at("name")=="Resonance Test Instrument";});
  restoreCheck(descriptor!=catalog.end(),"Redistributable VST3 instrument missing from actual discovery");
  write("plugin.add",{{"descriptor",*descriptor}});
  const auto plugin=app.view->session.document.at("nativePlugins").back().at("instanceID").get<std::string>();
  const auto instrument=write("instrument.create",{{"empty",true}}).at("instrument");
  write("plugin.instruments.set",{{"plugin",plugin},{"assignments",RestoreJson::array({{{"instrument",instrument},{"channel",1}}})}});
  write("pattern.apply",{{"cells",RestoreJson::array({{{"pattern",0},{"row",0},{"channel",0},{"note",60},{"instrument",instrument}}})}});
  write("mixer.enable",RestoreJson::object());
  const auto master=readMixer().at("buses").back().at("id").get<std::string>();
  write("mixer.bus.set",{{"bus",master},{"gainDB",-24}});
  const auto target=write("mixer.bus.add",{{"kind","group"},{"name","Instrument destination"}}).at("bus").get<std::string>();
  const auto sibling=write("mixer.bus.add",{{"kind","return"},{"name","Unrelated output"}}).at("bus").get<std::string>();
  write("mixer.bus.set",{{"bus",target},{"gainDB",-18}});
  write("mixer.plugin.route",{{"plugin",plugin},{"output",3},{"targets",RestoreJson::array({master,sibling})}});
  write("mixer.plugin.route",{{"plugin",plugin},{"output",1},{"targets",RestoreJson::array({sibling})}});
  const auto before=readMixer();
  const auto pluginState=app.documentOperation("plugin.state.get",{{"plugin",plugin}});
  const auto assignments=app.documentOperation("plugin.instruments.get",{{"plugin",plugin}});
  const auto render=[&](const std::filesystem::path &path,unsigned block){
    auto project=ScreamSeq::Project::openNativeProject(path);
    ScreamSeq::HostedProjectPlayback playback(*project.document,project.state,48000,{},true);
    std::vector<float> pcm(8192*2);
    for(unsigned offset=0;offset<8192;offset+=block){
      const auto frames=std::min(block,8192-offset);
      restoreCheck(playback.render(pcm.data()+offset*2,frames),"Hosted instrument offline render failed");
    }
    restoreCheck(!playback.failed(),"Hosted instrument reported a render failure");
    for(auto sample:pcm)restoreCheck(std::isfinite(sample),"Hosted routing produced non-finite PCM");
    return pcm;
  };
  const auto initialPCM=render(saveDepartureFixture(fixture),128);
  app.mixerTarget=target;app.command(mixerDetailsCommand);
  unsigned choicesMade=0,routeWrites=0;
  app.mixerInstrumentChooser=[&](const auto &destination,const auto &choices){
    ++choicesMade;restoreCheck(destination.find(wide(target))!=std::wstring::npos,"Native route menu changed its destination");
    for(size_t i=0;i<choices.size();++i)if(choices[i].plugin==plugin&&choices[i].output==3){
      restoreCheck(choices[i].enabled,"Declared instrument auxiliary output is unexpectedly unavailable");return unsigned(i+1);
    }
    throw std::runtime_error("Actual instrument port catalog omitted output 3");
  };
  app.sampleMutationCompletion=[&](const std::string &method){if(method=="mixer.instrument.route"){
    ++routeWrites;throw std::runtime_error("Owned loss after hosted output routing committed");
  }};
  restoreRejected([&]{app.mixerControlCommand(mixerRouteInstrument);});
  app.sampleMutationCompletion={};
  restoreCheck(choicesMade==1&&routeWrites==1&&app.nativeCommandCompletion.returned(),"Native route did not retain its committed receipt");
  const auto after=readMixer();auto expected=RestoreJson::array();
  for(const auto &route:before.at("instruments"))if(!(route.at("plugin")==plugin&&route.at("output")==3))expected.push_back(route);
  expected.push_back({{"plugin",plugin},{"output",3},{"target",target}});
  const auto canonical=[](RestoreJson routes){std::sort(routes.begin(),routes.end(),[](const auto &a,const auto &b){return a.dump()<b.dump();});return routes;};
  restoreCheck(canonical(after.at("instruments"))==canonical(expected)&&after.at("buses")==before.at("buses"),
    "Route Here changed unrelated output branches, bus controls or inserts");
  app.reviewNativeCommand();
  restoreCheck(!app.nativeCommandCompletion.retained()&&choicesMade==1&&routeWrites==1,"Review repeated the native chooser or route write");
  restoreCheck(app.documentOperation("plugin.state.get",{{"plugin",plugin}})==pluginState&&
    app.documentOperation("plugin.instruments.get",{{"plugin",plugin}})==assignments,
    "Routing modified saved opaque plugin state or instrument assignments");
  const auto changedPCM=render(saveDepartureFixture(fixture),128);
  bool different=false;for(size_t i=0;i<changedPCM.size();++i)different|=std::abs(changedPCM[i]-initialPCM[i])>1e-6f;
  restoreCheck(different,"Moving the real auxiliary output to the attenuated bus had no audible effect");
  app.mixerTarget=target;app.command(mixerDetailsCommand);const auto beforeNoop=songState(app);
  app.mixerControlCommand(mixerRouteInstrument);
  restoreCheck(choicesMade==2&&songState(app)==beforeNoop&&!app.nativeCommandCompletion.retained(),
    "Routing the same hosted output to its existing sole destination changed document state");
  write("history.undo",{{"domain","all"}});
  restoreCheck(readMixer().at("instruments")==before.at("instruments"),"One Undo did not restore the original fan-out");
  restoreCheck(render(saveDepartureFixture(fixture),128)==initialPCM,"Undo changed deterministic hosted PCM");
  write("history.redo",{{"domain","all"}});
  restoreCheck(readMixer().at("instruments")==after.at("instruments"),"Redo changed routed output identities");
  const auto saved=saveDepartureFixture(fixture);
  restoreCheck(render(saved,128)==changedPCM,"Redo changed deterministic hosted PCM");
  app.documentOperation("document.open",departureOpenRequest(app,saved));
  restoreCheck(readMixer().at("instruments")==after.at("instruments")&&readMixer().at("buses")==after.at("buses")&&
    app.documentOperation("plugin.state.get",{{"plugin",plugin}})==pluginState&&
    app.documentOperation("plugin.instruments.get",{{"plugin",plugin}})==assignments,
    "Hosted routing, plugin state or assignments did not survive native save/reopen");
  const auto partitioned=render(saved,17);
  for(size_t i=0;i<changedPCM.size();++i)restoreCheck(std::abs(partitioned[i]-changedPCM[i])<=1e-6f,
    "Hosted routing changed PCM across 17/128-frame callback partitions");
  fixture.close();
}
