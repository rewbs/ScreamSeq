void earlyNestedDraftOwner(HWND main) {
  using Json=ScreamSeq::Api::Json;
  DraftRegistryHost host(main);
  ScreamSeq::SampleLibraryWindow::Context context{"old-song","r1",false};
  const Json family={{"name","Parity"},{"suggestedOctaveShift",0},{"explanation","Generated fixture"},
    {"samples",Json::array({{{"path","C:/fixture/Parity_C3.wav"},{"filename","Parity_C3.wav"},{"sourceNote","C3"},{"semitone",36}}})}};
  ScreamSeq::SampleLibraryWindow browser(main,
    [](const std::string &method,const Json &)->Json {if(method=="sample.library.preview.stop")return Json::object();throw std::runtime_error("Unexpected nested-owner request");},
    []{return Json{{"roots",Json::array()},{"indexing",false}};},
    []{return Json{{"playing",false},{"path",""},{"frames",0},{"renderedFrames",0}};},
    [](double){},[&]{return context;},[](const Json &,const std::string &){},[](unsigned,const std::string &){},
    [](const auto &,const auto &)->ScreamSeq::Api::CompletedCall {throw std::runtime_error("Nested-owner test must not import");});
  ScreamSeq::Tests::ownGuiWindow(browser.window());
  struct FindChild {HWND owner,child=nullptr;};
  auto importWindow=[&] {
    FindChild found{browser.window()};
    EnumThreadWindows(GetCurrentThreadId(),[](HWND h,LPARAM opaque)->BOOL {
      auto &found=*reinterpret_cast<FindChild *>(opaque);wchar_t name[80]{};GetClassNameW(h,name,80);
      if(GetWindow(h,GW_OWNER)==found.owner&&!wcscmp(name,L"ScreamSeq.MultisampleImport")){found.child=h;return FALSE;}
      return TRUE;
    },reinterpret_cast<LPARAM>(&found));
    require(found.child,"Browser did not retain its early-created import editor");return found.child;
  };
  require(host.registry.capture(context.document,context.revision).drafts.empty(),"Unconfigured child manufactured retained work");
  browser.reviewFamily(family);const auto child=importWindow();ScreamSeq::Tests::ownGuiWindow(child);
  SetWindowTextW(GetDlgItem(child,5602),L"-");SendMessageW(child,WM_CLOSE,0,0);
  SetWindowTextW(GetDlgItem(browser.window(),5401),L"Keep library search");
  const auto captured=host.registry.capture(context.document,context.revision);
  require(captured.drafts.size()==1&&captured.drafts.front().dirty&&captured.drafts.front().document=="old-song",
      "Child constructed before parent finish escaped the draft census");
  require(host.registry.authorize(captured).status==Tracker::DocumentDeparture::Status::NeedsReview,"Configured hidden family did not block departure");
  auto consent=host.registry.authorize(captured,true);
  {
    auto admission=host.registry.admitForReplacement(*consent.token,context.document,context.revision);
    require(bool(admission.replacement),"Nested owner admission unavailable");
  }
  require(IsWindow(child)&&text(GetDlgItem(child,5602))==L"-"&&host.registry.capture(context.document,context.revision).sameWork(captured),
      "Canceled adoption discarded the nested raw draft");
  consent=host.registry.authorize(captured,true);
  auto admission=host.registry.admitForReplacement(*consent.token,context.document,context.revision);
  context={"new-song","r2",true};
  {
    const std::array roots{main,browser.window(),child};ScreamSeq::NativeInputGate gate(roots);
    require(admission.replacement&&admission.replacement->retireOwners(),"Nested owner retirement failed");
    require(!IsWindow(child)&&IsWindow(browser.window())&&!browser.retired(),"Document child retirement destroyed its global browser");
    require(text(GetDlgItem(browser.window(),5401))==L"Keep library search"&&!browser.snapshot().at("multisample").at("visible").get<bool>(),
        "Retirement lost global browser preferences or read a retired child's controls");
  }
  admission.replacement.reset();
  require(host.registry.capture(context.document,context.revision).drafts.empty(),"Retired nested owner remained in new-song census");
  browser.reviewFamily(family);const auto fresh=importWindow();ScreamSeq::Tests::ownGuiWindow(fresh);
  const auto next=host.registry.capture(context.document,context.revision);
  require(next.drafts.size()==1&&next.drafts.front().owner!=captured.drafts.front().owner&&next.drafts.front().document=="new-song",
      "Browser reused retired child identity or old captured song");
  require(text(GetDlgItem(fresh,5602))==L"0","New import editor reused invalid text from the discarded song");
  std::cout<<"Nested owner: early registration, hidden raw family, rollback, global-browser survival and fresh child identity passed\n";
}
