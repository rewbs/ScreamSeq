// Worker ownership and actual graph publication. Shared ParameterPreviewTests
// independently checks PCM, every copy role and realtime allocation/lock audit.
void liveRecipePreviewTests(const std::filesystem::path &directory) {
  bool active=false;unsigned stops=0;uint64_t epoch=1;HostedProjectPlayback *playback=nullptr;
  const auto producer=std::this_thread::get_id();PlaybackHooks hooks;
  hooks.feedback=[&]{PlaybackFeedback r;r.playing=active;r.audioActive=active;r.generation=epoch;return r;};
  hooks.publishNativeUpdate=[&](auto *target,auto &plan){
    need(std::this_thread::get_id()==producer,"Recipe commit escaped the UI producer");
    return active&&target==playback&&target->publishNativeUpdate(plan);
  };
  DocumentController c({},"live-recipe-preview",[&]{++stops;active=false;},[](const auto &){},{},64u*1024u*1024u,{},std::move(hooks));
  invoke(c,"mixer.enable");
  const auto master=call(c,"mixer.get",Json::object()).at("buses").back().at("id");
  const auto graph=invoke(c,"graph.create").at("graph");
  const auto node=invoke(c,"graph.node.add",{{"graph",graph},{"kind","plugin"},
    {"plugin",{{"format","Built-in"},{"classID","resonance.gainer.v1"}}},{"insertEdge",0}}).at("node");
  invoke(c,"graph.assign",{{"target",master},{"graph",graph}});
  const Json identity={{"graph",graph},{"node",node},{"parameter",1},{"gesture","live-recipe-drag"}};
  auto preview=[&](double value){auto p=identity;p["value"]=value;return invoke(c,"graph.plugin.preview.set",p);};
  auto commit=[&](double value){auto p=identity;p["value"]=value;return invoke(c,"graph.plugin.preview.commit",p);};
  auto cancel=[&]{return invoke(c,"graph.plugin.preview.cancel",identity);};
  auto owner=[&]{return call(c,"graph.plugin.preview.get",Json::object());};
  auto state=[&]{return call(c,"graph.get",Json::object());};
  auto manual=[&]{const auto parameters=call(c,"graph.plugin.get",{{"graph",graph},{"node",node}}).at("parameters");
    for(const auto &p:parameters)if(p.at("id")==1)return p.at("manualValue").get<double>();
    throw std::runtime_error("Missing recipe gain");
  };
  auto reject=[&](auto action,int code){bool rejected=false;try{action();}catch(const Api::ApiError &e){rejected=e.code==code;}need(rejected,"Recipe preview was not rejected as specified");};
  auto baseline=state();auto view=c.view();
  need(!preview(-18).at("audible").get<bool>()&&state()==baseline&&c.view()==view,"Stopped recipe preview changed accepted state");
  cancel();cancel();
  playback=c.prepare(48000,Json::object(),true,true).get();active=true;const auto stopped=stops;
  std::array<float,2048> pcm{};auto render=[&]{need(playback->render(pcm.data(),1024),"Recipe preview render failed");};
  render();view=c.view();
  need(preview(-12).at("audible").get<bool>(),"Active recipe preview did not publish");
  render();preview(-24);render();
  need(state()==baseline&&manual()==0&&c.view()==view&&active&&stops==stopped,"Recipe tracking changed baseline, history or transport");
  const auto retained=owner();
  reject([&]{auto p=identity;p["gesture"]="other";p["value"]=-3;invoke(c,"graph.plugin.preview.set",p);},-32602);
  reject([&]{preview(1000);},-32602);
  need(owner()==retained&&state()==baseline&&c.view()==view,"Invalid recipe preview replaced its owner");
  cancel();render();need(!owner().at("active").get<bool>()&&state()==baseline&&c.view()==view,"Recipe cancellation changed history");
  preview(-18);render();commit(-9);render();
  need(!owner().at("active").get<bool>()&&manual()==-9&&active&&stops==stopped,"Recipe release did not commit without stopping");
  const auto accepted=state();
  invoke(c,"history.undo");render();need(state()==baseline,"Recipe release required more than one Undo");
  invoke(c,"history.redo");render();need(state()==accepted,"Recipe Redo lost the final value");
  view=c.view();preview(-21);render();commit(-9);render();
  need(c.view()==view&&state()==accepted&&!owner().at("active").get<bool>(),"No-op recipe release created history or retained ownership");
  preview(-6);preview(-12);preview(-18);const auto fullOwner=owner();view=c.view();
  reject([&]{preview(-24);},-32002);reject([&]{cancel();},-32002);
  bool notCommitted=false;try{commit(-3);}catch(const Api::ApiError &e){notCommitted=e.outcome&&e.outcome->state==Tracker::CommitOutcome::NotCommitted;}
  need(notCommitted&&owner()==fullOwner&&c.view()==view&&state()==accepted,"Full graph queue changed the accepted recipe or discarded ownership");
  render();cancel();render();
  preview(-18);render();const auto stale=c.view()->session.revision;
  invoke(c,"graph.plugin.set",{{"graph",graph},{"node",node},{"parameters",Json::array({{{"id",1},{"value",-3}}})}});
  render();need(manual()==-3&&!owner().at("active").get<bool>(),"External recipe edit retained an older preview");
  reject([&]{auto p=identity;p["value"]=-18;p["expectedRevision"]=stale;call(c,"graph.plugin.preview.commit",p);},-32001);
  const auto saved=state();preview(-24);render();
  const auto path=directory/"recipe-preview.screamseq";
  invoke(c,"document.save",{{"path",path.generic_string()}});render();
  need(state()==saved&&!owner().at("active").get<bool>()&&active&&stops==stopped,"Save persisted a preview or stopped transport");
  preview(-18);render();++epoch;reject([&]{preview(-12);},-32001);render();
  need(!owner().at("active").get<bool>()&&state()==saved,"Retired host epoch retained a recipe gesture");
  preview(-6);preview(-12);preview(-18);active=false;cancel();render();
  need(!owner().at("active").get<bool>()&&state()==saved,"Quiescent full-queue restoration failed");
  invoke(c,"document.open",{{"path",path.generic_string()},{"discard",true}});
  need(state()==saved&&manual()==-3,"Recipe preview survived save/reopen");
  std::cout<<"PASS live recipe preview: guarded ownership, queue refusal, no-op/release history, save/reopen and host retirement\n";
}
