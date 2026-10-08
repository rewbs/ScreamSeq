// Observation and monitor selection are control-owner operations. The worker
// pins the prepared playback and immutable song while onMain drains its SPSC
// monitor; none of these requests flush vendor editors or create song history.
Json DocumentController::parameterActivityOperation(const std::string &method,const Json &p) {
  const auto require=[](bool ok,const char *why){if(!ok)throw Api::ApiError(-32602,why);};
  require(p.is_object(),"Expected parameter activity object");
  const auto integer=[&](const Json &v,uint64_t low,uint64_t high){
    require(v.is_number(),"Expected a parameter activity number, not a boolean");
    const auto n=v.get<double>();
    require(std::isfinite(n)&&n>=double(low)&&n<=double(high)&&std::floor(n)==n,"Parameter activity integer outside range");
    return uint64_t(n);
  };
  const auto field=[&](const char *key)->const Json &{require(p.contains(key),"Missing parameter activity field");return p.at(key);};
  const auto id=[](uint64_t value){return "n"+std::to_string(value);};
  Json result;onMain([&]{
    auto *activity=playback_&&playbackDocumentGeneration_==generation_?&playback_->chain().parameterActivity():nullptr;
    const auto feedback=playbackHooks_.feedback?playbackHooks_.feedback():PlaybackFeedback{};
    const bool active=feedback.playing||feedback.audioActive;
    const double rate=playback_?playback_->sampleRate():feedback.sampleRate;
    const auto processorObject=[&](const Tracker::ParameterProcessor &v)->Json {
      return {{"key",view_->session.documentId+"/"+v.key},{"name",v.name},{"plugin",v.plugin},{"graph",id(v.graph)},{"node",id(v.node)},
        {"target",id(v.target)},{"instrument",id(v.instrument)},{"channel",v.channel},{"role",v.role},{"bypass",v.bypass},{"parameterCount",v.parameters.size()}};
    };
    const auto processor=[&]()->const Tracker::ParameterProcessor & {
      require(activity!=nullptr,"No prepared processors; add a plugin or start playback");const auto &raw=field("target");
      require(raw.is_string()&&raw.get_ref<const std::string &>().size()<=1200,"Invalid processor key");
      const auto found=std::find_if(activity->processors.begin(),activity->processors.end(),[&](const auto &v){return view_->session.documentId+"/"+v.key==raw.get_ref<const std::string &>();});
      require(found!=activity->processors.end(),"Processor copy is unavailable; start playback and refresh the processor list");return *found;
    };
    if(method=="parameter.activity.targets") {
      keys(p,{});Json targets=Json::array();if(activity)for(const auto &v:activity->processors)targets.push_back(processorObject(v));
      result={{"targets",targets},{"active",active},{"engine",activity?activity->identity:0}};
    } else if(method=="parameter.activity.parameters") {
      keys(p,{"target"});const auto &target=processor();Json parameters=Json::array();
      for(const auto &v:target.parameters)parameters.push_back({{"id",v.id},{"name",v.name},{"min",v.min},{"max",v.max},{"value",v.value},{"unitLabel",v.unitLabel}});
      result={{"target",processorObject(target)},{"parameters",parameters}};
    } else if(method=="parameter.activity.watch") {
      keys(p,{"target","parameter","clear"});const auto &target=processor();const auto parameter=uint32_t(integer(field("parameter"),0,UINT32_MAX));
      const auto found=std::find_if(target.parameters.begin(),target.parameters.end(),[&](const auto &v){return v.id==parameter;});require(found!=target.parameters.end(),"Parameter no longer exists");
      const bool clear=flag(p,"clear");
      try{activity->watch(target.key,parameter,found->value,clear);}
      catch(const std::runtime_error &e){throw Api::ApiError(-32002,e.what());}
      if(!active)activity->begin(0);
      result={{"token",std::to_string(activity->identity)+":"+std::to_string(activity->generation())},{"target",processorObject(target)},{"parameter",parameter}};
    } else if(method=="parameter.activity.get") {
      keys(p,{"after","limit"});const auto after=integer(p.value("after",Json(0)),0,9007199254740991ULL),limit=integer(p.value("limit",Json(2048)),1,8192);
      static constexpr const char *origins[]={"baseline","manual","plugin-editor","recorded","envelope","pattern-set","pattern-slide","graph","reset","graph-source"};
      Json points=Json::array();uint64_t cursor=after,oldest=0,latest=0;
      if(activity){const auto &history=activity->history();if(!history.empty()){oldest=history.front().sequence;latest=history.back().sequence;}
        for(const auto &v:history)if(v.sequence>after){if(points.size()>=limit)break;cursor=v.sequence;const auto &s=v.source;
          points.push_back({{"sequence",v.sequence},{"frame",v.frame},{"seconds",v.frame/rate},{"value",v.value},{"minimum",v.minimum},{"maximum",v.maximum},
            {"pattern",v.pattern},{"order",v.order},{"position",v.position},{"audible",v.audible},{"source",{{"kind",origins[uint8_t(s.kind)]},{"id",id(s.id)},
            {"pattern",s.pattern},{"position",s.position},{"channel",s.channel},{"column",s.column},{"binding",s.binding}}}});
        }}
      result={{"token",activity?std::to_string(activity->identity)+":"+std::to_string(activity->generation()):"none"},{"target",activity&&activity->processor()?processorObject(*activity->processor()):Json()},
        {"parameter",activity?activity->parameter():0},{"points",points},{"cursor",cursor},{"oldest",oldest},{"latest",latest},{"dropped",activity?activity->dropped():0},
        {"active",active},{"sampleRate",rate},{"resolutionSeconds",.001}};
    } else if(method=="parameter.activity.sources") {
      keys(p,{"target","parameter"});const auto &target=processor();const auto parameter=uint32_t(integer(field("parameter"),0,UINT32_MAX));
      require(std::any_of(target.parameters.begin(),target.parameters.end(),[&](const auto &v){return v.id==parameter;}),"Parameter no longer exists");
      const auto &native=document_->native();Json sources=Json::array();size_t omitted=0;
      const auto add=[&](Json value){if(sources.size()<512)sources.push_back(std::move(value));else ++omitted;};
      const auto patternIndex=[&](uint64_t value){for(const auto &[index,entry]:native.patterns)if(entry.id==value)return uint32_t(index);return UINT32_MAX;};
      const auto decimal=[](double value,int precision){char buffer[64]{};snprintf(buffer,sizeof(buffer),"%.*f",precision,value);return std::string(buffer);};
      add({{"kind","baseline"},{"title","Saved / manual parameter value"},{"enabled",true},{"plugin",target.plugin},{"graph",id(target.graph)},{"node",id(target.node)}});
      if(!target.plugin.empty()) {
        for(const auto &lane:native.automation)if(lane.plugin==target.plugin&&lane.parameter==parameter)
          add({{"kind","envelope"},{"title","Pattern "+std::to_string(patternIndex(lane.pattern))+" envelope · "+std::to_string(lane.points.size())+" points"},{"id",id(lane.id)},
            {"pattern",patternIndex(lane.pattern)},{"enabled",lane.enabled},{"plugin",target.plugin},{"parameter",parameter}});
        for(const auto &c:native.performance.commands){const auto binding=native.performance.bindings.find(c.binding);if(binding==native.performance.bindings.end()||binding->second.plugin!=target.plugin||binding->second.parameter!=parameter)continue;
          uint16_t channel=UINT16_MAX;for(const auto &[index,track]:native.tracks)if(track.id==c.track)channel=index;
          const bool slide=c.kind==Tracker::PatternCommandKind::ParameterSlide;
          add({{"kind",slide?"pattern-slide":"pattern-set"},{"title",std::string(slide?"PL":"PS")+" · Pattern "+std::to_string(patternIndex(c.pattern))+" · row "+decimal(double(c.position)/65536,3)+" · channel "+std::to_string(unsigned(channel)+1)+" · FX "+std::to_string(unsigned(c.column)+1)},
            {"pattern",patternIndex(c.pattern)},{"position",c.position},{"channel",channel},{"column",c.column},{"binding",c.binding},{"enabled",true}});
        }
        const auto &rack=project_.preserved.at("plugins");const auto found=std::find_if(rack.begin(),rack.end(),[&](const auto &v){return v.at("instanceID")==target.plugin;});const auto slot=size_t(found-rack.begin());
        const auto &automation=project_.preserved.at("automation");const auto count=std::count_if(automation.begin(),automation.end(),[&](const auto &v){return v.at(0)==slot&&v.at(1)==parameter;});
        add({{"kind","recorded"},{"title","Recorded automation · "+std::to_string(count)+" timed points"},{"count",count},{"enabled",count>0},{"plugin",target.plugin},{"parameter",parameter}});
        bool base=false;for(const auto &m:native.signal.songModulation)if(m.plugin==target.plugin&&m.parameter==parameter){
          const auto source=std::find_if(native.signal.songSources.begin(),native.signal.songSources.end(),[&](const auto &v){return v.node.id==m.source;});if(source==native.signal.songSources.end())continue;
          if(!base&&m.enabled){add({{"kind","graph-source"},{"scope","song"},{"id",id(0)},{"title","Unmodulated base · manual or automated value"},{"enabled",true},{"graph",nullptr},{"plugin",target.plugin},{"parameter",parameter}});base=true;}
          add({{"kind","graph-source"},{"scope","song"},{"id",id(m.source)},{"title",source->node.name+" · contribution "+decimal(m.minimum,4)+" … "+decimal(m.maximum,4)+(m.quantized?" · discrete values":"")},
            {"enabled",m.enabled},{"graph",nullptr},{"node",id(m.source)},{"plugin",m.plugin},{"parameter",m.parameter},{"minimum",m.minimum},{"maximum",m.maximum},{"quantized",m.quantized},{"sourceKind",int(source->node.kind)}});
        }
      } else for(const auto &graph:native.signal.library)if(graph.id==target.graph){
        bool base=false;for(size_t index=0;index<graph.modulation.size();++index){const auto &m=graph.modulation[index];if(m.target!=target.node||m.parameter!=parameter)continue;
          const auto source=std::find_if(graph.nodes.begin(),graph.nodes.end(),[&](const auto &v){return v.id==m.source;});if(source==graph.nodes.end())continue;
          if(!base&&m.enabled){add({{"kind","graph-source"},{"id",id(0)},{"title","Modulation base · "+decimal(m.base,4)},{"enabled",true},{"graph",id(graph.id)},{"node",id(m.target)},{"connection",index},{"value",m.base}});base=true;}
          add({{"kind","graph-source"},{"id",id(m.source)},{"title",source->name+" · contribution "+decimal(m.minimum,4)+" … "+decimal(m.maximum,4)},{"enabled",m.enabled},
            {"graph",id(graph.id)},{"node",id(m.source)},{"connection",index},{"sourceKind",int(source->kind)}});
        }
        for(const auto &c:native.signal.commands)if(c.target==target.target&&(c.graph==target.graph||c.kind==Tracker::SignalCommandKind::Clear))
          add({{"kind","graph-command"},{"title","Graph activity / amount / wet · Pattern "+std::to_string(patternIndex(c.pattern))+" · row "+decimal(double(c.position)/65536,3)},
            {"pattern",patternIndex(c.pattern)},{"position",c.position},{"target",id(c.target)},{"column",c.column},{"graph",id(graph.id)},{"enabled",true}});
      }
      result={{"sources",sources},{"omitted",omitted},{"rule",target.graph?
        "Each graph copy has independent state. Modulation adds contributions to a base, then clamps to 0…1. Inactive copies still process silence; wet and activity affect audibility, not the parameter value.":
        "Enabled envelopes, parameter pattern commands and recorded automation are mutually exclusive base controls. Song modulation adds its contributions to that base, then clamps to 0…1 and rounds discrete targets. Manual edits may be overwritten by base automation. Plugin-internal modulation is not exposed."}};
    } else throw Api::ApiError(-32601,"Unknown parameter activity method");
  });return result;
}
