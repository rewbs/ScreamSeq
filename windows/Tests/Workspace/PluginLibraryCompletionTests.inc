void pluginLibraryCompletionReview(HWND main){
  using namespace ScreamSeq;using Json=Api::Json;
  for(unsigned action=0;action<3;++action)for(bool exact:{false,true}){
    DraftRegistryHost host(main);std::pair<std::string,std::string> context{"library-document","r1"};
    Json entry={{"catalogID","catalog-original"},{"name","Fixture"},{"format","Built-in"},{"category","Effects"},{"customCategory",""},
      {"favorite",false},{"hidden",false},{"isInstrument",false},{"descriptor",Json::object()}};
    auto library=[&]{return Json{{"plugins",Json::array({entry})},{"categories",Json::array({"Effects"})},{"libraryRevision","library:2"},{"warning",""}};};
    PluginLibraryWindow *owner=nullptr;unsigned writes=0,reads=0;bool failReview=false,malformed=false;
    const auto read=[&](const std::string &method,const Json &p)->Json{
      ++reads;if(failReview)throw std::runtime_error("Review read failed");
      if(method=="synchronizeView")return Json::object();
      if(method=="mixer.get")return {{"buses",Json::array({{{"id","n42"},{"inserts",Json::array({"unrelated-or-observed"})}}})}};
      if(method=="document.get")return {{"nativePlugins",malformed?Json():Json::array({{{"instanceID","unrelated-or-observed"}}})}};
      require(method=="plugin.library.get"&&!p.at("rescan").get<bool>(),"Review repeated a scan or another write");
      auto data=library();if(malformed)data["plugins"]=nullptr;return data;
    };
    PluginLibraryWindow tool(main,read,[&]{return context;},[&](const auto &method,const auto &p,const auto &ticket)->Api::CompletedCall{
      ++writes;require(owner->snapshot().at("pending")==true,"Library submission lost pending ownership");
      if(action==0)require(method=="plugin.library.get"&&p.at("rescan")==true,"Wrong retained scan");
      if(action==1)require(method=="plugin.library.set"&&p.at("catalogID")=="catalog-original"&&p.at("category")=="Original category","Preference request changed target or fields");
      Json result=action==0?library():action==1?Json{{"catalogID","catalog-original"},{"libraryRevision","library:2"}}:Json{{"slot",0},{"dryRun",false}};
      if(action==2){require(method=="plugin.add"&&p.at("target")=="n42","Wrong retained bus insert");context.second="r2";}
      if(exact)ticket->publish(std::make_shared<const Api::CompletedCall>(Api::CompletedCall{method,context.first,context.second,result}));
      SetWindowTextW(GetDlgItem(owner->window(),3210),L"Newer raw category");
      throw std::runtime_error("Lost library completion");
    });owner=&tool;Tests::ownGuiWindow(tool.window());clickDraftButton(tool.window(),3213);
    if(action==2){tool.setDestination(context.first,"n42","Texture return");clickDraftButton(tool.window(),3213);}
    SendMessageW(GetDlgItem(tool.window(),3207),LB_SETCURSEL,0,0);SendMessageW(tool.window(),WM_COMMAND,MAKEWPARAM(3207,LBN_SELCHANGE),LPARAM(GetDlgItem(tool.window(),3207)));
    if(action==1)SetWindowTextW(GetDlgItem(tool.window(),3210),L"Original category");
    clickDraftButton(tool.window(),action==0?3214:action==1?3211:3212);
    const auto retained=tool.snapshot();require(writes==1&&!retained.at("completion").is_null()&&tool.protectsClose(),"Library failure lost its submission or Close protection");
    if(action==2){bool refused=false;try{tool.setDestination(context.first,"n77","Other bus");}catch(const std::exception &){refused=true;}require(refused&&tool.snapshot().at("destination").at("bus")=="n42","Unresolved insertion was retargeted");}
    const auto census=host.registry.capture(context.first,context.second);
    require((host.registry.authorize(census,true).status==Tracker::DocumentDeparture::Status::Uncertain)==(action==2),"App library work and captured rack work share the wrong departure scope");
    for(int id:{3208,3209,3211,3212,3214})clickDraftButton(tool.window(),id);
    SendMessageW(GetDlgItem(tool.window(),3210),WM_KEYDOWN,VK_ESCAPE,0);tool.show();
    require(writes==1&&tool.snapshot().at("categoryDraft")==true&&text(GetDlgItem(tool.window(),3210))==L"Newer raw category","Unresolved operation repeated or Escape/reopen discarded raw input");
    failReview=true;clickDraftButton(tool.window(),3213);require(tool.snapshot().at("completion")==retained.at("completion"),"Failed Review lost the captured result");failReview=false;
    if(!exact){malformed=true;clickDraftButton(tool.window(),3213);require(!tool.snapshot().at("completion").is_null(),"Malformed observation resolved an unknown write");malformed=false;}
    if(action!=2)context={"another-document","another-revision"}; // App preferences survive song replacement.
    clickDraftButton(tool.window(),3213);
    const auto reviewed=tool.snapshot();require(writes==1&&reviewed.at("completion").is_null()&&reviewed.at("report").at("outcome")== (exact?"returned":"unverified"),"Library Review repeated or falsely attributed its operation");
    if(action==2&&!exact)require(reviewed.at("report").at("observed").at("mixer").at("buses")[0].at("id")=="n42","Unknown insertion review omitted destination routing");
    require(text(GetDlgItem(tool.window(),3210))==L"Newer raw category"&&reviewed.at("categoryDraft")==true,"Library Review erased newer input");
    if(!exact){clickDraftButton(tool.window(),3212);require(writes==1&&tool.snapshot().at("readbackNeedsReload")==true,"Observation silently enabled a second write");}
    clickDraftButton(tool.window(),3213);require(!tool.protectsClose()&&tool.snapshot().at("readbackNeedsReload")==false,"Explicit Reload did not release reviewed category work");
  }
  // Exercise native message-pump changes during destination admission, before
  // any plugin write. These are known non-submissions, not uncertain outcomes.
  for(unsigned scenario=0;scenario<4;++scenario){
    std::pair<std::string,std::string> context{"destination-document","r1"};unsigned writes=0;
    PluginLibraryWindow *owner=nullptr;
    Json effect={{"catalogID","effect"},{"name","Effect"},{"format","Built-in"},{"category","Effects"},{"customCategory",""},
      {"favorite",false},{"hidden",false},{"isInstrument",false},{"descriptor",Json::object()}};
    auto instrument=effect;instrument["catalogID"]="instrument";instrument["isInstrument"]=true;
    PluginLibraryWindow tool(main,[&](const std::string &method,const Json &)->Json{
      if(method=="plugin.library.get")return {{"plugins",Json::array({effect,instrument})},{"categories",Json::array({"Effects"})},{"libraryRevision","lib1"},{"warning",""}};
      require(method=="mixer.get","Unexpected destination read");
      if(scenario==1)context.second="r2";
      if(scenario==2)SetWindowTextW(GetDlgItem(owner->window(),3210),L"New category during admission");
      if(scenario==3){
        bool refused=false;try{owner->setDestination(context.first,"n88","Other");}catch(const std::exception &){refused=true;}
        require(refused,"Destination changed during a pending read");
        clickDraftButton(owner->window(),3212); // Must not recursively submit.
      }
      return {{"buses",scenario==0?Json::array():Json::array({{{"id","n42"}}})}};
    },[&]{return context;},[&](const auto &method,const auto &params,const auto &)->Api::CompletedCall{
      ++writes;require(method=="plugin.add"&&params.at("target")=="n42"&&params.at("expectedRevision")=="r1","Destination admission lost captured intent");
      context.second="r2";return {method,context.first,context.second,{{"slot",0},{"dryRun",false}}};
    });owner=&tool;Tests::ownGuiWindow(tool.window());tool.setDestination(context.first,"n42","Return");clickDraftButton(tool.window(),3213);
    require(tool.snapshot().at("plugins").size()==1,"Destination filtering exposed an instrument");
    SendMessageW(GetDlgItem(tool.window(),3207),LB_SETCURSEL,0,0);SendMessageW(tool.window(),WM_COMMAND,MAKEWPARAM(3207,LBN_SELCHANGE),LPARAM(GetDlgItem(tool.window(),3207)));
    clickDraftButton(tool.window(),3212);
    require(writes==(scenario==3?1u:0u)&&tool.snapshot().at("completion").is_null()&&!tool.snapshot().at("pending").get<bool>(),"Admission wrote through a stale destination or invented an uncertain result");
    require(tool.snapshot().at("destination").at("bus")=="n42","Admission retargeted the retained browser");
    if(scenario==2)require(text(GetDlgItem(tool.window(),3210))==L"New category during admission"&&tool.snapshot().at("categoryDraft")==true,"Admission lost newer raw category input");
  }
}
