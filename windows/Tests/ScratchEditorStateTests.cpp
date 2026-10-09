#include "windows/App/ScratchEditorState.hpp"
#include "windows/App/FormulaApplyState.hpp"
#include "editor/CurveFormulaReference.hpp"
#include <iostream>
#include <stdexcept>
using namespace ScreamSeq;
using Json=nlohmann::json;
static void check(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
template<typename F> static void reject(F f,const char *message){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}check(rejected,message);}
static void formulaApplyChecks(){
  FormulaApplyState state;state.baseline=L"initial";std::wstring text=L"submitted";uint64_t generation=1;unsigned applications=0;
  auto currentGeneration=[&]{return generation;};auto currentText=[&]{return text;};
  const auto result=state.apply(generation,text,[&]{
    ++applications;check(state.pending()&&state.retained(text),"An in-flight callback retains its window even before newer typing");
    text=L"newer formula";++generation;state.changed();
    const auto nested=state.apply(generation,text,[&]{++applications;return true;},currentGeneration,currentText);
    check(nested==FormulaApplyState::Result::Busy,"A message-pumping callback cannot reenter Use");return true;
  },currentGeneration,currentText);
  check(result==FormulaApplyState::Result::NewerDraft&&applications==1&&!state.pending(),"Only the submitted formula is applied during callback reentrancy");
  check(state.baseline==L"submitted"&&!state.accepted&&text==L"newer formula"&&state.retained(text),"Successful old completion must retain newer visible text rather than mark it accepted");
  const auto stale=state.apply(generation,text,[]{return false;},currentGeneration,currentText);
  check(stale==FormulaApplyState::Result::Rejected&&state.retained(text)&&state.baseline==L"submitted","A parent changed by the earlier write rejects reapply without rebasing or dropping the new draft");
  const auto saved=state.apply(generation,text,[&]{++applications;return true;},currentGeneration,currentText);
  check(saved==FormulaApplyState::Result::Accepted&&state.accepted&&!state.retained(text)&&state.baseline==text,"An unchanged successful application still accepts the exact text");
  text=L"retained after failure";++generation;state.changed();const auto baseline=state.baseline;
  reject([&]{state.apply(generation,text,[]()->bool{throw std::runtime_error("controlled save failure");},currentGeneration,currentText);},"Apply propagates write failures");
  check(!state.pending()&&state.retained(text)&&state.baseline==baseline,"A thrown callback releases its in-flight fence and preserves the draft/baseline");
}
int main(){try{
  formulaApplyChecks();
  Json reference{{"notes",std::string(Tracker::curveFormulaNotes)},{"symbols",Json::array()}};
  for(const auto &v:Tracker::curveFormulaSymbols)reference["symbols"].push_back({{"name",std::string(v.name)},{"description",std::string(v.description)}});
  const auto scratch=scratchFormulaReference(reference);const auto notes=scratch["notes"].get<std::string>();
  check(notes.find("per sample")!=std::string::npos&&notes.find("32-sample")==std::string::npos&&notes.find("SK beats / repeats")!=std::string::npos&&notes.find("segmentBeat")==std::string::npos,"Scratch reference must explain its actual per-sample repeat clock");
  for(const auto &v:scratch["symbols"])if(v["name"]=="row"||v["name"]=="beat"||v["name"]=="startBeat"||v["name"]=="endBeat")check(v["description"].get<std::string>().find("pattern")==std::string::npos||v["name"]=="row","Scratch symbols must not advertise pattern beat positions");
  auto phrase=ScratchJSON::gesture(Tracker::scratchPresets().front().gesture);phrase["id"]=7;phrase["uses"]=3;
  ScratchEditorState state;state.load(phrase);check(!state.empty()&&!state.dirty()&&state.id()==7&&!state.draft.contains("uses"),"Load keeps only editable phrase content");
  const auto original=state.draft;state.selected[0]=0;
  reject([&]{state.replace({{"position",2},{"value",.4},{"curve","smooth"}});},"The start endpoint must not move");check(state.draft==original,"Rejected endpoint move changes no draft");
  reject([&]{state.remove();},"Endpoints cannot be deleted");
  state.selected[0]=1;
  reject([&]{state.replace({{"position",65536},{"value",.4},{"curve","linear"}});},"Point collision must reject atomically");check(state.draft==original,"Collision must retain both lanes");
  state.replace({{"position",16384},{"value",.375},{"curve","scripted"},{"formula","mix(start,end,t*t)"}});
  check(state.dirty()&&state.points(0)[1]["position"]==16384&&state.points(1)==original["fader"],"One lane edit preserves its paired curve");
  const auto script=state.draft;reject([&]{state.replace({{"position",16384},{"value",.5},{"curve","scripted"},{"formula","bad("}});},"Invalid script must reject");check(state.draft==script,"Invalid script leaves the complete draft intact");
  state.lane=1;state.selected[1]=-1;state.replace({{"position",32768},{"value",.125},{"curve","step"}});
  check(state.points(1).size()==3&&state.selected[1]==1&&state.points(0)==script["motion"],"Keyboard insertion sorts and selects its new point without replacing Motion");
  state.remove();check(state.points(1)==original["fader"],"Delete removes only the selected interior point");
  const auto submitted=state.generation;const auto saved=state.draft;state.draft["name"]="A later draft";++state.generation;state.accept(saved,submitted);
  check(state.dirty()&&state.draft["name"]=="A later draft"&&state.baseline==saved,"A delayed completion cannot erase newer text");
  state.accept(state.draft,state.generation);check(!state.dirty(),"Exact accepted generation becomes clean");
  auto dense=original;dense["motion"]=Json::array();for(unsigned i=0;i<255;++i)dense["motion"].push_back({{"position",i*256},{"value",.5},{"curve","linear"}});dense["motion"].push_back({{"position",65536},{"value",.5},{"curve","linear"}});
  state.load(dense);state.lane=0;state.selected[0]=-1;reject([&]{state.replace({{"position",1},{"value",.5},{"curve","linear"}});},"Keyboard edits must enforce the shared 256-point bound");check(state.draft==dense,"Capacity rejection keeps the phrase");
  ScratchPatternTarget target;target.document="document:original:3";target.revision="12";target.patternID=9;target.trackID=15;target.pattern=2;target.row=3;target.channel=4;target.column=1;target.rows=16;target.rowsPerBeat=7;
  auto initial=target.use(11);check(initial["pattern"]==2&&initial["channel"]==4&&initial["column"]==1&&initial["command"]["parameters"]["beats"]==1,"New SK uses exactly the captured cell and a one-beat default");
  target.row=15;initial=target.use(11);check(initial["command"]["parameters"]["beats"].get<double>()==1./7,"Only a new default clamps to the remaining pattern beats");
  target.command={{"kind","native"},{"native","scratch"},{"offset",12345},{"parameters",{{"gesture",7},{"beats",.03123456789},{"travelMs",732.12345},{"repeats",17},{"reverse",true}}}};
  auto expected=target.command;expected["parameters"]["gesture"]=11;check(target.use(11).at("command")==expected,"Using an existing SK changes only its gesture slot, preserving offset and every precise parameter");
  auto moved=target;moved.pattern=8;moved.channel=1;check(target.sameCell(moved),"Stable identities survive index movement");moved.document="document:original:4";check(!target.sameCell(moved),"Reopening the same path is a different captured document generation");moved=target;++moved.column;check(!target.sameCell(moved),"Captured FX columns cannot silently retarget");
  target.command=nullptr;target.row=target.rows;reject([&]{target.use(1);},"Deleted pattern row must reject");target.row=0;reject([&]{target.use(0);},"Gesture zero is not a song phrase");reject([&]{target.use(256);},"Slot 256 is outside the bank");
  target.row=15;target.rowsPerBeat=7;target.command={{"kind","native"},{"native","scratch"},{"offset",65535},{"parameters",{{"gesture",7},{"beats",1.0/65536}}}};reject([&]{target.use(1);},"An impossible captured tail must not silently extend the pattern");
  std::cout<<"PASS Windows scratch editor state: paired atomic curves, endpoints/capacity, script validation, generation fencing, reentrant formula application and stable captured cells (no native UI execution)\n";
  return 0;
}catch(const std::exception &e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
