// Portable point/tool semantics are shared with Mac. The platform bridge only
// validates the wire contract and resolves the actual rack parameter catalogue.
namespace {
constexpr std::array<const char *,9> automationCurves={"step","linear","smooth","exponential","logarithmic","step-next","exponential-reverse","logarithmic-reverse","scripted"};
std::string automationID(uint64_t id){return "n"+std::to_string(id);}
uint64_t automationIdentity(const Json &raw){
  const auto value=text(raw,32);need(value.size()>1&&value[0]=='n'&&value[1]!='0',"Invalid automation identity");uint64_t result=0;
  for(size_t i=1;i<value.size();++i){need(value[i]>='0'&&value[i]<='9',"Invalid automation identity");const auto digit=unsigned(value[i]-'0');need(result<=(Tracker::NativeSong::maximumID-1-digit)/10,"Invalid automation identity");result=result*10+digit;}
  need(result>0&&result<Tracker::NativeSong::maximumID,"Invalid automation identity");return result;
}
Tracker::AutomationCurve automationCurve(const Json &raw){const auto name=text(raw,20);const auto found=std::find(automationCurves.begin(),automationCurves.end(),name);need(found!=automationCurves.end(),"Unknown automation curve");return Tracker::AutomationCurve(found-automationCurves.begin());}
std::vector<Tracker::AutomationPoint> automationPoints(const Json &raw,uint32_t length){
  std::vector<Tracker::AutomationPoint> result;for(const auto &p:array(raw,4096)){keys(p,{"position","value","curve","formula"});Tracker::AutomationPoint point{uint32_t(integer(field(p,"position"),0,length-1)),number(field(p,"value"),0,1),automationCurve(p.value("curve",Json("linear")))};
    if(p.contains("formula"))point.formula=Tracker::CurveFormula(text(p.at("formula"),2048));need(point.curve!=Tracker::AutomationCurve::Scripted||!point.formula.source().empty(),"Scripted points require a formula");result.push_back(std::move(point));}
  return result;
}
Json automationPointObjects(const std::vector<Tracker::AutomationPoint> &points){Json result=Json::array();for(const auto &p:points){Json v={{"position",p.position},{"value",p.value},{"curve",automationCurves.at(size_t(p.curve))}};if(!p.formula.source().empty())v["formula"]=p.formula.source();result.push_back(std::move(v));}return result;}
Json patternAutomation(Tracker::Document &document,const std::string &method,const Json &p,const std::function<void()> &stop,const PatternHostHooks &host){
  using namespace Tracker;
  try{
    const auto &song=document.song();auto indexOf=[&]{auto index=uint16_t(integer(field(p,"pattern"),0,UINT16_MAX));need(song.Patterns.IsValidPat(index),"Pattern does not exist");return index;};
    auto slotOf=[&](const std::string &id){return size_t(std::find(host.plugins.begin(),host.plugins.end(),id)-host.plugins.begin());};
    std::map<size_t,Json> catalogs;auto parameters=[&](size_t slot)->const Json &{if(!catalogs.contains(slot))catalogs[slot]=host.parameters?host.parameters(slot):Json::array();return catalogs.at(slot);};
    if(method=="automation.pattern.get"){
      keys(p,{"pattern"});const auto index=indexOf();const auto pattern=document.native().patterns.at(index).id;Json lanes=Json::array();
      for(const auto &lane:document.native().automation)if(lane.pattern==pattern){Json item={{"id",automationID(lane.id)},{"pattern",automationID(pattern)},{"plugin",lane.plugin},{"parameter",lane.parameter},{"enabled",lane.enabled},{"points",automationPointObjects(lane.points)},{"resolved",false}};
        const auto slot=slotOf(lane.plugin);if(slot<host.plugins.size()){item["slot"]=slot;for(const auto &v:parameters(slot))if(v.at("id")==lane.parameter){item["resolved"]=true;item["name"]=v.at("name");item["minimum"]=v.at("min");item["maximum"]=v.at("max");break;}}lanes.push_back(std::move(item));}
      return {{"pattern",index},{"patternID",automationID(pattern)},{"rows",song.Patterns[index].GetNumRows()},{"rowsPerBeat",rowsPerBeat(document,index)},{"unitsPerRow",256},{"lanes",lanes}};
    }
    need(document.editable(),"Document is not editable");auto next=document.native();uint64_t identifier=0;
    if(method=="automation.pattern.set"){
      keys(p,{"pattern","plugin","parameter","enabled","points","dryRun"});const auto index=indexOf();MusicalAutomationLane replacement;
      replacement.pattern=next.patterns.at(index).id;replacement.plugin=text(field(p,"plugin"),128);replacement.parameter=uint32_t(integer(field(p,"parameter"),0,UINT32_MAX));replacement.enabled=!p.contains("enabled")||flag(p,"enabled");
      const auto slot=slotOf(replacement.plugin);need(slot<host.plugins.size(),"Plugin instance does not exist");const auto &catalog=parameters(slot);
      need(std::any_of(catalog.begin(),catalog.end(),[&](const auto &v){return v.at("id")==replacement.parameter;}),"Plugin parameter does not exist or plugin is unavailable");
      if(replacement.enabled){need(!next.performance.controls(replacement.plugin,replacement.parameter),"Remove pattern commands for this parameter before enabling an envelope");need(!host.absoluteAutomation||!host.absoluteAutomation(slot,replacement.parameter),"Remove absolute automation before enabling pattern automation");}
      replacement.points=automationPoints(field(p,"points"),uint32_t(song.Patterns[index].GetNumRows())*256);
      auto existing=std::find_if(next.automation.begin(),next.automation.end(),[&](const auto &v){return v.pattern==replacement.pattern&&v.plugin==replacement.plugin&&v.parameter==replacement.parameter;});
      replacement.id=existing==next.automation.end()?next.makeEntity().id:existing->id;identifier=replacement.id;
      if(existing==next.automation.end())next.automation.push_back(std::move(replacement));else *existing=std::move(replacement);
    }else if(method=="automation.pattern.remove"){
      keys(p,{"lane","dryRun"});identifier=automationIdentity(field(p,"lane"));auto lane=std::find_if(next.automation.begin(),next.automation.end(),[&](const auto &v){return v.id==identifier;});need(lane!=next.automation.end(),"Automation lane does not exist");next.automation.erase(lane);
    }else{
      const bool copy=method=="automation.pattern.copy";need(copy||method=="automation.pattern.transform","Unknown automation operation");
      if(copy)keys(p,{"lane","start","end"});else keys(p,{"lane","operation","start","end","options","dryRun"});
      identifier=automationIdentity(field(p,"lane"));auto lane=std::find_if(next.automation.begin(),next.automation.end(),[&](const auto &v){return v.id==identifier;});need(lane!=next.automation.end(),"Automation lane does not exist");
      auto pattern=std::find_if(next.patterns.begin(),next.patterns.end(),[&](const auto &v){return v.second.id==lane->pattern;});need(pattern!=next.patterns.end(),"Automation pattern does not exist");const auto length=uint32_t(song.Patterns[pattern->first].GetNumRows())*256;
      AutomationTool tool;tool.start=uint32_t(integer(p.value("start",Json(0)),0,length-1));tool.end=uint32_t(integer(p.value("end",Json(length)),1,length));
      if(copy){const auto clip=copyAutomationPoints(lane->points,length,tool.start,tool.end);return {{"span",clip.span},{"points",automationPointObjects(clip.points)},{"unitsPerRow",256}};}
      tool.operation=text(field(p,"operation"),32);const auto options=p.value("options",Json::object());
      if(tool.operation=="shift"){keys(options,{"amount"});auto amount=number(field(options,"amount"),-double(length),length);need(std::floor(amount)==amount,"Shift must use integer musical units");tool.shift=int32_t(amount);}
      else if(tool.operation=="flip-time"||tool.operation=="flip-values")keys(options,{});
      else if(tool.operation=="scale"){keys(options,{"amount","offset"});tool.amount=number(field(options,"amount"),-16,16);tool.offset=number(options.value("offset",Json(0)),-1,1);}
      else if(tool.operation=="ramp"){keys(options,{"from","to","curve"});tool.from=number(options.value("from",Json(0)),0,1);tool.to=number(options.value("to",Json(1)),0,1);tool.curve=automationCurve(options.value("curve",Json("linear")));}
      else if(tool.operation=="sine"){keys(options,{"amplitude","center","cycles","phase","spacing","curve"});tool.amount=number(options.value("amplitude",Json(.5)),0,1);tool.offset=number(options.value("center",Json(.5)),0,1);tool.cycles=number(options.value("cycles",Json(1)),.000001,1024);tool.phase=number(options.value("phase",Json(0)),-360,360);tool.spacing=uint32_t(integer(options.value("spacing",Json(256)),1,length));tool.curve=automationCurve(options.value("curve",Json("linear")));}
      else if(tool.operation=="humanize"){keys(options,{"amount","jitter","seed"});tool.amount=number(options.value("amount",Json(.05)),0,1);tool.jitter=uint32_t(integer(options.value("jitter",Json(0)),0,length));tool.seed=uint32_t(integer(field(options,"seed"),0,UINT32_MAX));}
      else if(tool.operation=="paste"||tool.operation=="insert"){keys(options,{"clip","repeats"});need(!p.contains("end"),"Paste/insert extent comes from clipboard span and repetitions; omit end");tool.repeats=uint32_t(integer(options.value("repeats",Json(1)),1,4096));const auto &clip=field(options,"clip");keys(clip,{"span","points","unitsPerRow"});if(clip.contains("unitsPerRow"))integer(clip.at("unitsPerRow"),256,256);tool.clip.span=uint32_t(integer(field(clip,"span"),1,1048576));tool.clip.points=automationPoints(field(clip,"points"),tool.clip.span);}
      else need(false,"Unknown automation operation");
      const bool dry=flag(p,"dryRun");const auto before=lane->points;auto transformed=transformAutomationPoints(before,length,tool);lane->points=transformed.points;next.validate(song);
      const bool changed=next!=document.native();if(host.validateCandidate)host.validateCandidate(next);if(!dry&&changed){if(stop)stop();document.annotate([&](NativeSong &native){native=std::move(next);});}
      return {{"lane",automationID(identifier)},{"dryRun",dry},{"wouldChange",changed},{"before",automationPointObjects(before)},{"after",automationPointObjects(transformed.points)},{"clippedValues",transformed.clipped}};
    }
    reconcileEnvelopeLinks(next,song);next.validate(song);if(host.validateCandidate)host.validateCandidate(next);const bool dry=flag(p,"dryRun"),changed=next!=document.native();
    if(!dry&&changed){if(stop)stop();document.annotate([&](NativeSong &native){native=std::move(next);});}return {{"lane",automationID(identifier)},{"dryRun",dry},{"wouldChange",changed}};
  }catch(const std::invalid_argument &e){throw Api::ApiError(-32602,e.what());}catch(const std::out_of_range &e){throw Api::ApiError(-32602,e.what());}catch(const Json::exception &e){throw Api::ApiError(-32602,e.what());}
}
}
