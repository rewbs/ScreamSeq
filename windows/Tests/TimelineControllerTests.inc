// Actual serialized document worker. No devices or UI are needed for a time query.
#include <cmath>
void timelineControllerTests(const std::filesystem::path &directory) {
  auto document=Tracker::Document::demo();
  document->transaction([](auto &song){
    for(auto &pattern:song.Patterns)if(pattern.IsValid())for(auto &cell:pattern)cell.Clear();
    song.Patterns[0].Resize(8);song.Order().assign(2,0);
    song.m_nTempoMode=OpenMPT::TempoMode::Classic;
    song.Order().SetDefaultSpeed(6);song.Order().SetDefaultTempo(OpenMPT::TEMPO(125.0));
    auto &pattern=song.Patterns[0];pattern.GetpModCommand(0,0)->command=OpenMPT::CMD_TEMPO;pattern.GetpModCommand(0,0)->param=125;
    pattern.GetpModCommand(4,0)->command=OpenMPT::CMD_TEMPO;pattern.GetpModCommand(4,0)->param=250;
  });
  auto state=Project::newProjectState(*document);const auto path=directory/"timeline.screamseq";
  Project::saveNativeProject(*document,state,path,true);
  unsigned stops=0;DocumentController controller(path,"timeline-controller",[&]{++stops;},[](const auto &){});
  const auto before=controller.view();
  const auto first=call(controller,"pattern.timeline.get",{{"pattern",0}});
  const auto second=call(controller,"pattern.timeline.get",{{"pattern",0},{"order",1}});
  need(first.at("order")==0&&second.at("order")==1,"Timeline adapter lost the repeated order occurrence");
  need(std::abs(second.at("positions")[0].at("songSeconds").get<double>()-.72)<1e-6&&
    std::abs(second.at("positions")[7].at("patternSeconds").get<double>()-.66)<1e-6,"Worker timeline lost engine timing");
  for(const auto &params:{Json::object(),Json{{"pattern",true}},Json{{"pattern",.5}},Json{{"pattern",-1}},
      Json{{"pattern",65536}},Json{{"pattern",65535}},Json{{"pattern",0},{"order",true}},
      Json{{"pattern",0},{"order",.5}},Json{{"pattern",0},{"order",-1}},
      Json{{"pattern",0},{"order",UINT32_MAX}},Json{{"pattern",0},{"order",nullptr}},
      Json{{"pattern",0},{"extra",true}}}) {
    bool refused=false;try{call(controller,"pattern.timeline.get",params);}catch(const Api::ApiError &error){refused=error.code==-32602;}
    need(refused,"Malformed timeline request did not retain its validation error code");
  }
  need(controller.view()==before&&stops==0,"Read or refusal republished the view, changed history or stopped playback");
  invoke(controller,"document.timing.set",{{"speed",3}});
  const auto changed=call(controller,"pattern.timeline.get",{{"pattern",0},{"order",1}});
  need(std::abs(changed.at("positions")[0].at("songSeconds").get<double>()-.36)<1e-6,"Timeline reused stale timing after edit");
  invoke(controller,"history.undo",{{"domain","document"}});
  need(call(controller,"pattern.timeline.get",{{"pattern",0},{"order",1}})==second,"Undo did not restore timeline");
  invoke(controller,"history.redo",{{"domain","document"}});
  const auto copy=directory/"timeline-return.screamseq";
  invoke(controller,"document.save",{{"path",copy.generic_string()},{"overwrite",true}});
  invoke(controller,"document.open",{{"path",copy.generic_string()},{"discard",true}});
  need(call(controller,"pattern.timeline.get",{{"pattern",0},{"order",1}})==changed,"Save/reopen changed engine-derived positions");
  std::cout<<"PASS worker timeline occurrence, strict validation, purity, Undo/Redo and save/reopen\n";
}
