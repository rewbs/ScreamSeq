#pragma once
#include "../Project/ScratchGestureJSON.hpp"
#include <optional>

namespace ScreamSeq {
// Keep the shared expression grammar, specializing only its timing context.
inline nlohmann::json scratchFormulaReference(nlohmann::json reference){
  auto notes=reference.value("notes",std::string());const auto clock=notes.find("Playback evaluates");if(clock!=std::string::npos)notes.erase(clock);
  reference["notes"]="Scratch formulas are evaluated per sample, within each repeat. Motion is normalized record position; Fader is normalized gain. t is local segment phase 0..1. row is the synthetic cycle coordinate 0..256, not the song pattern row. beat is elapsed beats in this repeat; beats is elapsed segment beats and duration is the segment length. One repeat lasts SK beats / repeats (preview defaults to one beat without a captured SK). startBeat and endBeat locate segment endpoints within the repeat.\n"+notes;
  for(auto &symbol:reference["symbols"]){const auto name=symbol.value("name",std::string());if(name=="row")symbol["description"]="Synthetic cycle coordinate 0..256; independent of pattern rows";else if(name=="beat")symbol["description"]="Elapsed musical beats within the current scratch repeat";else if(name=="duration")symbol["description"]="This segment's length in beats, using SK beats / repeats";else if(name=="beats")symbol["description"]="Elapsed beats within this segment of the repeat";else if(name=="startBeat"||name=="endBeat")symbol["description"]="Segment endpoint in beats within the scratch repeat";}
  return reference;
}
// UI-owned immutable capture. Numeric indexes are resolved again from these
// identities before any pattern write; moving the cursor never changes it.
struct ScratchPatternTarget {
  std::string document,revision;
  uint64_t patternID=0,trackID=0;
  unsigned pattern=0,row=0,channel=0,column=0,rows=0,rowsPerBeat=4;
  nlohmann::json command=nullptr;
  unsigned gesture()const{return command.is_object()&&command.value("native",std::string())=="scratch"?command.at("parameters").at("gesture").get<unsigned>():0;}
  bool sameCell(const ScratchPatternTarget &other)const{return document==other.document&&patternID==other.patternID&&trackID==other.trackID&&row==other.row&&column==other.column;}
  nlohmann::json use(unsigned id)const{
    if(!id||id>255||row>=rows)throw std::invalid_argument("Captured scratch destination is unavailable");
    const bool existing=gesture()!=0;
    nlohmann::json next=existing?command:nlohmann::json{{"kind","native"},{"native","scratch"},{"parameters",{{"gesture",id},{"beats",1},{"travelMs",200},{"repeats",1},{"reverse",false}}},{"offset",0}};
    const auto offset=next.value("offset",0u);const double remaining=(double(rows-row)-offset/65536.0)/std::max(1u,rowsPerBeat);
    if(remaining<1.0/65536)throw std::invalid_argument("Too little pattern time remains for a scratch phrase");
    next["parameters"]["gesture"]=id;if(!existing)next["parameters"]["beats"]=std::min(1.0,remaining);
    return {{"pattern",pattern},{"row",row},{"channel",channel},{"column",column},{"command",next}};
  }
};
struct ScratchEditorState {
  using Json=nlohmann::json;
  Json baseline=Json::object(),draft=Json::object();
  std::array<int,2> selected{-1,-1};unsigned lane=0;
  uint64_t generation=0;
  bool dirty()const{return draft!=baseline;}
  bool empty()const{return !draft.contains("id");}
  unsigned id()const{return empty()?0:draft.at("id").get<unsigned>();}
  static Json content(const Json &item){Json result=Json::object();for(const char *key:{"id","name","motion","fader"})if(item.contains(key))result[key]=item.at(key);return result;}
  void load(const Json &item){baseline=draft=content(item);selected={-1,-1};++generation;}
  const Json &points(unsigned which)const{static const Json none=Json::array();return empty()?none:draft.at(which?"fader":"motion");}
  void replace(Json point){
    if(empty())throw std::invalid_argument("Choose a scratch phrase first");
    const auto position=ScratchJSON::integer(point.at("position"),0,65536);const auto before=points(lane);auto next=before;const int index=selected[lane];
    if(index>=0){const auto old=before.at(size_t(index)).at("position").get<unsigned>();if((old==0||old==65536)&&position!=old)throw std::invalid_argument("Scratch endpoints remain at cycle zero and one");}
    for(size_t i=0;i<next.size();++i)if(int(i)!=index&&next[i].at("position")==position)throw std::invalid_argument("Another point occupies this cycle position");
    if(index>=0)next.at(size_t(index))=std::move(point);else{if(next.size()>=256)throw std::invalid_argument("A scratch lane supports at most 256 points");next.push_back(std::move(point));}
    std::sort(next.begin(),next.end(),[](const auto &a,const auto &b){return a.at("position").template get<unsigned>()<b.at("position").template get<unsigned>();});
    auto candidate=draft;candidate[lane?"fader":"motion"]=next;ScratchJSON::decode(candidate,true);
    draft=std::move(candidate);++generation;for(size_t i=0;i<next.size();++i)if(next[i].at("position")==position)selected[lane]=int(i);
  }
  void remove(){
    if(empty()||selected[lane]<0)return;auto next=draft;auto &points=next[lane?"fader":"motion"];const auto index=size_t(selected[lane]);const auto position=points.at(index).at("position").get<unsigned>();
    if(position==0||position==65536)throw std::invalid_argument("Keep both cycle endpoints; change their values instead");
    points.erase(points.begin()+index);ScratchJSON::decode(next,true);const auto count=points.size();draft=std::move(next);selected[lane]=std::min(selected[lane],int(count)-1);++generation;
  }
  void validate()const{ScratchJSON::decode(draft,true);}
  void accept(const Json &saved,uint64_t submitted){baseline=content(saved);if(generation==submitted)draft=baseline;}
  void discard(){draft=baseline;++generation;}
};
}
