#include "NativeSong.hpp"
#include "EnvelopeBank.hpp"
#include "InstrumentEnvelopeTools.hpp"
#include "soundlib/mod_specifications.h"
#include <set>
#include <stdexcept>
namespace Tracker {
namespace {
void need(bool ok,const char *message){if(!ok)throw std::invalid_argument(message);}
const auto &findLane(const NativeSong &n,const EnvelopeTarget &t){auto i=std::find_if(n.automation.begin(),n.automation.end(),[&](const auto &l){return l.id==t.owner;});need(i!=n.automation.end(),"Envelope lane no longer exists");return *i;}
const SignalPatternEnvelope &findGraph(const NativeSong &n,const EnvelopeTarget &t){for(const auto &d:n.signal.library)for(const auto &node:d.nodes)if(node.id==t.owner&&node.kind==SignalNodeKind::Automation)for(const auto &e:node.envelopes)if(e.pattern==t.pattern)return e;throw std::invalid_argument("Graph envelope no longer exists");}
const OpenMPT::InstrumentEnvelope &findInstrument(const NativeSong &n,const OpenMPT::CSoundFile &s,const EnvelopeTarget &t){for(const auto &[slot,i]:n.instruments)if(i.id==t.owner&&s.Instruments[slot]){const auto &i=*s.Instruments[slot];if(t.kind==EnvelopeTargetKind::Volume)return i.VolEnv;if(t.kind==EnvelopeTargetKind::Pan)return i.PanEnv;if(t.kind==EnvelopeTargetKind::Pitch)return i.PitchEnv;}throw std::invalid_argument("Instrument envelope no longer exists");}
uint64_t patternID(const NativeSong &n,const EnvelopeTarget &t){return t.kind==EnvelopeTargetKind::Parameter?findLane(n,t).pattern:t.pattern;}
uint32_t scaled(uint32_t position,uint32_t from,uint32_t to){return from<=1?0:uint32_t(std::llround(double(position)*(to-1)/(from-1)));}
}
void validateEnvelopeShape(const EnvelopeShape &s){
 need(s.span>0&&s.span<=16777216&&s.rowsPerBeat>0&&s.rowsPerBeat<=65536,"Invalid envelope duration or beat division");
 need(!s.points.empty()&&s.points.size()<=4096,"Envelope needs 1 to 4096 points");
 uint32_t last=0;for(size_t i=0;i<s.points.size();++i){const auto &p=s.points[i];need(p.position<s.span&&(!i||p.position>last)&&std::isfinite(p.value)&&p.value>=0&&p.value<=1&&uint8_t(p.curve)<=8,"Envelope points must be ordered, distinct and normalized");need(p.curve!=AutomationCurve::Scripted||!p.formula.source().empty(),"Scripted points require a formula");last=p.position;}
 need((s.flags&~uint8_t(31))==0,"Invalid instrument envelope flags");
 if(s.instrument){for(auto m:s.markers)need(m==UINT32_MAX||m<s.span,"Envelope marker is outside its duration");need(s.markers[0]<=s.markers[1]&&s.markers[2]<=s.markers[3],"Envelope markers must be ordered");for(size_t i=0;i<4;++i)need(s.markers[i]!=UINT32_MAX,"Loop and sustain markers need positions");}
}
std::vector<AutomationPoint> fitEnvelope(const EnvelopeShape &s,uint32_t span){validateEnvelopeShape(s);need(span>0&&span<=16777216,"Invalid target envelope duration");auto result=s.points;uint32_t last=0;for(size_t i=0;i<result.size();++i){result[i].position=scaled(result[i].position,s.span,span);need(!i||result[i].position>last,"Template points collide at this duration; use fewer points or a longer envelope");last=result[i].position;}return result;}
EnvelopeShape captureInstrumentEnvelope(const OpenMPT::InstrumentEnvelope &e){
 EnvelopeShape s;s.instrument=true;s.span=e.empty()?49*256:uint32_t(e.back().tick)*256+1;s.flags=e.dwFlags.GetRaw();
 for(const auto &p:e)s.points.push_back({uint32_t(p.tick)*256,double(p.value)/64,AutomationCurve::Linear});
 if(s.points.empty())s.points.push_back({0,1});
 auto marker=[&](uint8_t i){return i<e.size()?uint32_t(e[i].tick)*256:0;};
 s.markers={marker(e.nLoopStart),marker(e.nLoopEnd),marker(e.nSustainStart),marker(e.nSustainEnd),e.nReleaseNode<e.size()?marker(e.nReleaseNode):UINT32_MAX};return s;
}
OpenMPT::InstrumentEnvelope bakeInstrumentEnvelope(const EnvelopeShape &s,uint32_t span,uint32_t maximumPoints){
 validateEnvelopeShape(s);need(span>0&&span<=65536&&maximumPoints>=2,"Instrument envelope duration or format is unsupported");
 // Compile-time bounded source, evaluated only during an explicit edit. Keep
 // curve corners/markers and simplify between them to at most half a value unit.
 std::vector<double> values(span);for(uint32_t tick=0;tick<span;++tick)values[tick]=automationValue(s.points,span<=1?0:double(tick)*(s.span-1)/(span-1),s.span,s.rowsPerBeat)*64;
 std::set<uint32_t> selected{0,span-1};for(const auto &p:s.points)selected.insert(scaled(p.position,s.span,span));
 if(s.instrument)for(auto m:s.markers)if(m!=UINT32_MAX)selected.insert(scaled(m,s.span,span));
 need(selected.size()<=maximumPoints,"Template has too many corners for this instrument format");
 for(;;){double worst=0;uint32_t at=0;for(auto i=selected.begin(),j=std::next(i);j!=selected.end();++i,++j)for(uint32_t tick=*i+1;tick<*j;++tick){const double a=std::round(values[*i]),b=std::round(values[*j]);const double error=std::abs(values[tick]-(a+(b-a)*double(tick-*i)/(*j-*i)));if(error>worst){worst=error;at=tick;}}
   if(worst<=0.500001)break;need(selected.size()<maximumPoints,"This curve needs more instrument points than the format supports; simplify it or shorten its duration");selected.insert(at);
 }
 OpenMPT::InstrumentEnvelope e;for(auto tick:selected)e.push_back(uint16_t(tick),uint8_t(std::clamp(std::lround(values[tick]),0L,64L)));
 e.dwFlags=OpenMPT::EnvelopeFlags(s.instrument?s.flags:1);
 auto marker=[&](size_t index){return uint8_t(std::distance(selected.begin(),selected.find(scaled(s.markers[index],s.span,span))));};
 if(s.instrument){e.nLoopStart=marker(0);e.nLoopEnd=marker(1);e.nSustainStart=marker(2);e.nSustainEnd=marker(3);e.nReleaseNode=s.markers[4]==UINT32_MAX?ENV_RELEASE_NODE_UNSET:marker(4);}return e;
}
bool envelopeTargetExists(const NativeSong &n,const OpenMPT::CSoundFile &s,const EnvelopeTarget &t){try{if(t.kind==EnvelopeTargetKind::Parameter)(void)findLane(n,t);else if(t.kind==EnvelopeTargetKind::Graph)(void)findGraph(n,t);else(void)findInstrument(n,s,t);if(t.kind<EnvelopeTargetKind::Volume){auto id=patternID(n,t);return std::any_of(n.patterns.begin(),n.patterns.end(),[&](const auto &p){return p.second.id==id&&s.Patterns.IsValidPat(p.first);});}return true;}catch(const std::invalid_argument &){return false;}}
uint32_t envelopeTargetSpan(const NativeSong &n,const OpenMPT::CSoundFile &s,const EnvelopeTarget &t){if(t.kind>=EnvelopeTargetKind::Volume){const auto &e=findInstrument(n,s,t);return e.empty()?49:uint32_t(e.back().tick)+1;}const auto id=patternID(n,t);for(const auto &[slot,p]:n.patterns)if(p.id==id)return uint32_t(s.Patterns[slot].GetNumRows())*256;throw std::invalid_argument("Envelope pattern no longer exists");}
EnvelopeShape captureEnvelope(const NativeSong &n,const OpenMPT::CSoundFile &s,const EnvelopeTarget &t){if(t.kind>=EnvelopeTargetKind::Volume)return captureInstrumentEnvelope(findInstrument(n,s,t));EnvelopeShape shape;shape.span=envelopeTargetSpan(n,s,t);shape.points=t.kind==EnvelopeTargetKind::Parameter?findLane(n,t).points:findGraph(n,t).points;shape.rowsPerBeat=s.m_nDefaultRowsPerBeat;for(const auto &[slot,p]:n.patterns)if(p.id==patternID(n,t)&&s.Patterns[slot].GetOverrideSignature())shape.rowsPerBeat=s.Patterns[slot].GetRowsPerBeat();return shape;}
void applyEnvelope(NativeSong &n,OpenMPT::CSoundFile &s,const EnvelopeTarget &t,const EnvelopeShape &shape,uint32_t span){
 if(t.kind==EnvelopeTargetKind::Parameter)const_cast<MusicalAutomationLane &>(findLane(n,t)).points=fitEnvelope(shape,span);
 else if(t.kind==EnvelopeTargetKind::Graph)const_cast<SignalPatternEnvelope &>(findGraph(n,t)).points=fitEnvelope(shape,span);
 else {need(t.kind!=EnvelopeTargetKind::Pitch||s.GetType()!=OpenMPT::MOD_TYPE_XM,"XM cannot store pitch envelopes");const_cast<OpenMPT::InstrumentEnvelope &>(findInstrument(n,s,t))=bakeInstrumentEnvelope(shape,span,s.GetModSpecifications().envelopePointsMax);}
}
void validateEnvelopeBank(const NativeSong &n,const OpenMPT::CSoundFile &s){
 need(n.envelopeBank.size()<=256&&n.envelopeLinks.size()<=4096,"Envelope bank exceeds its capacity");
 for(const auto &e:n.envelopeBank){need(!e.name.empty()&&e.name.size()<=1024&&e.name.find('\0')==std::string::npos,"Give the envelope template a name");validateEnvelopeShape(e.shape);}
 std::set<EnvelopeTarget> targets;for(const auto &l:n.envelopeLinks){need(targets.insert(l.target).second&&uint8_t(l.target.kind)<=4&&l.target.owner>0&&(l.target.kind==EnvelopeTargetKind::Graph?l.target.pattern>0:l.target.pattern==0),"Invalid or duplicate envelope link");auto e=std::find_if(n.envelopeBank.begin(),n.envelopeBank.end(),[&](const auto &e){return e.id==l.templateID;});need(e!=n.envelopeBank.end(),"Linked envelope template is missing");need(envelopeTargetExists(n,s,l.target),"Linked envelope target is missing");
  const char *message="This envelope is linked. Edit its song template or make it independent in the Envelope Bank first.";
  if(l.target.kind>=EnvelopeTargetKind::Volume)need(sameInstrumentEnvelope(findInstrument(n,s,l.target),bakeInstrumentEnvelope(e->shape,l.span,s.GetModSpecifications().envelopePointsMax)),message);
  else {need(l.span==envelopeTargetSpan(n,s,l.target),"Linked envelope duration no longer matches its pattern");need((l.target.kind==EnvelopeTargetKind::Parameter?findLane(n,l.target).points:findGraph(n,l.target).points)==fitEnvelope(e->shape,l.span),message);}
 }
}
void reconcileEnvelopeLinks(NativeSong &n,const OpenMPT::CSoundFile &s){
 std::erase_if(n.envelopeLinks,[&](const auto &l){return !envelopeTargetExists(n,s,l.target);});
 for(auto &l:n.envelopeLinks)if(l.target.kind<EnvelopeTargetKind::Volume){const auto span=envelopeTargetSpan(n,s,l.target);if(span!=l.span){auto e=std::find_if(n.envelopeBank.begin(),n.envelopeBank.end(),[&](const auto &e){return e.id==l.templateID;});if(e!=n.envelopeBank.end()){auto points=fitEnvelope(e->shape,span);if(l.target.kind==EnvelopeTargetKind::Parameter)const_cast<MusicalAutomationLane &>(findLane(n,l.target)).points=std::move(points);else const_cast<SignalPatternEnvelope &>(findGraph(n,l.target)).points=std::move(points);l.span=span;}}}
}
}
