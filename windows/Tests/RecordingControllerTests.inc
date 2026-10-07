Json stableRecording(DocumentController &c) {auto result=call(c,"recording.get",Json::object());result.erase("hostTime");return result;}
Json recordingInput(uint64_t timestamp,unsigned status,unsigned note,unsigned velocity) {
  return {{"timestamp",std::to_string(timestamp)},{"status",status},{"note",note},{"velocity",velocity}};
}
template<typename Action>void recordingRejected(DocumentController &c,Action action,const char *message,int code=-32602) {
  const auto view=c.view();const auto take=stableRecording(c);bool rejected=false;
  try{action();}catch(const Api::ApiError &e){rejected=e.code==code;}
  need(rejected&&view==c.view()&&take==stableRecording(c),message);
}
void recordingControllerTests(const std::filesystem::path &directory) {
  unsigned stops=0;DocumentController c({},"recording-worker",[&]{++stops;},[](const auto &){});
  const auto baseline=recoveryCapture(c);const auto original=c.view();
  recordingRejected(c,[&]{invoke(c,"recording.start");},"Missing start fields were not an atomic parameter error");
  for(const auto &bad:std::vector<Json>{
    {{"channels",Json::array()},{"instrument",1}},{{"channels",Json::array({0,0})},{"instrument",1}},
    {{"channels",Json::array({0})},{"instrument",true}},{{"channels",Json::array({0})},{"instrument",1},{"quantization",65537}},
    {{"channels",Json::array({0})},{"instrument",1},{"latencyMS",501}}})
    recordingRejected(c,[&]{invoke(c,"recording.start",bad);},"Invalid start changed recording state");
  auto started=invoke(c,"recording.start",{{"channels",Json::array({0,1})},{"instrument",1}});
  const auto id=started.at("take").get<std::string>();
  need(!id.empty()&&started.at("capturing")==true&&started.at("baseRevision")==original->session.revision,"Recording start did not pin the base");
  need(c.view()->session.revision==original->session.revision&&!c.view()->dirty&&!stops&&c.view()->session.document.at("hasRecoveryTake")==true,"Start changed music/dirty/playback or omitted take flag");
  need(c.view()->patterns.at(0)==original->patterns.at(0)&&c.view()->waves.at(1)==original->waves.at(1)&&c.view()->nativePattern==original->nativePattern,"Take summary rebuilt music caches");
  recordingRejected(c,[&]{invoke(c,"recording.start",{{"channels",Json::array({0})},{"instrument",1}});},"Repeated start replaced take");
  recordingRejected(c,[&]{invoke(c,"recording.stop",{{"take","another"}});},"Wrong take stopped capture");
  recordingRejected(c,[&]{invoke(c,"recording.stop");},"Missing take ID was not an atomic parameter error");
  recordingRejected(c,[&]{invoke(c,"recording.capture",{{"take",id}});},"Missing capture events were not an atomic parameter error");
  recordingRejected(c,[&]{invoke(c,"recording.capture",{{"take",id},{"events",Json::array({Json{{"timestamp","1"},{"status",144},{"note",60}}})}});},"Missing capture velocity was not an atomic parameter error");
  recordingRejected(c,[&]{call(c,"recording.capture",{{"expectedRevision","stale"},{"take",id},{"events",Json::array()}});},"Stale capture changed take",-32001);
  auto partial=Json::array({recordingInput(UINT64_MAX,0x90,60,100),recordingInput(UINT64_MAX,0x80,60,0)});partial[1]["velocity"]=true;
  recordingRejected(c,[&]{invoke(c,"recording.capture",{{"take",id},{"events",partial}});},"Malformed capture partially accepted first event");
  partial[1]["velocity"]=0;partial[1]["timestamp"]="18446744073709551616";
  recordingRejected(c,[&]{invoke(c,"recording.capture",{{"take",id},{"events",partial}});},"Decimal uint64 overflow changed take");
  partial=Json::array();for(unsigned i=0;i<1025;++i)partial.push_back(recordingInput(0,0x90,60,100));
  recordingRejected(c,[&]{invoke(c,"recording.capture",{{"take",id},{"events",partial}});},"Oversized batch changed take");
  invoke(c,"recording.capture",{{"take",id},{"events",Json::array({recordingInput(0,0x90,60,100)})}});
  need(stableRecording(c).at("missingTime")==1&&stableRecording(c).at("eventCount")==0,"Unmapped timestamp substituted cursor time");
  recordingRejected(c,[&]{invoke(c,"document.save",{{"path",(directory/"unfinished.screamseq").generic_string()}});},"Save did not guard unfinished take");
  recordingRejected(c,[&]{invoke(c,"document.open",{{"path",(directory/"missing.screamseq").generic_string()},{"discard",true}});},"Open discard bypassed unfinished take");

  // API start-before-Play binds the first real timed prepared renderer. Both
  // channels retain precise timing despite batch delivery order.
  auto *playback=recoveryWait(c,c.prepare(48000,{{"pattern",0},{"startRow",0},{"endRow",16}},true,true));
  const auto origin=hostTime100ns();const uint64_t stream=37;
  std::vector<float> pcm(96000*2);need(playback->render(pcm.data(),96000,RenderTime{origin,stream,48000,true,false}),"Recording timed render failed");
  const auto timeline=playback->recordingTimeline();
  const auto one=origin+10000,two=origin+20000,offOne=origin+80000,offTwo=origin+90000;
  const auto firstPosition=timeline->locate(one,stream);need(firstPosition.has_value(),"Timed render did not publish input mapping");
  invoke(c,"recording.capture",{{"take",id},{"events",Json::array({recordingInput(offTwo,0x81,60,0),recordingInput(two,0x91,60,77),recordingInput(offOne,0x80,60,0),recordingInput(one,0x90,60,93)})}});
  const auto recorded=stableRecording(c);
  need(recorded.at("eventCount")==4&&recorded.at("events")[0].at("position")==firstPosition->position&&recorded.at("events")[0].at("velocity")==93&&recorded.at("events")[0].at("track")!=recorded.at("events")[1].at("track"),"Timed capture lost sort, position, velocity or channel identity");
  need(recorded.at("missingTime")==1&&c.view()->session.revision==original->session.revision&&!c.view()->dirty&&!stops,"Capture changed music/history/playback");
  invoke(c,"recording.capture",{{"take",id},{"events",Json::array({recordingInput(one,0x90,65,100)})}});
  need(stableRecording(c).at("missingTime")==2&&stableRecording(c).at("eventCount")==4,"Out-of-order timestamp retargeted take");

  // Held duration belongs to saved bytes, while elapsed time alone does not
  // stale explicit restore's protection guard. Live events remain untouched.
  const auto heldTime=origin+100000;
  invoke(c,"recording.capture",{{"take",id},{"events",Json::array({recordingInput(heldTime,0x90,67,101)})}});
  const auto rawTake=stableRecording(c);
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(1);
  while(hostTime100ns()<heldTime+30000&&std::chrono::steady_clock::now()<deadline)std::this_thread::yield();
  const auto savedA=recoveryCapture(c);
  const auto nextTime=hostTime100ns()+20000;
  while(hostTime100ns()<nextTime&&std::chrono::steady_clock::now()<deadline)std::this_thread::yield();
  const auto savedB=recoveryCapture(c);
  need(rawTake==stableRecording(c)&&savedA->hasRecording&&savedA->needsProtection,"Recovery mutated live take");
  need(savedA->fingerprint!=savedB->fingerprint&&savedA->guardFingerprint==savedB->guardFingerprint,"Held duration and stable restore guard fingerprints were conflated");
  const auto savedTree=Project::decodePlist(*savedB->bytes);
  need(savedTree.at("recoveryTake").at("events").size()==6&&savedTree.at("recoveryTake").at("events").back().at("note")==255,"Recovery copy failed to close held note");
  const auto stopsBeforeRestore=stops;
  recoveryWait(c,c.recover(baseline->bytes,c.view()->session.revision,true,savedA->guardFingerprint));
  need(stops==stopsBeforeRestore+1&&stableRecording(c).at("take")==""&&c.view()->dirty,"Protected restore rejected only elapsed held duration or retained replaced take");

  // Generation replacement cannot silently attach an already-recorded take.
  started=invoke(c,"recording.start",{{"channels",Json::array({0})},{"instrument",1}});const auto secondID=started.at("take");
  playback=recoveryWait(c,c.prepare(48000,Json::object(),true,true));const auto secondOrigin=hostTime100ns();
  need(playback->render(pcm.data(),8192,RenderTime{secondOrigin,81,48000,true,false}),"Second stream fixture render");
  invoke(c,"recording.capture",{{"take",secondID},{"events",Json::array({recordingInput(secondOrigin+10000,0x90,60,99)})}});
  const auto bound=stableRecording(c);need(bound.at("eventCount")==1,"Second take failed to bind");
  need(playback->render(pcm.data(),8192,RenderTime{secondOrigin+2000000,82,48000,true,true}),"Restarted stream fixture render");
  const auto interruptedCopy=recoveryCapture(c);const auto interruptedTree=Project::decodePlist(*interruptedCopy->bytes).at("recoveryTake");
  need(stableRecording(c)==bound,"Clock-loss recovery snapshot changed the live take");
  need(interruptedTree.at("events").size()==2&&interruptedTree.at("missingTime")==1&&!interruptedTree.at("inputError").get<std::string>().empty(),"Recovery hid a held note's lost recording clock");
  invoke(c,"recording.stop",{{"take",secondID}});
  const auto interrupted=stableRecording(c);
  need(interrupted.at("eventCount")==2&&interrupted.at("missingTime")==1&&!interrupted.at("inputError").get<std::string>().empty(),"Stop hid clock loss when no later MIDI arrived");
  invoke(c,"recording.stop",{{"take",secondID}});need(stableRecording(c)==interrupted,"Repeated Stop duplicated clock-loss diagnostics");
  invoke(c,"recording.loss",{{"take",secondID},{"dropped",7},{"reason","Owned MIDI queue overflow"}});
  auto loss=stableRecording(c);need(loss.at("capturing")==false&&loss.at("eventCount")==2&&loss.at("overflow")==7&&loss.at("inputError")=="Owned MIDI queue overflow","Input loss failed to retain a stopped, closed, diagnosed take");
  const auto lostCopy=recoveryCapture(c);auto lostTree=Project::decodePlist(*lostCopy->bytes);
  need(lostTree.at("recoveryTake").at("inputError")==loss.at("inputError"),"Recovery omitted input-loss explanation");
  auto invalidLoss=lostTree;invalidLoss["recoveryTake"]["inputError"]=true;
  recordingRejected(c,[&]{recoveryWait(c,c.recover(recoveryBytes(invalidLoss),c.view()->session.revision,true));},"Malformed recovery loss reason replaced current take",-32003);
  invalidLoss["recoveryTake"]["inputError"]=std::string(513,'x');
  recordingRejected(c,[&]{recoveryWait(c,c.recover(recoveryBytes(invalidLoss),c.view()->session.revision,true));},"Oversized recovery loss reason replaced current take",-32003);
  recoveryWait(c,c.recover(lostCopy->bytes,c.view()->session.revision,true));
  const auto restoredLoss=stableRecording(c),restoredLossID=restoredLoss.at("take");
  need(restoredLossID!=secondID&&restoredLoss.at("capturing")==false&&restoredLoss.at("inputError")==loss.at("inputError")&&restoredLoss.at("overflow")==7,"Recovered stopped take cleared loss review reason or reused ID");
  need(*recoveryCapture(c)->bytes==*lostCopy->bytes,"Recovery loss reason failed exact native roundtrip");
  recordingRejected(c,[&]{invoke(c,"recording.capture",{{"take",restoredLossID},{"events",Json::array()}});},"Stopped take accepted capture");
  invoke(c,"recording.discard",{{"take",restoredLossID}});

  // Same-generation expired mapping is a loss while the stream is active,
  // whereas an explicit device Stop can safely use its final audible bound.
  for(const bool normalStop:{false,true}) {
    const auto shortID=invoke(c,"recording.start",{{"channels",Json::array({0})},{"instrument",1}}).at("take");
    playback=recoveryWait(c,c.prepare(48000,Json::object(),true,true));const auto shortOrigin=hostTime100ns();
    need(playback->render(pcm.data(),128,RenderTime{shortOrigin,91,48000,true,false}),"Short clock fixture render");
    invoke(c,"recording.capture",{{"take",shortID},{"events",Json::array({recordingInput(shortOrigin+1000,0x90,60,99)})}});
    const auto shortEnd=playback->recordingTimeline()->endHostTime.load();
    const auto shortDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(1);
    while(hostTime100ns()<=shortEnd&&std::chrono::steady_clock::now()<shortDeadline)std::this_thread::yield();
    if(normalStop)need(playback->render(pcm.data(),0,RenderTime{hostTime100ns(),91,48000,false,false,true}),"Normal Stop fixture notification");
    const auto beforeStop=stableRecording(c),copy=Project::decodePlist(*recoveryCapture(c)->bytes).at("recoveryTake");
    need(stableRecording(c)==beforeStop,"Expired clock snapshot changed live held state");
    need(copy.at("missingTime")==unsigned(!normalStop)&&copy.value("inputError",std::string()).empty()==normalStop,"Snapshot confused stopped bounds with expired live history");
    const auto stopped=invoke(c,"recording.stop",{{"take",shortID}});
    need(stopped.at("eventCount")==2&&stopped.at("missingTime")==unsigned(!normalStop)&&stopped.at("inputError").get<std::string>().empty()==normalStop,"Stop confused stopped bounds with expired live history");
    invoke(c,"recording.discard",{{"take",shortID}});
  }

  // Restore bounded native take metadata with unknown fields, then finish one
  // merged edit. Row replacement preserves FX and unrelated track notes.
  auto document=Tracker::Document::demo();document->transaction([](auto &song){auto &cell=*song.Patterns[0].GetpModCommand(0,0);cell.note=61;cell.instr=1;cell.volcmd=OpenMPT::VOLCMD_VOLUME;cell.vol=48;cell.command=OpenMPT::CMD_TEMPO;cell.param=125;});
  const auto pattern=document->native().patterns.at(0).id,track=document->native().tracks.at(0).id,otherTrack=document->native().tracks.at(1).id;
  document->annotate([&](auto &native){native.preciseNotes={{pattern,track,500,1,60,80},{pattern,otherTrack,3333,1,67,100}};});
  auto tree=Project::decodePlist(Project::serializeNativeProject(*document,Project::newProjectState(*document)));
  tree["recoveryTake"]=recoveryTake(*document);tree["recoveryTake"]["events"]=Json::array({
    {{"pattern","n"+std::to_string(pattern)},{"track","n"+std::to_string(track)},{"position",1234},{"note",61},{"instrument",1},{"velocity",93},{"futureEvent",Json::binary({0,255})}},
    {{"pattern","n"+std::to_string(pattern)},{"track","n"+std::to_string(track)},{"position",1234},{"note",65},{"instrument",1},{"velocity",99}},
    {{"pattern","n"+std::to_string(pattern)},{"track","n"+std::to_string(track)},{"position",9000},{"note",255},{"instrument",0},{"velocity",127}}});
  const auto recoveredBytes=recoveryBytes(tree);
  recoveryWait(c,c.recover(recoveredBytes,c.view()->session.revision,true));
  const auto hydrated=stableRecording(c),hydratedID=hydrated.at("take");const auto recoveredView=c.view();
  need(hydrated.at("capturing")==false&&hydrated.at("compatible")==true&&hydrated.at("eventCount")==3,"Compatible recovery failed to hydrate stopped take");
  need(*recoveryCapture(c)->bytes==*recoveredBytes,"Hydrated take lost unknown fields or exact events");
  const auto preview=invoke(c,"recording.commit",{{"take",hydratedID},{"replaceRows",true},{"dryRun",true}});
  need(preview.at("wouldChange")==true&&c.view()==recoveredView&&stableRecording(c)==hydrated,"Recording dry run changed music or consumed take");
  const auto previousCell=c.view()->cell(0,0,0);const auto previousNotes=call(c,"pattern.notes.get",{{"pattern",0}});
  invoke(c,"recording.commit",{{"take",hydratedID},{"replaceRows",true}});
  const auto committed=call(c,"pattern.notes.get",{{"pattern",0}});const auto cell=c.view()->cell(0,0,0);
  need(committed.at("events").size()==3&&std::any_of(committed.at("events").begin(),committed.at("events").end(),[](const auto &e){return e.at("position")==1234&&e.at("note")==65;}),"Latest recording event did not win or unrelated track disappeared");
  need(!cell.note&&!cell.instrument&&!cell.volumeCommand&&!cell.volume&&cell.effect==previousCell.effect&&cell.parameter==previousCell.parameter,"Replace rows changed unrelated tracker FX");
  need(stableRecording(c).at("take")==""&&!recoveryCapture(c)->hasRecording,"Successful commit left a resurrectable take wrapper");
  invoke(c,"history.undo",{{"domain","document"}});
  need(call(c,"pattern.notes.get",{{"pattern",0}})==previousNotes&&c.view()->cell(0,0,0)==previousCell&&stableRecording(c).at("take")=="","One Undo failed to restore all notes/cells or resurrected take");
  invoke(c,"history.redo",{{"domain","document"}});need(call(c,"pattern.notes.get",{{"pattern",0}})==committed,"One Redo failed to restore recording commit");
  const auto path=directory/"recorded.screamseq";invoke(c,"document.save",{{"path",path.generic_string()},{"overwrite",true}});
  invoke(c,"document.open",{{"path",path.generic_string()}});need(call(c,"pattern.notes.get",{{"pattern",0}})==committed&&!recoveryCapture(c)->hasRecording,"Recording save/reopen changed exact notes or resurrected take");

  // Incompatible imported events remain inspectable even with missing targets;
  // a later music Undo cannot revive their compatibility.
  tree["recoveryTake"]["compatible"]=false;tree["recoveryTake"]["events"][0]["track"]="n999999";
  recoveryWait(c,c.recover(recoveryBytes(tree),c.view()->session.revision,true));const auto unmatched=stableRecording(c).at("take");
  need(stableRecording(c).at("baseRevision")=="unmatched-recovered-take"&&stableRecording(c).at("compatible")==false,"Incompatible take was silently rebased");
  recordingRejected(c,[&]{invoke(c,"recording.commit",{{"take",unmatched}});},"Incompatible take committed");
  invoke(c,"document.patch",{{"title","Changed around retained take"}});invoke(c,"history.undo",{{"domain","document"}});
  recordingRejected(c,[&]{invoke(c,"recording.commit",{{"take",unmatched}});},"Undo revived incompatible take");
  invoke(c,"recording.discard",{{"take",unmatched}});need(!recoveryCapture(c)->hasRecording,"Discard left preserved wrapper");
  auto stale=invoke(c,"recording.start",{{"channels",Json::array({0})},{"instrument",1}}).at("take");
  invoke(c,"document.patch",{{"title","Stale take base"}});invoke(c,"recording.stop",{{"take",stale}});
  recordingRejected(c,[&]{invoke(c,"recording.commit",{{"take",stale}});},"Current write revision silently rebased stale take");
  invoke(c,"recording.discard",{{"take",stale}});
  auto empty=invoke(c,"recording.start",{{"channels",Json::array({0})},{"instrument",1}}).at("take");invoke(c,"recording.stop",{{"take",empty}});
  const auto beforeEmpty=c.view()->session.revision;const auto noChange=invoke(c,"recording.commit",{{"take",empty}});
  need(noChange.at("wouldChange")==false&&c.view()->session.revision==beforeEmpty&&stableRecording(c).at("take")=="","Empty finish manufactured music or failed to consume take");
  std::cout<<"PASS recording worker: guarded batches, clock binding, copy-only recovery, loss retention, hydration, one Undo/Redo and native persistence\n";
}
