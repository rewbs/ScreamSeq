#include "ParameterProvenance.hpp"
#include <algorithm>
#include <map>
#include <stdexcept>
#include <tuple>

namespace Tracker {
ParameterProvenancePage parameterProvenance(const NativeSong &native,const OpenMPT::CSoundFile &song,
    const std::string &plugin,uint32_t parameter,std::optional<uint16_t> filter,
    ParameterProvenanceRecording recorded,uint32_t offset,uint32_t limit) {
  if(plugin.empty()||plugin.size()>256||plugin.find('\0')!=std::string::npos)throw std::invalid_argument("Choose a stable plugin identity");
  if(!limit||limit>256||offset>100000)throw std::invalid_argument("Invalid provenance page bounds");
  if(filter&&!native.patterns.contains(*filter))throw std::invalid_argument("Pattern no longer exists");
  if(recorded.count>100000||recorded.firstFrame>recorded.lastFrame||recorded.lastFrame>uint64_t(48000)*604800)throw std::invalid_argument("Invalid recorded automation summary");
  std::map<uint64_t,uint16_t> patterns,tracks;
  for(const auto &[i,p]:native.patterns)if(song.Patterns.IsValidPat(i))patterns.emplace(p.id,i);
  for(const auto &[i,t]:native.tracks)tracks.emplace(t.id,i);
  std::vector<ParameterProvenanceSource> all;
  auto visible=[&](uint64_t id){auto p=patterns.find(id);return p!=patterns.end()&&(!filter||p->second==*filter);};
  for(const auto &lane:native.automation)if(lane.plugin==plugin&&lane.parameter==parameter&&visible(lane.pattern)) {
    ParameterProvenanceSource s;s.kind=ParameterProvenanceKind::Envelope;s.key="envelope:n"+std::to_string(lane.id);s.lane=lane.id;s.pattern=lane.pattern;s.patternIndex=patterns.at(lane.pattern);s.enabled=lane.enabled;s.count=uint32_t(lane.points.size());
    if(!lane.points.empty()){s.position=lane.points.front().position*256;s.endPosition=lane.points.back().position*256;}
    all.push_back(std::move(s));
  }
  using Column=std::tuple<uint64_t,uint64_t,uint8_t,uint16_t>;
  std::map<Column,size_t> columns;
  for(const auto &c:native.performance.commands) {
    if(c.kind!=PatternCommandKind::ParameterSet&&c.kind!=PatternCommandKind::ParameterSlide)continue;
    const auto binding=native.performance.bindings.find(c.binding);
    if(binding==native.performance.bindings.end()||binding->second.plugin!=plugin||binding->second.parameter!=parameter||!visible(c.pattern)||!tracks.contains(c.track))continue;
    const Column key{c.pattern,c.track,c.column,c.binding};auto found=columns.find(key);
    if(found==columns.end()) {
      ParameterProvenanceSource s;s.kind=ParameterProvenanceKind::PatternCommands;
      s.key="pattern:n"+std::to_string(c.pattern)+":n"+std::to_string(c.track)+":"+std::to_string(c.column)+":"+std::to_string(c.binding);
      s.pattern=c.pattern;s.patternIndex=patterns.at(c.pattern);s.track=c.track;s.channel=tracks.at(c.track);s.column=c.column;s.binding=c.binding;s.enabled=true;
      s.position=c.position;s.endPosition=c.position;found=columns.emplace(key,all.size()).first;all.push_back(std::move(s));
    }
    auto &s=all[found->second];s.count++;s.position=std::min(s.position,c.position);s.endPosition=std::max(s.endPosition,c.position);
    const ParameterProvenanceCommand item{c.kind,c.position,c.duration,c.value};
    const auto where=std::lower_bound(s.commands.begin(),s.commands.end(),item.position,[](const auto &a,uint32_t p){return a.position<p;});
    s.commands.insert(where,item);if(s.commands.size()>128){s.commands.pop_back();s.omittedCommands++;}
  }
  if(recorded.count) {ParameterProvenanceSource s;s.kind=ParameterProvenanceKind::Recorded;s.key="recorded:"+plugin+":"+std::to_string(parameter);s.count=recorded.count;s.firstFrame=recorded.firstFrame;s.lastFrame=recorded.lastFrame;s.enabled=true;all.push_back(std::move(s));}
  struct Occurrences{std::vector<std::pair<uint16_t,uint32_t>> orders;uint32_t omitted=0;};
  std::map<uint32_t,Occurrences> occurrences;for(const auto &s:all)if(s.patternIndex!=UINT32_MAX)occurrences.try_emplace(s.patternIndex);
  for(uint16_t sequence=0;sequence<song.Order.GetNumSequences();++sequence) {
    const auto &order=song.Order(OpenMPT::SEQUENCEINDEX(sequence));
    for(size_t position=0;position<order.size();++position)if(auto found=occurrences.find(order[position]);found!=occurrences.end()) {
      auto &o=found->second;if(o.orders.size()<256)o.orders.emplace_back(sequence,uint32_t(position));else o.omitted++;
    }
  }
  for(auto &s:all) {s.plugin=plugin;s.parameter=parameter;
    std::sort(s.commands.begin(),s.commands.end(),[](const auto &a,const auto &b){return a.position<b.position;});
    if(auto found=occurrences.find(s.patternIndex);found!=occurrences.end()){s.orders=found->second.orders;s.omittedOrders=found->second.omitted;}
  }
  std::sort(all.begin(),all.end(),[](const auto &a,const auto &b){return std::tie(a.kind,a.patternIndex,a.channel,a.column,a.binding,a.key)<std::tie(b.kind,b.patternIndex,b.channel,b.column,b.binding,b.key);});
  ParameterProvenancePage result;result.total=uint32_t(all.size());result.offset=offset;
  if(offset<all.size()){const auto end=std::min<size_t>(all.size(),size_t(offset)+limit);result.sources.assign(std::make_move_iterator(all.begin()+offset),std::make_move_iterator(all.begin()+end));}
  return result;
}
}
