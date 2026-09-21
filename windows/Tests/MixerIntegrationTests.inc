// Included by the document-controller worker harness.
void mixerIntegrationTests(const std::filesystem::path &directory) {
  unsigned stops=0,publications=0;bool active=false,refuse=false;HostedProjectPlayback *playback=nullptr;
  const auto owner=std::this_thread::get_id();
  PlaybackHooks hooks;
  hooks.feedback=[&]{need(std::this_thread::get_id()==owner,"Feedback escaped UI owner");PlaybackFeedback f;f.playing=active;if(active&&playback){f.meters=playback->chain().mixerMeters();f.activity=playback->chain().graphActivity();}return f;};
  hooks.controls=[&](const auto &values){need(std::this_thread::get_id()==owner,"Mixer queue has a second producer");++publications;return !refuse&&(!active||playback->chain().mixerControls(values));};
  DocumentController c({},"mixer-integration",[&]{++stops;active=false;},[](const auto &){},{},64u*1024u*1024u,{},std::move(hooks));
  auto view=c.view();invoke(c,"mixer.enable",{{"dryRun",true}});need(c.view()==view&&stops==0,"Enable preview changed music");
  invoke(c,"mixer.enable",Json::object());
  auto mixer=call(c,"mixer.get",Json::object());const auto master=mixer.at("buses").back().at("id");
  need(mixer.at("active")==true,"Mixer is not active");
  auto prepared=c.prepare(48000,Json::object(),true,true);playback=prepared.get();active=true;
  const auto enabled=c.view();const auto baseline=call(c,"mixer.get",Json::object()).at("buses");const auto stopped=stops;
  invoke(c,"mixer.bus.set",{{"bus",master},{"gainDB",-12},{"dryRun",true}});need(c.view()==enabled&&publications==0&&stops==stopped,"Dry run published controls");
  refuse=true;bool rejected=false;
  try{invoke(c,"mixer.bus.set",{{"bus",master},{"gainDB",-12}});}catch(const Api::ApiError &e){rejected=e.code==-32002;}
  need(rejected&&c.view()==enabled&&active&&stops==stopped,"Queue refusal changed music or stopped playback");
  need(call(c,"mixer.get",Json::object()).at("buses")==baseline,"Refused queue changed mixer baseline");
  refuse=false;invoke(c,"mixer.bus.set",{{"bus",master},{"gainDB",-12}});need(active&&stops==stopped,"Control-only edit stopped playback");
  std::array<float,2048> pcm{};need(playback->render(pcm.data(),1024),"Mixer render failed");
  need(call(c,"mixer.meters",Json::object()).at("meters").size()==baseline.size(),"Prepared mixer meters absent");
  auto changed=c.view();invoke(c,"mixer.bus.set",{{"bus",master},{"gainDB",-96},{"preview",true}});
  need(c.view()==changed&&call(c,"mixer.get",Json::object()).at("buses").back().at("gainDB")==-12,"Preview changed saved music");
  // A no-op baseline edit restores a cancelled preview without creating Undo.
  invoke(c,"mixer.bus.set",{{"bus",master},{"gainDB",-12}});need(c.view()==changed,"Preview reset created history");
  invoke(c,"history.undo",{{"domain","document"}});need(call(c,"mixer.get",Json::object()).at("buses")==baseline,"Queue refusal or preview left a phantom Undo entry");
  invoke(c,"history.redo",{{"domain","document"}});
  // Invalid topology never stops a prepared render; valid topology does.
  active=true;const auto beforeStops=stops;view=c.view();rejected=false;
  try{invoke(c,"mixer.bus.set",{{"bus",master},{"output",master}});}catch(const Api::ApiError &){rejected=true;}
  need(rejected&&c.view()==view&&active&&stops==beforeStops,"Invalid route stopped or committed");
  const auto group=invoke(c,"mixer.bus.add",{{"kind","group"},{"name","Disconnected send"}}).at("bus");
  need(!active&&stops==beforeStops+1,"Topology mutation did not stop");
  invoke(c,"mixer.bus.set",{{"bus",group},{"output",nullptr}});
  invoke(c,"mixer.sends.set",{{"bus",group},{"sends",Json::array({{{"target",master},{"gainDB",-6}}})}});
  const auto path=directory/"mixer-integration.screamseq";
  const auto saved=call(c,"mixer.get",Json::object()).at("buses");
  invoke(c,"document.save",{{"path",path.generic_string()},{"overwrite",true}});
  invoke(c,"mixer.bus.remove",{{"bus",group}});invoke(c,"history.undo",{{"domain","document"}});
  need(call(c,"mixer.get",Json::object()).at("buses")==saved,"Group removal Undo lost sends/disconnection");
  invoke(c,"document.open",{{"path",path.generic_string()},{"discard",true}});
  need(call(c,"mixer.get",Json::object()).at("buses")==saved,"Reopen lost mixer routes");
  // Offline PCM: a detached track is silent; a unity send restores its exact
  // contribution. Compare fresh renderer instances at three device rates.
  DocumentController pcmController({},"mixer-pcm",[]{},[](const auto &){});
  auto render=[&](unsigned rate){auto future=pcmController.prepare(rate,Json::object(),true,true);auto *p=future.get();std::vector<float> result(rate*2);for(unsigned at=0;at<rate;){const auto n=std::min(128u,rate-at);need(p->render(result.data()+2*at,n),"Routing PCM ended early");at+=n;}return result;};
  invoke(pcmController,"mixer.enable",Json::object());
  const auto buses=call(pcmController,"mixer.get",Json::object()).at("buses");const auto output=buses.back().at("id");
  std::map<unsigned,std::vector<float>> references;for(auto rate:{44100u,48000u,96000u})references[rate]=render(rate);
  for(const auto &b:buses)if(b.at("id")!=output)invoke(pcmController,"mixer.bus.set",{{"bus",b.at("id")},{"output",nullptr}});
  auto silence=render(48000);need(std::all_of(silence.begin(),silence.end(),[](float v){return v==0;}),"Detached tracks still audible");
  for(const auto &b:buses)if(b.at("id")!=output)invoke(pcmController,"mixer.sends.set",{{"bus",b.at("id")},{"sends",Json::array({{{"target",output},{"gainDB",0}}})}});
  for(auto rate:{44100u,48000u,96000u}){auto restored=render(rate);double delta=0,energy=0;for(size_t i=0;i<restored.size();++i){delta=std::max(delta,std::abs(double(restored[i])-references[rate][i]));energy+=std::abs(restored[i]);}need(delta<1e-6&&energy>1,"Unity sends did not restore disconnected-track PCM");}
  std::cout<<"PASS mixer live publication/refusal/preview/history, topology stop, disconnection/sends/reopen and PCM at 44.1/48/96 kHz\n";
}
