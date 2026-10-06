#include "editor/NativePatternCommands.hpp"
#include "editor/PatternCommands.hpp"
#include "editor/TrackerDocument.hpp"
#include "editor/PatternTools.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
using namespace Tracker;
using namespace OpenMPT;
static void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
template<class F>static void rejects(F &&f){try{f();}catch(const std::invalid_argument &){return;}throw std::runtime_error("Invalid native command was accepted");}
int main(){try{
  std::set<std::string_view> names,codes;
  for(const auto &entry:nativePatternCommands()){
    check(names.insert(entry.identifier).second&&codes.insert(entry.code).second,"Native operation IDs and display codes must be unique");
    check(entry.code.size()==2&&entry.parameters.size()<=maximumNativePatternParameters,"Native command exceeds bounded representation");
    check(&nativePatternCommand(entry.identifier)==&entry&&&nativePatternCommand(entry.operation)==&entry,"Native operation lookup identity");
    std::set<std::string_view> fields;PatternCommand c;c.kind=PatternCommandKind::Native;c.native=entry.operation;c.arguments=nativePatternDefaults(c.native);
    if(entry.duration==NativePatternDuration::Required)c.duration=65536;
    validateNativePatternCommand(c);
    for(size_t i=0;i<entry.parameters.size();++i){const auto &p=entry.parameters[i];check(fields.insert(p.key).second&&!p.unit.empty()&&!p.name.empty(),"Native fields need distinct keys and explicit units");
      auto bad=c;bad.arguments[i]=std::numeric_limits<double>::quiet_NaN();rejects([&]{validateNativePatternCommand(bad);});
      bad=c;bad.arguments[i]=p.maximum+1;rejects([&]{validateNativePatternCommand(bad);});
      if(p.type!=NativePatternFieldType::Number){bad=c;bad.arguments[i]=p.minimum+.5;rejects([&]{validateNativePatternCommand(bad);});}
    }
    auto bad=c;bad.value=1;rejects([&]{validateNativePatternCommand(bad);});
    bad=c;bad.arguments.back()=1;rejects([&]{validateNativePatternCommand(bad);});
  }
  for(auto format:{MOD_TYPE_MOD,MOD_TYPE_XM,MOD_TYPE_S3M,MOD_TYPE_IT,MOD_TYPE_MPT})
    for(const auto &tracker:patternCommands(format,false)){if(!tracker.command)continue;const auto code=tracker.mask?tracker.label.substr(0,2):"0"+tracker.label.substr(0,1);check(!codes.contains(code),"Native display code collides with a tracker command");}
  Document document;const auto before=document.native();
  document.annotate([&](NativeSong &n){n.scratchGestures[1]=scratchPresets().front().gesture;uint32_t row=0;for(const auto &entry:nativePatternCommands()){
    PatternCommand c{n.patterns.at(0).id,n.tracks.at(0).id,row++*performanceUnitsPerRow,entry.duration==NativePatternDuration::Required?65536u:0u,0,PatternCommandKind::Native};
    c.native=entry.operation;c.arguments=nativePatternDefaults(c.native);n.performance.commands.push_back(c);
  }});
  const auto accepted=document.native();document.undo();check(document.native()==before,"One Undo must remove the whole native batch");document.redo();check(document.native()==accepted,"Redo must restore every native field exactly");
  const auto revision=document.revision;rejects([&]{document.annotate([](NativeSong &n){n.performance.commands[0].arguments[0]=NAN;});});
  check(document.revision==revision&&document.native()==accepted,"Invalid native mutation must be atomic");
  document.addPattern(64,true,0);check(document.native().performance.commands.size()==2*accepted.performance.commands.size(),"Pattern copy must retain all native operation payloads");
  Document nudges;nudges.transaction([](auto &song){check(song.Patterns[0].SetSignature(7,28),"Nudge signature fixture");});
  for(const auto kind:{PatternCommandKind::NudgeForward,PatternCommandKind::NudgeReverse}){
    PatternCommand nudge;nudge.kind=kind;nudge.value=.75;nudge.durationBeats=.123456789012345;validateNativePatternCommand(nudge);
    for(double invalid:{0.,-1.,1.0/131072,65537.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}){auto bad=nudge;bad.durationBeats=invalid;rejects([&]{validateNativePatternCommand(bad);});}
    auto bad=nudge;bad.duration=65536;rejects([&]{validateNativePatternCommand(bad);});bad=nudge;bad.kind=PatternCommandKind::PitchSet;rejects([&]{validateNativePatternCommand(bad);});
    check(patternCommandDuration(kind)==NativePatternDuration::None&&patternCommandFields(kind).back().key=="durationBeats"&&patternCommandFields(kind).back().initial==1,"NF/NR own their beat field, with no generated row duration");
  }
  const auto noNudges=nudges.native();nudges.annotate([](NativeSong &n){PatternCommand c{n.patterns.at(0).id,n.tracks.at(0).id,2*performanceUnitsPerRow+8192,0,0,PatternCommandKind::NudgeForward,0,.75};c.durationBeats=.5;n.performance.commands.push_back(c);});
  const auto originalNudge=nudges.native();nudges.undo();check(nudges.native()==noNudges,"Nudge Undo removes its beat duration");nudges.redo();check(nudges.native()==originalNudge,"Nudge Redo retains exact beat duration");
  const auto nudgeRevision=nudges.revision;rejects([&]{nudges.annotate([](NativeSong &n){n.performance.commands.front().durationBeats=64./7;});});check(nudges.revision==nudgeRevision&&nudges.native()==originalNudge,"Nudge duration bounds use remaining pattern beats and reject atomically");
  PatternTransform transform;transform.fields=PatternEffect;transform.operation="expand";transform.amount=2;
  auto moved=prepareEffectTransform(nudges,{{0,0,64,0,1}},transform);check(moved.performance.commands.front().durationBeats==1&&moved.performance.commands.front().duration==0,"Expansion scales beat duration without row storage");
  transform.operation="shrink";moved=prepareEffectTransform(nudges,{{0,0,64,0,1}},transform);check(moved.performance.commands.front().durationBeats==.25,"Shrink scales beat duration continuously");
  transform.operation="reverse";rejects([&]{prepareEffectTransform(nudges,{{0,0,64,0,1}},transform);});transform.allowDataLoss=true;
  moved=prepareEffectTransform(nudges,{{0,0,64,0,1}},transform);check(moved.performance.commands.front().durationBeats==2.875/7,"Explicit clipping uses remaining beats at the pattern signature");
  auto tail=originalNudge;tail.performance.commands.front().position=64*performanceUnitsPerRow-1;tail.reconcile(nudges.song());check(tail.performance.commands.empty(),"Structural reconciliation drops nudges too short to retain at the final fractional row");
  nudges.annotate([](NativeSong &n){n.performance.commands.front().position=performanceUnitsPerRow-1;n.performance.commands.front().durationBeats=1.0/performanceUnitsPerRow;});
  moved=prepareEffectTransform(nudges,{{0,0,64,0,1}},transform);check(moved.performance.commands.empty(),"Explicit destructive row transform drops an unrepresentably short end nudge");
  std::cout<<"PASS native catalog uniqueness, typed bounds, normalized defaults, atomic history and pattern copying\n";return 0;
}catch(const std::exception &e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
