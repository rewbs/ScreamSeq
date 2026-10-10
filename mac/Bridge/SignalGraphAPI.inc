    } else if ([method isEqual:@"graph.listen.get"]) {
      allowed(@[]);result=[self listenTelemetry];
    } else if ([method isEqual:@"graph.listen.set"]) {
      allowed(@[@"port",@"gainDB"]);require(p[@"port"]!=nil,"Specify an observed output port or null to restore the normal mix");
      auto *observation=_audio->signalObservation();uint32_t token=0;
      const double db=p[@"gainDB"]?number(p[@"gainDB"],-60,12):0;
      if(p[@"port"]!=NSNull.null) {
        const auto key=string(p[@"port"],512);
        if(observation)for(size_t i=0;i<observation->ports.size();++i)if(observation->ports[i].key==key.UTF8String&&observation->ports[i].kind=="audio"&&observation->ports[i].output&&observation->available(uint32_t(i+1))){token=uint32_t(i+1);break;}
        require(token!=0,"Choose an observed audio output in the prepared graph");
      } else require(!p[@"gainDB"],"Monitor gain requires a selected output");
      if(observation)observation->listen.select(token,float(std::pow(10.,db/20.)));
      result=[self listenTelemetry];
    } else if ([method isEqual:@"graph.scope.watch"]) {
      allowed(@[@"port"]);require(p[@"port"]!=nil,"Specify an observed port or null to stop capture");
      auto *observation=_audio->signalObservation();uint32_t token=0;
      if(p[@"port"]!=NSNull.null) {
        const auto key=string(p[@"port"],512);
        if(observation)for(size_t i=0;i<observation->ports.size();++i)if(observation->ports[i].key==key.UTF8String&&observation->ports[i].kind=="audio"&&observation->available(uint32_t(i+1))){token=uint32_t(i+1);break;}
        require(token!=0,"Observed scope port does not exist in the prepared graph");
      }
      if(observation)observation->scope.watch(token);
      result=@{@"port":p[@"port"],@"capturing":@(token!=0)};
    } else if ([method isEqual:@"graph.scope.get"]) {
      allowed(@[@"spectrum"]);const bool spectrum=p[@"spectrum"]&&boolean(p[@"spectrum"]);
      auto *observation=_audio->signalObservation();auto capture=observation?observation->scope.snapshot(spectrum):SignalScopeSnapshot{};
      const auto reading=observation?observation->read(capture.token):SignalPortReading{};const bool fresh=reading.fresh&&reading.generation==capture.routeGeneration;if(!fresh){capture.waveform.clear();capture.spectrum.clear();capture.frames=capture.fftFrames=0;}
      auto waveform=[NSMutableArray array],bins=[NSMutableArray array];
      for(const auto &v:capture.waveform)[waveform addObject:@{@"first":@(v.first),@"last":@(v.last),@"minimum":@[@(v.minimum[0]),@(v.minimum[1])],@"maximum":@[@(v.maximum[0]),@(v.maximum[1])]}];
      for(auto v:capture.spectrum)[bins addObject:@(v)];
      const auto port=observation&&capture.token&&capture.token<=observation->ports.size()?@(observation->ports[capture.token-1].key.c_str()):(id)NSNull.null;
      result=@{@"port":port,@"available":@(reading.available),@"fresh":@(fresh),@"active":@(_audio->active()),@"generation":@(capture.generation),@"routeGeneration":@(capture.routeGeneration),@"sampleRate":@(_audio->sampleRate()),
        @"frames":@(capture.frames),@"through":@(capture.through),@"dropped":@(capture.dropped),@"invalid":@(capture.invalid),
        @"waveform":waveform,@"spectrum":bins,@"fftFrames":@(capture.fftFrames),@"window":@"Hann",@"downsample":@"min/max buckets; capture is full rate"};
    } else if ([method isEqual:@"graph.signal.get"]) {
      allowed(@[]);result=[self signalTelemetry];
    } else if ([method isEqual:@"graph.signal.clear"]) {
      allowed(@[@"port"]);const auto key=string(p[@"port"],512);
      auto *observation=_audio->signalObservation();bool found=false;
      if(observation)for(size_t i=0;i<observation->ports.size();++i)if(observation->ports[i].key==key.UTF8String){observation->clear(uint32_t(i+1));found=true;break;}
      require(found,"Observed signal port does not exist in the prepared graph");result=@{@"clearing":@YES};
#include "NoteActivityAPI.inc"
#include "GraphProvenanceAPI.inc"
#include "GroupBypassReadAPI.inc"
#include "GraphTrimReadAPI.inc"
    } else if ([method isEqual:@"graph.get"]) {
      allowed(@[@"includeState",@"includeImplicitMixer"]);NSMutableDictionary *data=[encodeSignalGraph(_document->native().signal,!p[@"includeState"]||boolean(p[@"includeState"])) mutableCopy];

      NSMutableArray *plugins=[NSMutableArray array];for(size_t slot=0;slot<_plugins.size();++slot){const auto &p=_plugins[slot];NSMutableDictionary *item=[descriptorDictionary(p.descriptor) mutableCopy];item[@"id"]=@(p.instanceID.c_str());item[@"slot"]=@(slot);item[@"bypass"]=@(p.bypass);item[@"audioLayout"]=@(p.audioLayout.c_str());NSMutableArray *instruments=[NSMutableArray array];for(const auto &a:pluginAssignments(p))[instruments addObject:@(a.instrument)];item[@"instruments"]=instruments;NSMutableArray *assignments=[NSMutableArray array];for(const auto &a:pluginAssignments(p)){const auto found=_document->native().instruments.find(a.instrument);if(found!=_document->native().instruments.end())[assignments addObject:@{@"instrument":@(a.instrument),@"instrumentID":nativeID(found->second.id),@"channel":@(a.channel)}];}item[@"assignments"]=assignments;item[@"audioBuses"]=pluginBusDictionaries(_audio->pluginBuses(slot));[plugins addObject:item];}data[@"plugins"]=plugins;
      NativeSong implicit;
      const bool showImplicit=p[@"includeImplicitMixer"]&&boolean(p[@"includeImplicitMixer"])&&!_document->native().mixer.active();
      if(showImplicit){implicit.tracks=_document->native().tracks;implicit.nextID=_document->native().nextID;implicit.masterID=_document->native().masterID;implicit.mixer=_document->native().mixer;implicit.ensureMixer();}
      data[@"mixer"]=encodeMixer(showImplicit?implicit.mixer:_document->native().mixer);data[@"implicitMixer"]=@(showImplicit);data[@"unitsPerRow"]=@65536;
      NSMutableArray *patterns=[NSMutableArray array];for(const auto &[index,entity]:_document->native().patterns)if(song.Patterns.IsValidPat(index))[patterns addObject:@{@"index":@(index),@"id":nativeID(entity.id),@"rows":@(song.Patterns[index].GetNumRows()),@"rowsPerBeat":@(preciseRowsPerBeat(song,index)),@"name":@(entity.name.c_str())}];data[@"patterns"]=patterns;
      NSMutableArray *instrumentList=[NSMutableArray array];for(const auto &[index,entity]:_document->native().instruments){bool plugin=false;for(const auto &p:_plugins)for(const auto &a:pluginAssignments(p))plugin|=a.instrument==index;[instrumentList addObject:@{@"index":@(index),@"id":nativeID(entity.id),@"name":songString(song,song.GetInstrumentName(index)),@"plugin":@(plugin)}];}data[@"instruments"]=instrumentList;
      data[@"activity"]=encodeSignalActivity(_audio->graphActivity());
      result=data;
    } else if ([method isEqual:@"graph.selection.copy"]) {
      allowed(@[@"graph",@"nodes"]);const auto graphIdentity=decodeNativeID(p[@"graph"]);const auto &library=_document->native().signal.library;
      const auto found=std::find_if(library.begin(),library.end(),[&](const auto &d){return d.id==graphIdentity;});require(found!=library.end(),"Subgraph no longer exists");
      std::vector<uint64_t> ids;for(id value:array(p[@"nodes"],128))ids.push_back(decodeNativeID(value));
      try{result=@{@"fragment":encodeSignalDefinition(copySignalSelection(*found,ids)),@"version":@1};}catch(const std::invalid_argument &e){throw Error(-32602,e.what());}
    } else if ([method isEqual:@"graph.automation.get"]) {
      allowed(@[@"graph",@"node",@"pattern"]);require(p[@"graph"]!=nil,"Specify a subgraph identity or null for a song source");
      const auto id=p[@"graph"]==NSNull.null?0:decodeNativeID(p[@"graph"]),nodeID=decodeNativeID(p[@"node"]);
      const SignalNode *node=nullptr;
      if(id) {const auto &library=_document->native().signal.library;const auto d=std::find_if(library.begin(),library.end(),[&](const auto &v){return v.id==id;});require(d!=library.end(),"Subgraph does not exist");
        const auto found=std::find_if(d->nodes.begin(),d->nodes.end(),[&](const auto &v){return v.id==nodeID;});if(found!=d->nodes.end())node=&*found;
      } else {for(const auto &source:_document->native().signal.songSources)if(source.node.id==nodeID)node=&source.node;}
      require(node&&node->kind==SignalNodeKind::Automation,"Choose an automation source node");
      const auto index=uint16_t(integer(p[@"pattern"],0,UINT16_MAX));require(song.Patterns.IsValidPat(index),"Pattern does not exist");const auto pattern=_document->native().patterns.at(index).id;
      const auto lane=std::find_if(node->envelopes.begin(),node->envelopes.end(),[&](const auto &v){return v.pattern==pattern;});NSMutableArray *points=[NSMutableArray array];if(lane!=node->envelopes.end())for(const auto &point:lane->points)[points addObject:encodeAutomationPoint(point)];
      result=@{@"graph":p[@"graph"],@"node":nativeID(nodeID),@"pattern":@(index),@"patternID":nativeID(pattern),@"rows":@(song.Patterns[index].GetNumRows()),@"rowsPerBeat":@(preciseRowsPerBeat(song,index)),@"unitsPerRow":@256,@"enabled":@(lane==node->envelopes.end()||lane->enabled),@"points":points};
    } else if ([method isEqual:@"graph.controller"]) {
      allowed(@[@"controller",@"value"]);result=@{@"delivered":@(_audio->graphController(uint8_t(integer(p[@"controller"],0,127)),uint8_t(integer(p[@"value"],0,127))))};
    } else if ([method hasPrefix:@"graph."]) {
      NativeSong next=_document->native();auto &graph=next.signal;
      decltype(_pluginPathHints.graph) clonedPathHints;
      const bool dry=p[@"dryRun"]?boolean(p[@"dryRun"]):false;
      uint64_t affected=0,nodeID=0,groupID=0;NSDictionary *clipboardFragment=nil;
      auto definition=[&](id raw)->SignalDefinition &{auto id=decodeNativeID(raw);auto found=std::find_if(graph.library.begin(),graph.library.end(),[&](const auto &d){return d.id==id;});require(found!=graph.library.end(),"Subgraph does not exist");return *found;};
      auto ensureMixer=[&](){next.ensureMixer();};
      auto bus=[&](id raw){ensureMixer();const auto id=decodeNativeID(raw);require(std::any_of(next.mixer.buses.begin(),next.mixer.buses.end(),[&](const auto &b){return b.id==id;}),"Graph target bus does not exist");return id;};
      auto newNumber=[&](){for(uint16_t number=1;number<=999;++number)if(std::none_of(graph.library.begin(),graph.library.end(),[&](const auto &d){return d.number==number;}))return number;throw Error(-32602,"No free subgraph number");};
#include "SongModulationAPI.inc"
#include "GraphPresentationAPI.inc"
#include "NoteRoutingAPI.inc"
#include "GroupBypassAPI.inc"
#include "GraphTrimWriteAPI.inc"
      if([method isEqual:@"graph.audio.connection.set"]) {
        allowed(@[@"source",@"target",@"output",@"input",@"gainDB",@"enabled",@"replace",@"dryRun"]);ensureMixer();auto value=decodeSignalStageCable(p);std::optional<SignalStageConnection> old;
        if(p[@"replace"]){auto r=object(p[@"replace"]);keys(r,@[@"source",@"target",@"output",@"input"]);old=decodeSignalStageCable(r);}
        const auto &key=old?*old:value;const auto previous=std::find_if(graph.stageConnections.begin(),graph.stageConnections.end(),[&](const auto &r){return r.source==key.source&&r.target==key.target&&r.output==key.output&&r.input==key.input;});
        if(previous!=graph.stageConnections.end()){if(!p[@"gainDB"])value.gainDB=previous->gainDB;if(!p[@"enabled"])value.enabled=previous->enabled;}
        for(bool input:{false,true}){const auto &e=input?value.target:value.source;if(e.stage){bus(nativeID(e.stage));continue;}
          const auto found=std::find_if(_plugins.begin(),_plugins.end(),[&](const auto &p){return p.instanceID==e.plugin;});require(found!=_plugins.end(),"Plugin endpoint no longer exists");
          const bool instrument=found->descriptor.instrument||found->descriptor.type==kAudioUnitType_MusicDevice;
          const auto ports=_audio->pluginBuses(size_t(found-_plugins.begin()));const auto port=input?value.input:value.output;require(std::any_of(ports.begin(),ports.end(),[&](const auto &b){return b.input==input&&b.index==port&&b.supported;}),"Choose an available physical plugin audio port");
          if(!instrument)rootDetachedMixerPlugin(next.mixer,e.plugin,[&]{return next.makeEntity().id;});
        }
        try{setSignalStageConnection(graph,value,old?&*old:nullptr);}catch(const std::invalid_argument &e){throw Error(-32602,e.what());}
        // Validate the complete typed DAG even on dry runs. Declared stage ports
        // are checked above; the real host rechecks its prepared catalogue.
        std::vector<MixerProcessorInfo> processors;std::vector<uint64_t> tracks;for(const auto &[i,t]:next.tracks)tracks.push_back(t.id);
        for(size_t slot=0;slot<_plugins.size();++slot){const auto &p=_plugins[slot];uint64_t inputs=0,outputs=1;uint32_t count=1;for(const auto &b:_audio->pluginBuses(slot))if(b.supported){if(b.input)inputs|=uint64_t(1)<<b.index;else{outputs|=uint64_t(1)<<b.index;count=std::max(count,b.index+1);}}processors.push_back({p.instanceID,0,0,p.descriptor.instrument||p.descriptor.type==kAudioUnitType_MusicDevice,false,count,outputs,inputs});processors.back().scheduledSource=processors.back().instrument;}
        for(const auto &b:next.mixer.buses){const auto in=signalStagePorts(graph,b.id,true),out=signalStagePorts(graph,b.id,false);if(in.empty()&&out.empty()&&std::none_of(graph.assignments.begin(),graph.assignments.end(),[&](const auto &a){return a.target==b.id;})&&std::none_of(graph.commands.begin(),graph.commands.end(),[&](const auto &c){return c.target==b.id&&(c.kind==SignalCommandKind::Row||c.kind==SignalCommandKind::Start);}))continue;uint64_t inputs=0,outputs=1;uint32_t count=1;for(auto p:in)inputs|=uint64_t(1)<<p;for(auto p:out){outputs|=uint64_t(1)<<p;count=std::max(count,p+1);}processors.push_back({signalBusIdentity(b.id),0,0,false,false,count,outputs,inputs});processors.back().scheduledSource=processors.back().instrument;}
        try{compileMixer(signalRoutingGraph(next.mixer,graph),tracks,processors,uint32_t(_audio->sampleRate()));}catch(const std::invalid_argument &e){throw Error(-32602,e.what());}
      } else if([method isEqual:@"graph.selection.cut"]) {
        allowed(@[@"graph",@"nodes",@"dryRun"]);affected=decodeNativeID(p[@"graph"]);std::vector<uint64_t> ids;for(id value:array(p[@"nodes"],128))ids.push_back(decodeNativeID(value));
        try{clipboardFragment=encodeSignalDefinition(cutSignalSelection(next,affected,ids));}catch(const std::invalid_argument &e){throw Error(-32602,e.what());}
      } else if([method isEqual:@"graph.selection.paste"]||[method isEqual:@"graph.selection.duplicate"]) {
        const bool duplicate=[method isEqual:@"graph.selection.duplicate"];allowed(duplicate?@[@"graph",@"nodes",@"parent",@"x",@"y",@"dryRun"]:@[@"graph",@"fragment",@"patternMap",@"parent",@"x",@"y",@"dryRun"]);
        affected=decodeNativeID(p[@"graph"]);SignalDefinition fragment;
        std::map<uint64_t,uint64_t> patterns;
        if(duplicate){std::vector<uint64_t> ids;for(id value:array(p[@"nodes"],128))ids.push_back(decodeNativeID(value));fragment=copySignalSelection(definition(p[@"graph"]),ids);for(const auto &n:fragment.nodes)for(const auto &e:n.envelopes)patterns[e.pattern]=e.pattern;}
        else{fragment=decodeSignalDefinition(p[@"fragment"]);for(id raw:array(p[@"patternMap"]?:@[],1024)){auto map=object(raw);keys(map,@[@"source",@"target"]);require(patterns.emplace(decodeNativeID(map[@"source"]),decodeNativeID(map[@"target"])).second,"Duplicate pattern mapping");}}
        try{const auto pasted=pasteSignalSelection(next,affected,fragment,patterns,p[@"parent"]?decodeNativeID(p[@"parent"]):0,number(p[@"x"]?:@0,0,100000),number(p[@"y"]?:@0,0,100000));
          for(const auto &[old,id]:pasted.identities){if(!nodeID)nodeID=id;if(duplicate)if(const auto hint=_pluginPathHints.graph.find({affected,old});hint!=_pluginPathHints.graph.end())clonedPathHints[{affected,id}]=hint->second;}
        }catch(const std::invalid_argument &e){throw Error(-32602,e.what());}
      } else if([method isEqual:@"graph.makeIndependent"]) {
        allowed(@[@"graph",@"target",@"scope",@"name",@"number",@"dryRun"]);const auto scope=string(p[@"scope"],16);require([scope isEqual:@"channel"]||[scope isEqual:@"instrument"],"Choose channel or instrument scope");
        const auto source=decodeNativeID(p[@"graph"]),target=decodeNativeID(p[@"target"]);
        try {const auto cloned=makeSignalUseIndependent(next,source,target,[scope isEqual:@"instrument"],p[@"name"]?std::optional<std::string>{string(p[@"name"],256).UTF8String}:std::nullopt,p[@"number"]?uint16_t(integer(p[@"number"],1,999)):0);affected=cloned.graph;
          for(const auto &[old,id]:cloned.identities)if(const auto hint=_pluginPathHints.graph.find({source,old});hint!=_pluginPathHints.graph.end())clonedPathHints[{affected,id}]=hint->second;
        }catch(const std::invalid_argument &e){throw Error(-32602,e.what());}
      } else if([method isEqual:@"graph.source.mute"]) {
        allowed(@[@"graph",@"node",@"muted",@"dryRun"]);require(p[@"graph"]!=nil,"Specify a graph identity or null for a song source");nodeID=decodeNativeID(p[@"node"]);affected=p[@"graph"]==NSNull.null?0:decodeNativeID(p[@"graph"]);
        try{muteSignalSource(next,affected,nodeID,boolean(p[@"muted"]));}catch(const std::invalid_argument &e){throw Error(-32602,e.what());}
      } else if([method isEqual:@"graph.connections.remove"]) {
        allowed(@[@"connections",@"dryRun"]);std::vector<SongConnectionRef> cables;
        for(id raw:array(p[@"connections"],512)) {
          auto c=object(raw);auto kind=string(c[@"kind"],32);SongConnectionRef ref{};
          if([kind isEqual:@"output"]||[kind isEqual:@"send"]) {keys(c,@[@"kind",@"source",@"target"]);ref.kind=[kind isEqual:@"output"]?SongConnectionKind::Output:SongConnectionKind::Send;ref.source=decodeNativeID(c[@"source"]);ref.target=decodeNativeID(c[@"target"]);}
          else if([kind isEqual:@"insert"]) {keys(c,@[@"kind",@"source",@"plugin"]);ref.kind=SongConnectionKind::Insert;ref.source=decodeNativeID(c[@"source"]);ref.plugin=string(c[@"plugin"],128).UTF8String;}
          else if([kind isEqual:@"master-output"]) {keys(c,@[@"kind",@"source"]);ref.kind=SongConnectionKind::MasterOutput;ref.source=decodeNativeID(c[@"source"]);}
          else if([kind isEqual:@"graph-input"]||[kind isEqual:@"graph-output"]) {const bool input=[kind isEqual:@"graph-input"];keys(c,input?@[@"kind",@"source",@"target",@"input"]:@[@"kind",@"source",@"target",@"output"]);ref.kind=input?SongConnectionKind::GraphInput:SongConnectionKind::GraphOutput;ref.source=decodeNativeID(c[@"source"]);ref.target=decodeNativeID(c[@"target"]);ref.port=uint32_t(integer(c[input?@"input":@"output"],1,63));}
          else if([kind isEqual:@"stage-connection"]) {keys(c,@[@"kind",@"source",@"target",@"output",@"input"]);const auto r=decodeSignalStageCable(c);ref.kind=SongConnectionKind::StageConnection;ref.source=r.source.stage;ref.sourcePlugin=r.source.plugin;ref.target=r.target.stage;ref.plugin=r.target.plugin;ref.output=r.output;ref.port=r.input;}
          else if([kind isEqual:@"plugin-connection"]) {keys(c,@[@"kind",@"source",@"output",@"target",@"input"]);ref.kind=SongConnectionKind::PluginConnection;ref.sourcePlugin=string(c[@"source"],128).UTF8String;ref.plugin=string(c[@"target"],128).UTF8String;ref.output=uint32_t(integer(c[@"output"],0,63));ref.port=uint32_t(integer(c[@"input"],0,63));}
          else if([kind isEqual:@"plugin-input"]) {keys(c,@[@"kind",@"source",@"plugin",@"input"]);ref.kind=SongConnectionKind::PluginInput;ref.source=decodeNativeID(c[@"source"]);ref.plugin=string(c[@"plugin"],256).UTF8String;ref.port=uint32_t(integer(c[@"input"],0,63));}
          else if([kind isEqual:@"plugin-output"]) {keys(c,@[@"kind",@"target",@"plugin",@"output"]);ref.kind=SongConnectionKind::PluginOutput;ref.target=decodeNativeID(c[@"target"]);ref.plugin=string(c[@"plugin"],256).UTF8String;ref.port=uint32_t(integer(c[@"output"],0,63));}
          else if([kind isEqual:@"note"]) {keys(c,@[@"kind",@"route",@"instrument"]);require(bool(c[@"route"])!=bool(c[@"instrument"]),"Choose an explicit note route or implicit instrument assignment");ref.kind=SongConnectionKind::Note;if(c[@"route"])ref.source=decodeNativeID(c[@"route"]);else ref.target=decodeNativeID(c[@"instrument"]);}
          else if([kind isEqual:@"modulation"]) {keys(c,@[@"kind",@"source",@"plugin",@"parameter"]);ref.kind=SongConnectionKind::Modulation;ref.source=decodeNativeID(c[@"source"]);ref.plugin=string(c[@"plugin"],256).UTF8String;ref.port=uint32_t(integer(c[@"parameter"],0,UINT32_MAX));}
          else if([kind isEqual:@"follower-input"]) {keys(c,@[@"kind",@"node",@"source",@"stage",@"plugin",@"output",@"preFader"]);ref.kind=SongConnectionKind::FollowerInput;ref.target=decodeNativeID(c[@"node"]);if(c[@"stage"])ref.stage=decodeNativeID(c[@"stage"]);if(c[@"source"])ref.source=decodeNativeID(c[@"source"]);if(c[@"plugin"])ref.plugin=string(c[@"plugin"],256).UTF8String;ref.port=uint32_t(integer(c[@"output"]?:@0,0,63));ref.preFader=c[@"preFader"]?boolean(c[@"preFader"]):false;}
          else throw Error(-32602,"Unknown song cable kind");
          cables.push_back(std::move(ref));
        }
        std::vector<std::string> instruments,effects;for(const auto &p:_plugins)(p.descriptor.instrument||p.descriptor.type==kAudioUnitType_MusicDevice?instruments:effects).push_back(p.instanceID);
        try{removeSongConnections(next,cables,instruments,effects);}catch(const std::invalid_argument &e){throw Error(-32602,e.what());}
      } else if([method hasPrefix:@"graph.song.group."]) {
        if([method isEqual:@"graph.song.group.create"]) {
          allowed(@[@"nodes",@"groups",@"parent",@"name",@"positions",@"dryRun"]);
          std::vector<std::string> members;std::vector<uint64_t> children;
          for(id raw:array(p[@"nodes"]?:@[],240)){const auto key=std::string(string(raw,256).UTF8String);require(std::any_of(graph.songSources.begin(),graph.songSources.end(),[&](const auto &source){return key=="source:n"+std::to_string(source.node.id);})||std::any_of(_plugins.begin(),_plugins.end(),[&](const auto &plugin){return key=="plugin:"+plugin.instanceID&&!plugin.descriptor.instrument&&plugin.descriptor.type!=kAudioUnitType_MusicDevice;}),"Select existing rack effects or modulation sources; instruments and mixer buses are separate boundaries");members.push_back(key);}
          for(id raw:array(p[@"groups"]?:@[],128))children.push_back(decodeNativeID(raw));
          for(id raw:array(p[@"positions"]?:@[],240)){auto v=object(raw);keys(v,@[@"node",@"x",@"y"]);auto key=std::string(string(v[@"node"],256).UTF8String);require(std::find(members.begin(),members.end(),key)!=members.end(),"Position must belong to a selected processor");graph.layout[key]={number(v[@"x"],0,100000),number(v[@"y"],0,100000)};}
          groupID=next.makeEntity().id;const auto parent=!p[@"parent"]||p[@"parent"]==NSNull.null?0:decodeNativeID(p[@"parent"]);
          try{groupSongSignalNodes(graph,members,children,groupID,parent,string(p[@"name"]?:@"Group",256).UTF8String);}catch(const std::invalid_argument &e){throw Error(-32602,e.what());}
        } else if([method isEqual:@"graph.song.group.update"]) {
          allowed(@[@"group",@"name",@"x",@"y",@"dryRun"]);groupID=decodeNativeID(p[@"group"]);
          auto g=std::find_if(graph.groups.begin(),graph.groups.end(),[&](const auto &g){return g.id==groupID;});require(g!=graph.groups.end(),"Song processing group does not exist");
          if(p[@"name"])g->name=string(p[@"name"],256).UTF8String;
          const auto x=p[@"x"]?number(p[@"x"],0,100000):g->x,y=p[@"y"]?number(p[@"y"],0,100000):g->y;
          if(p[@"x"]||p[@"y"])try{moveSongSignalGroup(graph,groupID,x,y);}catch(const std::invalid_argument &e){throw Error(-32602,e.what());}
        } else if([method isEqual:@"graph.song.group.remove"]) {
          allowed(@[@"group",@"dryRun"]);groupID=decodeNativeID(p[@"group"]);try{ungroupSongSignalNodes(graph,groupID);}catch(const std::invalid_argument &e){throw Error(-32602,e.what());}
        } else if([method isEqual:@"graph.song.group.export"]) {
          allowed(@[@"group",@"name",@"number",@"dryRun"]);groupID=decodeNativeID(p[@"group"]);
          std::set<uint64_t> members{groupID};for(size_t i=0;i<graph.groups.size();++i)for(const auto &g:graph.groups)if(members.contains(g.parent))members.insert(g.id);
          std::set<std::string> selected;for(const auto &g:graph.groups)if(members.contains(g.id))selected.insert(g.nodes.begin(),g.nodes.end());
          std::vector<std::pair<std::string,GraphPluginRecipe>> effects;
          for(size_t slot=0;slot<_plugins.size();++slot){const auto &state=_plugins[slot];const auto &d=state.descriptor;if(d.instrument||d.type==kAudioUnitType_MusicDevice)continue;
            GraphPluginRecipe recipe{d.format,d.name,d.path,d.classID,d.type,d.subtype,d.manufacturer,state.state,state.auxiliaryInputs,state.auxiliaryOutputs};
            recipe.bypass=state.bypass;recipe.audioLayout=state.audioLayout;
            for(const auto &value:_manualParameters)if(value.slot==slot)recipe.parameters[value.id]=value.value;
            effects.emplace_back(state.instanceID,std::move(recipe));
          }
          auto implicit=next;if(!implicit.mixer.active())implicit.ensureMixer();
          SignalDefinition copy;try{copy=extractSongSignalGroup(graph,implicit.mixer,groupID,effects,[&]{return next.makeEntity().id;});}catch(const std::invalid_argument &e){throw Error(-32602,e.what());}
          copy.number=p[@"number"]?uint16_t(integer(p[@"number"],1,999)):newNumber();if(p[@"name"])copy.name=string(p[@"name"],256).UTF8String;affected=copy.id;graph.library.push_back(std::move(copy));
        } else throw Error(-32601,"Unknown song processing group method");
      } else if([method isEqual:@"graph.create"]) {
        allowed(@[@"name",@"number",@"dryRun"]);SignalDefinition d;d.id=next.makeEntity().id;affected=d.id;
        d.number=p[@"number"]?uint16_t(integer(p[@"number"],1,999)):newNumber();d.name=string(p[@"name"]?:@"New subgraph",256).UTF8String;
        const auto input=next.makeEntity().id,output=next.makeEntity().id;d.nodes={{input,SignalNodeKind::Input,"Input",40,100},{output,SignalNodeKind::Output,"Output",620,100}};d.audio={{input,output}};graph.library.push_back(std::move(d));
      } else if([method isEqual:@"graph.clone"]||[method isEqual:@"graph.group.export"]) {
        const bool exporting=[method isEqual:@"graph.group.export"];
        allowed(exporting?@[@"graph",@"group",@"name",@"number",@"dryRun"]:@[@"graph",@"name",@"number",@"dryRun"]);auto copy=definition(p[@"graph"]);
        if(exporting){const auto input=next.makeEntity().id,output=next.makeEntity().id;try{copy=extractSignalGroup(copy,decodeNativeID(p[@"group"]),input,output);}catch(const std::invalid_argument &e){throw Error(-32602,e.what());}}
        const auto sourceGraph=copy.id;copy.id=next.makeEntity().id;affected=copy.id;copy.number=p[@"number"]?uint16_t(integer(p[@"number"],1,999)):newNumber();copy.name=string(p[@"name"]?:@(copy.name.c_str()),256).UTF8String;
        std::map<uint64_t,uint64_t> mapping;for(auto &node:copy.nodes){const auto id=next.makeEntity().id;mapping[node.id]=id;if(const auto hint=_pluginPathHints.graph.find({sourceGraph,node.id});hint!=_pluginPathHints.graph.end()){const auto stored=hint->second;clonedPathHints[{copy.id,id}]=stored;}node.id=id;}
        for(auto &group:copy.groups){const auto id=next.makeEntity().id;mapping[group.id]=id;group.id=id;}
        for(auto &group:copy.groups){if(group.parent)group.parent=mapping.at(group.parent);for(auto &member:group.nodes)member=mapping.at(member);}
        remapSignalGroupDryRoutes(copy,mapping);
        for(const auto &l:std::vector<EnvelopeLink>(next.envelopeLinks))if(l.target.kind==EnvelopeTargetKind::Graph&&mapping.contains(l.target.owner)){auto link=l;link.target.owner=mapping.at(l.target.owner);next.envelopeLinks.push_back(link);}
        std::map<std::string,std::string> visualIDs;for(auto [from,to]:mapping)visualIDs["n"+std::to_string(from)]="n"+std::to_string(to);remapSignalPresentation(copy.presentation,visualIDs);
        for(auto &e:copy.audio){e.source=mapping.at(e.source);e.target=mapping.at(e.target);}for(auto &e:copy.modulation){e.source=mapping.at(e.source);e.target=mapping.at(e.target);}graph.library.push_back(std::move(copy));
      } else if([method isEqual:@"graph.update"]) {
        allowed(@[@"definition",@"dryRun"]);NSMutableDictionary *replacement=[object(p[@"definition"]) mutableCopy];auto &previous=definition(replacement[@"id"]);
        if(!replacement[@"groups"])replacement[@"groups"]=encodeSignalDefinition(previous,false)[@"groups"];
        if(!replacement[@"presentation"])replacement[@"presentation"]=encodeSignalPresentation(previous.presentation);
        NSMutableArray *nodes=[NSMutableArray array];for(id rawNode:array(replacement[@"nodes"],64)){NSMutableDictionary *node=[object(rawNode) mutableCopy];if(node[@"plugin"]){NSMutableDictionary *recipe=[object(node[@"plugin"]) mutableCopy];if(!recipe[@"state"]){const auto id=decodeNativeID(node[@"id"]);auto existing=std::find_if(previous.nodes.begin(),previous.nodes.end(),[&](const auto &n){return n.id==id;});require(existing!=previous.nodes.end(),"Unknown node identity");recipe[@"state"]=encodeSignalRecipe(existing->plugin)[@"state"];if(!recipe[@"parameters"])recipe[@"parameters"]=encodeSignalRecipe(existing->plugin)[@"parameters"];}node[@"plugin"]=recipe;}[nodes addObject:node];}replacement[@"nodes"]=nodes;
        if(replacement[@"presentation"])decodeSignalPresentation(replacement[@"presentation"],true);
        auto updated=decodeSignalDefinition(replacement);affected=updated.id;
        // New identities are allocated by graph.node.add, never supplied by a client.
        for(const auto &node:updated.nodes)require(std::any_of(previous.nodes.begin(),previous.nodes.end(),[&](const auto &n){return n.id==node.id&&n.kind==node.kind;}),"Add nodes using graph.node.add; node identities and kinds cannot be replaced");
        // A changed bundle location is a hint to resolve, exactly as when opening a project.
        for(auto &node:updated.nodes)if(node.kind==SignalNodeKind::Plugin){auto existing=std::find_if(previous.nodes.begin(),previous.nodes.end(),[&](const auto &n){return n.id==node.id;});if(existing==previous.nodes.end()||existing->plugin!=node.plugin){[self resolveGraphRecipe:node.plugin];if(existing!=previous.nodes.end()&&!existing->plugin.audioLayout.empty()&&node.plugin.audioLayout.empty())node.plugin.audioLayout=existing->plugin.audioLayout;[self pinGraphRecipeLayout:node.plugin];}}
        for(const auto &group:updated.groups)require(std::any_of(previous.groups.begin(),previous.groups.end(),[&](const auto &g){return g.id==group.id;}),"Allocate group identities using graph.group.create");
        reconcileSignalPresentation(updated,previous);previous=std::move(updated);
      } else if([method isEqual:@"graph.group.create"]) {
        allowed(@[@"graph",@"nodes",@"parent",@"name",@"dryRun"]);auto &d=definition(p[@"graph"]);affected=d.id;groupID=next.makeEntity().id;
        std::vector<uint64_t> selection;for(id raw:array(p[@"nodes"],64))selection.push_back(decodeNativeID(raw));
        const auto parent=!p[@"parent"]||p[@"parent"]==NSNull.null?0:decodeNativeID(p[@"parent"]);
        try{groupSignalNodes(d,selection,groupID,parent,string(p[@"name"]?:@"Group",256).UTF8String);}catch(const std::invalid_argument &e){throw Error(-32602,e.what());}
      } else if([method isEqual:@"graph.group.remove"]) {
        allowed(@[@"graph",@"group",@"deleteContents",@"dryRun"]);auto &d=definition(p[@"graph"]);affected=d.id;groupID=decodeNativeID(p[@"group"]);
        require(std::any_of(d.groups.begin(),d.groups.end(),[&](const auto &g){return g.id==groupID;}),"Processing group does not exist");
        if(p[@"deleteContents"]&&boolean(p[@"deleteContents"])) {
          std::set<uint64_t> groups{groupID},members;bool changed=true;
          while(changed){changed=false;for(const auto &g:d.groups)if(groups.contains(g.parent))changed|=groups.insert(g.id).second;}
          for(const auto &g:d.groups)if(groups.contains(g.id))members.insert(g.nodes.begin(),g.nodes.end());
          std::erase_if(d.nodes,[&](const auto &n){return members.contains(n.id);});
          std::erase_if(d.audio,[&](const auto &e){return members.contains(e.source)||members.contains(e.target);});
          std::erase_if(d.modulation,[&](const auto &e){return members.contains(e.source)||members.contains(e.target);});
          pruneSignalGroups(d);
        } else try{ungroupSignalNodes(d,groupID);}catch(const std::invalid_argument &e){throw Error(-32602,e.what());}
      } else if([method isEqual:@"graph.group.update"]) {
        allowed(@[@"graph",@"group",@"name",@"x",@"y",@"dryRun"]);auto &d=definition(p[@"graph"]);affected=d.id;groupID=decodeNativeID(p[@"group"]);
        auto group=std::find_if(d.groups.begin(),d.groups.end(),[&](const auto &g){return g.id==groupID;});require(group!=d.groups.end(),"Processing group does not exist");
        if(p[@"name"])group->name=string(p[@"name"],256).UTF8String;
        if(p[@"x"]||p[@"y"]){const auto x=p[@"x"]?number(p[@"x"],-100000,100000):group->x,y=p[@"y"]?number(p[@"y"],-100000,100000):group->y;try{moveSignalGroup(d,groupID,x,y);}catch(const std::invalid_argument &e){throw Error(-32602,e.what());}}
      } else if([method isEqual:@"graph.remove"]) {
        allowed(@[@"graph",@"dryRun"]);affected=definition(p[@"graph"]).id;
        require(std::none_of(graph.instrumentAssignments.begin(),graph.instrumentAssignments.end(),[&](const auto &a){return a.graph==affected;})&&std::none_of(graph.assignments.begin(),graph.assignments.end(),[&](const auto &a){return a.graph==affected;})&&std::none_of(graph.commands.begin(),graph.commands.end(),[&](const auto &c){return c.graph==affected;}),"Remove this subgraph's assignments and pattern commands before deleting it");
        std::erase_if(graph.library,[&](const auto &d){return d.id==affected;});
      } else if([method isEqual:@"graph.node.add"]) {
        allowed(@[@"graph",@"kind",@"name",@"x",@"y",@"slot",@"plugin",@"insertAfter",@"insertEdge",@"connect",@"audioInput",@"parent",@"dryRun"]);auto &d=definition(p[@"graph"]);affected=d.id;
        SignalNode n;n.id=next.makeEntity().id;nodeID=n.id;n.kind=decodeSignalNodeKind(p[@"kind"]);require(n.kind!=SignalNodeKind::Input&&n.kind!=SignalNodeKind::Output,"Each subgraph already has Input and Output nodes");
        n.name=string(p[@"name"]?:signalNodeKind(n.kind),256).UTF8String;n.x=number(p[@"x"]?:@300,-100000,100000);n.y=number(p[@"y"]?:@100,-100000,100000);
        if(n.kind==SignalNodeKind::Plugin){require(bool(p[@"slot"])!=bool(p[@"plugin"]),"Supply a rack slot or a plugin recipe");
          if(p[@"slot"]){const auto slot=pluginSlot();auto source=_plugins[slot];if(!_audio->active()&&_pluginError.empty()&&!_audio->hasAutomatedState())source=_audio->pluginStates().at(slot);require(!source.descriptor.instrument&&source.descriptor.type!=kAudioUnitType_MusicDevice,"Subgraph inserts require an effect plugin");const auto &s=source.descriptor;
            n.plugin={s.format,s.name,s.path,s.classID,s.type,s.subtype,s.manufacturer,source.state,source.auxiliaryInputs,source.auxiliaryOutputs};
            n.plugin.bypass=source.bypass;n.plugin.audioLayout=source.audioLayout;
            n.name=string(p[@"name"]?:@(s.name.c_str()),256).UTF8String;
          }else {n.plugin=decodeSignalRecipe(p[@"plugin"]);[self resolveGraphRecipe:n.plugin];[self pinGraphRecipeLayout:n.plugin];}
        } else require(!p[@"slot"]&&!p[@"plugin"],"Only plugin nodes accept plugin recipes");
        require(!p[@"insertAfter"]||!p[@"insertEdge"],"Choose insertAfter or insertEdge, not both");
        if(p[@"insertEdge"]) {
          require(n.kind==SignalNodeKind::Plugin,"Only effects can be inserted into an audio chain");
          const auto index=size_t(integer(p[@"insertEdge"],0,255));require(index<d.audio.size(),"Insertion cable no longer exists");
          // Preserve the original source tap and edge gain; the new effect uses its main buses.
          auto &edge=d.audio[index];const auto source=edge.source;const auto output=edge.output;const auto gain=edge.gain;
          edge.source=nodeID;edge.output=0;edge.gain=1;d.audio.push_back({source,nodeID,output,0,gain});
        }
        if(p[@"insertAfter"]){require(n.kind==SignalNodeKind::Plugin,"Only effects can be inserted into an audio chain");const auto after=decodeNativeID(p[@"insertAfter"]);auto edge=std::find_if(d.audio.begin(),d.audio.end(),[&](const auto &e){return e.source==after&&e.output==0&&e.input==0;});require(edge!=d.audio.end()&&std::count_if(d.audio.begin(),d.audio.end(),[&](const auto &e){return e.source==after&&e.output==0;})==1,"Insert after a node with one main audio destination");edge->source=nodeID;d.audio.push_back({after,nodeID});}
        if(p[@"connect"]) {
          require(!p[@"insertAfter"]&&!p[@"insertEdge"],"Choose cable insertion or connection, not both");
          auto c=object(p[@"connect"]);keys(c,@[@"node",@"port",@"output",@"modulation",@"base",@"quantized"]);
          const auto endpoint=decodeNativeID(c[@"node"]);require(std::any_of(d.nodes.begin(),d.nodes.end(),[&](const auto &v){return v.id==endpoint;}),"Connection endpoint no longer exists");
          const bool output=boolean(c[@"output"]),mod=c[@"modulation"]?boolean(c[@"modulation"]):false;
          const auto port=uint32_t(integer(c[@"port"]?:@0,0,mod?UINT32_MAX:63));
          if(mod) {
            require(!output,"Choose a parameter input before creating its modulation source");
            double base=number(c[@"base"]?:@0,0,1);
            auto peer=std::find_if(d.modulation.begin(),d.modulation.end(),[&](const auto &e){return e.target==endpoint&&e.parameter==port;});if(peer!=d.modulation.end())base=peer->base;
            d.modulation.push_back({nodeID,endpoint,port,0,0,base,true,c[@"quantized"]?boolean(c[@"quantized"]):false});
          } else {
            require(!c[@"quantized"],"Quantized mode applies only to a parameter modulation connection");
            d.audio.push_back({output?endpoint:nodeID,output?nodeID:endpoint,output?port:0,output?0:port});
            // A connected auxiliary bus must be active in the prepared recipe.
            auto &existing=*std::find_if(d.nodes.begin(),d.nodes.end(),[&](const auto &v){return v.id==endpoint;});
            if(port>0&&existing.kind==SignalNodeKind::Plugin) {auto &ports=output?existing.plugin.outputs:existing.plugin.inputs;if(std::find(ports.begin(),ports.end(),port)==ports.end())ports.push_back(port);}
          }
        }
        if(p[@"audioInput"]) {
          require(n.kind==SignalNodeKind::Follower&&p[@"connect"],"An audioInput creates a follower and its parameter connection together");
          auto connect=object(p[@"connect"]);require(connect[@"modulation"]&&boolean(connect[@"modulation"]),"audioInput requires a parameter modulation connection");
          auto input=object(p[@"audioInput"]);keys(input,@[@"node",@"port"]);
          const auto source=decodeNativeID(input[@"node"]);const auto port=uint32_t(integer(input[@"port"]?:@0,0,63));
          auto existing=std::find_if(d.nodes.begin(),d.nodes.end(),[&](const auto &v){return v.id==source;});require(existing!=d.nodes.end(),"Follower audio source no longer exists");
          d.audio.push_back({source,nodeID,port,0});
          if(port>0&&existing->kind==SignalNodeKind::Plugin){auto &ports=existing->plugin.outputs;if(std::find(ports.begin(),ports.end(),port)==ports.end())ports.push_back(port);}
        }
        if(p[@"parent"]&&p[@"parent"]!=NSNull.null) {const auto id=decodeNativeID(p[@"parent"]);auto parent=std::find_if(d.groups.begin(),d.groups.end(),[&](const auto &g){return g.id==id;});require(parent!=d.groups.end(),"Processing group parent does not exist");parent->nodes.push_back(nodeID);}
        d.nodes.push_back(std::move(n));
      } else if([method isEqual:@"graph.nodes.detach"]) {
        allowed(@[@"graph",@"nodes",@"remove",@"positions",@"heal",@"dryRun"]);auto &d=definition(p[@"graph"]);affected=d.id;
        std::vector<uint64_t> moving;for(id raw:array(p[@"nodes"],64))moving.push_back(decodeNativeID(raw));
        const bool remove=p[@"remove"]?boolean(p[@"remove"]):false;
        require(!remove||!p[@"positions"],"Removed nodes cannot have positions");
        std::optional<SignalHealPath> heal;if(p[@"heal"]){auto h=object(p[@"heal"]);keys(h,@[@"incoming",@"outgoing"]);heal.emplace();if(h[@"incoming"]&&h[@"incoming"]!=NSNull.null)heal->incoming=size_t(integer(h[@"incoming"],0,255));if(h[@"outgoing"]&&h[@"outgoing"]!=NSNull.null)heal->outgoing=size_t(integer(h[@"outgoing"],0,255));}
        try{detachSignalNodes(d,moving,remove,heal);}catch(const std::invalid_argument &e){throw Error(-32602,e.what());}
        for(id raw:array(p[@"positions"]?:@[],64)){auto v=object(raw);keys(v,@[@"node",@"x",@"y"]);const auto id=decodeNativeID(v[@"node"]);const auto x=number(v[@"x"],0,100000),y=number(v[@"y"],0,100000);if(std::any_of(d.groups.begin(),d.groups.end(),[&](const auto &g){return g.id==id;})){const auto members=signalGroupMembers(d,id);require(std::all_of(members.begin(),members.end(),[&](auto node){return std::find(moving.begin(),moving.end(),node)!=moving.end();}),"Positioned group must be entirely selected");moveSignalGroup(d,id,x,y);}else{require(std::find(moving.begin(),moving.end(),id)!=moving.end(),"Position must belong to a detached node");auto &node=*std::find_if(d.nodes.begin(),d.nodes.end(),[&](const auto &n){return n.id==id;});node.x=x;node.y=y;}}
      } else if([method isEqual:@"graph.nodes.insert"]) {
        allowed(@[@"graph",@"nodes",@"edge",@"positions",@"dryRun"]);auto &d=definition(p[@"graph"]);affected=d.id;
        std::vector<uint64_t> moving;for(id raw:array(p[@"nodes"],32))moving.push_back(decodeNativeID(raw));
        try{insertSignalNodes(d,moving,size_t(integer(p[@"edge"],0,255)));}catch(const std::invalid_argument &e){throw Error(-32602,e.what());}
        for(id raw:array(p[@"positions"]?:@[],32)){auto v=object(raw);keys(v,@[@"node",@"x",@"y"]);const auto id=decodeNativeID(v[@"node"]);require(std::find(moving.begin(),moving.end(),id)!=moving.end(),"Position must belong to an inserted node");auto &node=*std::find_if(d.nodes.begin(),d.nodes.end(),[&](const auto &n){return n.id==id;});node.x=number(v[@"x"],0,100000);node.y=number(v[@"y"],0,100000);}
      } else if([method isEqual:@"graph.node.remove"]) {
        allowed(@[@"graph",@"node",@"dryRun"]);auto &d=definition(p[@"graph"]);affected=d.id;nodeID=decodeNativeID(p[@"node"]);
        auto n=std::find_if(d.nodes.begin(),d.nodes.end(),[&](const auto &v){return v.id==nodeID;});require(n!=d.nodes.end()&&n->kind!=SignalNodeKind::Input&&n->kind!=SignalNodeKind::Output,"Select an existing processor or modulation node");
        d.nodes.erase(n);std::erase_if(d.audio,[&](const auto &e){return e.source==nodeID||e.target==nodeID;});std::erase_if(d.modulation,[&](const auto &e){return e.source==nodeID||e.target==nodeID;});
        pruneSignalGroups(d);
      } else if([method isEqual:@"graph.automation.set"]) {
        allowed(@[@"graph",@"node",@"pattern",@"enabled",@"points",@"dryRun"]);require(p[@"graph"]!=nil,"Specify a subgraph identity or null for a song source");nodeID=decodeNativeID(p[@"node"]);
        SignalNode *node=nullptr;
        if(p[@"graph"]!=NSNull.null) {auto &d=definition(p[@"graph"]);affected=d.id;auto found=std::find_if(d.nodes.begin(),d.nodes.end(),[&](const auto &v){return v.id==nodeID;});if(found!=d.nodes.end())node=&*found;}
        else {for(auto &source:graph.songSources)if(source.node.id==nodeID)node=&source.node;}
        require(node&&node->kind==SignalNodeKind::Automation,"Choose an automation source node");
        const auto index=uint16_t(integer(p[@"pattern"],0,UINT16_MAX));require(song.Patterns.IsValidPat(index),"Pattern does not exist");const auto pattern=next.patterns.at(index).id;
        SignalPatternEnvelope lane;lane.pattern=pattern;lane.enabled=p[@"enabled"]?boolean(p[@"enabled"]):true;
        for(id point:array(p[@"points"],4096))lane.points.push_back(decodeAutomationPoint(point,uint32_t(song.Patterns[index].GetNumRows())*256-1));
        replaceSignalEnvelope(node->envelopes,std::move(lane));
      } else if([method isEqual:@"graph.assign"]) {
        allowed(@[@"target",@"graph",@"amount",@"wet",@"dryRun"]);const auto target=bus(p[@"target"]);std::erase_if(graph.assignments,[&](const auto &a){return a.target==target;});
        if(p[@"graph"]!=NSNull.null){affected=definition(p[@"graph"]).id;graph.assignments.push_back({target,affected,number(p[@"amount"]?:@1,0,1),number(p[@"wet"]?:@1,0,1)});}
      } else if([method isEqual:@"graph.instrument.assign"]) {
        allowed(@[@"instrument",@"graph",@"amount",@"wet",@"dryRun"]);const auto index=instrumentIndex();ensureMixer();const auto id=next.instruments.at(index).id;
        std::erase_if(graph.instrumentAssignments,[&](const auto &a){return a.target==id;});
        if(p[@"graph"]!=NSNull.null){affected=definition(p[@"graph"]).id;graph.instrumentAssignments.push_back({id,affected,number(p[@"amount"]?:@1,0,1),number(p[@"wet"]?:@1,0,1)});}
      } else if([method isEqual:@"graph.layout.set"]) {
        allowed(@[@"positions",@"groups",@"reset",@"dryRun"]);
        for(id raw:array(p[@"groups"]?:@[],128)){auto v=object(raw);keys(v,@[@"group",@"x",@"y"]);try{moveSongSignalGroup(graph,decodeNativeID(v[@"group"]),number(v[@"x"],0,100000),number(v[@"y"],0,100000));}catch(const std::invalid_argument &e){throw Error(-32602,e.what());}}
        if(p[@"reset"]&&boolean(p[@"reset"]))graph.layout.clear();for(id raw:array(p[@"positions"]?:@[],8192)){auto v=object(raw);keys(v,@[@"node",@"x",@"y"]);graph.layout[string(v[@"node"],256).UTF8String]={number(v[@"x"],0,100000),number(v[@"y"],0,100000)};}
      } else if([method isEqual:@"graph.routes.set"]) {
        allowed(@[@"inputs",@"outputs",@"dryRun"]);
        if(p[@"inputs"]){graph.inputs.clear();for(id raw:array(p[@"inputs"],128)){auto r=object(raw);keys(r,@[@"source",@"target",@"input",@"gainDB",@"preFader"]);graph.inputs.push_back({bus(r[@"source"]),bus(r[@"target"]),uint32_t(integer(r[@"input"],1,63)),number(r[@"gainDB"]?:@0,-96,12),r[@"preFader"]?boolean(r[@"preFader"]):false});}}
        if(p[@"outputs"]){graph.outputs.clear();for(id raw:array(p[@"outputs"],128)){auto r=object(raw);keys(r,@[@"source",@"target",@"output"]);graph.outputs.push_back({bus(r[@"source"]),bus(r[@"target"]),uint32_t(integer(r[@"output"],1,63))});}}
      } else if([method isEqual:@"graph.commands.set"]) {
        allowed(@[@"pattern",@"lanes",@"commands",@"dryRun"]);const auto index=uint16_t(integer(p[@"pattern"],0,UINT16_MAX));require(song.Patterns.IsValidPat(index),"Pattern does not exist");const auto pattern=next.patterns.at(index).id;
        if(p[@"lanes"])for(id rawLane:array(p[@"lanes"],240)){auto l=object(rawLane);keys(l,@[@"target",@"count"]);const auto target=bus(l[@"target"]);auto count=uint8_t(integer(l[@"count"],0,8));if(count)graph.lanes[target]=count;else graph.lanes.erase(target);}
        if(p[@"commands"]){std::erase_if(graph.commands,[&](const auto &c){return c.pattern==pattern;});for(id rawCommand:array(p[@"commands"],65536)){NSMutableDictionary *c=[object(rawCommand) mutableCopy];require(c[@"pattern"]==nil,"Pattern is supplied once for the command set");c[@"pattern"]=nativeID(pattern);if([c[@"kind"] isEqual:@"clear"]&&!c[@"graph"])c[@"graph"]=@"";graph.commands.push_back(decodeSignalCommand(c));}}
      } else throw Error(-32601,"Unknown graph method");
      reconcileEnvelopeLinks(next,song);
      try{next.validate(song);}catch(const std::invalid_argument &e){throw Error(-32602,e.what());}
      const bool changed=next!=_document->native();if(changed&&!dry){
        if([method isEqual:@"graph.connections.remove"]&&next.signal.noteRouting==_document->native().signal.noteRouting) {
          if(!_audio->mixerRoutingReady())throw Error(-32002,"Routing transition is busy; retry the same revision");
          auto routing=_audio->prepareMixerRouting(next);
          if(!routing&&_audio->active())throw Error(-32002,"This cable edit cannot be prepared during playback; stop playback and retry");
          if(!routing)_audio->stop();
          _document->annotate([&](NativeSong &native){native=next;},[&]{if(routing&&!_audio->publishMixerRouting(routing))throw Error(-32002,"Routing transition is busy; retry the same revision");});
        } else {
          const bool processing=next.mixer!=_document->native().mixer||!sameSignalProcessing(next.signal,_document->native().signal);
          const bool controls=processing&&next.mixer==_document->native().mixer;
          auto plan=controls?_audio->prepareGraphControls(next):nullptr;
          auto routing=processing&&!plan?_audio->prepareMixerRouting(next):nullptr;
          if(processing && !plan && !routing){if(_audio->active())throw Error(-32002,"This graph edit cannot yet be prepared during playback; stop playback and retry");_audio->stop();}
          _document->annotate([&](NativeSong &native){native=next;},[&]{if(plan)require(_audio->publishGraphControls(std::move(plan)),"Graph parameter queue is busy; retry the edit");if(routing&&!_audio->publishMixerRouting(routing))throw Error(-32002,"Routing transition is busy; retry the same revision");});
        }
      }
      if(changed&&!dry)_pluginPathHints.graph.merge(clonedPathHints);
      result=@{@"dryRun":@(dry),@"wouldChange":@(changed),@"graph":affected?nativeID(affected):@"",@"node":nodeID?nativeID(nodeID):@"",@"route":[method hasPrefix:@"graph.note."]&&nodeID?nativeID(nodeID):@"",@"group":groupID?nativeID(groupID):@""};
      if(clipboardFragment){auto copiedResult=[result mutableCopy];copiedResult[@"fragment"]=clipboardFragment;copiedResult[@"version"]=@1;result=copiedResult;}
