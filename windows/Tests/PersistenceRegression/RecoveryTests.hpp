Json recoveryFixture(Document &doc,bool compatible=true) {
  auto tree=nativeProjectTree(doc,newProjectState(doc));
  tree["recoveryTake"]={{"compatible",compatible},{"missingTime",5},{"exhaustedVoices",7},{"overflow",11},
    {"future",{{"data",Json::binary({0,255,42})},{"flag",false},{"zero",-0.0}}},
    {"events",Json::array({{{"pattern","n"+std::to_string(doc.native().patterns.begin()->second.id)},
      {"track","n"+std::to_string(doc.native().tracks.begin()->second.id)},{"position",17},{"instrument",1},{"note",61},{"velocity",100},
      {"futureEvent",Json::binary({0,255,127,8})}}})}};
  return tree;
}
void recoveryLifecycle(const std::filesystem::path &dir) {
  auto doc=Document::demo();
  doc->transaction([](auto &song){auto next=song.Order.AddSequence();song.Order(next).assign(1,0);song.Order.SetSequence(0);});
  auto tree=recoveryFixture(*doc);auto input=dir/"lifecycle-input.screamseq";
  writeTree(input,tree);const auto events=encodePlist(tree["recoveryTake"]["events"]);
  auto edited=[](Document &d) {auto old=d.cell(0,0,0),next=old;next.note=62;d.edit({{0,0,0,old,next}});};
  for(const std::string scenario:{"unchanged","no-op","edit-save-save","undo","failed-save","already-false","sequence","new-capture","missing-origin","external-hook"}) {
    auto loaded=openNativeProject(input);auto &d=*loaded.document;auto &s=loaded.state;
    auto origin=s.recoveryOrigin;auto output=dir/(scenario+".screamseq");
    bool expected=scenario=="unchanged" || scenario=="no-op" || scenario=="new-capture";
    if(scenario=="no-op") {auto c=d.cell(0,0,0);d.edit({{0,0,0,c,c}});check(d.revision==origin->revision,"no-op must not change musical revision");}
    if(scenario=="edit-save-save" || scenario=="undo" || scenario=="failed-save" || scenario=="new-capture") edited(d);
    if(scenario=="undo") {d.undo();check(d.revision!=origin->revision,"Undo keeps changed provenance even if song content returns");}
    if(scenario=="already-false") s.preserved["recoveryTake"]["compatible"]=false;
    if(scenario=="sequence") {d.song().Order.SetSequence(1);check(d.revision==origin->revision,"sequence-only change exercises sequence guard");}
    if(scenario=="new-capture") {
      // Fixture-only capture seam, not a recording implementation. Start a REAL
      // new take after an unsaved edit: savedRevision is not its origin.
      s.preserved["recoveryTake"]=tree["recoveryTake"];
      s.recoveryOrigin=RecoveryOrigin{d.revision,unsigned(d.song().Order.GetCurrentSequenceIndex())};origin=s.recoveryOrigin;
      check(origin->revision!=s.savedRevision,"capture starts after unsaved edit");
    }
    if(scenario=="missing-origin") s.recoveryOrigin.reset();
    if(scenario=="external-hook") invalidateRecoveryTake(s);
    if(scenario=="failed-save") {
      writeTree(output,tree);auto bytes=readProjectBytes(output);auto before=s;
      bool rejected=false;try {saveNativeProject(d,s,output,false);}catch(const std::exception &){rejected=true;}
      check(rejected && readProjectBytes(output)==bytes,"failed publication leaves original file");
      check(s.recoveryOrigin==before.recoveryOrigin && s.savedRevision==before.savedRevision && sameStoredValue(s.preserved,before.preserved),"failed publication must not update origin or preserved baseline");
    }
    const auto preview=nativeProjectTree(d,s);
    check(preview["recoveryTake"]["compatible"].get<bool>()==expected,"serialized recovery compatibility "+scenario);
    saveNativeProject(d,s,output,true);
    check(s.recoveryOrigin==(scenario=="missing-origin" ? std::optional<RecoveryOrigin>{} : origin),"successful save never rebases origin");
    auto once=decodePlist(readProjectBytes(output));
    check(encodePlist(once["recoveryTake"]["events"])==events,"all event bytes retained "+scenario);
    auto take=once["recoveryTake"];take["compatible"]=true;
    check(sameStoredValue(take,decodePlist(encodePlist(tree))["recoveryTake"]),"all counters and unknown take fields retained");
    saveNativeProject(d,s,output,true);
    check(sameStoredValue(decodePlist(readProjectBytes(output)),once),"repeated save does not rebase compatibility");
    auto reopened=openNativeProject(output);
    check(reopened.state.preserved["recoveryTake"]["compatible"].get<bool>()==expected,"reopen compatibility "+scenario);
    auto reopenedPath=dir/(scenario+"-reopened.screamseq");saveNativeProject(*reopened.document,reopened.state,reopenedPath,true);
    check(decodePlist(readProjectBytes(reopenedPath))["recoveryTake"]["compatible"].get<bool>()==expected,"false cannot become true on reopen then save");
    if(scenario=="edit-save-save") {
      d.undo();saveNativeProject(d,s,output,true);
      check(!openNativeProject(output).state.preserved["recoveryTake"]["compatible"].get<bool>(),"Undo after successful saves cannot revive compatibility");
    }
    if(scenario=="sequence") {
      d.song().Order.SetSequence(0);saveNativeProject(d,s,output,true);
      check(!openNativeProject(output).state.preserved["recoveryTake"]["compatible"].get<bool>(),"return to old sequence after invalidation cannot revive compatibility");
    }
    std::cout<<"PASS recovery "<<scenario<<'\n';
  }
}
