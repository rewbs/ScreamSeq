#include "DocumentOperations.hpp"
#include "windows/Api/SessionAdapter.hpp"
#include "editor/TrackerDocument.hpp"
#include "editor/PatternCommands.hpp"
#include "soundlib/mod_specifications.h"
#include <cmath>
#include <set>
#include <tuple>
namespace ScreamSeq {
namespace {
using namespace Tracker;
void require(bool condition, const char *message) {
  if(!condition) throw Api::ApiError(-32602,message);
}
void keys(const Json &p, std::initializer_list<const char *> allowed) {
  require(p.is_object(),"Expected an object");
  for(auto it=p.begin();it!=p.end();++it)
    require(std::any_of(allowed.begin(),allowed.end(),[&](auto k){return it.key()==k;}),"Unknown parameter or field");
}
const Json &field(const Json &p, const char *key) {
  require(p.contains(key),"Missing required parameter");
  return p.at(key);
}
uint64_t integer(const Json &v, uint64_t low, uint64_t high) {
  require(v.is_number() && !v.is_boolean(),"Expected a number, not a boolean");
  const auto n=v.get<double>();
  require(std::isfinite(n) && n>=double(low) && n<=double(high) && std::floor(n)==n,"Integer outside its allowed range");
  return uint64_t(n);
}
bool boolean(const Json &v) {
  require(v.is_boolean(),"Expected a boolean"); return v.get<bool>();
}
double number(const Json &v,double low,double high) {
  require(v.is_number() && !v.is_boolean(),"Expected a number, not a boolean");
  const auto n=v.get<double>();
  require(std::isfinite(n) && n>=low && n<=high,"Number outside its allowed range");
  return n;
}
std::string string(const Json &v,size_t maximum) {
  require(v.is_string(),"Expected a bounded string");
  const auto &s=v.get_ref<const std::string &>();
  require(s.size()<=maximum*4 && s.find('\0')==std::string::npos,"String is oversized or contains NUL");
  if(!s.empty()) {
    const auto units=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),nullptr,0);
    require(units>0 && size_t(units)<=maximum,"Invalid UTF-8 or oversized string");
  }
  return s;
}
// Run the same shared primitive on an exact snapshot first. This validates
// native reference integrity and format limits before touching live transport.
// No Windows copy of channel resizing, order editing, or metadata reconciliation.
void validateStructural(Document &document,const std::function<void(Document &)> &operation) {
  try {
    auto candidate=std::make_unique<Document>(document.snapshotData());
    candidate->restoreNative(document.native());
    candidate->song().Order.SetSequence(document.song().Order.GetCurrentSequenceIndex());
    operation(*candidate);
  } catch(const std::invalid_argument &e) { throw Api::ApiError(-32602,e.what()); }
    catch(const std::out_of_range &e) { throw Api::ApiError(-32602,e.what()); }
}
Json cellObject(const Cell &c) {
  return {{"note",c.note},{"instrument",c.instrument},{"volumeCommand",c.volumeCommand},
    {"volume",c.volume},{"effect",c.effect},{"parameter",c.parameter}};
}
}
DocumentOperations::DocumentOperations(Tracker::Document &document, std::function<void()> stopPlayback,
    std::function<void(const std::vector<Tracker::Edit>&)> publishEdits)
  : document_(document), stopPlayback_(std::move(stopPlayback)), publishEdits_(std::move(publishEdits)) {}
std::vector<std::string> DocumentOperations::reads() { return {"pattern.commands","sample.get","sample.waveform.get"}; }
std::vector<std::string> DocumentOperations::writes() {
  return {"pattern.apply","history.undo","history.redo","document.patch","pattern.create","order.edit","sequence.select"};
}
Json DocumentOperations::invoke(const std::string &method, const Json &p) {
  using namespace Tracker;
  if(method=="pattern.commands") {
    keys(p,{});
    Json catalog={{"effect",Json::array()},{"volume",Json::array()}};
    if(!document_.editable()) return catalog;
    for(bool volume:{false,true}) for(const auto &c:patternCommands(document_.song().GetType(),volume))
      catalog[volume ? "volume" : "effect"].push_back({{"command",c.command},{"parameterMask",c.mask},
        {"parameterValue",c.value},{"suggestedParameter",c.suggested},{"label",c.label},{"name",c.name},
        {"family",c.family},{"description",c.description},{"minimum",c.minimum},{"maximum",c.maximum}});
    return catalog;
  }
  if(method=="sample.get" || method=="sample.waveform.get") {
    if(method=="sample.get") keys(p,{"sample"});
    else keys(p,{"sample","start","end","bins","channels"});
    const auto &song=document_.song();
    const auto index=SAMPLEINDEX(integer(field(p,"sample"),1,song.GetNumSamples()));
    const auto &sample=song.GetSample(index);
    if(method=="sample.get") {
      const auto name=::OpenMPT::mpt::ToCharset(::OpenMPT::mpt::Charset::UTF8,song.GetCharsetInternal(),song.GetSampleName(index));
      return {{"index",index},{"name",name},{"frames",sample.nLength},{"rate",sample.nC5Speed},
        {"volume",sample.nVolume/4},{"pan",sample.nPan},{"loopStart",sample.nLoopStart},{"loopEnd",sample.nLoopEnd},
        {"loop",bool(sample.uFlags[CHN_LOOP])},{"pingpong",bool(sample.uFlags[CHN_PINGPONGLOOP])},
        {"sustainStart",sample.nSustainStart},{"sustainEnd",sample.nSustainEnd},
        {"sustainLoop",bool(sample.uFlags[CHN_SUSTAINLOOP])},{"sustainPingpong",bool(sample.uFlags[CHN_PINGPONGSUSTAIN])},
        {"reverseLoop",(sample.nativeReverseLoops&1)!=0},{"sustainReverse",(sample.nativeReverseLoops&2)!=0},
        {"bits",sample.uFlags[CHN_16BIT] ? 16 : 8},{"channels",sample.GetNumChannels()}};
    }
    const auto start=uint32_t(p.contains("start") ? integer(p.at("start"),0,sample.nLength) : 0);
    const auto end=uint32_t(p.contains("end") ? integer(p.at("end"),start,sample.nLength) : sample.nLength);
    const auto bins=size_t(p.contains("bins") ? integer(p.at("bins"),1,16384) : 2048);
    const auto channels=p.contains("channels") ? string(p.at("channels"),10) : "both";
    require(channels=="both" || channels=="left" || channels=="right","Unknown waveform channel selection");
    require(channels!="right" || sample.GetNumChannels()==2,"This sample has no right channel");
    const auto selected=channels=="both" ? SampleChannels::Both : channels=="left" ? SampleChannels::Left : SampleChannels::Right;
    return {{"sample",index},{"start",start},{"end",end},{"bins",bins},{"channels",channels},
      {"peaks",document_.waveform(index,start,end,bins,selected)}};
  }
  if(method=="pattern.apply") {
    keys(p,{"cells","dryRun"});
    require(document_.editable(),"This document is read-only");
    const auto &input=field(p,"cells");
    require(input.is_array() && !input.empty() && input.size()<=4096,"An edit batch must contain 1..4096 cells");
    const bool dry=p.contains("dryRun") ? boolean(p.at("dryRun")) : false;
    std::set<std::tuple<int,int,int>> seen;
    std::vector<Edit> edits;
    edits.reserve(input.size());
    Json changes=Json::array();
    for(const auto &item:input) {
      keys(item,{"pattern","row","channel","note","instrument","volumeCommand","volume","effect","parameter"});
      const auto pattern=uint16_t(integer(field(item,"pattern"),0,UINT16_MAX));
      const auto row=uint16_t(integer(field(item,"row"),0,UINT16_MAX));
      const auto channel=uint16_t(integer(field(item,"channel"),0,UINT16_MAX));
      require(document_.valid(pattern,row,channel),"Cell is outside the pattern");
      require(seen.emplace(pattern,row,channel).second,"Duplicate cell in batch");
      require(item.size()>3,"Cell patch has no fields");
      const auto before=document_.cell(pattern,row,channel);
      auto after=before;
      auto set=[&](const char *name,uint8_t &v,unsigned max=255) {
        if(item.contains(name)) v=uint8_t(integer(item.at(name),0,max));
      };
      set("note",after.note); set("instrument",after.instrument);
      set("volumeCommand",after.volumeCommand,MAX_VOLCMDS-1); set("volume",after.volume);
      set("effect",after.effect,MAX_EFFECTS-1); set("parameter",after.parameter);
      require(after.volumeCommand!=VOLCMD_VOLUME || after.volume<=64,"Absolute volume must be 0..64");
      edits.push_back({pattern,row,channel,before,after});
      if(before!=after) changes.push_back({{"pattern",pattern},{"row",row},{"channel",channel},
        {"before",cellObject(before)},{"after",cellObject(after)}});
    }
    try { document_.validateEdits(edits); }
    catch(const std::invalid_argument &e) { throw Api::ApiError(-32602,e.what()); }
    if(!dry && !changes.empty()) {
      const auto applied=document_.edit(edits);
      if(publishEdits_) publishEdits_(applied);
    }
    return {{"dryRun",dry},{"changedCells",changes.size()},{"changes",std::move(changes)}};
  }
  if(method=="document.patch") {
    keys(p,{"title","tempo","speed","channels"});
    require(document_.editable(),"This document is read-only");
    const auto &song=document_.song();
    const auto &spec=song.GetModSpecifications();
    const auto title=p.contains("title")
      ? ::OpenMPT::mpt::ToCharset(song.GetCharsetInternal(),::OpenMPT::mpt::Charset::UTF8,string(p.at("title"),200))
      : song.m_songName;
    const auto tempo=p.contains("tempo") ? TEMPO(number(p.at("tempo"),32,512)) : song.Order().GetDefaultTempo();
    const auto speed=p.contains("speed") ? uint32_t(integer(p.at("speed"),1,31)) : song.Order().GetDefaultSpeed();
    const auto channels=p.contains("channels") ? int(integer(p.at("channels"),spec.channelsMin,spec.channelsMax)) : int(song.GetNumChannels());
    if(title==song.m_songName && tempo==song.Order().GetDefaultTempo() && speed==song.Order().GetDefaultSpeed()
      && channels==song.GetNumChannels()) return Json::object();
    const auto operation=[&](Document &d) {
      d.transaction([&](CSoundFile &s) {
        if(p.contains("title")) s.SetTitle(title);
        if(p.contains("tempo")) s.Order().SetDefaultTempo(tempo);
        if(p.contains("speed")) s.Order().SetDefaultSpeed(speed);
        if(p.contains("channels")) Document::resizeChannels(s,channels);
      });
    };
    validateStructural(document_,operation);
    if(stopPlayback_) stopPlayback_();
    operation(document_);
    return Json::object();
  }
  if(method=="pattern.create") {
    keys(p,{"rows","source"});
    require(document_.editable(),"This document is read-only");
    const auto &song=document_.song();
    const auto &spec=song.GetModSpecifications();
    const int rows=int(integer(field(p,"rows"),spec.patternRowsMin,spec.patternRowsMax));
    const bool duplicate=p.contains("source");
    const int source=duplicate ? int(integer(p.at("source"),0,UINT16_MAX)) : 0;
    require(!duplicate || song.Patterns.IsValidPat(source),"Source pattern does not exist");
    require(song.Patterns.GetRemainingCapacity()>0 && song.Order().size()<spec.ordersMax,
      "The format has no more pattern or order slots");
    const auto operation=[&](Document &d){d.addPattern(rows,duplicate,source);};
    validateStructural(document_,operation);
    if(stopPlayback_) stopPlayback_();
    return {{"pattern",document_.addPattern(rows,duplicate,source)}};
  }
  if(method=="order.edit") {
    keys(p,{"order","pattern","operation"});
    require(document_.editable(),"This document is read-only");
    const auto operation=string(field(p,"operation"),20);
    require(operation=="before" || operation=="after" || operation=="assign" || operation=="up"
      || operation=="down" || operation=="remove","Unknown order operation");
    const int order=int(integer(field(p,"order"),0,document_.song().Order().size()));
    const int pattern=p.contains("pattern") ? int(integer(p.at("pattern"),0,UINT16_MAX)) : 0;
    require(order<int(document_.song().Order().size()),"Select a valid order");
    if(operation=="assign" && document_.song().Patterns.IsValidPat(pattern)
      && document_.song().Order()[order]==pattern) return Json::object();
    const auto change=[&](Document &d){d.editOrder(order,pattern,operation);};
    validateStructural(document_,change);
    if(stopPlayback_) stopPlayback_();
    change(document_);
    return Json::object();
  }
  if(method=="sequence.select") {
    keys(p,{"sequence"});
    require(document_.editable(),"This document is read-only");
    auto &orders=document_.song().Order;
    const auto sequence=SEQUENCEINDEX(integer(field(p,"sequence"),0,orders.GetNumSequences()-1));
    if(sequence!=orders.GetCurrentSequenceIndex()) {
      if(stopPlayback_) stopPlayback_();
      orders.SetSequence(sequence);
    }
    // Like Mac, navigation is not a document Undo step. The caller's opaque
    // revision must include the selected sequence as well as Document::revision.
    return Json::object();
  }
  if(method=="history.undo" || method=="history.redo") {
    keys(p,{"domain"});
    require(document_.editable(),"This document is read-only");
    require(field(p,"domain")=="document","Only document history is supported; plugin history belongs to the host");
    const bool redo=method=="history.redo";
    if(redo ? !document_.canRedo() : !document_.canUndo()) return Json::object();
    const bool structural=redo
      ? document_.redoChangesStructure() || document_.redoChangesAutomation() || document_.redoChangesMixer()
      : document_.undoChangesStructure() || document_.undoChangesAutomation() || document_.undoChangesMixer();
    // A native-only history entry can also change column mutes. This layer has no
    // renderer-metadata publisher; stop rather than leave its live copy obsolete.
    const bool nativeChange=document_.historyNative(redo)!=document_.native();
    if((structural || nativeChange) && stopPlayback_) stopPlayback_();
    const auto applied=redo ? document_.redo() : document_.undo();
    if(!applied.empty() && publishEdits_) publishEdits_(applied);
    return Json::object();
  }
  throw Api::ApiError(-32601,"Unknown document operation");
}
}
