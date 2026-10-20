#include "DocumentOperations.hpp"
#include "windows/Project/NativePatternJSON.hpp"
#include "windows/Api/SessionAdapter.hpp"
#include "windows/Api/PipeServer.hpp"
#include "editor/TrackerDocument.hpp"
#include "editor/PatternCommands.hpp"
#include "editor/ArrangementTools.hpp"
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
void validateStructural(Document &document,const std::function<void(Document &)> &operation,
    const std::function<void(Document &)> &validateCandidate) {
  try {
    auto candidate=std::make_unique<Document>(document.snapshotData());
    candidate->restoreNative(document.native());
    candidate->song().Order.SetSequence(document.song().Order.GetCurrentSequenceIndex());
    operation(*candidate);
    if(validateCandidate) validateCandidate(*candidate);
  } catch(const std::invalid_argument &e) { throw Api::ApiError(-32602,e.what()); }
    catch(const std::out_of_range &e) { throw Api::ApiError(-32602,e.what()); }
}
Json cellObject(const Cell &c) {
  return {{"note",c.note},{"instrument",c.instrument},{"volumeCommand",c.volumeCommand},
    {"volume",c.volume},{"effect",c.effect},{"parameter",c.parameter}};
}
uint64_t annotationID(const Json &value) {
  const auto text=string(value,32);
  require(text.size()>1&&text[0]=='n'&&text[1]>='1'&&text[1]<='9',"Invalid native identity");
  uint64_t id=0;
  for(size_t i=1;i<text.size();++i) {
    require(text[i]>='0'&&text[i]<='9'&&id<NativeSong::maximumID/10,"Invalid native identity");
    id=id*10+unsigned(text[i]-'0');
  }
  require(id>0&&id<NativeSong::maximumID,"Invalid native identity");return id;
}
NativeEntity *annotationTarget(NativeSong &native,uint64_t id) {
  for(auto *entities:{&native.patterns,&native.tracks})for(auto &[index,entity]:*entities)if(entity.id==id)return &entity;
  for(auto &sequence:native.sequences) {
    if(sequence.info.id==id)return &sequence.info;
    for(auto &order:sequence.orders)if(order.id==id)return &order;
  }
  return nullptr;
}
bool presentationOnly(const NativeSong &before,const NativeSong &after) {
  // Normalize only the explicitly supported presentation fields. Whole-song
  // equality below keeps new musical fields conservative without another list.
  auto normalized=after;
  // Bank definitions and links describe reuse; their audible materialization
  // lives in automation, graph sources or instrument data. Those fields remain
  // in the equality check, so a linked shape edit still publishes or stops.
  normalized.envelopeBank=before.envelopeBank;
  normalized.envelopeLinks=before.envelopeLinks;
  auto entity=[](const NativeEntity &a,NativeEntity &b) {
    if(a.id!=b.id)return false;
    b.name=a.name;b.annotation=a.annotation;b.color=a.color;return true;
  };
  auto collection=[&](const auto &a,auto &b) {
    if(a.size()!=b.size())return false;
    for(const auto &[key,value]:a) {auto found=b.find(key);if(found==b.end()||!entity(value,found->second))return false;}
    return true;
  };
  if(!collection(before.patterns,normalized.patterns)||!collection(before.tracks,normalized.tracks)||before.sequences.size()!=normalized.sequences.size())return false;
  for(size_t i=0;i<before.sequences.size();++i) {
    const auto &a=before.sequences[i];auto &b=normalized.sequences[i];
    if(!entity(a.info,b.info)||a.orders.size()!=b.orders.size())return false;
    for(size_t j=0;j<a.orders.size();++j)if(!entity(a.orders[j],b.orders[j]))return false;
  }
  return before==normalized;
}
}
DocumentOperations::DocumentOperations(Tracker::Document &document, std::function<void()> stopPlayback,
    std::function<void(const std::vector<Tracker::Edit>&)> publishEdits,
    std::function<void(Tracker::Document&)> validateCandidate,
  std::function<void(const Tracker::NativeSong&)> validateNativeCandidate,
  std::function<std::function<void()>(const Tracker::NativeSong &,const Tracker::NativeSong &)> prepareNativeUpdate)
  : document_(document), stopPlayback_(std::move(stopPlayback)), publishEdits_(std::move(publishEdits)),
    validateCandidate_(std::move(validateCandidate)),validateNativeCandidate_(std::move(validateNativeCandidate)),prepareNativeUpdate_(std::move(prepareNativeUpdate)) {}
Json DocumentOperations::entityInfo(const Tracker::NativeEntity &entity) {
  return {{"id","n"+std::to_string(entity.id)},{"name",entity.name},{"annotation",entity.annotation},{"color",entity.color}};
}
Tracker::NativeSong DocumentOperations::annotationCandidate(const Tracker::Document &document,const Json &p) {
  keys(p,{"id","name","annotation","color"});require(document.editable(),"This document is read-only");
  const auto id=annotationID(field(p,"id"));require(p.contains("name")||p.contains("annotation")||p.contains("color"),"Provide at least one annotation field");
  std::optional<std::string> name,annotation;std::optional<uint32_t> color;
  if(p.contains("name"))name=string(p.at("name"),256);
  if(p.contains("annotation"))annotation=string(p.at("annotation"),4096);
  if(p.contains("color"))color=uint32_t(integer(p.at("color"),0,0xffffff));
  auto next=document.native();auto *target=annotationTarget(next,id);
  require(target,"Choose a pattern, track, sequence or order identity from document.get/arrangement.get");
  if(name)target->name=*name;if(annotation)target->annotation=*annotation;if(color)target->color=*color;
  next.validate(document.song());return next;
}
std::vector<std::string> DocumentOperations::reads() { return {"pattern.commands","sample.get","sample.waveform.get","arrangement.get","arrangement.matrix"}; }
std::vector<std::string> DocumentOperations::writes() {
  return {"pattern.apply","history.undo","history.redo","document.patch","pattern.create","order.edit","sequence.select","song.annotate","arrangement.copyBlock"};
}
Json DocumentOperations::invoke(const std::string &method, const Json &p) {
  using namespace Tracker;
  if(method=="arrangement.matrix") {
    keys(p,{"startOrder","orderCount","startChannel","channelCount"});
    const auto &song=document_.song();const auto &native=document_.native();ArrangementMatrixRange range;
    if(p.contains("startOrder"))range.startOrder=uint32_t(integer(p.at("startOrder"),0,song.Order().size()));
    if(p.contains("orderCount"))range.orderCount=uint32_t(integer(p.at("orderCount"),1,128));
    if(p.contains("startChannel"))range.startChannel=uint32_t(integer(p.at("startChannel"),0,song.GetNumChannels()-1));
    if(p.contains("channelCount"))range.channelCount=uint32_t(integer(p.at("channelCount"),1,std::min<uint32_t>(32,song.GetNumChannels()-range.startChannel)));
    ArrangementMatrixSummary summary;
    try {summary=summarizeArrangement(document_,range);}
    catch(const std::invalid_argument &e){throw Api::ApiError(-32602,e.what());}
    const auto &slots=native.sequences.at(summary.sequence).orders;
    std::vector<Json> blocks;blocks.reserve(summary.patterns.size());
    for(const auto &pattern:summary.patterns) {
      auto values=Json::array();for(const auto &block:pattern.blocks)values.push_back({{"channel",block.channel},{"trackID","n"+std::to_string(block.trackID)},
        {"events",block.events},{"notes",block.notes},{"bins",block.bins},{"trackerEvents",block.trackerEvents},{"preciseEvents",block.preciseEvents},{"nativeFxEvents",block.nativeFxEvents}});
      blocks.push_back(std::move(values));
    }
    Json result={{"orders",Json::array()},{"tracks",Json::array()},{"totalOrders",summary.totalOrders},{"totalChannels",summary.totalChannels},
      {"startOrder",summary.startOrder},{"startChannel",summary.startChannel}};
    constexpr size_t maximum=Api::PipeServer::maxResponseBytes-4096;size_t bytes=result.dump().size();
    auto append=[&](const char *key,Json item) {
      const auto size=item.dump().size()+1;
      if(size>maximum||bytes>maximum-size)throw Api::ApiError(-32003,"Matrix response exceeds transport limit; request a smaller page");
      bytes+=size;result[key].push_back(std::move(item));
    };
    for(const auto &order:summary.orders) {
      auto item=entityInfo(slots.at(order.order));item["order"]=order.order;item["pattern"]=order.pattern;item["rows"]=0;item["blocks"]=Json::array();
      if(order.summaryIndex!=noArrangementPatternSummary) {
        const auto &pattern=summary.patterns.at(order.summaryIndex);item["rows"]=pattern.rows;item["blocks"]=blocks.at(order.summaryIndex);
        item["patternID"]="n"+std::to_string(pattern.patternID);item["patternName"]=native.patterns.at(pattern.pattern).name;
      }
      append("orders",std::move(item));
    }
    for(uint32_t ch=summary.startChannel;ch<summary.startChannel+summary.channelCount;++ch) {
      auto item=entityInfo(native.tracks.at(ch));item["channel"]=ch;append("tracks",std::move(item));
    }
    return result;
  }
  if(method=="arrangement.copyBlock") {
    keys(p,{"sourceOrder","targetOrder","sourceChannel","targetChannel","channelCount","mode","makeUnique","clip","dryRun"});
    require(document_.editable(),"This document is read-only");ArrangementCopy copy;
    copy.sourceOrder=uint16_t(integer(field(p,"sourceOrder"),0,UINT16_MAX));copy.targetOrder=uint16_t(integer(field(p,"targetOrder"),0,UINT16_MAX));
    copy.sourceChannel=uint16_t(integer(field(p,"sourceChannel"),0,UINT16_MAX));copy.targetChannel=uint16_t(integer(field(p,"targetChannel"),0,UINT16_MAX));
    if(p.contains("channelCount"))copy.channels=uint16_t(integer(p.at("channelCount"),1,document_.song().GetNumChannels()));
    if(p.contains("mode"))copy.mode=string(p.at("mode"),20);
    if(p.contains("makeUnique"))copy.makeUnique=boolean(p.at("makeUnique"));if(p.contains("clip"))copy.clip=boolean(p.at("clip"));
    const bool dry=p.contains("dryRun")?boolean(p.at("dryRun")):false;
    try {
      const auto plan=prepareArrangementCopy(document_,copy,validateCandidate_);
      Json result={{"dryRun",dry},{"wouldChange",plan.changed()},{"changedCells",plan.edits.size()},{"clonesPattern",plan.clone},
        {"targetPattern",plan.targetPattern},{"targetOrder",plan.targetOrder}};
      if(!dry&&plan.changed()){if(stopPlayback_)stopPlayback_();applyArrangementCopy(document_,plan);}
      return result;
    }catch(const std::invalid_argument &e){throw Api::ApiError(-32602,e.what());}
     catch(const std::out_of_range &e){throw Api::ApiError(-32602,e.what());}
  }
  if(method=="arrangement.get") {
    keys(p,{});const auto &song=document_.song();const auto &native=document_.native();
    const auto sequence=song.Order.GetCurrentSequenceIndex();const auto &slots=native.sequences.at(sequence).orders;
    require(slots.size()==song.Order().size(),"Arrangement metadata does not match the active sequence");
    Json result={{"sequence",sequence},{"sequenceID","n"+std::to_string(native.sequences.at(sequence).info.id)},
      {"orders",Json::array()},{"sections",Json::array()}};
    // Reserve space for the host's revision/context envelope. Charge each item
    // before retaining it so the full untrimmed read cannot construct a giant reply.
    constexpr size_t maximum=Api::PipeServer::maxResponseBytes-4096;size_t bytes=result.dump().size();
    auto append=[&](const char *key,Json item) {
      const auto size=item.dump().size()+1;
      if(size>maximum||bytes>maximum-size)throw Api::ApiError(-32003,"Arrangement response exceeds transport limit");
      bytes+=size;result[key].push_back(std::move(item));
    };
    for(size_t i=0;i<slots.size();++i) {
      auto item=entityInfo(slots[i]);const auto pattern=song.Order()[i];item["order"]=i;item["pattern"]=pattern;
      if(song.Patterns.IsValidPat(pattern))item["patternID"]="n"+std::to_string(native.patterns.at(pattern).id);
      append("orders",std::move(item));
      if(!slots[i].name.empty()) {
        size_t end=i+1;while(end<slots.size()&&slots[end].name.empty())++end;
        append("sections",{{"id","n"+std::to_string(slots[i].id)},{"name",slots[i].name},{"firstOrder",i},{"lastOrder",end-1},{"color",slots[i].color}});
      }
    }
    return result;
  }
  if(method=="song.annotate") {
    auto next=annotationCandidate(document_,p);const auto result=entityInfo(*annotationTarget(next,annotationID(p.at("id"))));
    if(next!=document_.native()) {
      if(validateNativeCandidate_)validateNativeCandidate_(next);
      document_.annotate([&](NativeSong &native){native=std::move(next);});
    }
    return result;
  }
  if(method=="pattern.commands") {
    keys(p,{});
    Json catalog={{"effect",Json::array()},{"volume",Json::array()},{"native",PatternJSON::catalog()}};
    if(!document_.editable()) return catalog;
    for(bool volume:{false,true}) for(const auto &c:patternCommands(document_.song().GetType(),volume))
      catalog[volume ? "volume" : "effect"].push_back({{"command",c.command},{"parameterMask",c.mask},{"displayCode",c.command==0?"..":c.mask?c.label.substr(0,2):"0"+c.label.substr(0,1)},
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
        if(document_.undoChangesAutomation() && stopPlayback_) stopPlayback_();
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
    validateStructural(document_,operation,validateCandidate_);
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
    validateStructural(document_,operation,validateCandidate_);
    if(stopPlayback_) stopPlayback_();
    return {{"pattern",document_.addPattern(rows,duplicate,source)}};
  }
  if(method=="order.edit") {
    keys(p,{"order","pattern","operation","destination"});
    require(document_.editable(),"This document is read-only");
    const auto operation=string(field(p,"operation"),20);
    require(operation=="before" || operation=="after" || operation=="assign" || operation=="up"
      || operation=="down" || operation=="remove" || operation=="move","Unknown order operation");
    const int order=int(integer(field(p,"order"),0,document_.song().Order().size()));
    require(order<int(document_.song().Order().size()),"Select a valid order");
    require(!p.contains("destination")||operation=="move","Destination is only used by move");
    const int pattern=operation=="move" ? int(integer(field(p,"destination"),0,document_.song().Order().size()-1))
      : p.contains("pattern") ? int(integer(p.at("pattern"),0,UINT16_MAX)) : 0;
    // Shared preflight precedes the candidate validator so an exact no-op
    // allocates no snapshot. Preserve that validator's public error contract.
    try {if(!document_.orderEditChanges(order,pattern,operation))return Json::object();}
    catch(const std::invalid_argument &e){throw Api::ApiError(-32602,e.what());}
    catch(const std::out_of_range &e){throw Api::ApiError(-32602,e.what());}
    const auto change=[&](Document &d){d.editOrder(order,pattern,operation);};
    validateStructural(document_,change,validateCandidate_);
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
    const bool structureChange=redo ? document_.redoChangesStructure() : document_.undoChangesStructure();
    const bool structural=structureChange || (redo
      ? document_.redoChangesAutomation() || document_.redoChangesMixer()
      : document_.undoChangesAutomation() || document_.undoChangesMixer());
    auto target=document_.historyNative(redo);
    // Shared history never reuses an allocated identity. Compare the effective
    // target, not an older allocator watermark stored with an annotation entry.
    target.nextID=std::max(target.nextID,document_.native().nextID);
    const bool nativeChange=target!=document_.native();
    // Independent plugin edits can use space released by an undone structural
    // edit. Validate the actual restored song with the current host state before
    // stopping playback or consuming history, including sample/splice/slot edits.
    if(structureChange&&validateCandidate_) {
      auto candidate=document_.historyCandidate(redo);
      validateCandidate_(*candidate);
    }
    if(nativeChange&&!structureChange&&validateNativeCandidate_)validateNativeCandidate_(target);
    std::function<void()> publish;
    if(structural || (nativeChange&&!presentationOnly(document_.native(),target))) {
      if(document_.historyNativeOnly(redo))
        publish=prepareNativeUpdate_?prepareNativeUpdate_(document_.native(),target):stopPlayback_;
      else if(stopPlayback_)stopPlayback_();
    }
    const auto applied=redo ? document_.redo(publish) : document_.undo(publish);
    if(!applied.empty() && publishEdits_) publishEdits_(applied);
    return Json::object();
  }
  throw Api::ApiError(-32601,"Unknown document operation");
}
}
