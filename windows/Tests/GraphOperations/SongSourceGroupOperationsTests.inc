void songSourceGroupOperations() {
  Fixture f;ScreamSeq::GraphHostHooks hooks;unsigned publications=0;bool rejectPublication=false;
  hooks.cachedRack={{{{"name","Gain"},{"isInstrument",false}},"rack-a",0}};
  hooks.cachedParameters["rack-a"]={{7,"Gain",0,1,.5,0,"",{}}};
  hooks.preparePublication=[&](const NativeSong &){return [&]{if(rejectPublication)throw ScreamSeq::Api::ApiError(-32002,"Controlled publication rejection");++publications;};};
  GraphOperations api(*f.doc,[&]{++f.stops;},hooks);
  const auto first=api.invoke("graph.song.source.add",{{"source",{{"kind","lfo"}}}}).at("node");
  const auto group=api.invoke("graph.song.group.create",{{"nodes",{"source:"+first.get<std::string>()}},{"name","Inside"}}).at("group");
  const Json request={{"source",{{"kind","lfo"},{"name","Grouped motion"},{"rate",.75},{"x",120},{"y",80}}},{"parent",group},{"connect",{{"plugin","rack-a"},{"parameter",7}}}};
  const auto before=f.doc->native();const auto revision=f.doc->revision;const auto history=f.doc->historyBytes();const auto published=publications;
  auto dry=request;dry["dryRun"]=true;const auto preview=api.invoke("graph.song.source.add",dry);
  CHECK(preview.at("wouldChange")==true&&f.doc->native()==before&&f.doc->revision==revision&&f.doc->historyBytes()==history&&publications==published);
  auto reject=[&](const Json &input,int code){
    bool failed=false;try{api.invoke("graph.song.source.add",input);}catch(const ScreamSeq::Api::ApiError &e){CHECK(e.code==code);failed=true;}
    CHECK(failed&&f.doc->native()==before&&f.doc->revision==revision&&f.doc->historyBytes()==history&&publications==published&&f.stops==0);
  };
  for(const Json &parent:std::vector<Json>{nullptr,true,1,first,"n999999"}){auto invalid=request;invalid["parent"]=parent;reject(invalid,-32602);invalid["dryRun"]=true;reject(invalid,-32602);}
  auto invalidTarget=request;invalidTarget["connect"]["plugin"]="missing";reject(invalidTarget,-32602);
  rejectPublication=true;reject(request,-32002);rejectPublication=false;

  struct GuardedHost final:ScreamSeq::Api::SessionHost {
    Document &document;GraphOperations &operations;unsigned calls=0;
    GuardedHost(Document &d,GraphOperations &a):document(d),operations(a){}
    ScreamSeq::Api::SessionSnapshot snapshot()override{return {std::to_string(document.revision),"source-parent-test",Json::object(),Json::object(),{{"playing",false}}};}
    bool supportsDocumentOperations()const override{return true;}
    std::vector<std::string> additionalDocumentWrites()const override{return {"graph.song.source.add"};}
    Json documentOperation(const std::string &method,const Json &input)override{++calls;auto p=input;p.erase("expectedRevision");return operations.invoke(method,p);}
    ScreamSeq::Api::PatternSnapshot pattern(unsigned)override{return {};}
    void play(const Json &)override{}
    void stop()override{}
  } host(*f.doc,api);
  ScreamSeq::Api::SessionAdapter adapter(host);auto stale=request;stale["expectedRevision"]=std::to_string(revision-1);
  const auto refused=adapter.handle({{"jsonrpc","2.0"},{"id","stale-parent"},{"method","graph.song.source.add"},{"params",stale}});
  CHECK(refused.at("error").at("code")==-32001&&host.calls==0&&f.doc->native()==before&&f.doc->revision==revision&&f.doc->historyBytes()==history);
  auto guarded=request;guarded["expectedRevision"]=std::to_string(revision);
  const auto added=adapter.handle({{"jsonrpc","2.0"},{"id","add-parent"},{"method","graph.song.source.add"},{"params",guarded}});
  CHECK(!added.contains("error"));const auto source=added.at("result").at("data").at("node");
  CHECK(source==preview.at("node")&&host.calls==1&&f.doc->revision==revision+1&&publications==published+1&&f.stops==0);
  const auto after=f.doc->native();CHECK(after.signal.songSources.size()==before.signal.songSources.size()+1);
  CHECK(after.signal.groups[0].nodes==std::vector<std::string>({"source:"+first.get<std::string>(),"source:"+source.get<std::string>()}));
  CHECK(after.signal.songModulation.size()==1&&after.signal.songModulation[0].source==after.signal.songSources.back().node.id);
  f.doc->undo();auto restored=before;restored.nextID=after.nextID;CHECK(f.doc->native()==restored);
  f.doc->redo();CHECK(f.doc->native()==after);
  Document reopened(f.doc->snapshotData());reopened.restoreNative(ScreamSeq::Project::decodeNativeMetadata(ScreamSeq::Project::encodeNativeMetadata(after)));CHECK(reopened.native()==after);
  const auto root=api.invoke("graph.song.source.add",{{"source",{{"kind","lfo"}}}}).at("node");
  CHECK(f.doc->native().signal.groups==after.signal.groups&&root!=source);
}
