template<class T> T recoveryWait(DocumentController &c,std::future<T> future) {
  while(future.wait_for(std::chrono::milliseconds(1))!=std::future_status::ready)c.service();
  c.service();return future.get();
}
std::shared_ptr<const RecoverySnapshot> recoveryCapture(DocumentController &c) {
  return recoveryWait(c,c.captureRecovery(c.view()->session.revision));
}
std::shared_ptr<const std::vector<std::byte>> recoveryBytes(const Json &tree) {
  return std::make_shared<const std::vector<std::byte>>(Project::encodePlist(tree));
}
Json recoveryTake(const Tracker::Document &document) {
  return {{"compatible",true},{"missingTime",5},{"exhaustedVoices",7},{"overflow",11},
    {"future",Json::binary({0,255,42})},
    {"events",Json::array({{{"pattern","n"+std::to_string(document.native().patterns.begin()->second.id)},
      {"track","n"+std::to_string(document.native().tracks.begin()->second.id)},
      {"position",17},{"instrument",1},{"note",61},{"velocity",100},{"futureEvent",Json::binary({8,0,255})}}})}};
}
void recoveryControllerTests(const std::filesystem::path &directory) {
  unsigned stops=0;bool failView=false,failStop=false,active=false;
  DocumentController c({},"recovery",[&]{if(failStop)throw std::runtime_error("controlled stop rejection");++stops;active=false;},[](const auto &){},
    [&]{if(failView)throw std::runtime_error("controlled recovery view failure");});
  auto *playing=recoveryWait(c,c.prepare(48000,Json::object(),true,true));active=true;
  std::array<float,1024> pcm{};need(playing->render(pcm.data(),512),"recovery fixture playback");
  const auto original=c.view();const auto initialStops=stops;
  auto first=recoveryCapture(c),again=recoveryCapture(c);
  need(first->revision==original->session.revision && first->fingerprint.size()==64 && *first->bytes==*again->bytes && first->fingerprint==again->fingerprint,"recovery capture identity/fingerprint drift");
  need(c.view()==original && stops==initialStops && active && !first->needsProtection && !first->source && !first->hasRecording,"clean recovery capture mutated current state");
  need(playing->render(pcm.data(),512),"capture interrupted prepared playback");

  invoke(c,"pattern.apply",{{"cells",Json::array({{{"pattern",0},{"row",3},{"channel",0},{"note",63}}})}});
  auto changed=c.view();auto changedCapture=recoveryCapture(c);
  need(changedCapture->needsProtection && c.view()==changed && changed->session.document.at("canUndo")==true,"capture consumed dirty music/history");
  auto rejectedRestore=[&](std::shared_ptr<const std::vector<std::byte>> bytes,const std::string &expected,bool discard,int code=0) {
    auto old=c.view();const auto oldStops=stops;bool rejected=false;
    try{recoveryWait(c,c.recover(std::move(bytes),expected,discard));}
    catch(const Api::ApiError &e){rejected=!code||e.code==code;}
    catch(const std::exception &){rejected=!code;}
    need(rejected && c.view()==old && stops==oldStops,"rejected recovery changed document/view/playback");
  };
  rejectedRestore(first->bytes,original->session.revision,true,-32001);
  rejectedRestore(first->bytes,changed->session.revision,false,-32602);
  rejectedRestore(std::make_shared<const std::vector<std::byte>>(std::vector<std::byte>{std::byte{1}}),changed->session.revision,true,-32003);
  auto unsupported=Project::decodePlist(*first->bytes);unsupported["version"]=5;
  rejectedRestore(recoveryBytes(unsupported),changed->session.revision,true,-32003);
  auto invalid=Project::decodePlist(*first->bytes);invalid["native"]["version"]=999;
  rejectedRestore(recoveryBytes(invalid),changed->session.revision,true,-32003);
  failView=true;rejectedRestore(first->bytes,changed->session.revision,true);failView=false;
  failStop=true;rejectedRestore(first->bytes,changed->session.revision,true);failStop=false;
  need(recoveryCapture(c)->fingerprint==changedCapture->fingerprint,"rejected restore changed serialized music");
  const auto beforeRestoreStops=stops;
  auto recovered=recoveryWait(c,c.recover(first->bytes,changed->session.revision,true));
  need(stops==beforeRestoreStops+1 && recovered==c.view() && recovered->dirty && recovered->path.empty(),"recovery did not publish one stopped unsaved document");
  need(recovered->session.documentId!=changed->session.documentId && recovered->session.document.at("canUndo")==false && recovered->session.document.at("canUndoPlugins")==false,"recovery reused identity or manufactured history");
  need(*recoveryCapture(c)->bytes==*first->bytes,"recovered exact PCM/native snapshot differs");
  const auto output=directory/"recovery-saved.screamseq";
  invoke(c,"document.save",{{"path",output.generic_string()},{"overwrite",true}});
  need(!c.view()->dirty && c.view()->path==output && !recoveryCapture(c)->needsProtection,"save failed to clear recovered-unsaved state");
  need(recoveryCapture(c)->source==std::optional<std::filesystem::path>(output),"capture source path incorrect");
  const auto savedRevision=c.view()->session.revision;
  recovered=recoveryWait(c,c.recover(first->bytes,savedRevision));
  need(recovered->dirty && recovered->path.empty(),"clean guarded restore did not become unsaved");
  const auto unsaved=c.view();bool failedSave=false;
  try{invoke(c,"document.save",{{"path",output.generic_string()}});}catch(const std::exception &){failedSave=true;}
  need(failedSave && c.view()==unsaved && c.view()->dirty,"failed save cleared recovered unsaved flag");
  bool blockedOpen=false;
  try{invoke(c,"document.open",{{"path",output.generic_string()}});}catch(const Api::ApiError &){blockedOpen=true;}
  need(blockedOpen && c.view()==unsaved,"normal open discarded recovered unsaved state");

  {
    unsigned automationStops=0;DocumentController automated({},"recovery-automation",[&]{++automationStops;},[](const auto &){});
    const auto catalog=call(automated,"plugin.discover",{{"format","Built-in"}});
    const auto gain=std::find_if(catalog.begin(),catalog.end(),[](const auto &p){return p.at("classID")=="resonance.gainer.v1";});
    need(gain!=catalog.end(),"recovery automated fixture needs Gainer");invoke(automated,"plugin.add",{{"descriptor",*gain}});
    invoke(automated,"automation.replaceLane",{{"slot",0},{"id",1},{"points",Json::array({{{"frame",0},{"value",-24}}})}});
    call(automated,"plugin.parameters.get",{{"slot",0}}); // Retain a manual baseline editor.
    auto baseline=recoveryCapture(automated);const auto automaticView=automated.view();const auto stopped=automationStops;
    auto *playback=recoveryWait(automated,automated.prepare(48000,Json::object(),true,true));
    need(playback->render(pcm.data(),512),"automated recovery fixture render");
    auto parameters=playback->chain().parameters(0);auto parameter=std::find_if(parameters.begin(),parameters.end(),[](const auto &p){return p.id==1;});
    need(parameter!=parameters.end() && parameter->value==-24,"automation did not change prepared copy");
    need(*recoveryCapture(automated)->bytes==*baseline->bytes && automated.view()==automaticView && automationStops==stopped,"capture baked automated playback values or mutated baseline");
    need(playback->render(pcm.data(),512),"capture interrupted automated playback");
  }

  // Unknown fields, unsupported opaque plugin state and take provenance survive
  // capture/recover byte-for-byte, without requiring that vendor to be loaded.
  auto opaque=Project::decodePlist(*first->bytes);
  opaque["futureRoot"]={{"raw",Json::binary({0,1,255,128})},{"negativeZero",-0.0}};
  opaque["native"]["futureNative"]=Json::binary({4,0,255});
  opaque["plugins"]=Json::array({{{"format","AU"},{"type",0},{"subtype",1},{"manufacturer",2},{"name","Opaque unavailable"},
    {"instanceID","recovery-opaque"},{"state",Json::binary({0,255,0,37})},{"instrument",0},{"instrumentAssignments",Json::array()},{"futurePlugin",Json::binary({27,0,91})}}});
  auto fixtureDocument=Project::openNativeProjectBytes(*first->bytes);opaque["recoveryTake"]=recoveryTake(*fixtureDocument.document);
  auto opaqueBytes=recoveryBytes(opaque);
  recoveryWait(c,c.recover(opaqueBytes,c.view()->session.revision,true));
  auto retained=recoveryCapture(c);
  need(retained->hasRecording && retained->needsProtection && c.view()->session.document.at("hasRecoveryTake")==true,"preserved take missing from recovery eligibility");
  need(*retained->bytes==*opaqueBytes,"recovery changed PCM/opaque metadata/plugin/take bytes");
  bool unfinishedRejected=false;const auto retainedView=c.view();
  try{invoke(c,"document.save",{{"path",output.generic_string()},{"overwrite",true}});}catch(const Api::ApiError &e){unfinishedRejected=e.code==-32602;}
  need(unfinishedRejected&&c.view()==retainedView&&recoveryCapture(c)->hasRecording,"normal Save must guard the hydrated unfinished take");
  // The project codec still preserves imported opaque take fields exactly;
  // document Save now requires explicit Finish/Discard before reaching it.
  auto preservedProject=Project::openNativeProjectBytes(*opaqueBytes);
  Project::saveNativeProject(*preservedProject.document,preservedProject.state,output,true);
  need(Project::readProjectBytes(output)==*opaqueBytes,"project codec changed preserved recovery contents");

  // Reject oversized immutable-view candidates before stopping the old song.
  auto large=Tracker::Document::demo();large->transaction([](auto &song){Tracker::Document::resizeChannels(song,64);
    for(unsigned p=0;p<16;++p){song.Patterns.Remove(p);need(song.Patterns.Insert(p,1024),"recovery budget fixture");}});
  auto largeBytes=std::make_shared<const std::vector<std::byte>>(Project::serializeNativeProject(*large,Project::newProjectState(*large)));
  unsigned limitedStops=0;DocumentController limited({},"recovery-budget",[&]{++limitedStops;},[](const auto &){},{},2u*1024u*1024u);
  const auto limitedBefore=limited.view();bool limitedRejected=false;
  try{recoveryWait(limited,limited.recover(largeBytes,limitedBefore->session.revision,true));}catch(const Api::ApiError &){limitedRejected=true;}
  need(limitedRejected && limited.view()==limitedBefore && !limitedStops,"recovery view budget replaced source or stopped playback");

  // A failed view publication cannot make capture report the stale UI revision.
  bool publicationFailure=false;DocumentController pending({},"recovery-pending",[]{},[](const auto &){},[&]{if(publicationFailure)throw std::runtime_error("pending view");});
  const auto old=pending.view();publicationFailure=true;
  try{invoke(pending,"pattern.apply",{{"cells",Json::array({{{"pattern",0},{"row",0},{"channel",0},{"note",62}}})}});}catch(const Api::ApiError &){}
  need(pending.publicationPending() && pending.view()==old,"pending publication fixture");
  bool stale=false;try{recoveryWait(pending,pending.captureRecovery(old->session.revision));}catch(const Api::ApiError &e){stale=e.code==-32001;}
  need(stale && pending.publicationPending() && pending.view()==old,"capture accepted stale cached revision or published view");
  auto actual=old->session.revision;const auto split=actual.find(':',actual.find(':')+1),end=actual.find(':',split+1);
  actual.replace(split+1,end-split-1,std::to_string(std::stoull(actual.substr(split+1,end-split-1))+1));
  const auto capturedPending=recoveryWait(pending,pending.captureRecovery(actual));
  need(capturedPending->revision==actual && pending.publicationPending() && pending.view()==old,"capture revision differs from pending worker state");
  need(Project::openNativeProjectBytes(*capturedPending->bytes).document->cell(0,0,0).note==62,"pending capture lost committed music");
  publicationFailure=false;call(pending,"synchronizeView",Json::object());
  std::cout<<"PASS recovery capture purity/playback, exact PCM/native/opaque/take persistence, guarded transactional restore, dirty/save, corruption/budget and pending revisions\n";
}

void recoveryManualEditorTests(const std::filesystem::path &scanner,const std::filesystem::path &cache,const std::filesystem::path &directory) {
  const char *stage="initial plugin setup";
  try {
  Tracker::WindowsVST3::configure(scanner.generic_string(),cache.generic_string());
  unsigned stops=0;DocumentController c({},"recovery-manual",[&]{++stops;},[](const auto &){});
  const auto catalog=call(c,"plugin.discover",{{"format","VST3"}});Json descriptor;
  for(const auto &p:catalog)if(p.at("classID")=="5245534F4E414E434546464543540001")descriptor=p;
  need(!descriptor.is_null(),"Recovery manual editor requires registered deterministic VST3 gain fixture");
  invoke(c,"plugin.add",{{"descriptor",descriptor}});
  const auto path=directory/"recovery-manual.screamseq";invoke(c,"document.save",{{"path",path.generic_string()},{"overwrite",true}});
  const auto savedBeforeOpen=Project::readProjectBytes(path);
  stage="open and capture attached gesture";
  invoke(c,"plugin.editor.open",{{"slot",0}});
  need(c.view()->hasOpenEditors,"open editor did not publish recovery timer eligibility");
  // Fixture View::attached deliberately emits a .625 gesture from its original
  // .5 baseline. Opening is therefore already a real pending manual change.
  const auto openedView=c.view();const auto openedStops=stops;const auto attached=recoveryCapture(c);
  need(attached->needsProtection && *attached->bytes!=savedBeforeOpen && !openedView->dirty && c.view()==openedView && stops==openedStops,
    "recovery did not capture fixture's real attached gesture without mutation");
  stage="save attached gesture baseline";
  // Match native Save's explicit editor synchronization before capturing its
  // expectedRevision. The attached gesture legitimately advances plugin history.
  call(c,"flushPluginEditors",{{"force",true}});
  invoke(c,"document.save",{{"path",path.generic_string()},{"overwrite",true}});
  const auto saved=Project::readProjectBytes(path);
  need(saved==*attached->bytes,"capture consumed the attached gesture before normal Save committed it");
  const auto module=GetModuleHandleW(std::filesystem::u8path(descriptor.at("path").get<std::string>()).c_str());
  need(module!=nullptr,"Recovery manual fixture module is not loaded");
  const auto gesture=reinterpret_cast<int(*)(double)>(GetProcAddress(module,"ResonanceFixtureGesture"));
  need(gesture!=nullptr,"Recovery fixture gesture export missing");
  const auto before=c.view();const auto stopped=stops;
  stage="capture clean baseline and manual gesture";
  auto baseline=recoveryCapture(c);need(!baseline->needsProtection && !before->dirty && baseline->hasOpenEditors,"unchanged clean editor incorrectly needs protection");
  Tracker::WindowsVST3::pluginMainCall([&]{need(gesture(.375)>0,"fixture manual gesture rejected");});
  const auto snapshot=recoveryCapture(c);
  need(snapshot->needsProtection && snapshot->fingerprint!=baseline->fingerprint && snapshot->revision==baseline->revision,"manual recovery overlay missing or forged revision");
  need(c.view()==before && !c.view()->dirty && stops==stopped && Project::readProjectBytes(path)==saved,"manual capture mutated source dirty/history/path/file or stopped playback");
  const auto copied=Project::decodePlist(*snapshot->bytes),original=Project::decodePlist(saved);
  need(copied.at("plugins")[0].at("state")!=original.at("plugins")[0].at("state"),"manual plugin state absent from recovery bytes");
  auto unchanged=copied;unchanged["plugins"][0]["state"]=original.at("plugins")[0].at("state");
  need(Project::encodePlist(unchanged)==saved,"manual overlay changed unrelated project bytes");
  need(recoveryCapture(c)->fingerprint==snapshot->fingerprint,"manual capture fingerprint is unstable");
  stage="guard default restore of unsaved manual state";
  bool reject=false;try{recoveryWait(c,c.recover(baseline->bytes,before->session.revision));}catch(const Api::ApiError &e){reject=e.code==-32602;}
  need(reject && c.view()==before && stops==stopped,"default recovery discarded uncommitted manual state");
  // A capture must not eat the notification: normal subsequent editor flush
  // still commits exactly the user's gesture through ordinary plugin history.
  stage="commit retained notification and undo";
  call(c,"flushPluginEditors",{{"force",true}});
  need(c.view()->session.revision!=before->session.revision && c.view()->dirty,"capture consumed pending editor commit");
  invoke(c,"history.undo",{{"domain","plugins"}});
  need(*recoveryCapture(c)->bytes==*baseline->bytes,"manual state capture added an extra plugin Undo step");

  // Reopen a compatible preserved take, repeat the overlay, then return the
  // manual instance to baseline. The source take must never have been invalidated.
  stage="restore preserved take and reopen manual editor";
  auto takeTree=original;auto doc=Project::openNativeProjectBytes(saved);takeTree["recoveryTake"]=recoveryTake(*doc.document);
  recoveryWait(c,c.recover(recoveryBytes(takeTree),c.view()->session.revision,true));
  invoke(c,"plugin.editor.open",{{"slot",0}});
  // Undo/recovery can unload the prior editor module; resolve the current one.
  const auto takeModule=GetModuleHandleW(std::filesystem::u8path(descriptor.at("path").get<std::string>()).c_str());
  need(takeModule!=nullptr,"Reloaded recovery fixture module is not loaded");
  const auto takeGesture=reinterpret_cast<int(*)(double)>(GetProcAddress(takeModule,"ResonanceFixtureGesture"));
  need(takeGesture!=nullptr,"Reloaded recovery fixture gesture export missing");
  const auto parameters=call(c,"plugin.parameters.get",{{"slot",0}});
  const auto parameter=std::find_if(parameters.begin(),parameters.end(),[](const auto &p){return p.at("id")==7;});
  need(parameter!=parameters.end(),"take fixture gain missing");const auto baselineValue=parameter->at("value").get<double>();
  auto takeBase=recoveryCapture(c);const auto takeBefore=c.view();
  stage="capture take overlay and reject late manual change";
  Tracker::WindowsVST3::pluginMainCall([&]{need(takeGesture(baselineValue<.75?.9:.1)>0,"take manual gesture rejected");});
  const auto overlaid=recoveryCapture(c);need(!Project::decodePlist(*overlaid->bytes).at("recoveryTake").at("compatible").get<bool>(),"changed copy retained compatible take");
  need(c.view()==takeBefore && Project::decodePlist(*takeBase->bytes).at("recoveryTake").at("compatible").get<bool>(),"overlay changed captured take provenance");
  bool lateRejected=false;const auto beforeLateStops=stops;
  try{recoveryWait(c,c.recover(baseline->bytes,takeBefore->session.revision,true,takeBase->guardFingerprint));}catch(const Api::ApiError &e){lateRejected=e.code==-32001;}
  need(lateRejected && c.view()==takeBefore && stops==beforeLateStops,"restore lost manual edit made after protective snapshot");
  Tracker::WindowsVST3::pluginMainCall([&]{need(takeGesture(baselineValue)>0,"return manual baseline rejected");});
  need(*recoveryCapture(c)->bytes==*takeBase->bytes,"recovery overlay permanently changed source take compatibility");
  std::cout<<"PASS recovery real manual VST3 overlay, stable fingerprint, capture purity, pending gesture history and copy-only take incompatibility\n";
  }catch(const std::exception &e){throw std::runtime_error(std::string("Recovery manual stage [")+stage+"]: "+e.what());}
}
