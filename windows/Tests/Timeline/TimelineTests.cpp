#include "windows/Session/TimelineOperations.hpp"
#include "editor/TrackerDocument.hpp"
#include <iostream>
#include <stdexcept>
#include <cmath>
#include "windows/Api/SessionAdapter.hpp"
using Json=nlohmann::json;
static void check(bool ok,const char *why){if(!ok)throw std::runtime_error(why);}
void runPreciseNoteRegressions();
int main(){try{
  runPreciseNoteRegressions();
  auto doc=Tracker::Document::demo();unsigned stops=0;
  ScreamSeq::TimelineOperations api(*doc,[&]{++stops;});
  auto before=doc->cell(0,4,0);auto revision=doc->revision;
  Json params={{"pattern",0},{"events",Json::array({{{"channel",0},{"row",4},{"offsetBeats",0.125},{"note",61},{"instrument",1},{"velocity",93}}})},
    {"clearRows",Json::array({{{"row",4},{"channel",0}}})},{"dryRun",true}};
  auto preview=api.invoke("pattern.notes.set",params);
  check(preview["wouldChange"]==true && doc->revision==revision && stops==0,"precise preview does not mutate or stop");
  params["dryRun"]=false;api.invoke("pattern.notes.set",params);
  auto result=api.invoke("pattern.notes.get",{{"pattern",0}});
  check(result["events"].size()==1 && result["events"][0]["position"]==4*65536+32768,"beat offset uses current pattern signature");
  check(doc->cell(0,4,0).note==0 && stops==1,"ordinary onset cleared atomically with native event");
  check(doc->cell(0,0,1).note!=0,"unrelated channel preserved");
  auto native=doc->native();auto afterRevision=doc->revision;
  api.invoke("pattern.notes.set",params);check(doc->revision==afterRevision && stops==1,"no-op retains history and playback");
  doc->undo();check(doc->native().preciseNotes.empty() && doc->cell(0,4,0)==before,"one shared Undo restores native event and legacy cell");
  doc->redo();check(doc->native()==native,"Redo restores precise event");
  std::cout<<"PASS precise notes: actual shared edit, beat timing, atomic clear, no-op, preview, Undo/Redo\n";
  const auto timing=api.invoke("document.timing.get",Json::object());
  auto beforeTimingRevision=doc->revision;auto beforeStops=stops;
  Json timingEdit={{"mode","modern"},{"tempo",123.4567},{"rowsPerBeat",4},{"rowsPerMeasure",16},{"groove",Json::array({1.2,0.8,1.2,0.8})},{"dryRun",true}};
  auto timingPreview=api.invoke("document.timing.set",timingEdit);
  check(timingPreview["wouldChange"]==true&&doc->revision==beforeTimingRevision&&stops==beforeStops,"timing preview uses shared validation without a mutation");
  timingEdit["dryRun"]=false;api.invoke("document.timing.set",timingEdit);
  auto actualTiming=api.invoke("document.timing.get",Json::object());
  check(actualTiming["mode"]=="modern"&&actualTiming["grooveActive"]==true,"modern groove applied");
  check(std::abs(actualTiming["tempo"].get<double>()-123.4567)<0.0001,"fractional tempo retained");
  auto currentRevision=doc->revision;api.invoke("document.timing.set",timingEdit);
  check(doc->revision==currentRevision&&stops==beforeStops+1,"timing no-op retains transport and history");
  doc->undo();check(api.invoke("document.timing.get",Json::object())==timing,"shared timing Undo exact");
  doc->redo();check(api.invoke("document.timing.get",Json::object())==actualTiming,"shared timing Redo exact");
  std::cout<<"PASS timing: fractional BPM, normalized groove, preview/no-op and exact history\n";
  auto expectInvalid=[&](const std::string &method,const Json &request){
    const auto state=doc->native();auto rev=doc->revision;auto count=stops;bool rejected=false;
    try{api.invoke(method,request);}catch(const ScreamSeq::Api::ApiError&e){rejected=e.code==-32602;}
    check(rejected&&doc->native()==state&&doc->revision==rev&&stops==count,"invalid request rejects atomically before stop");
  };
  for(const auto &event:Json::array({
    {{"channel",true},{"position",0},{"note",61}},
    {{"channel",0},{"position",0},{"row",1},{"note",61}},
    {{"channel",0},{"row",1},{"offsetRows",1.0},{"note",61}},
    {{"channel",0},{"row",1},{"offsetRows",0.0},{"offsetBeats",0.0},{"note",61}},
    {{"channel",0},{"position",0},{"note",255},{"instrument",1}},
    {{"channel",0},{"position",0},{"note",61},{"velocity",0}},
    {{"channel",0},{"position",0},{"note",61},{"unexpected",0}}}))
      expectInvalid("pattern.notes.set",{{"pattern",0},{"events",Json::array({params["events"][0],event})}});
  expectInvalid("pattern.notes.set",{{"pattern",0},{"events",Json::array({params["events"][0],params["events"][0]})}});
  expectInvalid("pattern.notes.set",{{"pattern",0},{"events",Json::array()},{"clearRows",Json::array({{{"row",64},{"channel",0}}})}});
  expectInvalid("document.timing.set",{{"mode","classic"}});
  expectInvalid("document.timing.set",{{"groove",Json::array({1.0,1.0,1.0})}});
  expectInvalid("document.timing.set",{{"rowsPerBeat",true}});
  check(doc->song().Patterns[0].SetSignature(8,32),"pattern beat override fixture");
  auto sig=api.invoke("pattern.notes.get",{{"pattern",0}});check(sig["rowsPerBeat"]==8,"pattern signature used instead of global signature");
  Json occurrence={{"channel",0},{"row",5},{"offsetBeats",1.0/16.0},{"note",61},{"instrument",1}};
  api.invoke("pattern.notes.set",{{"pattern",0},{"events",Json::array({occurrence})}});
  check(api.invoke("pattern.notes.get",{{"pattern",0}})["events"][0]["position"]==5*65536+32768,"beat conversion uses override");
  occurrence["offsetBeats"]=1.0/8.0;expectInvalid("pattern.notes.set",{{"pattern",0},{"events",Json::array({occurrence})}});
  std::cout<<"PASS atomic invalid batches, release semantics, timing gates and beat overrides\n";
  auto reference=api.invoke("automation.formula.reference",Json::object());
  check(reference["symbols"].size()>10 && reference["notes"].is_string(),"shared formula reference available");
  auto formulaRevision=doc->revision;auto formulaStops=stops;
  Json formula={{"rows",2},{"end",256},{"samples",5},{"points",Json::array({
    {{"position",0},{"value",0.0},{"curve","scripted"},{"formula","mix(start,end,t^2)"}},
    {{"position",256},{"value",1.0},{"curve","linear"}}})}};
  auto previewCurve=api.invoke("automation.formula.preview",formula);
  check(previewCurve["values"].size()==5&&std::abs(previewCurve["values"][2][1].get<double>()-0.25)<1e-12,"formula preview uses shared evaluator");
  check(doc->revision==formulaRevision&&stops==formulaStops,"formula reads never mutate or stop");
  formula["points"][0]["formula"]="mi";expectInvalid("automation.formula.preview",formula);
  std::cout<<"PASS bounded formula preview/reference through shared evaluator\n";
  return 0;
}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
