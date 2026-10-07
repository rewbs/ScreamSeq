#include "windows/App/PatternFields.hpp"
#include <iostream>
#include <stdexcept>
using namespace ScreamSeq;
static void check(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
int main(){try{
  PatternFields::Column empty;empty.include(nlohmann::json::array({PatternFields::trackerField()}));
  check(empty.widths.size()==1&&std::abs(empty.widths[0]-28.8f)<.001f&&empty.width()<60,"Legacy/empty FX reserves four hexadecimal characters only");
  const auto catalog=PatternJSON::catalog();
  for(const auto &descriptor:catalog){auto command=PatternFields::defaults(descriptor);const auto fields=PatternFields::fields(descriptor,false);
    if(descriptor.value("native",std::string())=="row-length")for(const auto &f:fields)if(f.at("key")=="offset")check(f.at("maximum")==0,"RL catalog must expose its row-boundary-only offset");
    for(const auto &field:fields){const auto value=PatternFields::get(command,field);if(value.is_number())for(unsigned rpb:{3u,4u,7u})for(bool rows:{false,true}){
      const auto displayed=PatternFields::displayNumber(value.get<double>(),field,rpb,rows,command);const auto restored=PatternFields::rawNumber(displayed,field,rpb,rows,command);
      check(std::abs(restored-value.get<double>())<1e-10,"Displayed musical units must round-trip at each pattern signature");
    }}
    PatternFields::Column geometry;const auto inlineFields=PatternFields::fields(descriptor,true);geometry.include(inlineFields);
    nlohmann::json keys=nlohmann::json::array();for(const auto &f:inlineFields)keys.push_back(f.at("key"));
    const auto native=descriptor.value("native",std::string());
    if(descriptor.at("kind")=="nudge-forward"||descriptor.at("kind")=="nudge-reverse"){
      check(keys==nlohmann::json::array({"value","durationBeats"})&&descriptor.at("durationStorage")=="durationBeats"&&descriptor.at("durationUnit")=="beats"&&!command.contains("duration")&&command.at("durationBeats")==1,"NF/NR default to one beat without a hidden row duration");
      const auto beatField=inlineFields.at(1);PatternFields::set(command,beatField,.123456789012345);
      for(unsigned rpb:{3u,4u,7u}){const auto displayed=PatternFields::displayNumber(.123456789012345,beatField,rpb,true,command);check(std::abs(PatternFields::rawNumber(displayed,beatField,rpb,true,command)-.123456789012345)<1e-15,"Nudge Rows presentation preserves fractional beats without integer rounding");}
    }
    if(native=="vibrato"||native=="tremolo"||native=="panbrello")check(keys==nlohmann::json::array({"depth","rate","duration"}),"Modulation defaults to three musical slots; advanced controls remain in the full descriptor");
    if(native=="arpeggio")check(keys==nlohmann::json::array({"offset1","offset2","rate","duration"}),"Arpeggio has four common inline slots");
    if(native=="tremor")check(keys==nlohmann::json::array({"onBeats","offBeats","depth","duration"}),"Tremor phase does not reserve an inline slot");
    if(native=="retrigger")check(keys==nlohmann::json::array({"intervalBeats","count","volumeFactor","volumeStep"}),"Retrigger retains all four musical controls");
    if(native=="scratch")check(keys==nlohmann::json::array({"gesture","beats","travelMs","repeats"})&&command["parameters"]["gesture"]==1&&command["parameters"]["beats"]==1&&command["parameters"]["travelMs"]==200&&command["parameters"]["reverse"]==false,"Scratch uses typed gesture, beat duration, travel and repeat slots; reverse is advanced");
    if(native=="scratch-stop")check(keys==nlohmann::json::array({"offset"})&&command["parameters"].empty()&&!command.contains("duration"),"Scratch stop exposes its exact onset offset without gesture parameters or duration");
    for(size_t i=0;i<inlineFields.size();++i){check(geometry.hit(geometry.start(i)+geometry.widths[i]/2)==i,"Drawing and hit testing must select the same parameter slot");if(i)check(geometry.start(i)>=geometry.start(i-1)+geometry.widths[i-1],"Inline parameter slots overlap");}
  }
  Tracker::PatternCommand vibrato;vibrato.kind=Tracker::PatternCommandKind::Native;vibrato.native=Tracker::NativePatternOp::Vibrato;vibrato.arguments=Tracker::nativePatternDefaults(vibrato.native);vibrato.arguments[0]=.123456789;vibrato.arguments[1]=7.375;vibrato.arguments[3]=2;vibrato.duration=32768;vibrato.position=65536+12345;
  auto payload=PatternFields::command(vibrato);const auto original=payload;const auto descriptor=PatternFields::descriptor(payload,catalog);
  check(PatternFields::fields(descriptor,false).size()==8,"Advanced modulation fields remain editable without changing the payload schema");
  const auto rate=*std::find_if(descriptor.at("parameters").begin(),descriptor.at("parameters").end(),[](const auto &f){return f.at("key")=="rate";});
  for(unsigned rpb:{3u,4u,7u}){
    check(PatternFields::displayNumber(7.375,rate,rpb,true,payload)==7.375/rpb,"Beat rate displays cycles per row using the pattern signature");
    check(std::abs(PatternFields::rawNumber(7.375/rpb,rate,rpb,true,payload)-7.375)<1e-12,"Editing cycles per row converts back to stored cycles per beat");
  }
  check(PatternFields::text(payload,rate,4,false,true)=="7.375/b"&&PatternFields::text(payload,rate,4,true,true)=="1.8438/r","Grid rate labels distinguish beat and row rates compactly");
  check(PatternFields::unit(rate,true,payload)=="cycles/row","Inspector explains the compact row-rate suffix");
  auto hz=payload;hz["parameters"]["rateMode"]="hz";
  check(PatternFields::displayNumber(7.375,rate,7,true,hz)==7.375&&PatternFields::rawNumber(7.375,rate,7,true,hz)==7.375,"Hz ignores the beats/rows preference");
  check(PatternFields::text(hz,rate,7,true,true)=="7.375Hz"&&PatternFields::unit(rate,true,hz)=="Hz","Hz remains explicit in both editors");
  for(const auto &f:descriptor.at("parameters"))if(f.at("key")=="rate")PatternFields::set(payload,f,3.25);
  auto expected=original;expected["parameters"]["rate"]=3.25;check(payload==expected,"One inline parameter edit must preserve all unrelated typed values and timing");
  auto decoded=vibrato;PatternJSON::decode(decoded,payload);check(decoded.arguments[1]==3.25&&decoded.arguments[0]==vibrato.arguments[0]&&decoded.duration==32768,"Typed native value round-trip");
  const auto offset=*std::find_if(descriptor.at("parameters").begin(),descriptor.at("parameters").end(),[](const auto &f){return f.at("key")=="offset";});
  for(unsigned rpb:{3u,7u}){const auto displayed=PatternFields::displayNumber(12345,offset,rpb,false);check(PatternFields::rawNumber(displayed,offset,rpb,false)==12345,"Beat conversion must preserve exact native row units");}
  bool rejected=false;try{PatternFields::rawNumber(1,offset,4,false);}catch(const std::invalid_argument&){rejected=true;}check(rejected,"Offset cannot silently clamp into a different row");
  std::cout<<"PASS native field units, geometry, typed edits and precise offset preservation\n";return 0;
}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
