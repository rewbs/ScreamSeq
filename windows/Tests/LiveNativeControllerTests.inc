void patternAutomationAdmissionTests() {
  auto document=Tracker::Document::demo();unsigned stops=0,preparations=0,publications=0;bool denyAdmission=false,denyPublication=false;
  PatternHostHooks hooks;hooks.plugins={"admission-plugin"};
  hooks.parameters=[](size_t){return Json::array({{{"id",1},{"name","Gain"},{"min",0},{"max",1}}});};
  hooks.validateCandidate=[&](const auto &next){next.validate(document->song());if(denyAdmission)throw Api::ApiError(-32602,"Controlled view cache admission refusal");};
  hooks.prepareNativeUpdate=[&](const auto &before,const auto &next)->std::function<void()>{
    need(before==document->native()&&before!=next,"Automation prepared from a stale/no-op candidate");++preparations;
    return [&,before]{need(document->native()==before,"Automation mutated before publication");++publications;if(denyPublication)throw Api::ApiError(-32002,"Controlled automation queue refusal");};
  };
  PatternOperations operations(*document,[&]{++stops;},hooks);
  const Json set={{"pattern",0},{"plugin","admission-plugin"},{"parameter",1},{"points",Json::array({{{"position",0},{"value",.25}},{{"position",256},{"value",.25}}})}};
  const auto retiredLane=operations.invoke("automation.pattern.set",set).at("lane");document->undo();
  const auto reject=[&](const char *method,const Json &params,bool admission){
    const auto before=document->native();const auto revision=document->revision;
    const bool undo=document->canUndo(),redo=document->canRedo();const auto prepared=preparations,published=publications;
    denyAdmission=admission;denyPublication=!admission;bool rejected=false;
    try{operations.invoke(method,params);}catch(const Api::ApiError &e){rejected=e.code==(admission?-32602:-32002);}
    denyAdmission=denyPublication=false;
    need(rejected&&document->native()==before&&document->revision==revision&&document->canUndo()==undo&&document->canRedo()==redo&&stops==0,"Automation admission/publication refusal changed native state or history");
    need(preparations==prepared+(admission?0:1)&&publications==published+(admission?0:1),"Rejected automation candidate crossed the wrong admission boundary");
  };
  need(document->canRedo(),"Automation admission fixture lost its Redo branch");
  reject("automation.pattern.set",set,true);reject("automation.pattern.set",set,false);
  // Undo preserves the native allocator's high-water mark. The accepted retry
  // creates a new lane; later refusal probes must address that current identity.
  const auto lane=operations.invoke("automation.pattern.set",set).at("lane");
  need(lane!=retiredLane,"Automation retry reused a retired native identity");
  const Json transform={{"lane",lane},{"operation","flip-values"}},remove={{"lane",lane}};
  for(const auto &[method,params]:std::vector<std::pair<const char *,Json>>{{"automation.pattern.transform",transform},{"automation.pattern.remove",remove}}) {
    reject(method,params,true);reject(method,params,false);
  }
  const auto before=document->native();operations.invoke("automation.pattern.transform",transform);document->undo();
  need(document->native()==before,"Accepted automation retry was not exactly one Undo");
  std::cout<<"PASS Automation candidate admission precedes preparation, queue refusal retains revision/Undo/Redo, accepted retry is atomic\n";
}

void livePatternAutomationControllerTests() {
  bool active=false,refuse=false;unsigned stops=0,publishes=0;HostedProjectPlayback *playback=nullptr;
  const auto owner=std::this_thread::get_id();PlaybackHooks hooks;
  hooks.feedback=[&]{PlaybackFeedback feedback;feedback.playing=feedback.audioActive=active;feedback.generation=1;return feedback;};
  hooks.publishNativeUpdate=[&](auto *target,auto &plan){need(std::this_thread::get_id()==owner,"Automation publication escaped UI producer");++publishes;return !refuse&&active&&target==playback&&target->publishNativeUpdate(plan);};
  DocumentController c({},"live-pattern-automation",[&]{++stops;active=false;},[](const auto &){},{},64u*1024u*1024u,{},hooks);
  const auto library=call(c,"plugin.discover",{{"format","Built-in"}});
  const auto gain=std::find_if(library.begin(),library.end(),[](const auto &v){return v.at("classID")=="resonance.gainer.v1";});need(gain!=library.end(),"Automation fixture gain unavailable");
  invoke(c,"plugin.add",{{"descriptor",*gain}});invoke(c,"mixer.enable",Json::object());
  const auto plugin=c.view()->session.document.at("nativePlugins").at(0).at("instanceID");
  playback=c.prepare(48000,Json::object(),true,true).get();active=true;const auto preparedStops=stops;
  const auto baseline=c.view();const auto read=[&]{return call(c,"automation.pattern.get",{{"pattern",0}});};const auto empty=read();
  const Json set={{"pattern",0},{"plugin",plugin},{"parameter",1},{"points",Json::array({{{"position",0},{"value",.25}},{{"position",256},{"value",.25}}})}};
  auto dry=set;dry["dryRun"]=true;need(invoke(c,"automation.pattern.set",dry).at("wouldChange")==true&&c.view()==baseline&&publishes==0,"Automation dry run published or changed revision");
  const auto rejected=[&](const char *method,const Json &params){const auto before=c.view();const auto lanes=read();refuse=true;bool caught=false;
    try{invoke(c,method,params);}catch(const Api::ApiError &e){caught=e.code==-32002;}refuse=false;
    need(caught&&c.view()==before&&read()==lanes&&active&&stops==preparedStops,"Rejected live Automation changed lanes/cache/history or stopped playback");};
  rejected("automation.pattern.set",set);
  const auto targets=call(c,"parameter.activity.targets",Json::object()).at("targets");
  const auto processor=std::find_if(targets.begin(),targets.end(),[&](const auto &v){return v.at("plugin")==plugin;});need(processor!=targets.end(),"Prepared Automation processor absent");
  const auto target=processor->at("key");const auto parameters=call(c,"parameter.activity.parameters",{{"target",target}}).at("parameters");
  const auto parameter=std::find_if(parameters.begin(),parameters.end(),[](const auto &v){return v.at("id")==1;});need(parameter!=parameters.end(),"Prepared Automation parameter absent");
  const double minimum=parameter->at("min"),maximum=parameter->at("max");
  const auto watch=[&]{call(c,"parameter.activity.watch",{{"target",target},{"parameter",1},{"clear",true}});};
  std::vector<float> pcm(1024*2);const auto render=[&]{need(playback->render(pcm.data(),1024),"Live Automation renderer failed");};
  const auto observed=[&](double normalized){const auto trace=call(c,"parameter.activity.get",{{"limit",8192}}).at("points");
    need(std::any_of(trace.begin(),trace.end(),[&](const auto &point){return point.at("source").at("kind")=="envelope"&&std::abs(point.at("value").template get<double>()-(minimum+normalized*(maximum-minimum)))<.0001;}),"Rendered Automation did not apply the expected normalized envelope value");};
  watch();const auto lane=invoke(c,"automation.pattern.set",set).at("lane");render();observed(.25);const auto first=read();
  need(c.view()->nativePattern==baseline->nativePattern&&c.view()->graphPattern==baseline->graphPattern&&c.view()->patterns==baseline->patterns&&c.view()->cacheBytes==baseline->cacheBytes,"Ordinary Automation needlessly rebuilt musical view caches");
  const auto unchanged=c.view();const auto noOpPublishes=publishes;
  need(invoke(c,"automation.pattern.set",set).at("wouldChange")==false&&c.view()==unchanged&&publishes==noOpPublishes,"Automation no-op consumed publication/history");
  const Json transform={{"lane",lane},{"operation","flip-values"}},remove={{"lane",lane}};
  auto dryTransform=transform;dryTransform["dryRun"]=true;invoke(c,"automation.pattern.transform",dryTransform);
  need(c.view()==unchanged&&publishes==noOpPublishes,"Automation transform preview changed state");rejected("automation.pattern.transform",transform);
  watch();invoke(c,"automation.pattern.transform",transform);render();observed(.75);const auto transformed=read();
  invoke(c,"history.undo");render();need(read()==first,"One live Automation transform Undo lost exact points");
  invoke(c,"history.redo");render();need(read()==transformed,"Live Automation transform Redo lost exact points");
  const auto beforeRemove=c.view();const auto removePublishes=publishes;auto dryRemove=remove;dryRemove["dryRun"]=true;invoke(c,"automation.pattern.remove",dryRemove);
  need(c.view()==beforeRemove&&publishes==removePublishes,"Automation remove preview changed state");rejected("automation.pattern.remove",remove);
  invoke(c,"automation.pattern.remove",remove);render();need(read()==empty,"Automation remove left a live lane");
  invoke(c,"history.undo");render();need(read()==transformed,"One Automation remove Undo did not restore exact lane identity/points");
  invoke(c,"history.undo");render();need(read()==first,"Transform history contained a rejected/preview entry");
  invoke(c,"history.undo");render();need(read()==empty,"Set history contained a rejected/no-op entry");
  for(unsigned i=0;i<2;++i){invoke(c,"history.redo");render();}need(read()==transformed,"Automation history replay lost restored lane");
  auto snapshot=c.captureRecovery(c.view()->session.revision);while(snapshot.wait_for(std::chrono::milliseconds(1))!=std::future_status::ready)c.service();c.service();
  const auto restored=Project::openNativeProjectBytes(*snapshot.get()->bytes);PatternHostHooks catalog;catalog.plugins={plugin.get<std::string>()};catalog.parameters=[&](size_t){return parameters;};
  PatternOperations reopened(*restored.document,{},catalog);need(reopened.invoke("automation.pattern.get",{{"pattern",0}})==transformed,"Native project round-trip lost live Automation identity/points");
  need(active&&stops==preparedStops,"Ordinary Automation or its history stopped active playback");
  patternAutomationAdmissionTests();
  std::cout<<"PASS live Automation set/transform/remove, rendered envelope values, dry/no-op/refusal, one Undo/Redo, cache reuse and native persistence\n";
}

void liveNativeControllerTests() {
  bool active=false,refuse=false,changeEpoch=false;unsigned stops=0,publishes=0,feedbackReads=0;uint64_t epoch=1;
  HostedProjectPlayback *playback=nullptr;const auto ownerThread=std::this_thread::get_id();
  PlaybackHooks hooks;
  hooks.feedback=[&]{PlaybackFeedback feedback;feedback.playing=feedback.audioActive=active;
    if(changeEpoch&&++feedbackReads==3)++epoch;feedback.generation=epoch;return feedback;};
  hooks.publishNativeUpdate=[&](auto *owner,auto &prepared){need(std::this_thread::get_id()==ownerThread,"Native update was not marshalled to UI producer");++publishes;return !refuse&&active&&owner==playback&&owner->publishNativeUpdate(prepared);};
  DocumentController c({},"live-native",[&]{++stops;active=false;},[](const auto &){},{},64u*1024u*1024u,{},hooks);
  const auto library=call(c,"plugin.discover",{{"format","Built-in"}});const auto gain=std::find_if(library.begin(),library.end(),[](const auto &v){return v.at("classID")=="resonance.gainer.v1";});need(gain!=library.end(),"Live fixture gain unavailable");
  invoke(c,"plugin.add",{{"descriptor",*gain}});invoke(c,"mixer.enable",Json::object());
  const auto graph=invoke(c,"graph.create",Json::object()).at("graph");
  const auto input=call(c,"graph.get",Json::object()).at("library").at(0).at("nodes").at(0).at("id");
  const auto node=invoke(c,"graph.node.add",{{"graph",graph},{"kind","plugin"},{"slot",0},{"insertAfter",input}}).at("node");
  const auto master=call(c,"mixer.get",Json::object()).at("buses").back().at("id");invoke(c,"graph.assign",{{"target",master},{"graph",graph}});
  playback=c.prepare(48000,Json::object(),true,true).get();active=true;const auto beforeStops=stops;
  const auto patch=[&](double value,bool dry=false){return invoke(c,"graph.plugin.set",{{"graph",graph},{"node",node},{"parameters",Json::array({{{"id",1},{"value",value}}})},{"dryRun",dry}});};
  const auto baseline=c.view();patch(-6,true);need(c.view()==baseline&&publishes==0&&stops==beforeStops,"Live dry run published or stopped");
  bool rejected=false;refuse=true;try{patch(-6);}catch(const Api::ApiError &e){rejected=e.code==-32002;}refuse=false;
  need(rejected&&c.view()==baseline&&active&&stops==beforeStops,"Refused native publication changed data/history/playback");
  const auto beforeEpochPublishes=publishes;changeEpoch=true;feedbackReads=0;rejected=false;
  try{patch(-6);}catch(const Api::ApiError &e){rejected=e.code==-32002;}changeEpoch=false;
  need(rejected&&feedbackReads==3&&publishes==beforeEpochPublishes&&c.view()==baseline&&active,"Changed playback epoch reached native publication");
  patch(-6);need(c.view()!=baseline&&active&&stops==beforeStops,"Supported native update stopped instead of publishing");
  std::vector<float> pcm(512*2);need(playback->render(pcm.data(),512),"Prepared live update failed rendering");
  const auto changed=c.view();const auto accepted=publishes;patch(-6);need(c.view()==changed&&publishes==accepted,"Native no-op consumed another plan/history");
  invoke(c,"history.undo",{{"domain","all"}});need(active&&stops==beforeStops,"Live native Undo stopped playback");
  need(playback->render(pcm.data(),512),"Live Undo plan failed render");
  invoke(c,"history.redo",{{"domain","document"}});need(active&&stops==beforeStops,"Live native Redo stopped playback");
  need(playback->render(pcm.data(),512),"Live Redo plan failed render");
  const auto retained=c.view();rejected=false;
  try{invoke(c,"graph.node.add",{{"graph",graph},{"kind","plugin"},{"slot",0},{"insertAfter",input}});}catch(const Api::ApiError &e){rejected=e.code==-32002;}
  need(rejected&&c.view()==retained&&active&&stops==beforeStops,"Unsupported live processor addition consumed history or stopped playback");
  // A bank's metadata-only edits and their chronological history must not
  // consume a graph publication, even with an actual prepared graph playing.
  active=false;
  const auto curve=invoke(c,"graph.node.add",{{"graph",graph},{"kind","automation"}}).at("node");
  const Json shape={{"span",16384},{"points",Json::array({{{"position",0},{"value",.25}},{{"position",16383},{"value",.75}}})}};
  const auto bank=invoke(c,"envelope.bank.save",{{"name","Live bank"},{"shape",shape}}).at("id");
  const Json target={{"kind","graph"},{"graph",graph},{"node",curve},{"pattern",0}};
  invoke(c,"envelope.bank.apply",{{"template",bank},{"target",target},{"linked",true}});
  playback=c.prepare(48000,Json::object(),true,true).get();active=true;
  const auto bankStops=stops,bankPublishes=publishes;
  const auto readBank=[&]{return call(c,"envelope.bank.list",{{"target",target}});};
  const auto linkedBank=readBank();
  invoke(c,"envelope.bank.save",{{"id",bank},{"name","Renamed live bank"},{"shape",shape}});
  const auto renamedBank=readBank();need(renamedBank!=linkedBank&&renamedBank.at("shape")==linkedBank.at("shape"),"Bank rename changed materialized envelope");
  invoke(c,"history.undo",{{"domain","all"}});need(readBank()==linkedBank,"One bank rename Undo did not restore exact metadata and shape");
  invoke(c,"history.redo",{{"domain","all"}});need(readBank()==renamedBank,"One bank rename Redo did not restore exact metadata and shape");
  invoke(c,"envelope.bank.unlink",{{"target",target}});const auto unlinkedBank=readBank();
  need(unlinkedBank.at("linkedTemplate")==""&&unlinkedBank.at("shape")==linkedBank.at("shape"),"Unlink changed materialized envelope");
  invoke(c,"history.undo",{{"domain","all"}});need(readBank()==renamedBank,"One unlink Undo lost bank binding");
  invoke(c,"history.redo",{{"domain","all"}});need(readBank()==unlinkedBank,"One unlink Redo changed retained envelope");
  need(active&&stops==bankStops&&publishes==bankPublishes,"Bank metadata edits or Undo/Redo stopped or published audio");
  std::cout<<"PASS prepared native dry/no-op/refusal/epoch/Undo/Redo and unsupported active topology preservation\n";
  std::cout<<"PASS bank rename/link metadata edits and chronological Undo/Redo preserve active playback without publication\n";
  livePatternAutomationControllerTests();
}

void liveLoopControllerTests() {
  bool rejectPrepare=false,rejectPublish=false;unsigned prepared=0,published=0,stops=0;const auto owner=std::this_thread::get_id();
  PlaybackHooks hooks;hooks.prepareSampleLoops=[&](unsigned sample,const Tracker::SampleEditGeometry &geometry)->std::function<void()>{
    need(std::this_thread::get_id()==owner&&sample==1&&geometry.loopEnd==32,"Loop preparation used wrong owner/sample/geometry");++prepared;
    if(rejectPrepare)throw Api::ApiError(-32002,"Loop queue unavailable");
    return [&]{need(std::this_thread::get_id()==owner,"Loop publication used worker as audio producer");if(rejectPublish)throw Api::ApiError(-32002,"Loop renderer epoch changed");++published;};
  };
  DocumentController c({},"live-loops",[&]{++stops;},[](const auto &){},{},64u*1024u*1024u,{},hooks);
  const auto initial=c.view();const auto sample=call(c,"sample.get",{{"sample",1}});
  const auto patch=[&](bool dry=false){return invoke(c,"sample.loops.set",{{"sample",1},{"normal",{{"start",8},{"end",32},{"enabled",true}}},{"dryRun",dry}});};
  need(patch(true).at("loopsChanged")==true&&c.view()==initial&&prepared==0,"Loop dry run entered live publication");
  for(bool entry:{true,false}){rejectPrepare=entry;rejectPublish=!entry;bool rejected=false;
    try{patch();}catch(const Api::ApiError &e){rejected=e.code==-32002;}
    need(rejected&&c.view()==initial&&call(c,"sample.get",{{"sample",1}})==sample&&published==0&&stops==0,"Rejected loop prepare/publication mutated song/history or stopped playback");
  }
  rejectPrepare=rejectPublish=false;patch();const auto changed=c.view();need(published==1&&stops==0,"Loop change did not use live queue");
  const auto count=prepared;patch();need(c.view()==changed&&prepared==count&&published==1,"Loop no-op consumed queue/history");
  const auto loop=call(c,"sample.get",{{"sample",1}});
  invoke(c,"history.undo",{{"domain","all"}});need(call(c,"sample.get",{{"sample",1}})==sample&&stops==1,"One stopped Undo did not restore pre-live loop geometry");
  invoke(c,"history.redo",{{"domain","all"}});need(call(c,"sample.get",{{"sample",1}})==loop&&stops==2,"One stopped Redo did not restore live-edited loop geometry");
  const auto beforeCellStops=stops;
  invoke(c,"pattern.apply",{{"cells",Json::array({{{"pattern",0},{"row",2},{"channel",0},{"note",61}}})}});
  invoke(c,"history.undo",{{"domain","all"}});invoke(c,"history.redo",{{"domain","all"}});
  need(c.view()->cell(0,2,0).note==61&&stops==beforeCellStops,"Ordinary cell history lost its live/no-stop behavior");
  std::cout<<"PASS sample-loop prepare/publication rejection before history, UI owner, dry/no-op, stopped Undo/Redo and live cell history\n";
}
