// Real HWND notifications with bounded request callbacks. These cover native
// completion ownership, not worker/audio rendering or document replacement.
struct DraftRegistryHost {
  HWND window;
  ScreamSeq::DocumentDraftRegistry registry;
  explicit DraftRegistryHost(HWND owner):window(owner){require(SetPropW(window,ScreamSeq::documentDraftRegistryProperty,reinterpret_cast<HANDLE>(&registry)),"Install test draft registry");}
  ~DraftRegistryHost(){RemovePropW(window,ScreamSeq::documentDraftRegistryProperty);}
};
void clickDraftButton(HWND window,int id){SendMessageW(window,WM_COMMAND,MAKEWPARAM(id,BN_CLICKED),reinterpret_cast<LPARAM>(GetDlgItem(window,id)));}
void chooseDraftField(HWND window,int id,int value){SendMessageW(GetDlgItem(window,id),CB_SETCURSEL,value,0);SendMessageW(window,WM_COMMAND,MAKEWPARAM(id,CBN_SELCHANGE),reinterpret_cast<LPARAM>(GetDlgItem(window,id)));}
Tracker::DocumentDraft draftOf(const ScreamSeq::NativeToolWindow &owner){auto draft=owner.documentDraft();require(draft.has_value(),"Expected a captured native draft summary");return *draft;}

void trimRequestRetention(HWND main){
  using Json=ScreamSeq::Api::Json;
  DraftRegistryHost host(main);
  ScreamSeq::GraphTrimsWindow::Context context{"trim-document","r1"};
  ScreamSeq::GraphTrimsWindow *owner=nullptr;
  enum class Inject{None,Text,Link,Read,Reject};Inject inject=Inject::None;
  unsigned writes=0;bool pendingObserved=false,discardRefused=false,targetRetained=false;Json submitted;
  const auto request=[&](const std::string &method,const Json &params)->Json {
    if(method=="graph.get")return {{"mixer",{{"buses",Json::array({{{"id","n1"},{"name","Track"}}})}}},{"assignments",Json::array()},{"commands",Json::array()},{"library",Json::array()}};
    if(method=="graph.trim.get"){
      if(inject==Inject::Read){inject=Inject::None;SetWindowTextW(GetDlgItem(owner->window(),4203),L"-");}
      return {{"ports",Json::array({
        {{"key","i:0"},{"name","Input"},{"output",false},{"gainDB",0},{"linkTo",nullptr},{"modulation",Json::array()}},
        {{"key","o:0"},{"name","Output"},{"output",true},{"gainDB",0},{"linkTo",nullptr},{"modulation",Json::array()}}
      })},{"sources",Json::array()}};
    }
    if(method!="graph.trim.set")throw std::runtime_error("Unexpected trim method");
    ++writes;submitted=params;const auto snapshot=host.registry.capture(context.document,context.revision);
    pendingObserved=snapshot.drafts.size()==1&&snapshot.drafts.front().pending;
    discardRefused=host.registry.authorize(snapshot,true).status==Tracker::DocumentDeparture::Status::Pending;
    if(inject==Inject::Reject)throw std::runtime_error("Fixture admission refusal");
    if(inject==Inject::Text)SetWindowTextW(GetDlgItem(owner->window(),4203),L"-.");
    if(inject==Inject::Link){
      const auto captured=draftOf(*owner).target;chooseDraftField(owner->window(),4202,1);
      targetRetained=SendMessageW(GetDlgItem(owner->window(),4202),CB_GETCURSEL,0,0)==0&&draftOf(*owner).target==captured;
      chooseDraftField(owner->window(),4204,1);
    }
    context.revision="r"+std::to_string(writes+1);return Json::object();
  };
  {
    ScreamSeq::GraphTrimsWindow trim(main,request,[&]{return context;});owner=&trim;ScreamSeq::Tests::ownGuiWindow(trim.window());trim.open();
    require(host.registry.capture(context.document,context.revision).drafts.empty(),"Clean trim owner must not require review");
    SetWindowTextW(GetDlgItem(trim.window(),4203),L"-6");const auto before=draftOf(trim);
    inject=Inject::Text;clickDraftButton(trim.window(),4208);const auto after=draftOf(trim);
    require(writes==1&&submitted.at("gainDB")==-6&&submitted.at("expectedRevision")=="r1","Trim request lost its submitted value or revision");
    require(pendingObserved&&discardRefused,"Pending trim request was discardable or absent from census");
    require(text(GetDlgItem(trim.window(),4203))==L"-."&&after.dirty&&!after.pending&&after.generation>before.generation&&after.revision=="r1"&&after.target==before.target,"Trim completion erased or rebased newer raw text");
    clickDraftButton(trim.window(),4208);require(writes==1,"Stale trim draft retried a committed request");
    inject=Inject::None;clickDraftButton(trim.window(),4209);
    require(!draftOf(trim).dirty&&draftOf(trim).revision==context.revision,"Explicit trim reload did not capture current state");
    SetWindowTextW(GetDlgItem(trim.window(),4203),L"-3");const auto linkBefore=draftOf(trim);
    inject=Inject::Link;clickDraftButton(trim.window(),4208);const auto linkAfter=draftOf(trim);
    require(writes==2&&submitted.at("linkTo").is_null(),"Late link changed the already submitted request");
    require(SendMessageW(GetDlgItem(trim.window(),4204),CB_GETCURSEL,0,0)==1&&linkAfter.dirty&&linkAfter.generation>linkBefore.generation&&linkAfter.revision==linkBefore.revision,"Trim completion erased or rebased a newer selector choice");
    require(targetRetained,"Pending trim request allowed its displayed port to diverge from the captured target");
    inject=Inject::Read;clickDraftButton(trim.window(),4209);
    require(text(GetDlgItem(trim.window(),4203))==L"-"&&draftOf(trim).dirty&&!draftOf(trim).pending,"Trim read erased newer invalid raw text");
    SetWindowTextW(GetDlgItem(trim.window(),4203),L"-2");const auto rejected=draftOf(trim);inject=Inject::Reject;clickDraftButton(trim.window(),4208);
    require(writes==3&&draftOf(trim).sameWork(rejected)&&text(GetDlgItem(trim.window(),4203))==L"-2","Rejected trim apply changed captured work");
    trim.hide();require(host.registry.capture(context.document,context.revision).drafts.size()==1,"Hidden trim draft lost registration");
  }
  require(host.registry.capture(context.document,context.revision).drafts.empty(),"Trim destruction left a registered owner");
  std::cout<<"Trim requests retain newer text/selector fields, reject stale retries and protect pending work\n";
}

void renderRequestRetention(HWND main){
  using Json=ScreamSeq::Api::Json;
  DraftRegistryHost host(main);
  ScreamSeq::PatternSampleRenderWindow::Target target{"render-document","r1",2,4,15,1,3};
  ScreamSeq::PatternSampleRenderWindow *owner=nullptr;
  bool reject=false,newer=false,pendingObserved=false,discardRefused=false;unsigned requests=0,commits=0;Json submitted;
  const auto request=[&](const std::string &method,const Json &params)->Json {
    ++requests;submitted=params;
    const auto state=host.registry.capture(target.document,target.revision);
    pendingObserved=method=="sample.renderSelection"&&state.drafts.size()==1&&state.drafts.front().pending;
    discardRefused=host.registry.authorize(state,true).status==Tracker::DocumentDeparture::Status::Pending;
    if(reject)throw ScreamSeq::Api::ApiError(-32003,"Fixture render refusal",Tracker::WriteOutcome{Tracker::CommitOutcome::NotCommitted});
    if(!params.value("dryRun",false))target.revision="r"+std::to_string(commits+2);
    return {{"sample",7},{"instrument",0}};
  };
  const auto committed=[&](const std::string &document,const Json &){
    if(document==target.document)++commits;
    if(newer)SetWindowTextW(GetDlgItem(owner->window(),6602),L"Newer name");
  };
  {
    ScreamSeq::PatternSampleRenderWindow render(main,request,[&]{return target;},committed,[&](const auto &method,const auto &params){auto result=request(method,params);return ScreamSeq::Api::CompletedCall{method,target.document,target.revision,std::move(result)};});owner=&render;ScreamSeq::Tests::ownGuiWindow(render.window());render.openAt(false,target);
    require(host.registry.capture(target.document,target.revision).drafts.empty(),"Clean render form must not require review");
    SetWindowTextW(GetDlgItem(render.window(),6602),L"Submitted name");SetWindowTextW(GetDlgItem(render.window(),6603),L"2.5");
    const auto draft=draftOf(render);
    clickDraftButton(render.window(),6605);
    require(requests==1&&commits==0&&pendingObserved&&discardRefused&&draftOf(render).sameWork(draft),"Render Check changed draft or allowed pending discard");
    reject=true;clickDraftButton(render.window(),6606);
    require(requests==2&&commits==0&&draftOf(render).sameWork(draft),"Rejected render cleared submitted raw fields");
    reject=false;newer=true;clickDraftButton(render.window(),6606);const auto after=draftOf(render);
    require(requests==3&&commits==1&&submitted.at("name")=="Submitted name"&&submitted.at("tailSeconds")==2.5&&submitted.at("expectedRevision")=="r1","Render did not submit exact captured parameters once");
    require(after.dirty&&!after.pending&&after.generation>draft.generation&&after.target==draft.target&&after.revision=="r1"&&render.snapshot().at("name")=="Newer name"&&render.snapshot().at("stale")==true,"Render completion cleared or rebased a newer draft");
    clickDraftButton(render.window(),6606);require(requests==3,"Stale render was resubmitted");
    newer=false;clickDraftButton(render.window(),6601);clickDraftButton(render.window(),6606);
    require(requests==4&&commits==2&&!draftOf(render).dirty&&!draftOf(render).pending,"Successful unchanged render did not clear its submitted draft");
    SetWindowTextW(GetDlgItem(render.window(),6603),L"-");render.hide();
    require(host.registry.capture(target.document,target.revision).drafts.size()==1,"Hidden invalid render draft lost registration");
  }
  require(host.registry.capture(target.document,target.revision).drafts.empty(),"Render destruction left a registered owner");
  std::cout<<"Render Check, refusal and completion preserve captured work and newer raw fields\n";
}
