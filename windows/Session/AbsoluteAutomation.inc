// Absolute points share the plugin history and the existing project wrapper.
// Values are native parameter units; timestamps are canonical 48-kHz frames.
Json PluginOperations::invokeAutomation(const std::string &method,const Json &p) {
  const auto &saved=project_.preserved.at("automation");
  if(method=="automation.get") {
    keys(p,{"offset","limit"});const auto offset=integer(p.value("offset",Json(0)),0,saved.size());
    const auto limit=integer(p.value("limit",Json(1000)),1,4096);Json points=Json::array();
    for(size_t i=size_t(offset);i<std::min<uint64_t>(saved.size(),offset+limit);++i){const auto &v=saved[i];points.push_back({{"slot",v[0]},{"id",v[1]},{"value",v[2]},{"frame",v[3]}});}
    return {{"points",points},{"total",saved.size()},{"offset",offset},{"sampleRate",48000}};
  }
  keys(p,{"slot","id","points"});const auto index=slot(p);const auto id=uint32_t(integer(field(p,"id"),0,UINT32_MAX));
  const auto &raw=field(p,"points");need(raw.is_array()&&raw.size()<=100000,"Automation exceeds 100000 points");
  const auto &rack=project_.preserved.at("plugins");const auto plugin=rack[index].at("instanceID").get<std::string>();
  if(!raw.empty()) {
    need(std::none_of(document_.native().automation.begin(),document_.native().automation.end(),[&](const auto &lane){return lane.enabled&&lane.plugin==plugin&&lane.parameter==id;}),"Disable pattern automation for this parameter before adding absolute automation");
    need(!document_.native().performance.controls(plugin,id),"Remove pattern commands for this parameter before adding absolute automation");
  }
  // Even deletion validates its destination, matching the Mac API contract.
  const auto catalog=editor(index).parameters();const auto parameter=std::find_if(catalog.begin(),catalog.end(),[&](const auto &v){return v.id==id;});
  need(parameter!=catalog.end(),"Plugin parameter does not exist or plugin is unavailable");
  Json lane=Json::array();std::set<uint64_t> seen;
  for(const auto &point:raw){keys(point,{"frame","value"});const auto frame=integer(field(point,"frame"),0,uint64_t(48000)*604800);
    need(seen.insert(frame).second,"Duplicate automation timestamp");lane.push_back(Json::array({index,id,float(number(field(point,"value"),parameter->min,parameter->max)),frame}));}
  const auto chronological=[](const auto &a,const auto &b){return a.at(3)<b.at(3);};std::stable_sort(lane.begin(),lane.end(),chronological);
  Json previous=Json::array(),next=Json::array();for(const auto &point:saved){if(point.at(0)==index&&point.at(1)==id)previous.push_back(point);else next.push_back(point);}
  need(next.size()+lane.size()<=100000,"Automation exceeds 100000 points");std::stable_sort(previous.begin(),previous.end(),chronological);
  if(previous==lane)return {{"points",saved.size()}}; // Preserve Redo and other lanes' equal-time order.
  for(auto &point:lane)next.push_back(std::move(point));std::stable_sort(next.begin(),next.end(),chronological);
  const auto total=next.size();commit(rack,std::move(next));return {{"points",total}};
}
