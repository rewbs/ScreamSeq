// The real worker prepares/opens/recoveries while a native-owner registry stays
// on the test's UI thread. This is controller admission evidence, not HWND or
// foreground coverage; the application owner registrations are separate.
void documentDepartureControllerTests(const std::filesystem::path &directory) {
  struct Admission final:DocumentReplacementAdmission {
    ScreamSeq::DocumentDraftRegistry registry;
    Tracker::DocumentDraft fields;
    ScreamSeq::DocumentDraftRegistry::Registration owner;
    std::optional<Tracker::DocumentDeparture::Token> consent;
    std::optional<ScreamSeq::DocumentDraftRegistry::AdmittedReplacement> replacement;
    DocumentController *controller=nullptr;
    std::string departingDocument;
    unsigned calls=0,accepted=0,rolledBack=0,retired=0;
    bool completionIdentity=true;
    Admission() {
      fields.dirty=true;fields.target="precise-row/n12";fields.generation=1;
      owner=registry.add("Precise notes",[this]()->std::optional<Tracker::DocumentDraft>{return fields;},[]{},[this]()noexcept{
        completionIdentity&=registry.departing()&&controller->view()->session.documentId!=departingDocument;
        fields.dirty=false;++retired;return true;
      });
    }
    void review(bool discard) {
      const auto current=controller->view();departingDocument=current->session.documentId;
      fields.document=departingDocument;fields.revision=current->session.revision;
      auto result=registry.authorize(registry.capture(departingDocument,fields.revision),discard);
      consent=std::move(result.token);
    }
    void admit(const std::string &document,const std::string &revision)override {
      ++calls;
      if(!consent)throw Api::ApiError(-32002,"Native editor drafts need review");
      auto result=registry.admitForReplacement(*consent,document,revision);consent.reset();
      if(!result.replacement)throw Api::ApiError(-32001,"Native editor drafts changed during preparation");
      replacement=std::move(result.replacement);
    }
    void finish(bool adopted)noexcept override {
      // The lease is held through publication; a failed Stop still sees the
      // exact departing view. No raw text is cleared on the failure path.
      completionIdentity&=registry.departing()&&
        ((controller->view()->session.documentId!=departingDocument)==adopted);
      if(adopted){++accepted;completionIdentity&=replacement&&replacement->retireOwners()&&registry.departing();}
      else ++rolledBack;
      replacement.reset();
    }
  } admission;
  bool failStop=false;std::atomic<bool> failView=false;unsigned stops=0;
  std::atomic<bool> candidateReached=false,continueCandidate=true;
  DocumentController c({},"departure",[&]{
    need(admission.registry.departing(),"Stop must run inside the native departure lease");
    if(failStop)throw std::runtime_error("controlled Stop refusal");++stops;
  },[](const auto &){},[&]{
    if(failView.load())throw std::runtime_error("controlled candidate preparation failure");
    if(!continueCandidate.load()) {
      candidateReached=true;
      while(!continueCandidate.load())std::this_thread::yield();
    }
  },64u*1024u*1024u,{}, {},{}, {},&admission);
  admission.controller=&c;
  need(admission.calls==0,"initial document construction must not call native departure");
  const auto path=directory/"departure.screamseq";
  invoke(c,"document.save",{{"path",path.generic_string()}});
  const auto original=c.view();
  auto open=[&]{invoke(c,"document.open",{{"path",path.generic_string()},{"discard",true}});};
  bool refused=false;
  try{open();}catch(const Api::ApiError &e){refused=e.code==-32002;}
  need(refused&&c.view()==original&&stops==0&&admission.accepted==0,"API discard must not bypass native draft admission");
  admission.review(true);failView=true;const auto before=admission.calls;refused=false;
  try{open();}catch(const std::runtime_error &){refused=true;}failView=false;
  need(refused&&admission.calls==before&&!admission.registry.departing()&&c.view()==original,
    "fallible candidate preparation must precede native admission");
  admission.review(true);failStop=true;refused=false;
  try{open();}catch(const std::runtime_error &){refused=true;}failStop=false;
  need(refused&&admission.rolledBack==1&&admission.retired==0&&!admission.registry.departing()&&admission.fields.dirty&&c.view()==original,
    "Stop failure must release lease and retain original view/draft");
  admission.review(true);continueCandidate=false;
  auto pending=c.invoke("document.open",{{"path",path.generic_string()},{"discard",true},{"expectedRevision",c.view()->session.revision}});
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
  while(!candidateReached.load()&&std::chrono::steady_clock::now()<deadline)std::this_thread::yield();
  const bool reached=candidateReached.load();
  // Equivalent to raw native input while the UI services a worker wait.
  ++admission.fields.generation;continueCandidate=true;
  refused=false;try{recoveryWait(c,std::move(pending));}catch(const Api::ApiError &e){refused=e.code==-32001;}
  need(reached&&refused&&c.view()==original&&stops==0,"newer raw input during staging must refuse before Stop");
  admission.review(true);open();
  need(admission.accepted==1&&admission.retired==1&&!admission.fields.dirty&&admission.completionIdentity&&stops==1&&!admission.registry.departing(),
    "successful open must publish before finishing its native lease");
  const auto opened=c.view();auto capture=recoveryCapture(c);
  admission.consent.reset();refused=false;
  try{recoveryWait(c,c.recover(capture->bytes,opened->session.revision,true));}catch(const Api::ApiError &e){refused=e.code==-32002;}
  need(refused&&c.view()==opened&&stops==1,"recovery discard must use the same native admission boundary");
  admission.review(true);recoveryWait(c,c.recover(capture->bytes,opened->session.revision,true));
  need(admission.accepted==2&&admission.retired==2&&admission.completionIdentity&&stops==2&&!admission.registry.departing(),
    "successful recovery must publish then release its lease exactly once");
  for(bool demo:{false,true}) {
    admission.fields.dirty=true;++admission.fields.generation;admission.consent.reset();
    const auto previous=c.view();const auto previousStops=stops,previousRetired=admission.retired;
    refused=false;try{invoke(c,"document.new",{{"demo",demo},{"discard",true}});}catch(const Api::ApiError &e){refused=e.code==-32002;}
    need(refused&&c.view()==previous&&stops==previousStops&&admission.retired==previousRetired,
      "New/Demo bypassed retained native work or stopped before admission");
    admission.review(true);failStop=true;refused=false;
    try{invoke(c,"document.new",{{"demo",demo},{"discard",true}});}catch(const std::runtime_error &){refused=true;}failStop=false;
    need(refused&&c.view()==previous&&admission.fields.dirty&&!admission.registry.departing(),"Failed New/Demo Stop retired native work");
    admission.review(true);invoke(c,"document.new",{{"demo",demo},{"discard",true}});
    need(c.view()->session.documentId!=previous->session.documentId&&admission.retired==previousRetired+1&&
      stops==previousStops+1&&admission.completionIdentity&&!admission.registry.departing(),"New/Demo did not adopt once through the shared lease");
  }
  std::cout<<"Native departure admission, candidate failure, pumped raw edit, Stop rollback, open, recovery, New and Demo passed\n";
}

void documentCreationControllerTests(const std::filesystem::path &directory) {
  bool failView=false;unsigned stops=0;
  DocumentController c({},"creation",[&]{++stops;},[](const auto &){},[&]{if(failView)throw std::runtime_error("creation staging failure");});
  const auto original=c.view();
  const auto samePatterns=[](const DocumentView &a,const DocumentView &b) {
    if(a.patterns.size()!=b.patterns.size())return false;
    for(const auto &[index,pattern]:a.patterns) {
      const auto other=b.patterns.find(index);if(other==b.patterns.end())return false;
      if(pattern->rows!=other->second->rows||pattern->channels!=other->second->channels||pattern->cells!=other->second->cells)return false;
    }
    return true;
  };
  const auto reject=[&](const Json &params,int code) {
    const auto before=c.view();const auto stopped=stops;bool refused=false;
    try{call(c,"document.new",params);}catch(const Api::ApiError &e){refused=e.code==code;}
    need(refused&&c.view()==before&&stops==stopped,"Invalid New partially changed song or playback");
  };
  reject(Json::object(),-32602);reject({{"expectedRevision","stale"}},-32001);
  for(const auto &extra:std::vector<Json>{{{"demo",1}},{{"demo","false"}},{{"discard",1}},{{"unknown",true}},{{"dryRun",true}},{{"path",""}}}) {
    auto params=extra;params["expectedRevision"]=c.view()->session.revision;reject(params,-32602);
  }
  const auto library=call(c,"plugin.discover",{{"format","Built-in"}});invoke(c,"plugin.add",{{"descriptor",library.at(0)}});
  need(c.view()->dirty&&!c.view()->session.document.at("nativePlugins").empty(),"Creation preservation fixture lacks dirty plugin state");
  reject({{"expectedRevision",c.view()->session.revision}},-32602);
  const auto dirty=c.view();const auto beforeStops=stops;failView=true;bool refused=false;
  try{invoke(c,"document.new",{{"discard",true}});}catch(const std::runtime_error &){refused=true;}failView=false;
  need(refused&&c.view()==dirty&&stops==beforeStops,"Failed New preparation lost dirty plugin state");
  invoke(c,"document.new",{{"discard",true}});
  const auto blank=c.view();Tracker::Document sharedBlank;
  need(blank->session.documentId!=original->session.documentId&&blank->session.document.at("title")=="Untitled"&&
    blank->channels==sharedBlank.song().GetNumChannels()&&blank->path.empty()&&!blank->dirty&&blank->patterns.size()==1&&
    blank->patterns.at(0)->rows==64&&blank->session.document.at("nativePlugins").empty()&&
    blank->session.document.at("canUndo")==false&&blank->session.document.at("canRedo")==false,
    "New did not use shared blank defaults or reset its independent identity/history/path/plugins");
  for(const auto &cell:blank->patterns.at(0)->cells)for(const auto &field:cell)need(field==0,"Blank song contains a musical cell");
  const auto path=directory/"created-blank.screamseq";
  invoke(c,"document.save",{{"path",path.generic_string()}});invoke(c,"document.open",{{"path",path.generic_string()}});
  need(samePatterns(*c.view(),*blank)&&c.view()->session.document.at("tracks")==blank->session.document.at("tracks"),
    "Blank save/reopen changed cells or stable track identities");
  invoke(c,"document.new",{{"demo",true}});const auto demo=c.view();
  need(samePatterns(*demo,*original)&&demo->session.document.at("samples")==original->session.document.at("samples")&&
    demo->session.document.at("canUndo")==false&&demo->session.document.at("canRedo")==false&&demo->path.empty(),
    "Open Demo diverged from the shared demo or retained another song's history/path");
  const auto take=invoke(c,"recording.start",{{"channels",{0}},{"instrument",1}}).at("take");
  reject({{"expectedRevision",c.view()->session.revision},{"demo",false},{"discard",true}},-32602);
  invoke(c,"recording.discard",{{"take",take}});
  std::cout<<"New/Demo validation, shared defaults, plugin preservation, staging failure, history, save/reopen and retained take passed\n";
}
