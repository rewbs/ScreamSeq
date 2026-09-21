#include "PatternOperations.hpp"
#include "windows/Api/SessionAdapter.hpp"
#include "editor/TrackerDocument.hpp"
#include "editor/PatternCommands.hpp"
#include "editor/PatternTools.hpp"
#include "soundlib/mod_specifications.h"
#include "soundlib/NativeNoteEffects.h"
#include <cmath>
#include <optional>
#include <set>
#include <tuple>
namespace ScreamSeq {
namespace {
using namespace Tracker;
void need(bool value,const char *why){if(!value)throw Api::ApiError(-32602,why);}
void keys(const Json &p,std::initializer_list<const char *> allowed){need(p.is_object(),"Expected an object");for(auto i=p.begin();i!=p.end();++i)need(std::any_of(allowed.begin(),allowed.end(),[&](auto k){return i.key()==k;}),"Unknown pattern field");}
const Json &field(const Json &p,const char *key){need(p.contains(key),"Missing required pattern field");return p.at(key);}
double number(const Json &v,double lo,double hi){need(v.is_number()&&!v.is_boolean(),"Expected a number");const auto n=v.get<double>();need(std::isfinite(n)&&n>=lo&&n<=hi,"Number outside its allowed range");return n;}
uint64_t integer(const Json &v,uint64_t lo,uint64_t hi){const auto n=number(v,double(lo),double(hi));need(std::floor(n)==n,"Expected an integer");return uint64_t(n);}
bool flag(const Json &p,const char *key){if(!p.contains(key))return false;need(p.at(key).is_boolean(),"Expected a boolean");return p.at(key).get<bool>();}
const Json &array(const Json &v,size_t limit){need(v.is_array()&&v.size()<=limit,"Pattern collection exceeds its limit");return v;}
std::string text(const Json &v,size_t limit){need(v.is_string(),"Expected a string");const auto &s=v.get_ref<const std::string &>();need(s.size()<=limit*4&&s.find('\0')==std::string::npos,"Text exceeds its limit or contains NUL");if(!s.empty()){const auto n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),nullptr,0);need(n>0&&size_t(n)<=limit,"Invalid UTF-8 or oversized text");}return s;}
const std::array<const char *,6> kinds={"parameter-set","parameter-slide","pitch-set","pitch-slide","note-cut","tracker"};
PatternCommandKind kind(const Json &v){const auto name=text(v,32);const auto found=std::find(kinds.begin(),kinds.end(),name);need(found!=kinds.end(),"Unknown pattern effect");return PatternCommandKind(found-kinds.begin());}
Json commandObject(const PatternCommand &c,unsigned channel){return {{"channel",channel},{"position",c.position},{"duration",c.duration},{"column",c.column},{"kind",kinds.at(unsigned(c.kind))},{"binding",c.binding},{"value",c.value},{"pitchRange",c.pitchRange},{"effect",c.effect},{"parameter",c.parameter}};}
Json effectObjects(const Document &document,unsigned pattern){const auto &song=document.song();const auto &native=document.native();Json result=Json::array();
  for(unsigned row=0;row<song.Patterns[pattern].GetNumRows();++row)for(unsigned channel=0;channel<song.GetNumChannels();++channel){const auto &cell=*song.Patterns[pattern].GetpModCommand(row,channel);if(!cell.IsPcNote()&&(cell.command||cell.param)){PatternCommand c{native.patterns.at(pattern).id,native.tracks.at(channel).id,row*performanceUnitsPerRow,0,0,PatternCommandKind::TrackerEffect};c.effect=cell.command;c.parameter=cell.param;result.push_back(commandObject(c,channel));}}
  for(const auto &c:native.performance.commands)if(c.pattern==native.patterns.at(pattern).id){const auto track=std::find_if(native.tracks.begin(),native.tracks.end(),[&](const auto &t){return t.second.id==c.track;});result.push_back(commandObject(c,track->first));}return result;
}
unsigned rowsPerBeat(const Document &d,unsigned pattern){const auto &s=d.song();return std::max(1u,s.Patterns[pattern].GetOverrideSignature()?unsigned(s.Patterns[pattern].GetRowsPerBeat()):s.m_nDefaultRowsPerBeat?unsigned(s.m_nDefaultRowsPerBeat):4u);}
Json noteEffects(Document &d){auto result=Json::array();DocumentOperations operations(d);for(auto entry:operations.invoke("pattern.commands",Json::object()).at("effect")){Json values=Json::array();const auto command=entry.at("command").get<uint8_t>();for(int p=entry.at("minimum");p<=entry.at("maximum").get<int>();++p)if((p&entry.at("parameterMask").get<int>())==entry.at("parameterValue").get<int>()&&OpenMPT::NativeNoteEffectSupported(command,uint8_t(p)))values.push_back(p);if(!values.empty()){entry["allowedParameters"]=values;if(std::find(values.begin(),values.end(),entry.at("suggestedParameter"))==values.end())entry["suggestedParameter"]=values[0];if(command==CMD_NONE)entry["description"]="No continuing effect for this hit. Ends the previous hit's effect; ordinary row effects stay active until a hit overrides them.";if(command==CMD_S3MCMDEX&&entry.at("parameterValue")==0x90)entry["description"]="Local sound control: 90 surround off, 91 surround on, 9E play forward, 9F play backward.";result.push_back(std::move(entry));}}return result;}
}
#include "PatternTransform.inc"
PatternOperations::PatternOperations(Tracker::Document &d,std::function<void()> stop,PatternHostHooks host):document_(d),stop_(std::move(stop)),host_(std::move(host)){}
std::vector<std::string> PatternOperations::reads(){return {"pattern.performance.get","pattern.effects.get","pattern.notes.get"};}
std::vector<std::string> PatternOperations::writes(){return {"pattern.performance.set","pattern.effects.set","pattern.effect.set","pattern.notes.set","pattern.transform"};}
Json PatternOperations::invoke(const std::string &method,const Json &input){
  using namespace Tracker;const auto &song=document_.song();Json p=input;
  if(method=="pattern.transform")return transformPattern(document_,p,stop_,host_);
  const auto index=uint16_t(integer(field(p,"pattern"),0,UINT16_MAX));need(song.Patterns.IsValidPat(index),"Pattern does not exist");
  const auto pattern=document_.native().patterns.at(index).id;const auto rows=song.Patterns[index].GetNumRows();const uint32_t end=uint32_t(rows)*performanceUnitsPerRow;
  std::optional<std::tuple<uint16_t,uint16_t,uint8_t>> single;
  if(method=="pattern.effect.set"){
    keys(p,{"pattern","row","channel","column","command","dryRun"});const auto row=uint16_t(integer(field(p,"row"),0,rows-1)),channel=uint16_t(integer(field(p,"channel"),0,song.GetNumChannels()-1));const auto column=uint8_t(integer(field(p,"column"),0,maximumEffectColumns-1));const auto &replacement=field(p,"command");
    // Keyboard edits validate the same candidate without serializing every FX
    // cell in a potentially large pattern into temporary JSON on each keypress.
    single=std::tuple(row,channel,column);Json commands=Json::array();
    if(!replacement.is_null()){auto c=replacement;keys(c,{"kind","binding","value","pitchRange","effect","parameter","offset","duration"});const auto offset=integer(c.value("offset",Json(0)),0,performanceUnitsPerRow-1);c.erase("offset");c["position"]=uint32_t(row)*performanceUnitsPerRow+offset;c["channel"]=channel;c["column"]=column;commands.push_back(std::move(c));}
    p={{"pattern",index},{"commands",commands},{"dryRun",flag(p,"dryRun")}};
  }
  std::map<size_t,Json> parameterCache;
  const auto parameters=[&](size_t slot)->const Json &{if(!parameterCache.contains(slot))parameterCache[slot]=host_.parameters?host_.parameters(slot):Json::array();return parameterCache.at(slot);};
  const auto slotOf=[&](const std::string &id){return size_t(std::find(host_.plugins.begin(),host_.plugins.end(),id)-host_.plugins.begin());};
  if(method=="pattern.performance.get"||method=="pattern.effects.get"){
    keys(p,{"pattern"});const auto &native=document_.native();const auto &performance=native.performance;Json columns=Json::array(),bindings=Json::array();
    for(const auto &[channel,track]:native.tracks)columns.push_back({{"channel",channel},{"track","n"+std::to_string(track.id)},{"count",performance.columns.contains(track.id)?performance.columns.at(track.id):1}});
    for(const auto &[id,binding]:performance.bindings){Json item={{"id",id},{"plugin",binding.plugin},{"parameter",binding.parameter},{"name",binding.name},{"resolved",false},{"canSlide",false}};auto slot=slotOf(binding.plugin);if(slot<host_.plugins.size()){item["slot"]=slot;for(const auto &v:parameters(slot))if(v.at("id")==binding.parameter){item["resolved"]=v.at("writable").get<bool>()&&v.at("max").get<double>()>v.at("min").get<double>();item["canSlide"]=v.at("canSlide");item["parameterName"]=v.at("name");item["minimum"]=v.at("min");item["maximum"]=v.at("max");break;}}bindings.push_back(std::move(item));}
    return {{"pattern",index},{"patternID","n"+std::to_string(pattern)},{"rows",rows},{"unitsPerRow",performanceUnitsPerRow},{"columns",columns},{"bindings",bindings},{"commands",effectObjects(document_,index)}};
  }
  if(method=="pattern.notes.get"){
    keys(p,{"pattern"});const auto &native=document_.native();Json events=Json::array();for(const auto &note:native.preciseNotes)if(note.pattern==pattern){auto track=std::find_if(native.tracks.begin(),native.tracks.end(),[&](const auto &t){return t.second.id==note.track;});Json e={{"channel",track->first},{"position",note.position},{"instrument",note.instrument},{"note",note.note},{"velocity",note.velocity}};if(note.effect){e["effect"]=note.effect;e["parameter"]=note.parameter;}events.push_back(std::move(e));}
    return {{"pattern",index},{"patternID","n"+std::to_string(pattern)},{"rows",rows},{"unitsPerRow",performanceUnitsPerRow},{"rowsPerBeat",rowsPerBeat(document_,index)},{"effects",noteEffects(document_)},{"events",events}};
  }
  need(document_.editable(),"Document is not editable");const bool dry=flag(p,"dryRun");auto next=document_.native();std::vector<Edit> edits;
  if(method=="pattern.notes.set"){
    keys(p,{"pattern","events","clearLegacy","clearRows","clearRowEffects","dryRun"});const bool clear=flag(p,"clearLegacy"),clearEffects=flag(p,"clearRowEffects");std::vector<PreciseNote> replacement;std::set<std::pair<uint16_t,uint16_t>> cells;
    const auto clearCell=[&](uint16_t row,uint16_t channel){if(cells.emplace(row,channel).second){auto c=document_.cell(index,row,channel);c.note=c.instrument=0;if(c.volumeCommand==VOLCMD_VOLUME)c.volumeCommand=c.volume=0;if(clearEffects)c.effect=c.parameter=0;edits.push_back({index,row,channel,{},c});}};
    for(const auto &e:array(field(p,"events"),maximumPreciseNotes)){keys(e,{"channel","position","row","offsetRows","offsetBeats","note","instrument","velocity","effect","parameter"});const auto channel=uint16_t(integer(field(e,"channel"),0,song.GetNumChannels()-1));const auto note=uint8_t(integer(field(e,"note"),1,255));uint32_t position;
      if(e.contains("position")){need(!e.contains("row")&&!e.contains("offsetRows")&&!e.contains("offsetBeats"),"Use position or row with one offset unit");position=uint32_t(integer(e.at("position"),0,end-1));}
      else{auto row=uint32_t(integer(field(e,"row"),0,rows-1));need(e.contains("offsetRows")!=e.contains("offsetBeats"),"Specify exactly one offset unit");const auto offset=e.contains("offsetRows")?number(e.at("offsetRows"),0,1):number(e.at("offsetBeats"),0,1)*rowsPerBeat(document_,index);need(offset<1,"Precise offset must be inside the row");position=row*performanceUnitsPerRow+std::min(performanceUnitsPerRow-1,uint32_t(std::lround(offset*performanceUnitsPerRow)));}
      replacement.push_back({pattern,next.tracks.at(channel).id,position,uint16_t(integer(e.value("instrument",Json(0)),0,255)),note,uint8_t(integer(e.value("velocity",Json(127)),1,127)),uint8_t(integer(e.value("effect",Json(0)),0,255)),uint8_t(integer(e.value("parameter",Json(0)),0,255))});if(clear)clearCell(uint16_t(position/performanceUnitsPerRow),channel);
    }
    if(p.contains("clearRows"))for(const auto &e:array(p.at("clearRows"),8192)){keys(e,{"row","channel"});clearCell(uint16_t(integer(field(e,"row"),0,rows-1)),uint16_t(integer(field(e,"channel"),0,song.GetNumChannels()-1)));}
    replacePreciseNotesForPattern(next.preciseNotes,pattern,std::move(replacement));try{next.validate(song);document_.validateEdits(edits);}catch(const std::invalid_argument &e){throw Api::ApiError(-32602,e.what());}
    const bool changed=next!=document_.native()||std::any_of(edits.begin(),edits.end(),[&](const auto &e){return document_.cell(e.pattern,e.row,e.channel)!=e.after;});if(host_.validateCandidate)host_.validateCandidate(next);if(changed&&!dry){if(stop_)stop_();document_.editNative(std::move(next),edits);}
    return {{"dryRun",dry},{"wouldChange",changed},{"events",p.at("events").size()},{"clearedRows",cells.size()}};
  }
  need(method=="pattern.performance.set"||method=="pattern.effects.set"||method=="pattern.effect.set","Unknown pattern operation");keys(p,{"pattern","columns","bindings","removeBindings","commands","dryRun"});auto &performance=next.performance;std::set<uint16_t> changedBindings;
  if(p.contains("columns"))for(const auto &item:array(p.at("columns"),127)){keys(item,{"channel","count"});const auto channel=uint16_t(integer(field(item,"channel"),0,song.GetNumChannels()-1));need(changedBindings.insert(channel).second,"Duplicate effect-column configuration");const auto count=uint8_t(integer(field(item,"count"),1,maximumEffectColumns));const auto track=next.tracks.at(channel).id;if(count>1)performance.columns[track]=count;else performance.columns.erase(track);}
  changedBindings.clear();if(p.contains("removeBindings"))for(const auto &raw:array(p.at("removeBindings"),255)){auto id=uint16_t(integer(raw,1,255));need(changedBindings.insert(id).second,"Duplicate binding edit");need(performance.bindings.erase(id)!=0,"Binding does not exist");}
  if(p.contains("bindings"))for(const auto &item:array(p.at("bindings"),255)){keys(item,{"id","plugin","parameter","name"});auto id=uint16_t(integer(field(item,"id"),1,255));need(changedBindings.insert(id).second,"Duplicate binding edit");ParameterBinding binding{text(field(item,"plugin"),128),uint32_t(integer(field(item,"parameter"),0,UINT32_MAX)),text(item.value("name",Json("")),256)};auto slot=slotOf(binding.plugin);need(slot<host_.plugins.size(),"Binding plugin instance does not exist");const auto &catalog=parameters(slot);need(std::any_of(catalog.begin(),catalog.end(),[&](const auto &v){return v.at("id")==binding.parameter&&v.at("writable").template get<bool>()&&v.at("max").template get<double>()>v.at("min").template get<double>();}),"Binding parameter has no writable range");performance.bindings[id]=std::move(binding);}
  if(p.contains("commands")){
    std::map<std::pair<uint16_t,uint16_t>,Cell> primary;
    const auto clearPrimary=[&](uint16_t row,uint16_t channel){auto cell=document_.cell(index,row,channel);if(!ModCommand::IsPcNote(cell.note)&&(cell.effect||cell.parameter)){cell.effect=cell.parameter=0;primary[{row,channel}]=cell;}};
    if(single){const auto [row,channel,column]=*single;if(!column)clearPrimary(row,channel);}
    else for(uint16_t row=0;row<rows;++row)for(uint16_t channel=0;channel<song.GetNumChannels();++channel)clearPrimary(row,channel);
    std::set<std::tuple<uint16_t,uint16_t,uint8_t>> occupied;
    std::erase_if(performance.commands,[&](const auto &c){return c.pattern==pattern&&(!single||(c.position/performanceUnitsPerRow==std::get<0>(*single)&&c.track==next.tracks.at(std::get<1>(*single)).id&&c.column==std::get<2>(*single)));});
    for(const auto &item:array(p.at("commands"),maximumPatternCommands+size_t(rows)*song.GetNumChannels())){
      keys(item,{"channel","position","duration","column","kind","binding","value","pitchRange","effect","parameter"});const auto channel=uint16_t(integer(field(item,"channel"),0,song.GetNumChannels()-1));const auto k=kind(field(item,"kind"));const bool pitch=k==PatternCommandKind::PitchSet||k==PatternCommandKind::PitchSlide,cut=k==PatternCommandKind::NoteCut,tracker=k==PatternCommandKind::TrackerEffect;
      const auto position=uint32_t(integer(field(item,"position"),0,end-1));const auto column=uint8_t(integer(field(item,"column"),0,maximumEffectColumns-1));need(column||!song.Patterns[index].GetpModCommand(position/performanceUnitsPerRow,channel)->IsPcNote(),"Imported parameter-control notes own FX 1");need(occupied.emplace(channel,uint16_t(position/performanceUnitsPerRow),column).second,"Duplicate FX cell");
      PatternCommand c{pattern,next.tracks.at(channel).id,position,uint32_t(integer(item.value("duration",Json(0)),0,end-position)),column,k,uint16_t(integer(item.value("binding",Json(0)),(pitch||cut||tracker)?0:1,(pitch||cut||tracker)?0:255)),number(item.value("value",(cut||tracker)?Json(0):Json()),pitch?-96:0,(cut||tracker)?0:pitch?96:1),uint8_t(integer(item.value("pitchRange",Json(2)),1,96)),uint8_t(integer(item.value("effect",Json(0)),0,MAX_EFFECTS-1)),uint8_t(integer(item.value("parameter",Json(0)),0,255))};
      need(tracker||(!c.effect&&!c.parameter),"Only tracker effects have effect/parameter bytes");if(tracker)need(!c.duration&&c.position%performanceUnitsPerRow==0&&c.pitchRange==2&&song.GetModSpecifications().HasCommand(EffectCommand(c.effect)),"Tracker effects require row boundaries and module command ranges");
      if(tracker&&!column){const auto row=uint16_t(position/performanceUnitsPerRow);auto cell=document_.cell(index,row,channel);cell.effect=c.effect;cell.parameter=c.parameter;primary[{row,channel}]=cell;}else performance.commands.push_back(c);
    }
    for(const auto &[at,cell]:primary)if(cell!=document_.cell(index,at.first,at.second))edits.push_back({index,at.first,at.second,{},cell});
  }
  std::sort(performance.commands.begin(),performance.commands.end(),[](const auto &a,const auto &b){return std::tie(a.pattern,a.track,a.position,a.column)<std::tie(b.pattern,b.track,b.position,b.column);});try{next.validate(song);document_.validateEdits(edits);}catch(const std::invalid_argument &e){throw Api::ApiError(-32602,e.what());}
  std::map<std::string,std::set<uint32_t>> targets;
  const auto less=[](const PatternCommand &a,const PatternCommand &b){return std::tie(a.pattern,a.track,a.position,a.duration,a.column,a.kind,a.binding,a.value,a.pitchRange,a.effect,a.parameter)<std::tie(b.pattern,b.track,b.position,b.duration,b.column,b.kind,b.binding,b.value,b.pitchRange,b.effect,b.parameter);};
  const auto &previousCommands=document_.native().performance.commands;
  const std::set<PatternCommand,decltype(less)> previous(previousCommands.begin(),previousCommands.end(),less);
  for(const auto &command:performance.commands){if(command.kind!=PatternCommandKind::ParameterSet&&command.kind!=PatternCommandKind::ParameterSlide)continue;const auto &binding=performance.bindings.at(command.binding);const auto slot=slotOf(binding.plugin);const bool edited=changedBindings.contains(command.binding)||!previous.contains(command);
    if(slot==host_.plugins.size()){need(!edited,"Command binding is unresolved; repair it first");continue;}const auto &catalog=parameters(slot);const auto parameter=std::find_if(catalog.begin(),catalog.end(),[&](const auto &v){return v.at("id")==binding.parameter&&v.at("writable").template get<bool>()&&v.at("max").template get<double>()>v.at("min").template get<double>();});if(parameter==catalog.end()){need(!edited,"Command parameter is unresolved; repair it first");continue;}
    need(command.kind!=PatternCommandKind::ParameterSlide||parameter->at("canSlide").get<bool>(),"Discrete parameters cannot slide");need(std::none_of(next.automation.begin(),next.automation.end(),[&](const auto &lane){return lane.enabled&&lane.plugin==binding.plugin&&lane.parameter==binding.parameter;}),"Disable this parameter's envelope before using pattern commands");need(!host_.absoluteAutomation||!host_.absoluteAutomation(slot,binding.parameter),"Remove absolute automation before using pattern commands");targets[binding.plugin].insert(binding.parameter);need(targets[binding.plugin].size()<=64,"Use at most 64 pattern-command parameters per plugin");
  }
  const bool changed=next!=document_.native()||!edits.empty();const auto commandCount=performance.commands.size(),bindingCount=performance.bindings.size();if(host_.validateCandidate)host_.validateCandidate(next);if(changed&&!dry){if(stop_)stop_();document_.editNative(std::move(next),edits);}return {{"dryRun",dry},{"wouldChange",changed},{"commands",commandCount},{"bindings",bindingCount}};
}
}
