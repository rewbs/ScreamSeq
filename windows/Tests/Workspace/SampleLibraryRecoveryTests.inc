void sampleLibraryRecovery(HWND main){
  using namespace ScreamSeq;using Json=Api::Json;
  for(bool rootsChange:{false,true})for(unsigned mode=0;mode<3;++mode){
    DraftRegistryHost host(main);SampleLibraryWindow::Context context{"original-song","r1",false};
    Json state={{"roots",Json::array({"C:/pack"})},{"count",4},{"indexing",false},{"ready",true},{"libraryRevision","library:1"},{"error",nullptr},{"indexedAt",0},{"warnings",Json::array()},{"extensions",Json::array()}};
    unsigned writes=0;bool failRead=mode==0,malformed=false;SampleLibraryWindow *owner=nullptr;
    const auto read=[&](const std::string &method,const Json &)->Json{
      if(method=="sample.library.preview.stop")return Json::object();
      if(method=="sample.library.search")return {{"items",Json::array()},{"tags",Json::array()},{"total",0},{"libraryRevision",state.at("libraryRevision")}};
      require(method=="sample.library.get","Library Review repeated a write or read the song");
      if(failRead)throw std::runtime_error("Owned library read failure");auto result=state;if(malformed)result["roots"]=false;return result;
    };
    SampleLibraryWindow tool(main,read,[&]{return state;},[]{return Json{{"playing",false},{"path",""},{"frames",0},{"renderedFrames",0}};},[](double){},[&]{return context;},
      [](const Json &,const std::string &){throw std::runtime_error("Library work revealed a musical import");},[](unsigned,const std::string &){},
      [&](const auto &method,const auto &params,const auto &ticket)->Api::CompletedCall{
        ++writes;require(owner&&owner->protectsClose(),"Pending library write did not protect Close");
        require(host.registry.capture(context.document,context.revision).drafts.empty(),"App-wide library operation acquired a song draft");
        require(method==(rootsChange?"sample.library.roots.set":"sample.library.rescan")&&params.at("expectedLibraryRevision")=="library:1","Changed library submission");
        if(rootsChange){require(params.at("roots").empty(),"Remove folder did not capture the exact retained roots");state["roots"]=Json::array();}
        state["libraryRevision"]="library:2";state["indexing"]=true;
        Api::CompletedCall call{method,context.document,"library:2",state};if(mode==0)return call;
        if(mode==1)ticket->publish(std::make_shared<const Api::CompletedCall>(call));throw std::runtime_error("Dropped library return");
      });owner=&tool;Tests::ownGuiWindow(tool.window());tool.show();SendMessageW(tool.window(),WM_TIMER,1,0);
    if(rootsChange){const auto combo=GetDlgItem(tool.window(),5402);SendMessageW(combo,CB_SETCURSEL,1,0);SendMessageW(tool.window(),WM_COMMAND,MAKEWPARAM(5402,CBN_SELCHANGE),LPARAM(combo));}
    clickDraftButton(tool.window(),rootsChange?5406:5407);const auto retained=tool.snapshot().at("libraryCompletion");
    require(writes==1&&!retained.is_null()&&tool.protectsClose(),"Library failure lost its exact intent or Close protection");
    for(int id:{5405,5406,5408,5409,5414})clickDraftButton(tool.window(),id);
    tool.hide();tool.show();SendMessageW(tool.window(),WM_TIMER,1,0);
    require(writes==1&&tool.snapshot().at("libraryCompletion")==retained,"Hiding/refresh discarded or replayed library work");
    context={"replacement-song","r9",false};failRead=true;clickDraftButton(tool.window(),5407);
    require(tool.protectsClose(),"Failed Review released library uncertainty");failRead=false;malformed=true;clickDraftButton(tool.window(),5407);
    require(tool.protectsClose(),"Malformed Review released library uncertainty");malformed=false;state["indexing"]=false;clickDraftButton(tool.window(),5407);
    require(writes==1&&!tool.protectsClose()&&tool.snapshot().at("libraryReport").at("outcome")== (mode==2?"unverified":"returned"),"Library Review replayed or falsely attributed work");
    if(mode==2){require(tool.snapshot().at("libraryNeedsReload")==true,"Unknown library result silently rebased");clickDraftButton(tool.window(),5407);require(writes==1&&!tool.snapshot().at("libraryNeedsReload").get<bool>(),"Explicit library Reload rescanned");}
    require(host.registry.capture(context.document,context.revision).drafts.empty(),"Reviewed library retained a song draft");
  }
}
