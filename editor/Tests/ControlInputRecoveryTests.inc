static void controlInputRecoveryTests() {
  using Json=nlohmann::json;
  const Json event={{"plugin","stable-plugin"},{"parameter",17},{"value",1.00000000000001},
    {"hostTime","9007199254740993"},{"state","unknown"},{"error","Completion unavailable"}};
  const Json archive={{"version",1},{"reviewOnly",true},{"document","original-document"},{"revision","original-revision"},
    {"take","original-take"},{"accepted","18446744073709551615"},{"lost","2"},{"previewError",""},
    {"events",Json::array({event,event})}};
  auto decoded=readControlInputRecovery(archive);
  check(decoded==archive,"Control recovery narrowed original doubles/times or coalesced repeated input");
  decoded["events"][0]["value"]=0;check(archive.at("events")[0].at("value")==1.00000000000001,"Control recovery aliased mutable input");
  const auto refuses=[&](auto change){auto invalid=archive;change(invalid);const auto before=invalid;bool rejected=false;
    try{(void)readControlInputRecovery(invalid);}catch(const std::exception &){rejected=true;}
    check(rejected&&invalid==before,"Unsupported control recovery was accepted or changed while refusing it");};
  for(const Json version:{Json(true),Json(1.0),Json(0),Json(2)})refuses([&](auto &v){v["version"]=version;});
  refuses([](auto &v){v["reviewOnly"]=false;});refuses([](auto &v){v["unexpected"]=0;});
  refuses([](auto &v){v["take"]=std::string(81,'x');});refuses([](auto &v){v["document"]=std::string("a\0b",3);});
  for(const char *time:{"0","01","-1","1.5","18446744073709551616"})refuses([&](auto &v){v["events"][0]["hostTime"]=time;});
  refuses([](auto &v){v["accepted"]=1;});refuses([](auto &v){v["lost"]="-1";});
  refuses([](auto &v){v["events"][0]["parameter"]=true;});refuses([](auto &v){v["events"][0]["parameter"]=-1;});
  refuses([](auto &v){v["events"][0]["parameter"]=uint64_t(UINT32_MAX)+1;});
  refuses([](auto &v){v["events"][0]["value"]=false;});refuses([](auto &v){v["events"][0]["value"]=std::numeric_limits<double>::infinity();});
  refuses([](auto &v){v["events"][0]["state"]="accepted";});refuses([](auto &v){v["events"][0]["error"]=std::string(2049,'e');});
  refuses([&](auto &v){v["events"]=Json::array();for(size_t i=0;i<=maximumControlRecoveryEvents;++i)v["events"].push_back(event);});
  Json preserved={{"unrelated",Json::binary({1,0,255})}};
  overlayControlInputRecovery(preserved,archive);const auto first=preserved;
  overlayControlInputRecovery(preserved,archive);check(preserved==first,"Repeated input recovery duplicated the same captured take");
  auto later=archive;later["take"]="second";overlayControlInputRecovery(preserved,later);
  check(preserved.at(controlInputRecoveryKey).size()==2&&preserved.at(controlInputRecoveryKey)[0]==archive&&
    preserved.at("unrelated")==first.at("unrelated"),"Recovery replaced earlier/unrelated evidence");
  auto unsupported=preserved;unsupported[controlInputRecoveryKey][0]["version"]=2;const auto exact=unsupported;
  bool refused=false;try{overlayControlInputRecovery(unsupported,archive);}catch(const std::exception &){refused=true;}
  check(refused&&unsupported==exact,"A future matching control archive was silently replaced");
}
