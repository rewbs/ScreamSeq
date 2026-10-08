namespace ScreamSeq {
inline GraphWorkflowWindow::GraphWorkflowWindow(HWND owner,Callbacks callbacks):NativeToolWindow(owner),callbacks_(std::move(callbacks)) {
  require(bool(callbacks_.context)&&bool(callbacks_.request),"Graph workflow callbacks are required");minimumClientWidth_=780;minimumClientHeight_=620;
  create(L"ScreamSeq.GraphWorkflows",L"Graph workflows",880,710);clampToOwnerWorkArea();
  add(page,L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS);
  add(graph,L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL|CBS_OWNERDRAWFIXED|CBS_HASSTRINGS);
  button(reload,L"Reload");button(close,L"Close");button(check,L"Actions: apply");add(title,L"STATIC",L"",SS_LEFT);add(statusText,L"STATIC",L"",SS_LEFT);
  add(nodes,L"LISTBOX",L"",LBS_EXTENDEDSEL|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT|WS_VSCROLL);
  add(provenanceList,L"LISTBOX",L"",LBS_NOTIFY|LBS_NOINTEGRALHEIGHT|WS_VSCROLL);
  add(details,L"EDIT",L"",ES_READONLY|ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL);
  for(const auto *label:pages_)SendMessageW(controls_.at(page),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));SendMessageW(controls_.at(page),CB_SETCURSEL,0,0);
  combo(group,0,L"Existing group");combo(parent,0,L"Parent group");editField(name,0,L"Group / exported recipe name",L"Group");editField(x,0,L"Position X",L"40");editField(y,0,L"Position Y",L"40");
  actionButton(groupCreate,0,L"Group selected");actionButton(groupUpdate,0,L"Rename / move");actionButton(groupRemove,0,L"Ungroup");actionButton(groupExport,0,L"Save as recipe");
  combo(processor,1,L"Rack effect to copy");combo(edge,1,L"Insert at audio cable");combo(pluginParameter,1,L"Selected effect parameter");editField(pluginValue,1,L"Manual parameter value",L"0");
  actionButton(addEffect,1,L"Copy effect");actionButton(insert,1,L"Insert selected");actionButton(detach,1,L"Detach / heal");actionButton(heal,1,L"Delete / heal");actionButton(bypass,1,L"Bypass / enable");actionButton(setParameter,1,L"Set parameter");
  combo(source,2,L"Existing song source");combo(sourceKind,2,L"New source kind");editField(sourceName,2,L"Source name",L"Modulation");editField(rate,2,L"Rate / cycles per beat",L"1");editField(phase,2,L"Phase / 0–1",L"0");editField(attack,2,L"Attack / seconds",L"0.01");editField(release,2,L"Release / seconds",L"0.1");editField(controller,2,L"MIDI controller / 0–127",L"1");editField(amount,2,L"Amount / 0–1",L"1");combo(audioBus,2,L"Follower audio bus");combo(audioPlugin,2,L"Follower rack effect");editField(output,2,L"Audio output / 0–63",L"0");combo(noteTarget,2,L"Note channel / bus");combo(noteInstrument,2,L"Note instrument");boolField(preFader,2,L"Pre-fader audio tap",false);
  std::vector<std::pair<std::wstring,Json>> kinds;for(const auto *k:{"automation","lfo","follower","random","note-envelope","midi","amount"})kinds.emplace_back(wide(k),k);fill(sourceKind,kinds,"lfo");
  actionButton(sourceAdd,2,L"Add source");actionButton(sourceUpdate,2,L"Update source");actionButton(sourceRemove,2,L"Remove source");actionButton(sourceCurve,2,L"Pattern curve…");
  combo(connection,3,L"Existing modulation cable");combo(modSource,3,L"Song source");combo(modPlugin,3,L"Destination plugin");combo(modParameter,3,L"Stable parameter");editField(minimum,3,L"Minimum contribution / −1…1",L"0");editField(maximum,3,L"Maximum contribution / −1…1",L"0");boolField(enabled,3,L"Enabled",true);boolField(quantized,3,L"Quantize stepped parameter",false);
  actionButton(modSet,3,L"Add / update");actionButton(modRemove,3,L"Remove cable");actionButton(inspectActivity,3,L"Activity…");actionButton(inspectSources,3,L"Existing sources");
  combo(region,4,L"Existing annotation");combo(regionKind,4,L"New annotation kind");fill(regionKind,{{L"Visual frame","frame"},{L"Comment","comment"}},"frame");editField(regionTitle,4,L"Title",L"Frame");editField(regionText,4,L"Comment text",L"",4096);editField(regionX,4,L"Position X",L"40");editField(regionY,4,L"Position Y",L"40");editField(regionWidth,4,L"Width / at least 120",L"320");editField(regionHeight,4,L"Height / at least 60",L"180");editField(regionColor,4,L"RGB color / hexadecimal",L"658b82",6);combo(regionScope,4,L"Processing group scope");boolField(regionCollapsed,4,L"Collapse frame",false);
  actionButton(regionAdd,4,L"Add annotation");actionButton(regionSet,4,L"Update selected");actionButton(regionRemove,4,L"Remove annotation");actionButton(collapseNodes,4,L"Collapse / expand nodes");
  combo(cable,5,L"Audio / modulation cable");combo(reroutePoint,5,L"Existing reroute point");editField(rerouteX,5,L"Point X",L"200");editField(rerouteY,5,L"Point Y",L"140");
  actionButton(rerouteAdd,5,L"Add point");actionButton(rerouteMove,5,L"Move point");actionButton(rerouteRemove,5,L"Remove point");actionButton(rerouteClear,5,L"Clear cable points");
  combo(port,6,L"Prepared signal port");editField(listenGain,6,L"Listen gain / −60…12 dB",L"0");boolField(spectrum,6,L"Spectrum",false);
  actionButton(signalRefresh,6,L"Refresh ports");actionButton(scopeStart,6,L"Scope");actionButton(scopeStop,6,L"Stop scope");actionButton(listenStart,6,L"Listen here");actionButton(listenStop,6,L"Normal mix");actionButton(clearClip,6,L"Clear overload");
  actionButton(provenanceRefresh,7,L"Refresh sources");actionButton(provenanceNext,7,L"Next page");actionButton(provenanceOpen,7,L"Open selected source");
  finish();
}
inline void GraphWorkflowWindow::populate() {
  auto previous=[&](int id){return choice(id,false);};
  std::vector<std::pair<std::wstring,Json>> graphs{{L"Song graph",nullptr}},groups{{L"None",nullptr}},members,effects,plugins,sources{{L"New source",nullptr}},buses{{L"None",""}},instruments{{L"Any instrument",""}};
  for(const auto &d:catalog_.at("library"))graphs.emplace_back(wide(d.at("name").get<std::string>()+" / "+d.at("id").get<std::string>()),d.at("id"));fill(graph,graphs,graph_);
  const auto &d=definition();for(const auto &g:d.value("groups",Json::array()))groups.emplace_back(wide(g.at("name").get<std::string>()+" / "+g.at("id").get<std::string>()),g.at("id"));
  fill(group,groups,previous(group));fill(parent,groups,previous(parent));auto scopes=groups;scopes.front().second="";fill(regionScope,scopes,previous(regionScope));
  if(!graph_.is_null())for(const auto &n:d.at("nodes"))members.emplace_back(wide(n.at("name").get<std::string>()+" / "+n.at("kind").get<std::string>()),n.at("id"));
  for(const auto &p:catalog_.at("plugins")){const auto id=p.at("id");plugins.emplace_back(wide(p.value("name",id.get<std::string>())),id);if(!p.value("isInstrument",false))effects.emplace_back(plugins.back());if(graph_.is_null())members.emplace_back(wide(p.value("name",id.get<std::string>())),"plugin:"+id.get<std::string>());}
  for(const auto &s:catalog_.at("songSources")){sources.emplace_back(wide(s.at("name").get<std::string>()+" / "+s.at("kind").get<std::string>()),s.at("id"));if(graph_.is_null())members.emplace_back(sources.back().first,"source:"+s.at("id").get<std::string>());}
  for(const auto &b:catalog_.at("mixer").at("buses")){buses.emplace_back(wide(b.at("name").get<std::string>()),b.at("id"));if(graph_.is_null())members.emplace_back(buses.back());}
  for(const auto &i:catalog_.at("instruments"))instruments.emplace_back(wide(i.at("name").get<std::string>()),i.at("id"));
  fill(nodes,members,captured_.value("node",Json(nullptr)));fill(processor,effects,previous(processor));fill(modPlugin,plugins,previous(modPlugin));auto taps=plugins;taps.insert(taps.begin(),{L"None",""});fill(audioPlugin,taps,previous(audioPlugin));fill(source,sources,previous(source));fill(modSource,sources,previous(modSource));fill(audioBus,buses,previous(audioBus));fill(noteTarget,buses,previous(noteTarget));fill(noteInstrument,instruments,previous(noteInstrument));
  std::vector<std::pair<std::wstring,Json>> edges,cables,connections{{L"New modulation cable",nullptr}};
  if(!graph_.is_null())for(bool modulation:{false,true}){const auto &items=d.at(modulation?"modulation":"audio");for(size_t i=0;i<items.size();++i){const auto &e=items[i];auto label=wide((modulation?"Control ":"Audio ")+e.at("source").get<std::string>()+" → "+e.at("target").get<std::string>());auto key=Json{{"source",e.at("source")},{"target",e.at("target")},{"output",modulation?0:e.value("output",0u)},{"input",modulation?e.at("parameter").get<uint32_t>():e.value("input",0u)},{"modulation",modulation}};cables.emplace_back(label,key);if(!modulation)edges.emplace_back(label,i);}}
  for(const auto &e:catalog_.at("songModulation")){connections.emplace_back(wide(e.at("source").get<std::string>()+" → "+e.at("plugin").get<std::string>()+" / "+e.at("parameter").dump()),e);if(graph_.is_null())cables.emplace_back(connections.back().first,Json{{"source","source:"+e.at("source").get<std::string>()},{"target","plugin:"+e.at("plugin").get<std::string>()},{"output",0},{"input",e.at("parameter")},{"modulation",true}});}
  fill(edge,edges,previous(edge));fill(cable,cables,previous(cable));fill(connection,connections,previous(connection));
  const auto visual=presentation();std::vector<std::pair<std::wstring,Json>> regions{{L"New annotation",nullptr}};for(const auto &r:visual["regions"])regions.emplace_back(wide(r.at("title").get<std::string>()+" / "+r.at("kind").get<std::string>()),r.at("id"));fill(region,regions,previous(region));
  fill(pluginParameter,{});fill(modParameter,{});fill(reroutePoint,{});
}
inline void GraphWorkflowWindow::loadSelectedFields(int id) {
  require(!pending_,"Operation pending");const auto visual=presentation();
  // Read before suppressing native EN_CHANGE. A pumped request may legitimately
  // deliver newer raw input; setting_ must never hide its generation change.
  Json parameterData;const auto generation=generation_;
  if(id==modPlugin||id==connection){const auto e=id==connection?choice(connection,false):Json(nullptr);const auto plugin=e.is_null()?choice(modPlugin,false):e.at("plugin");if(!plugin.is_null())parameterData=call("plugin.parameters.get",{{"plugin",plugin}},false);}
  if(id==nodes&&page_==1&&!graph_.is_null()){const auto selected=selectedNodes();if(selected.size()==1)parameterData=call("graph.plugin.get",{{"graph",graph_},{"node",selected[0]}},false);}
  require(generation==generation_,"Newer raw fields retained while loading the target");
  setting_=true;struct Guard{bool &v;~Guard(){v=false;}}guard{setting_};
  auto select=[&](int control,const Json &value){const auto &values=choices_[control];const auto i=std::find(values.begin(),values.end(),value);const auto index=i==values.end()?-1:i-values.begin();SendMessageW(controls_.at(control),CB_SETCURSEL,index,0);acceptedSelection_[control]=index;};
  if(id==group){const auto wanted=choice(group,false);if(wanted.is_null())return;for(const auto &g:definition().value("groups",Json::array()))if(g.at("id")==wanted){set(name,g.at("name"));set(x,g.at("x"));set(y,g.at("y"));select(parent,g.value("parent",Json(nullptr)));}}
  if(id==source&&!choice(source,false).is_null())for(const auto &s:catalog_.at("songSources"))if(s.at("id")==choice(source)){set(sourceName,s.at("name"));select(sourceKind,s.at("kind"));for(auto [control,key,fallback]:std::initializer_list<std::tuple<int,const char *,double>>{{rate,"rate",1},{phase,"phase",0},{attack,"attack",.01},{release,"release",.1},{controller,"controller",1},{amount,"amount",1},{output,"output",0}})set(control,s.value(key,Json(fallback)));for(auto [control,key]:std::initializer_list<std::pair<int,const char *>>{{audioBus,"audioBus"},{audioPlugin,"audioPlugin"},{noteTarget,"noteTarget"},{noteInstrument,"noteInstrument"}})select(control,s.value(key,Json("")));select(preFader,s.value("preFader",false));}
  if(id==modPlugin||id==connection){const auto e=id==connection?choice(connection,false):Json(nullptr);if(!e.is_null()){select(modPlugin,e.at("plugin"));select(modSource,e.at("source"));set(minimum,e.at("minimum"));set(maximum,e.at("maximum"));select(enabled,e.at("enabled"));select(quantized,e.at("quantized"));}
    const auto plugin=choice(modPlugin,false);if(!plugin.is_null()){const auto &data=parameterData;std::vector<std::pair<std::wstring,Json>> params;for(const auto &p:data)params.emplace_back(wide(p.at("name").get<std::string>()+" / "+p.at("id").dump()),p.at("id"));fill(modParameter,params,e.is_null()?Json(nullptr):e.at("parameter"));}}
  if(id==nodes&&page_==1&&!graph_.is_null()){const auto selected=selectedNodes();if(selected.size()!=1)return;const auto &data=parameterData;std::vector<std::pair<std::wstring,Json>> params;for(const auto &p:data.at("parameters"))params.emplace_back(wide(p.at("name").get<std::string>()+" / "+p.at("id").dump()),p);fill(pluginParameter,params);}
  if(id==pluginParameter||(id==nodes&&page_==1&&!graph_.is_null())){const auto p=choice(pluginParameter,false);if(!p.is_null())set(pluginValue,p.value("manualValue",p.at("value")));}
  if(id==region){const auto wanted=choice(region,false);if(wanted.is_null())return;for(const auto &r:visual["regions"])if(r.at("id")==wanted){select(regionKind,r.at("kind"));set(regionTitle,r.at("title"));set(regionText,r.at("text"));set(regionX,r.at("x"));set(regionY,r.at("y"));set(regionWidth,r.at("width"));set(regionHeight,r.at("height"));std::wostringstream color;color<<std::hex<<r.at("color").get<uint32_t>();set(regionColor,color.str());select(regionScope,r.at("scope"));select(regionCollapsed,r.at("collapsed"));acceptedNodes_.clear();for(size_t n=0;n<choices_[nodes].size();++n){const bool selected=std::find(r.at("nodes").begin(),r.at("nodes").end(),choices_[nodes][n])!=r.at("nodes").end();SendMessageW(controls_.at(nodes),LB_SETSEL,selected,n);if(selected)acceptedNodes_.push_back(int(n));}}}
  if(id==cable){std::vector<std::pair<std::wstring,Json>> points;const auto key=choice(cable,false);if(!key.is_null())for(const auto &c:visual["cables"]){auto identity=c;identity.erase("points");if(identity==key)for(size_t i=0;i<c.at("points").size();++i)points.emplace_back(L"Point "+std::to_wstring(i+1),i);}fill(reroutePoint,points);}
  if(id==reroutePoint||id==cable){const auto key=choice(cable,false),point=choice(reroutePoint,false);if(!key.is_null()&&!point.is_null())for(const auto &c:visual["cables"]){auto identity=c;identity.erase("points");if(identity==key){const auto &p=c.at("points").at(point.get<size_t>());set(rerouteX,p[0]);set(rerouteY,p[1]);}}}
}
inline void GraphWorkflowWindow::openSongSource(std::string sourceID,std::string plugin,uint32_t parameter) {
  require(available()&&!dirty_,"Apply or Reload the retained graph workflow fields before following another source");
  const auto before=callbacks_.context();const auto generation=generation_;auto data=call("graph.get",{{"includeState",false},{"includeImplicitMixer",true}},false);
  require(generation_==generation,"Newer raw fields retained while following the source");
  for(const auto *key:{"library","plugins","songSources","songModulation","groups"})require(data.contains(key)&&data.at(key).is_array(),"Incomplete graph catalogue");
  if(sourceID=="n0")sourceID.clear();
  if(!sourceID.empty())require(std::any_of(data.at("songSources").begin(),data.at("songSources").end(),[&](const auto &s){return s.at("id")==sourceID;}),"Captured song source is unavailable");
  Json parameters=Json::array();if(!plugin.empty()){
    require(std::any_of(data.at("plugins").begin(),data.at("plugins").end(),[&](const auto &p){return p.at("id")==plugin;}),"Captured source plugin is unavailable");
    parameters=call("plugin.parameters.get",{{"plugin",plugin}},false);require(generation_==generation,"Newer raw fields retained while following the parameter");
    require(std::any_of(parameters.begin(),parameters.end(),[&](const auto &p){return p.at("id")==parameter;}),"Captured modulation parameter is unavailable");
  }
  require(callbacks_.context().at("documentId")==before.at("documentId")&&callbacks_.context().at("revision")==before.at("revision"),"Song changed while following source; retained target unchanged");
  catalog_=std::move(data);captured_=before;graph_=nullptr;page_=plugin.empty()?2:3;setting_=true;
  try {
    populate();SendMessageW(controls_.at(page),CB_SETCURSEL,page_,0);
    auto select=[&](int control,const Json &value){const auto &values=choices_[control];const auto it=std::find(values.begin(),values.end(),value);const auto at=it==values.end()?-1:it-values.begin();SendMessageW(controls_.at(control),CB_SETCURSEL,at,0);acceptedSelection_[control]=at;};
    select(source,sourceID.empty()?Json(nullptr):Json(sourceID));select(modSource,sourceID.empty()?Json(nullptr):Json(sourceID));
    if(!plugin.empty()) {select(modPlugin,plugin);std::vector<std::pair<std::wstring,Json>> values;for(const auto &p:parameters)values.emplace_back(wide(p.at("name").get<std::string>()+" / "+p.at("id").dump()),p.at("id"));fill(modParameter,values,parameter);
      const auto &edges=catalog_.at("songModulation");const auto edge=std::find_if(edges.begin(),edges.end(),[&](const auto &e){return e.at("source")==sourceID&&e.at("plugin")==plugin&&e.at("parameter")==parameter;});
      if(edge!=edges.end()){select(connection,*edge);set(minimum,edge->at("minimum"));set(maximum,edge->at("maximum"));select(enabled,edge->at("enabled"));select(quantized,edge->at("quantized"));}else select(connection,nullptr);
    }
  }catch(...){setting_=false;throw;}setting_=false;++generation_;error_.clear();message_="Captured song source and parameter selected";
  if(plugin.empty()&&!sourceID.empty())loadSelectedFields(source);show();layout();SetFocus(controls_.at(plugin.empty()?source:modSource));
}
inline GraphWorkflowWindow::Json GraphWorkflowWindow::snapshot()const {
  Json fields=Json::object(),bounds=Json::array();for(const auto &f:fields_)fields[std::to_string(f.id)]=f.combo?choice(f.id,false):Json(text(f.id));
  const float scale=96.f/GetDpiForWindow(window_);for(auto [id,h]:controls_){RECT r{};GetWindowRect(h,&r);MapWindowPoints(nullptr,window_,reinterpret_cast<POINT *>(&r),2);bounds.push_back({{"id",id},{"bounds",{r.left*scale,r.top*scale,r.right*scale,r.bottom*scale}},{"visible",IsWindowVisible(h)!=FALSE},{"enabled",IsWindowEnabled(h)!=FALSE}});}
  return {{"visible",visible()},{"pending",pending_},{"dirty",dirty_},{"stale",!captured_.empty()&&!current()},{"captured",captured_},{"graph",graph_},{"page",page_},{"generation",generation_},{"fields",fields},{"selectedNodes",choices_.contains(nodes)?selectedNodes():Json::array()},{"status",message_},{"error",error_},{"signals",signals_},{"scope",scope_},{"provenance",provenance_},{"controls",bounds}};
}
}
