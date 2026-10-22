static void pluginInputRecoveryReviewTests() {
  using Json=nlohmann::json;
  const Json control={{"version",1},{"reviewOnly",true},{"document","old-document"},{"revision","old-revision"},
    {"take","old-take"},{"accepted","9007199254740993"},{"lost","18446744073709551615"},{"previewError","Preview refused"},
    {"events",Json::array({{{"plugin","retired-plugin"},{"parameter",17},{"value",1.00000000000001},
      {"hostTime","9007199254740995"},{"state","unknown"},{"error","Completion unavailable"}}})}};
  const Json first={{"plugin","retired-plugin"},{"parameter",17},{"minimum",0},{"maximum",1},{"sourceLoss","3"},{"overflow","1"},{"unmapped",1},
    {"events",Json::array({{{"hostTime","9007199254740995"},{"value",.25},{"frame","240"}},
      {{"hostTime","9007199254740995"},{"value",.5},{"frame","240"}},{{"hostTime","0"},{"value",.75}}})}};
  auto second=first;second["parameter"]=18;second["unmapped"]=0;second["events"].erase(2);
  const Json gesture={{"version",1},{"reviewOnly",true},{"take","old-take"},{"revision","old-revision"},
    {"playbackEpoch","7"},{"startTime","1000"},{"completed",false},{"failure","provider"},{"rejected","2"},
    {"rack",Json::array({"retired-plugin"})},{"baseline",Json::array()},
    {"providers",Json::array({{{"plugin","retired-plugin"},{"incarnation","7"},{"initialDropped","0"},{"dropped","3"},{"drained",false}}})},
    {"targets",Json::array({first,second})}};
  auto future=control;future["version"]=2;future["futureEvidence"]=Json::binary({0,255});
  Json preserved={{gestureRecoveryKey,Json::array({gesture})},{controlInputRecoveryKey,Json::array({control,future})},
    {"unrelated",Json::binary({0,1,255})}};const auto before=preserved;
  const auto list=pluginInputRecoveryList(preserved,1,1);
  check(list.at("total")==3&&list.at("entries").size()==1&&list.at("entries")[0].at("kind")=="nativeControls"&&
    list.at("entries")[0].at("index")==0&&list.at("entries")[0].at("headerSupported")==true,
    "Recovery catalogue pagination mixed collection and archive indices");
  const auto tail=pluginInputRecoveryList(preserved,2,128);
  check(tail.at("entries").size()==1&&tail.at("entries")[0].at("headerSupported")==false,
    "Future recovery archive was hidden or advertised as a supported header");
  const auto page=pluginInputRecoveryPage(preserved,"gestures",0,2,2);
  check(page.at("total")==5&&page.at("events").size()==2&&page.at("events")[0].at("hostTime")=="0"&&
    page.at("events")[0].at("state")=="unmapped"&&!page.at("events")[0].contains("frame")&&
    page.at("events")[1].at("targetIndex")==1&&page.at("events")[1].at("eventIndex")==0&&
    page.at("events")[1].at("parameter")==18&&page.at("summary").at("providers")[0].at("dropped")=="3",
    "Recovery input pagination lost target identity, unmapped input or original source loss");
  const auto repeated=pluginInputRecoveryPage(preserved,"gestures",0,0,2);
  check(repeated.at("events")[0].at("hostTime")==repeated.at("events")[1].at("hostTime")&&
    repeated.at("events")[0].at("value")==.25&&repeated.at("events")[1].at("value")==.5,
    "Recovery review sorted or coalesced repeated original events");
  auto native=pluginInputRecoveryPage(preserved,"nativeControls",0);
  check(native.at("events")[0].at("value")==1.00000000000001&&native.at("events")[0].at("hostTime")=="9007199254740995"&&
    native.at("summary").at("lost")=="18446744073709551615"&&native.at("summary").at("accepted")=="9007199254740993",
    "Native recovery review narrowed input precision or 64-bit counters");
  native["events"][0]["value"]=0;check(preserved==before,"Detached review changed the original preserved project");
  const auto refuses=[&](auto action){bool rejected=false;try{action();}catch(const std::exception &){rejected=true;}
    check(rejected&&preserved==before,"Unsupported recovery review was accepted or changed project data");};
  refuses([&]{pluginInputRecoveryPage(preserved,"nativeControls",1);});
  refuses([&]{pluginInputRecoveryPage(preserved,"other",0);});
  refuses([&]{pluginInputRecoveryPage(preserved,"gestures",1);});
  refuses([&]{pluginInputRecoveryPage(preserved,"gestures",0,6);});
  refuses([&]{pluginInputRecoveryPage(preserved,"gestures",0,0,513);});
  refuses([&]{pluginInputRecoveryList(preserved,4);});
  refuses([&]{pluginInputRecoveryList(preserved,0,0);});
  check(pluginInputRecoveryPage(preserved,"gestures",0,5).at("events").empty(),"Exact end offset did not return an empty review page");
  auto malformed=preserved;malformed[gestureRecoveryKey]=Json::object();const auto malformedBefore=malformed;
  const auto badList=pluginInputRecoveryList(malformed);
  check(badList.at("entries")[0].at("index").is_null()&&badList.at("entries")[0].contains("error")&&
    pluginInputRecoveryPage(malformed,"nativeControls",0).at("total")==1&&malformed==malformedBefore,
    "An unreadable collection hid independent valid native evidence or was rewritten");
}
