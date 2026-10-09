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
  std::cout<<"Native departure admission, candidate failure, pumped raw edit, Stop rollback, open and recovery passed\n";
}
