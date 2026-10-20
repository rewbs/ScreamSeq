void projectIntegerRecovery() {
  namespace P=ScreamSeq::Project;
  const auto fixture=std::getenv("SCREAMSEQ_SNAPSHOT_REFERENCE_PROJECT");CHECK(fixture&&*fixture);
  const auto fixtureBytes=P::readProjectBytes(std::filesystem::u8path(fixture));
  auto initial=P::openNativeProjectBytes(fixtureBytes);
  auto expected=P::decodePlist(fixtureBytes);
  const auto slot=expected.at("plugins").size();
  const auto opaque=Json::binary({0,255,17,0,128,42});
  expected["plugins"].push_back({
    {"format","VST3"},{"path","C:/ParityMissing/Unavailable.vst3"},
    {"classID","00112233445566778899AABBCCDDEEFF"},
    {"name","Unavailable integer recovery fixture"},{"instanceID","integer-recovery-provider"},
    {"type",0},{"subtype",0},{"manufacturer",0},{"isInstrument",true},
    {"bypass",false},{"instrument",1},{"instrumentAssignments",Json::array({{{"instrument",1},{"channel",2}}})},
    {"auxiliaryInputs",Json::array({1,2})},{"auxiliaryOutputs",Json::array({1,3})},
    {"audioLayout",""},{"state",opaque},{"futureReal",0.0}});
  expected["automation"]=Json::array({Json::array({slot,7,0.5,480})});
  const auto pattern="n"+std::to_string(initial.document->native().patterns.begin()->second.id);
  const auto track="n"+std::to_string(initial.document->native().tracks.begin()->second.id);
  expected["recoveryTake"]={{"compatible",true},{"missingTime",5},{"exhaustedVoices",7},{"overflow",11},
    {"events",Json::array({{{"pattern",pattern},{"track",track},{"position",17},{"instrument",1},{"note",61},{"velocity",100},{"futureReal",0.0}}})}};
  // Establish strict validity without loading the unavailable vendor.
  (void)P::openNativeProjectBytes(P::encodePlist(expected));
  const auto pluginPath="/plugins/"+std::to_string(slot);
  const std::vector<std::string> strictFields{
    "/sequence",pluginPath+"/type",pluginPath+"/instrument",
    pluginPath+"/instrumentAssignments/0/channel",pluginPath+"/auxiliaryInputs/0",
    "/automation/0/0","/automation/0/1","/automation/0/3",
    "/recoveryTake/missingTime","/recoveryTake/events/0/position"};
  for(const auto &field:strictFields) {
    auto noncanonical=expected;const Json::json_pointer pointer(field);
    noncanonical[pointer]=double(noncanonical.at(pointer).get<uint64_t>());
    rejects([&]{(void)P::openNativeProjectBytes(P::encodePlist(noncanonical));});
  }
  auto represented=expected;
  represented["version"]=6.0;represented["native"]["version"]=17.0;
  represented["sequence"]=double(expected["sequence"].get<unsigned>());
  for(auto &plugin:represented["plugins"]) {
    for(const char *key:{"type","subtype","manufacturer","instrument"})plugin[key]=double(plugin.at(key).get<uint64_t>());
    for(const char *key:{"auxiliaryInputs","auxiliaryOutputs"})for(auto &bus:plugin[key])bus=double(bus.get<uint64_t>());
    for(auto &assignment:plugin["instrumentAssignments"])for(const char *key:{"instrument","channel"})assignment[key]=double(assignment.at(key).get<uint64_t>());
  }
  for(auto &point:represented["automation"])for(size_t index:{size_t(0),size_t(1),size_t(3)})point[index]=double(point[index].get<uint64_t>());
  auto &take=represented["recoveryTake"];
  for(const char *key:{"missingTime","exhaustedVoices","overflow"})take[key]=double(take.at(key).get<uint64_t>());
  for(auto &event:take["events"])for(const char *key:{"position","instrument","note","velocity"})event[key]=double(event.at(key).get<uint64_t>());
  const auto bytes=P::encodePlist(represented);
  rejects([&]{(void)P::openNativeProjectBytes(bytes);});
  const auto directory=std::filesystem::u8path(std::getenv("TMPDIR"));
  const auto source=directory/"project-integer-representations.screamseq",saved=directory/"project-integer-canonical.screamseq";
  P::writeProjectFile(source,bytes,true);
  auto recovered=P::openNativeProject(source);
  CHECK(!recovered.state.requiresSaveAs&&!recovered.state.recoveryLossy&&recovered.state.loadWarnings.empty());
  CHECK(core(*recovered.document)==core(*initial.document)&&recovered.document->native()==initial.document->native());
  // Compare the encoded typed tree: JSON numeric equality alone would wrongly
  // accept a real remaining in a canonical integer field.
  CHECK(P::encodePlist(recovered.state.preserved)==P::encodePlist(expected));
  CHECK(recovered.state.preserved["plugins"][slot]["state"]==opaque);
  CHECK(recovered.state.preserved["plugins"][slot]["futureReal"].is_number_float());
  CHECK(recovered.state.preserved["recoveryTake"]["events"][0]["futureReal"].is_number_float());
  P::saveNativeProject(*recovered.document,recovered.state,saved,true);
  auto reopened=P::openNativeProject(saved);
  CHECK(!reopened.state.requiresSaveAs&&reopened.state.loadWarnings.empty());
  CHECK(P::encodePlist(reopened.state.preserved)==P::encodePlist(expected));
  (void)P::openNativeProjectBytes(P::readProjectBytes(saved));
  // File import must not loosen the captured-byte or canonical writer contract.
  rejects([&]{(void)P::openNativeProjectBytes(bytes);});
  auto noncanonical=recovered.state;noncanonical.preserved=represented;
  rejects([&]{(void)P::serializeNativeProject(*recovered.document,noncanonical);});
  CHECK(P::readProjectBytes(source)==bytes);
  const auto malformed=directory/"project-integer-malformed.screamseq";
  for(const auto &bad:std::vector<Json>{true,-1,0.5,4294967296.0,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
    auto damaged=expected;damaged["plugins"][slot]["type"]=bad;
    const auto original=P::encodePlist(damaged);P::writeProjectFile(malformed,original,true);
    rejects([&]{(void)P::openNativeProjectBytes(original);});
    auto partial=P::openNativeProject(malformed);
    CHECK(partial.state.requiresSaveAs&&partial.state.recoveryLossy&&!partial.state.loadWarnings.empty());
    CHECK(partial.state.preserved["plugins"].size()==slot);
    CHECK(partial.state.preserved["plugins"]==initial.state.preserved["plugins"]);
    CHECK(partial.state.preserved["automation"].empty());
    CHECK(partial.state.preserved["screamseqLoadRecovery"]["sourceSections"]["plugins"]==P::decodePlist(original)["plugins"]);
    rejects([&]{P::saveNativeProject(*partial.document,partial.state,malformed,true);});
    CHECK(P::readProjectBytes(malformed)==original);
  }
  for(const auto &bad:std::vector<Json>{true,-1,0.5,double(uint64_t(48000)*604800)+1}) {
    auto damaged=expected;damaged["automation"][0][3]=bad;
    const auto original=P::encodePlist(damaged);P::writeProjectFile(malformed,original,true);
    auto partial=P::openNativeProject(malformed);
    CHECK(partial.state.requiresSaveAs&&partial.state.recoveryLossy&&partial.state.preserved["automation"].empty());
    CHECK(partial.state.preserved["plugins"]==expected["plugins"]);
    CHECK(P::readProjectBytes(malformed)==original);
  }
  CHECK(P::readProjectBytes(std::filesystem::u8path(fixture))==fixtureBytes);
  std::filesystem::remove(source);std::filesystem::remove(saved);std::filesystem::remove(malformed);
}
