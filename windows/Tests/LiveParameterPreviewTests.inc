// Run beside liveParameterTests: same real worker, UI producer and prepared
// renderer, with no hardware dependency. Shared preview tests measure PCM.
void liveParameterPreviewTests(const std::filesystem::path &directory) {
  bool active=false;unsigned stops=0;uint64_t epoch=1;HostedProjectPlayback *playback=nullptr;
  const auto producer=std::this_thread::get_id();PlaybackHooks hooks;
  hooks.feedback=[&]{PlaybackFeedback r;r.playing=active;r.audioActive=active;r.generation=epoch;return r;};
  DocumentController c({},"live-parameter-preview",[&]{++stops;active=false;},[](const auto &){},{},64u*1024u*1024u,
    [&](std::span<const Tracker::ParameterChange> changes){need(std::this_thread::get_id()==producer,"Preview commit escaped UI producer");publishLiveParameters(active,playback,changes);},std::move(hooks));
  invoke(c,"document.new",{{"demo",true},{"discard",true}});
  const auto library=call(c,"plugin.discover",{{"format","Built-in"}});
  const auto descriptor=std::find_if(library.begin(),library.end(),[](const auto &p){return p.at("classID")=="resonance.gainer.v1";});
  need(descriptor!=library.end(),"Preview fixture needs Gainer");invoke(c,"plugin.add",{{"descriptor",*descriptor}});
  const auto plugin=c.view()->session.document.at("nativePlugins")[0].at("instanceID");
  const Json identity={{"plugin",plugin},{"parameter",1},{"gesture","fixture-drag"}};
  auto preview=[&](float value){auto p=identity;p["value"]=value;return invoke(c,"plugin.parameters.preview.set",p);};
  auto commit=[&](float value){auto p=identity;p["value"]=value;return invoke(c,"plugin.parameters.preview.commit",p);};
  auto cancel=[&]{return invoke(c,"plugin.parameters.preview.cancel",identity);};
  auto owner=[&]{return call(c,"plugin.parameters.preview.get",Json::object());};
  auto state=[&]{return call(c,"plugin.state.get",{{"plugin",plugin}});};
  auto manual=[&]{for(const auto &p:call(c,"plugin.parameters.get",{{"plugin",plugin}}))if(p.at("id")==1)return p.at("value").get<float>();throw std::runtime_error("Missing saved gain");};
  auto view=c.view();const auto baseline=state();
  need(!preview(-18).at("audible").get<bool>()&&c.view()==view&&state()==baseline,"Stopped preview changed accepted state");
  cancel();cancel();need(!owner().at("active").get<bool>()&&c.view()==view,"Idempotent stopped cancellation changed revision");
  playback=c.prepare(48000,Json::object(),true,true).get();active=true;const auto stopped=stops;
  std::array<float,512> pcm{};
  auto render=[&]{need(playback->render(pcm.data(),256),"Preview render failed");};
  auto gain=[&]{for(const auto &p:playback->chain().parameters(0))if(p.id==1)return p.value;throw std::runtime_error("Missing audible gain");};
  auto reject=[&](auto action,int code){bool rejected=false;try{action();}catch(const Api::ApiError &e){rejected=e.code==code;}need(rejected,"Preview request was not rejected as specified");};
  // Export is an accepted-song observation, unlike Save's explicit finishing
  // of a gesture. Use actual sample music so a silent fixture cannot mask a
  // leaked temporary gain value in the independent full-song renderer.
  const auto acceptedExport=directory/"accepted-before-preview.wav";
  invoke(c,"document.exportAudio",{{"path",acceptedExport.generic_string()}});
  const auto acceptedPCM=Project::readProjectBytes(acceptedExport);
  need(acceptedPCM.size()>44&&std::any_of(acceptedPCM.begin()+44,acceptedPCM.end(),[](std::byte b){return b!=std::byte{0};}),
    "Preview export fixture must contain sample audio");
  view=c.view();need(preview(-12).at("audible").get<bool>(),"Active preview was not queued");render();
  need(gain()==-12&&manual()==0&&state()==baseline&&c.view()==view&&active&&stops==stopped,"Preview changed manual/opaque state, revision or transport");
  const auto retained=owner();
  const auto previewExport=directory/"accepted-during-preview.wav";
  invoke(c,"document.exportAudio",{{"path",previewExport.generic_string()}});render();
  need(Project::readProjectBytes(previewExport)==acceptedPCM&&owner()==retained&&gain()==-12&&
       manual()==0&&state()==baseline&&c.view()==view&&active&&stops==stopped,
       "Audio export baked in or canceled a live preview, changed history or stopped playback");
  reject([&]{auto p=identity;p["gesture"]="other-drag";p["value"]=-3;invoke(c,"plugin.parameters.preview.set",p);},-32602);
  reject([&]{auto p=identity;p["parameter"]=true;p["value"]=-3;invoke(c,"plugin.parameters.preview.set",p);},-32602);
  reject([&]{preview(1000);},-32602);
  reject([&]{auto p=identity;p["gesture"]="other-drag";invoke(c,"plugin.parameters.preview.cancel",p);},-32602);
  need(owner()==retained&&state()==baseline&&c.view()==view,"Invalid preview replaced the retained owner");
  preview(-24);render();cancel();render();need(gain()==0&&manual()==0&&c.view()==view&&!owner().at("active").get<bool>(),"Cancel did not restore accepted gain without history");
  preview(-18);render();commit(-9);render();
  need(gain()==-9&&manual()==-9&&!owner().at("active").get<bool>()&&active&&stops==stopped,"Release did not commit final value once");
  const auto committed=state();invoke(c,"history.undo");render();need(gain()==0&&state()==baseline,"One Undo did not restore pre-drag state");
  invoke(c,"history.redo");render();need(gain()==-9&&state()==committed,"Redo lost final drag value");
  view=c.view();preview(-21);render();commit(-9);render();need(gain()==-9&&c.view()==view,"No-op release left a preview or created history");
  preview(-18);render();
  std::vector<Tracker::ParameterChange> full(4096,{0,2,0,0});need(playback->chain().enqueueParameters(full),"Fill preview queue");
  const auto saturated=owner();view=c.view();
  reject([&]{preview(-24);},-32002);reject([&]{cancel();},-32002);
  bool known=false;try{commit(-6);}catch(const Api::ApiError &e){known=e.outcome&&e.outcome->state==Tracker::CommitOutcome::NotCommitted;}
  need(known&&owner()==saturated&&c.view()==view&&state()==committed,"Queue refusal lost preview ownership or changed history");
  render();cancel();render();need(gain()==-9&&!owner().at("active").get<bool>(),"Explicit cancellation after queue drain failed");
  preview(-18);render();const auto beforeExternal=c.view()->session.revision;
  invoke(c,"plugin.parameters.set",{{"plugin",plugin},{"values",Json::array({{{"id",1},{"value",-3}}})}});render();
  need(gain()==-3&&!owner().at("active").get<bool>(),"External accepted edit retained an earlier audible draft");
  reject([&]{auto p=identity;p["value"]=-18;p["expectedRevision"]=beforeExternal;call(c,"plugin.parameters.preview.commit",p);},-32001);
  const auto savedState=state();preview(-24);render();
  const auto path=directory/"parameter-preview.screamseq";invoke(c,"document.save",{{"path",path.generic_string()}});render();
  need(gain()==-3&&state()==savedState&&!owner().at("active").get<bool>()&&active&&stops==stopped,"Save baked preview into baseline or stopped playback");
  preview(-18);render();++epoch;reject([&]{preview(-12);},-32001);render();need(gain()==-3&&!owner().at("active").get<bool>(),"Retired playback epoch did not cancel preview");
  preview(-21);render();need(playback->chain().enqueueParameters(full),"Fill stopped cancellation queue");active=false;cancel();render();
  need(gain()==-3&&!owner().at("active").get<bool>(),"Quiescent full-queue cancellation could not restore baseline");
  invoke(c,"document.open",{{"path",path.generic_string()},{"discard",true}});
  need(state()==savedState&&manual()==-3,"Save/reopen persisted an uncommitted preview");
  std::cout<<"PASS live rack preview: ownership, queue refusal, cancellation, final commit, Undo/Redo, save/reopen and host retirement\n";
}
