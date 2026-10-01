// Included inside the test namespace. These historical fixtures are generated.
void addExtensions(Json &tree,unsigned &ordinal) {
  if(tree.is_object()) {
    for(auto &v:tree) addExtensions(v,ordinal);
    // Future fields may have names that look like identities. They are not
    // identity keys unless the KNOWN schema uses them at this location.
    if(!tree.contains("id")) tree["id"]="opaque-future-id-"+std::to_string(ordinal);
    tree["future-extension"]={{"ordinal",++ordinal},{"payload",Json::binary({0,255,1})},{"zero",-0.0},{"flag",false},
      {"date",Json::binary(std::vector<uint8_t>(8,0),uint64_t(OpaqueType::Date))},{"uid",Json::binary({7},uint64_t(OpaqueType::UID))}};
  } else if(tree.is_array()) for(auto &v:tree) addExtensions(v,ordinal);
}
Json extensions(const Json &tree) {
  Json result=Json::object();
  std::function<void(const Json &)> visit=[&](const Json &j) {
    if(j.is_object()) for(auto i=j.begin();i!=j.end();++i) {
      if(i.key()=="future-extension") result[std::to_string(i.value().at("ordinal").get<unsigned>())]={{"extension",i.value()},{"id",j.value("id",Json())}};else visit(i.value());
    } else if(j.is_array()) for(const auto &v:j) visit(v);
  };visit(tree);return result;
}
Json fixtureTree(Document &doc,unsigned version) {
  auto state=newProjectState(doc);
  for(auto id:{"fx-identity","instrument-identity","persistent-plugin-uuid"})
    state.preserved["plugins"].push_back({{"format","AU"},{"type",0},{"subtype",0},{"manufacturer",0},{"instanceID",id},{"state",Json::binary({1,2,0,255})}});
  auto tree=nativeProjectTree(doc,state);tree["native"]=legacy(tree["native"],version);return tree;
}
void canonicalFields(const Json &actual,const Json &known) {
  if(known.is_object()) {
    for(auto i=known.begin();i!=known.end();++i) {
      check(actual.contains(i.key()),"every canonical known field materialized: "+i.key());
      canonicalFields(actual.at(i.key()),i.value());
    }
  } else if(known.is_array()) {
    check(actual.is_array() && actual.size()==known.size(),"canonical known array shape");
    for(size_t i=0;i<known.size();++i) canonicalFields(actual[i],known[i]);
  } else check(sameStoredValue(actual,known),"canonical known scalar type/value/bits");
}
void historicalPromotion(const std::filesystem::path &dir) {
  auto rich=richDocument();auto original=rich->native();
  original.performance.bindings[1]={"persistent-plugin-uuid",3,"First"};
  original.signal.lanes[original.tracks.at(1).id]=2;original.signal.layout["another-layout-key"]={4,7};
  for(unsigned version=1;version<=13;++version) {
    rich->restoreNative(historicalModel(original,version));
    auto tree=fixtureTree(*rich,version);auto &meta=tree["native"];
    // Valid omitted defaults and old scalar representations, including paths
    // nested inside non-identity arrays. Only the baseline decoder sets intent.
    if(version>=8) for(auto &c:meta["performance"]["commands"]) if(c.contains("pitchRange")) c.erase("pitchRange");
    if(version>=10) {
      auto &g=meta["signalGraph"];
      for(auto &n:g["library"][0]["nodes"]) for(auto k:{"name","x","y","phase","attack","release","rate","controller"}) n.erase(k);
      g["library"][0]["nodes"][2]["plugin"].erase("inputs");
      g["library"][0]["nodes"][2]["plugin"].erase("outputs");
      // Remove routes depending on those optional plugin buses.
      auto &audio=g["library"][0]["audio"];audio.erase(3);audio.erase(1);
      g["inputs"]=Json::array();g["outputs"]=Json::array();
      for(auto &a:audio) {a.erase("gain");a.erase("input");a.erase("output");}
      for(auto &m:g["library"][0]["modulation"]) for(auto k:{"minimum","maximum","base","enabled"}) m.erase(k);
      for(auto &a:g["assignments"]) {a.erase("amount");a.erase("wet");}
      for(auto &c:g["commands"]) for(auto k:{"amount","wet","tails","column"}) c.erase(k);
      if(version<13) g.erase("instrumentAssignments");
      if(version>=13) for(auto &e:g["library"][0]["nodes"][9]["envelopes"]) {e.erase("enabled");for(auto &p:e["points"]) if(p["curve"]=="linear") p.erase("curve");}
    }
    unsigned ordinal=0;addExtensions(meta,ordinal);
    // Decoder sorts these keyed maps. Opaque members must follow keys, not slots.
    auto reverse=[](Json &j){std::reverse(j.begin(),j.end());};
    reverse(meta["patterns"]);reverse(meta["tracks"]);
    if(version>=7) reverse(meta["performance"]["bindings"]);
    if(version>=10) {reverse(meta["signalGraph"]["lanes"]);reverse(meta["signalGraph"]["layout"]);}
    auto input=dir/("generated-v"+std::to_string(version)+".screamseq");writeTree(input,tree);
    auto loaded=openNativeProject(input);auto baseline=loaded.state.preserved;auto initial=loaded.document->native();
    auto output=dir/("generated-v"+std::to_string(version)+"-saved.screamseq");
    saveNativeProject(*loaded.document,loaded.state,output,true);
    check(sameStoredValue(decodePlist(readProjectBytes(output)),baseline),"v"+std::to_string(version)+" unedited save retains entire typed tree");
    // A cell-only edit must also preserve the old metadata verbatim.
    auto old=loaded.document->cell(0,0,0),changed=old;changed.note=62;
    loaded.document->edit({{0,0,0,old,changed}});saveNativeProject(*loaded.document,loaded.state,output,true);
    check(sameStoredValue(loaded.state.preserved["native"],baseline["native"]),"cell-only edit retains old metadata");
    loaded.document->annotate([](auto &n){n.patterns.begin()->second.name="Promotion edit";n.sequences[0].info.annotation="Sequence edit";if(!n.mixer.buses.empty()) std::reverse(n.mixer.buses.begin(),n.mixer.buses.end());});
    const auto intended=loaded.document->native();saveNativeProject(*loaded.document,loaded.state,output,true);
    auto reopened=openNativeProject(output);const auto &actual=reopened.state.preserved;
    check(actual["native"]["version"]==14,"historical metadata promoted");
    canonicalFields(actual["native"],decodePlist(encodePlist(encodeNativeMetadata(intended))));
    check(reopened.document->native()==intended,"complete promoted model matches intended v"+std::to_string(version));
    check(sameStoredValue(extensions(actual["native"]),extensions(baseline["native"])),"all unknown typed extension payloads survive");
    check(sameStoredValue(actual["plugins"],baseline["plugins"]),"opaque plugin data survives");
    for(auto path:{"/patterns","/tracks","/mixer/buses","/performance/bindings","/signalGraph/lanes","/signalGraph/layout"}) {
      Json::json_pointer p(path);if(!baseline["native"].contains(p)) continue;
      auto key=std::string(path)=="/signalGraph/lanes" ? "target" : std::string(path)=="/signalGraph/layout" ? "node" : "id";
      for(const auto &oldEntry:baseline["native"].at(p)) {
        const auto &entity=oldEntry.is_array() ? oldEntry[1] : oldEntry;
        for(const auto &newEntry:actual["native"].at(p)) {
          const auto &other=newEntry.is_array() ? newEntry[1] : newEntry;
          if(other[key]==entity[key]) check(sameStoredValue(other["future-extension"],entity["future-extension"]),"opaque data follows stable key");
        }
      }
    }
    auto once=actual;saveNativeProject(*loaded.document,loaded.state,output,true);
    check(sameStoredValue(decodePlist(readProjectBytes(output)),once),"second save typed stability");
    std::cout<<"PASS generated historical v"<<version<<" -> v14\n";
  }
}
void validation(const std::filesystem::path &dir) {
  auto doc=richDocument();auto tree=fixtureTree(*doc,14);auto input=dir/"validation-input.screamseq",output=dir/"validation-destination.screamseq";
  writeTree(input,tree);writeTree(output,tree);const auto bytes=readProjectBytes(output);
  for(unsigned defect=0;defect<4;++defect) {
    auto loaded=openNativeProject(input);
    // Valid but different intended model, missing required wire field, and
    // snapshot-relative bounds respectively. The unchanged-branch shortcut
    // used to bypass all three before successful atomic publication.
    if(defect==0) loaded.state.preserved["native"]["patterns"][0][1]["name"]="Wrong intended model";
    if(defect==1) loaded.state.preserved["native"]["mixer"]["buses"][0].erase("prePan");
    if(defect==2) loaded.state.preserved["native"]["preciseNotes"][0]["position"]=UINT32_MAX;
    if(defect==3) {
      auto shorter=Document::demo();shorter->transaction([](auto &song){song.Patterns[0].Resize(1);});
      auto bytes=shorter->snapshotData();std::vector<uint8_t> data(bytes.size());std::memcpy(data.data(),bytes.data(),bytes.size());
      loaded.state.preserved["module"]=Json::binary(std::move(data));
    }
    auto before=loaded.state;
    bool rejected=false;try {saveNativeProject(*loaded.document,loaded.state,output,true);}catch(const std::exception &){rejected=true;}
    check(rejected,"invalid final merged tree must reject before publication, defect "+std::to_string(defect));
    check(readProjectBytes(output)==bytes,"rejected final tree retains exact destination bytes");
    check(sameStoredValue(loaded.state.preserved,before.preserved) && loaded.state.path==before.path && loaded.state.savedRevision==before.savedRevision,"rejected final tree retains state");
  }
  auto loaded=openNativeProject(input);auto before=loaded.state;
  loaded.state.preserved["native"]["preciseNotes"][0]["future-ambiguous"]=Json::binary({8,0,9});
  loaded.document->annotate([](auto &n){n.preciseNotes[0].position+=1;});
  bool rejected=false;try {saveNativeProject(*loaded.document,loaded.state,output,true);}catch(const std::exception &){rejected=true;}
  check(rejected && readProjectBytes(output)==bytes,"ambiguous unknown-bearing edited array rejects before loss");
}
