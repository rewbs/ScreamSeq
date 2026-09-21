#include "TimelineOperations.hpp"
#include "windows/Api/SessionAdapter.hpp"
#include "editor/TrackerDocument.hpp"
#include "editor/PatternCommands.hpp"
#include "editor/SongTiming.hpp"
#include "editor/AutomationTools.hpp"
#include "editor/CurveFormulaReference.hpp"
#include "soundlib/NativeNoteEffects.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <tuple>
namespace ScreamSeq {
namespace {
using Json=nlohmann::json;
using namespace Tracker;
void need(bool ok,const char *why){if(!ok)throw Api::ApiError(-32602,why);}
void keys(const Json &p,std::initializer_list<const char *> allowed){need(p.is_object(),"Expected object");for(auto i=p.begin();i!=p.end();++i)need(std::any_of(allowed.begin(),allowed.end(),[&](auto key){return i.key()==key;}),"Unknown parameter");}
const Json &field(const Json &p,const char *key){need(p.contains(key),"Required parameter missing");return p.at(key);}
uint64_t integer(const Json &v,uint64_t lo,uint64_t hi){
  need(v.is_number(),"Expected integer, not boolean");
  if(v.is_number_unsigned()){auto n=v.get<uint64_t>();need(n>=lo&&n<=hi,"Integer out of range");return n;}
  if(v.is_number_integer()){auto n=v.get<int64_t>();need(n>=0&&uint64_t(n)>=lo&&uint64_t(n)<=hi,"Integer out of range");return uint64_t(n);}
  auto n=v.get<double>();need(std::isfinite(n)&&n>=double(lo)&&n<=double(hi)&&n<=9007199254740991.0&&std::floor(n)==n,"Integer out of range");return uint64_t(n);
}
double number(const Json &v,double lo,double hi){need(v.is_number(),"Expected number");auto n=v.get<double>();need(std::isfinite(n)&&n>=lo&&n<=hi,"Number out of range");return n;}
std::string text(const Json &v,size_t maximum){
  need(v.is_string(),"Expected text");const auto &s=v.get_ref<const std::string&>();need(s.size()<=maximum*4&&s.find('\0')==std::string::npos,"Text exceeds its bound or contains NUL");
  if(!s.empty()){auto count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),nullptr,0);need(count>0&&size_t(count)<=maximum,"Invalid UTF-8 or text length");}return s;
}
AutomationPoint decodePoint(const Json &v,uint32_t maximum){
  keys(v,{"position","value","curve","formula"});
  static const std::vector<std::string> names={"step","linear","smooth","exponential","logarithmic","step-next","exponential-reverse","logarithmic-reverse","scripted"};
  auto name=v.contains("curve")?text(v.at("curve"),20):"linear";auto at=std::find(names.begin(),names.end(),name);need(at!=names.end(),"Unknown curve");
  AutomationPoint point{uint32_t(integer(field(v,"position"),0,maximum)),number(field(v,"value"),0,1),AutomationCurve(at-names.begin())};
  if(v.contains("formula"))point.formula=CurveFormula(text(v.at("formula"),2048));
  need(point.curve!=AutomationCurve::Scripted||!point.formula.source().empty(),"Scripted points require a formula");return point;
}
bool flag(const Json &v){need(v.is_boolean(),"Expected boolean");return v.get<bool>();}
const Json &array(const Json &v,size_t max){need(v.is_array()&&v.size()<=max,"Invalid array or capacity");return v;}
uint32_t rowsPerBeat(const OpenMPT::CSoundFile &song,uint16_t pattern){
  return std::max<uint32_t>(1,song.Patterns[pattern].GetOverrideSignature()?song.Patterns[pattern].GetRowsPerBeat():(song.m_nDefaultRowsPerBeat?song.m_nDefaultRowsPerBeat:4));
}
Json timingInfo(const SongTiming &timing,const OpenMPT::CSoundFile &song){
  static const char *modes[]={"classic","alternative","modern"};need(unsigned(timing.mode)<3,"Unknown timing mode");
  Json groove=Json::array(),overrides=Json::array();for(auto value:timing.groove)groove.push_back(double(value)/double(OpenMPT::TempoSwing::Unity));
  for(OpenMPT::PATTERNINDEX p=0;p<song.Patterns.Size();++p)if(song.Patterns.IsValidPat(p)&&(song.Patterns[p].GetOverrideSignature()||song.Patterns[p].HasTempoSwing()))overrides.push_back(p);
  auto sequence=song.Order.GetCurrentSequenceIndex();const auto &current=timing.sequences.at(sequence);
  return {{"mode",modes[unsigned(timing.mode)]},{"tempo",double(current.tempo)/double(OpenMPT::TEMPO::fractFact)},{"speed",current.speed},
    {"rowsPerBeat",timing.rowsPerBeat},{"rowsPerMeasure",timing.rowsPerMeasure},{"groove",groove},{"sequence",sequence},
    {"patternOverrides",overrides},{"grooveActive",timing.mode==OpenMPT::TempoMode::Modern&&!timing.groove.empty()}};
}
Json preciseEffects(OpenMPT::MODTYPE format){
  Json result=Json::array();
  for(const auto &c:patternCommands(format,false)){
    Json allowed=Json::array();for(int p=c.minimum;p<=c.maximum;++p)
      if((p&c.mask)==c.value&&OpenMPT::NativeNoteEffectSupported(c.command,uint8_t(p)))allowed.push_back(p);
    if(allowed.empty())continue;
    auto suggested=std::find(allowed.begin(),allowed.end(),c.suggested)!=allowed.end()?Json(c.suggested):allowed[0];
    std::string description=c.description;
    if(c.command==OpenMPT::CMD_NONE)description="No continuing effect for this hit. Ends the previous hit's effect; ordinary row effects stay active until a hit overrides them.";
    if(c.command==OpenMPT::CMD_S3MCMDEX&&c.value==0x90)description="Local sound control: 90 surround off, 91 surround on, 9E play forward, 9F play backward.";
    result.push_back({{"command",c.command},{"parameterMask",c.mask},{"parameterValue",c.value},{"suggestedParameter",suggested},
      {"label",c.label},{"name",c.name},{"family",c.family},{"description",description},{"minimum",c.minimum},{"maximum",c.maximum},{"allowedParameters",allowed}});
  }
  return result;
}
}
TimelineOperations::TimelineOperations(Tracker::Document &d,std::function<void()> stop):document_(d),stopPlayback_(std::move(stop)){}
std::vector<std::string> TimelineOperations::reads(){return {"pattern.notes.get","document.timing.get","automation.formula.reference","automation.formula.preview"};}
std::vector<std::string> TimelineOperations::writes(){return {"pattern.notes.set","document.timing.set"};}
Json TimelineOperations::invoke(const std::string &method,const Json &p){
  using namespace Tracker;
  auto &song=document_.song();
  if(method=="automation.formula.reference"){
    keys(p,{});Json symbols=Json::array();for(const auto &s:curveFormulaSymbols)
      symbols.push_back({{"name",std::string(s.name)},{"insert",std::string(s.insert)},{"category",std::string(s.category)},{"description",std::string(s.description)}});
    return {{"symbols",symbols},{"notes",std::string(curveFormulaNotes)}};
  }
  if(method=="automation.formula.preview"){
    keys(p,{"points","rows","rowsPerBeat","span","start","end","samples"});
    auto rows=uint32_t(integer(field(p,"rows"),1,65536));auto beat=number(p.value("rowsPerBeat",Json(4)),1,65535);
    auto span=uint32_t(integer(p.value("span",Json(rows*256)),1,rows*256));auto start=number(p.value("start",Json(0)),0,span);
    auto end=number(p.value("end",Json(span)),start,span);auto count=uint32_t(integer(p.value("samples",Json(1024)),2,4096));
    std::vector<AutomationPoint> points;try{for(const auto &point:array(field(p,"points"),4096))points.push_back(decodePoint(point,span-1));validateAutomationPoints(points,span);}
    catch(const std::invalid_argument&e){throw Api::ApiError(-32602,e.what());}
    Json values=Json::array();for(uint32_t i=0;i<count;++i){double position=start+(end-start)*i/(count-1);values.push_back(Json::array({position,automationValue(points,position,span,beat)}));}
    return {{"values",values},{"fallback","Domain errors use linear interpolation; output is clamped to 0..1"}};
  }
  if(method=="document.timing.get"){keys(p,{});return timingInfo(songTiming(song),song);}
  if(method=="document.timing.set"){
    keys(p,{"mode","tempo","speed","rowsPerBeat","rowsPerMeasure","groove","dryRun"});need(document_.editable(),"This document is read-only");
    auto before=songTiming(song),next=before;
    if(p.contains("mode")){need(p.at("mode").is_string(),"Expected timing mode name");auto mode=p.at("mode").get<std::string>();
      need(mode=="classic"||mode=="alternative"||mode=="modern","Unknown timing mode");next.mode=mode=="classic"?OpenMPT::TempoMode::Classic:mode=="modern"?OpenMPT::TempoMode::Modern:OpenMPT::TempoMode::Alternative;}
    auto &sequence=next.sequences.at(song.Order.GetCurrentSequenceIndex());
    if(p.contains("tempo"))sequence.tempo=OpenMPT::TEMPO(number(p.at("tempo"),32,512)).GetRaw();
    if(p.contains("speed"))sequence.speed=uint32_t(integer(p.at("speed"),1,31));
    if(p.contains("rowsPerBeat"))next.rowsPerBeat=uint32_t(integer(p.at("rowsPerBeat"),1,32));
    if(p.contains("rowsPerMeasure"))next.rowsPerMeasure=uint32_t(integer(p.at("rowsPerMeasure"),1,128));
    need(next.rowsPerMeasure>=next.rowsPerBeat,"A bar cannot contain fewer rows than a beat");
    if(p.contains("groove")){std::vector<double> values;for(const auto &v:array(p.at("groove"),32))values.push_back(number(v,.25,4));
      try{next.groove=normalizedGroove(values);}catch(const std::invalid_argument&e){throw Api::ApiError(-32602,e.what());}}
    need(next.groove.empty()||next.groove.size()==next.rowsPerBeat,"Supply one groove value per row of a beat");
    need(next.groove.empty()||next.mode==OpenMPT::TempoMode::Modern||(next.groove==before.groove&&next.mode==before.mode),"Groove requires modern timing; clear it explicitly before changing to legacy timing");
    const bool dry=p.contains("dryRun")?flag(p.at("dryRun")):false,changed=next!=before;
    try{validateSongTiming(next);
      if(changed){auto candidate=std::make_unique<Document>(document_.snapshotData());candidate->restoreNative(document_.native());candidate->song().Order.SetSequence(song.Order.GetCurrentSequenceIndex());
        applySongTiming(candidate->song(),next);candidate->native().validate(candidate->song());}
    }catch(const std::invalid_argument&e){throw Api::ApiError(-32602,e.what());}
    auto result=Json{{"before",timingInfo(before,song)},{"after",timingInfo(next,song)},{"wouldChange",changed},{"dryRun",dry}};
    if(changed&&!dry){if(stopPlayback_)stopPlayback_();document_.transaction([&](OpenMPT::CSoundFile&s){applySongTiming(s,next);});}
    return result;
  }
  if(method!="pattern.notes.get"&&method!="pattern.notes.set")throw Api::ApiError(-32601,"Unknown timeline operation");
  const bool write=method=="pattern.notes.set";
  if(write)keys(p,{"pattern","events","clearLegacy","clearRows","clearRowEffects","dryRun"});else keys(p,{"pattern"});
  const auto index=uint16_t(integer(field(p,"pattern"),0,UINT16_MAX));need(song.Patterns.IsValidPat(index),"Pattern does not exist");
  const auto id=document_.native().patterns.at(index).id;
  if(!write){
    Json events=Json::array();
    for(const auto &n:document_.native().preciseNotes)if(n.pattern==id){
      auto track=std::find_if(document_.native().tracks.begin(),document_.native().tracks.end(),[&](const auto &t){return t.second.id==n.track;});
      need(track!=document_.native().tracks.end(),"Precise note refers to missing track");
      Json event={{"position",n.position},{"instrument",n.instrument},{"note",n.note},{"velocity",n.velocity},{"channel",track->first}};
      if(n.effect){event["effect"]=n.effect;event["parameter"]=n.parameter;}events.push_back(std::move(event));
    }
    return {{"pattern",index},{"patternID","n"+std::to_string(id)},{"rows",song.Patterns[index].GetNumRows()},
      {"unitsPerRow",performanceUnitsPerRow},{"rowsPerBeat",rowsPerBeat(song,index)},{"effects",preciseEffects(song.GetType())},{"events",events}};
  }
  need(document_.editable(),"This document is read-only");
  const bool dry=p.contains("dryRun")?flag(p.at("dryRun")):false,clear=p.contains("clearLegacy")?flag(p.at("clearLegacy")):false;
  const bool clearEffects=p.contains("clearRowEffects")?flag(p.at("clearRowEffects")):false;
  const auto &events=array(field(p,"events"),maximumPreciseNotes);
  auto native=document_.native();std::vector<PreciseNote> replacement;
  const auto rows=song.Patterns[index].GetNumRows();const uint32_t end=uint32_t(rows)*performanceUnitsPerRow;
  std::set<std::pair<uint16_t,uint16_t>> cleared;std::vector<Edit> edits;
  auto clearCell=[&](uint16_t row,uint16_t channel){if(!cleared.emplace(row,channel).second)return;auto cell=document_.cell(index,row,channel);
    cell.note=cell.instrument=0;if(cell.volumeCommand==OpenMPT::VOLCMD_VOLUME)cell.volumeCommand=cell.volume=0;
    if(clearEffects)cell.effect=cell.parameter=0;edits.push_back({index,row,channel,{},cell});};
  for(const auto &e:events){
    keys(e,{"channel","position","row","offsetRows","offsetBeats","note","instrument","velocity","effect","parameter"});
    auto channel=uint16_t(integer(field(e,"channel"),0,song.GetNumChannels()-1));auto note=uint8_t(integer(field(e,"note"),1,255));uint32_t position=0;
    if(e.contains("position")){
      need(!e.contains("row")&&!e.contains("offsetRows")&&!e.contains("offsetBeats"),"Use position or row plus one offset unit, not both");
      position=uint32_t(integer(e.at("position"),0,end-1));
    }else{
      auto row=uint32_t(integer(field(e,"row"),0,rows-1));need(e.contains("offsetRows")!=e.contains("offsetBeats"),"Supply exactly one offset unit");
      double offset=e.contains("offsetRows")?number(e.at("offsetRows"),0,1):number(e.at("offsetBeats"),0,1)*rowsPerBeat(song,index);
      need(offset<1,"Offset must be inside its row");position=row*performanceUnitsPerRow+std::min<uint32_t>(performanceUnitsPerRow-1,uint32_t(std::lround(offset*performanceUnitsPerRow)));
    }
    replacement.push_back({id,native.tracks.at(channel).id,position,uint16_t(integer(e.value("instrument",Json(0)),0,255)),note,
      uint8_t(integer(e.value("velocity",Json(127)),1,127)),uint8_t(integer(e.value("effect",Json(0)),0,255)),uint8_t(integer(e.value("parameter",Json(0)),0,255))});
    if(clear)clearCell(uint16_t(position/performanceUnitsPerRow),channel);
  }
  if(p.contains("clearRows"))for(const auto &v:array(p.at("clearRows"),8192)){
    keys(v,{"row","channel"});clearCell(uint16_t(integer(field(v,"row"),0,rows-1)),uint16_t(integer(field(v,"channel"),0,song.GetNumChannels()-1)));
  }
  replacePreciseNotesForPattern(native.preciseNotes,id,std::move(replacement));
  try{native.validate(song);document_.validateEdits(edits);}catch(const std::invalid_argument &e){throw Api::ApiError(-32602,e.what());}
  bool changed=native!=document_.native()||std::any_of(edits.begin(),edits.end(),[&](const auto&e){return document_.cell(e.pattern,e.row,e.channel)!=e.after;});
  if(changed&&!dry){if(stopPlayback_)stopPlayback_();document_.editNative(std::move(native),edits);}
  return {{"dryRun",dry},{"wouldChange",changed},{"events",events.size()},{"clearedRows",cleared.size()}};
}
}
