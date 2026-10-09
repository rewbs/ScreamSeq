void sampleBrowserCompletionReview(HWND main){
  using namespace ScreamSeq;using Json=Api::Json;
  for(unsigned scenario=0;scenario<6;++scenario){
    const auto mode=scenario%3;const bool chosen=scenario>=3;
    DraftRegistryHost host(main);SampleLibraryWindow::Context context{"sample-browser","r1",false};
    const Json state={{"roots",Json::array()},{"count",1},{"indexing",false},{"libraryRevision","library:1"},{"error",nullptr}};
    const Json result={{"count",1},{"dryRun",false},{"samples",Json::array({{{"path","C:/fixture/one.wav"},{"sample",7},{"instrument",0}}})}};
    unsigned writes=0,reveals=0,reads=0;bool failReview=false,malformed=false;
    const auto request=[&](const std::string &method,const Json &)->Json{
      if(method=="sample.library.preview.stop")return Json::object();
      if(method=="sample.library.search")return {{"items",Json::array({{{"path","C:/fixture/one.wav"},{"name","one"}}})},{"tags",Json::array()},{"total",1},{"libraryRevision","library:1"}};
      if(method=="sample.library.inspect")return {{"peaks",Json::array()},{"rate",48000},{"channels",1},{"seconds",1.0}};
      if(method=="sample.library.multisample.get")return {{"group",nullptr}};
      ++reads;if(failReview)throw std::runtime_error("Review unavailable");
      if(method=="synchronizeView")return Json::object();
      require(method=="document.get","Import Review repeated a write or scan");
      return {{"samples",malformed?Json():Json::array({{{"index",7},{"id","n17"}}})},{"instruments",Json::array()}};
    };
    SampleLibraryWindow tool(main,request,[&]{return state;},[]{return Json{{"playing",false},{"path",""},{"frames",0},{"renderedFrames",0}};},[](double){},[&]{return context;},
      [&](const Json &,const std::string &){++reveals;throw std::runtime_error("Reveal failed after partial native completion");},[](unsigned,const std::string &){},
      [&](const auto &method,const auto &params,const auto &ticket)->Api::CompletedCall{
        ++writes;require(method=="sample.importMany"&&params.at("paths")==Json::array({"C:/fixture/one.wav"})&&params.at("expectedRevision")=="r1"&&params.at("createInstruments").template get<bool>()==chosen,"Browser import changed its captured request");
        require(host.registry.authorize(host.registry.capture(context.document,context.revision),true).status==Tracker::DocumentDeparture::Status::Pending,"Pending browser import escaped departure admission");
        context.revision="r2";Api::CompletedCall call{method,context.document,context.revision,result};
        if(mode==0)return call;
        if(mode==1)ticket->publish(std::make_shared<const Api::CompletedCall>(call));
        throw std::runtime_error("Dropped import return");
      });Tests::ownGuiWindow(tool.window());tool.show();SendMessageW(tool.window(),WM_TIMER,1,0);
    const auto list=GetDlgItem(tool.window(),5404);SendMessageW(list,LB_SETSEL,TRUE,0);SendMessageW(list,LB_SETCARETINDEX,0,FALSE);
    SendMessageW(tool.window(),WM_COMMAND,MAKEWPARAM(5404,LBN_SELCHANGE),LPARAM(list));
    unsigned choices=0;
    const auto choose=[&]{++choices;require(host.registry.authorize(host.registry.capture(context.document,context.revision),true).status==Tracker::DocumentDeparture::Status::Pending,"File picker escaped native departure admission");return std::vector{std::filesystem::path(L"C:/fixture/one.wav")};};
    if(chosen){try{tool.chooseAndImport(true,choose);}catch(const std::exception &){}}else clickDraftButton(tool.window(),5414);
    if(chosen){try{tool.chooseAndImport(false,choose);}catch(const std::exception &){}require(choices==1,"Uncertain direct import reopened the chooser");}
    const auto retained=tool.snapshot();require(writes==1&&!retained.at("importCompletion").is_null(),"Failed browser import lost its captured result");
    require(host.registry.authorize(host.registry.capture(context.document,context.revision),true).status==Tracker::DocumentDeparture::Status::Uncertain,"Unresolved browser import allowed departure");
    for(int id:{5405,5406,5407,5408,5409,5415})clickDraftButton(tool.window(),id);
    tool.hide();tool.show();SendMessageW(tool.window(),WM_TIMER,1,0);
    require(tool.snapshot().at("importCompletion")==retained.at("importCompletion")&&writes==1,"Hidden browser refreshed away or replayed its import");
    failReview=true;clickDraftButton(tool.window(),5414);require(!tool.snapshot().at("importCompletion").is_null(),"Failed read released import");failReview=false;
    if(mode==2){malformed=true;clickDraftButton(tool.window(),5414);require(!tool.snapshot().at("importCompletion").is_null(),"Malformed samples resolved unknown import");malformed=false;}
    context.revision="r3";clickDraftButton(tool.window(),5414);
    require(writes==1&&reveals==(mode==0?1u:0u)&&tool.snapshot().at("importCompletion").is_null(),"Review repeated import/reveal or lost the known result");
    require(tool.snapshot().at("importReport").at("outcome")== (mode==2?"unverified":"returned"),"Review falsely attributed observed samples");
    if(mode==2){require(tool.snapshot().at("importNeedsRebase")==true,"Unknown review silently enabled another import");clickDraftButton(tool.window(),5414);require(writes==1&&!tool.snapshot().at("importNeedsRebase").get<bool>(),"Explicit rebase repeated import");}
    require(host.registry.capture(context.document,context.revision).drafts.empty(),"Reviewed browser retained a phantom document draft");
  }
}
