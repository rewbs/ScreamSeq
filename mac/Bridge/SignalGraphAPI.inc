    } else if ([method isEqual:@"graph.get"]) {
      allowed(@[@"includeState"]);NSMutableDictionary *data=[encodeSignalGraph(_document->native().signal,!p[@"includeState"]||boolean(p[@"includeState"])) mutableCopy];

      NSMutableArray *plugins=[NSMutableArray array];for(size_t slot=0;slot<_plugins.size();++slot){const auto &p=_plugins[slot];NSMutableDictionary *item=[descriptorDictionary(p.descriptor) mutableCopy];item[@"id"]=@(p.instanceID.c_str());item[@"slot"]=@(slot);item[@"bypass"]=@(p.bypass);NSMutableArray *instruments=[NSMutableArray array];for(const auto &a:pluginAssignments(p))[instruments addObject:@(a.instrument)];item[@"instruments"]=instruments;[plugins addObject:item];}data[@"plugins"]=plugins;
      data[@"mixer"]=encodeMixer(_document->native().mixer);data[@"unitsPerRow"]=@65536;
      data[@"activity"]=encodeSignalActivity(_audio->graphActivity());
      result=data;
    } else if ([method isEqual:@"graph.controller"]) {
      allowed(@[@"controller",@"value"]);result=@{@"delivered":@(_audio->graphController(uint8_t(integer(p[@"controller"],0,127)),uint8_t(integer(p[@"value"],0,127))))};
    } else if ([method hasPrefix:@"graph."]) {
      NativeSong next=_document->native();auto &graph=next.signal;
      const bool dry=p[@"dryRun"]?boolean(p[@"dryRun"]):false;
      uint64_t affected=0,nodeID=0;
      auto definition=[&](id raw)->SignalDefinition &{auto id=decodeNativeID(raw);auto found=std::find_if(graph.library.begin(),graph.library.end(),[&](const auto &d){return d.id==id;});require(found!=graph.library.end(),"Subgraph does not exist");return *found;};
      auto ensureMixer=[&](){if(next.mixer.active())return;const auto master=next.makeEntity().id;for(const auto &[channel,track]:next.tracks)next.mixer.buses.push_back({track.id,master,MixerBusKind::Track,track.name.empty()?"Track "+std::to_string(channel+1):track.name,track.color});next.mixer.buses.push_back({master,0,MixerBusKind::Master,"Master"});};
      auto bus=[&](id raw){ensureMixer();const auto id=decodeNativeID(raw);require(std::any_of(next.mixer.buses.begin(),next.mixer.buses.end(),[&](const auto &b){return b.id==id;}),"Graph target bus does not exist");return id;};
      auto newNumber=[&](){for(uint16_t number=1;number<=999;++number)if(std::none_of(graph.library.begin(),graph.library.end(),[&](const auto &d){return d.number==number;}))return number;throw Error(-32602,"No free subgraph number");};
      if([method isEqual:@"graph.create"]) {
        allowed(@[@"name",@"number",@"dryRun"]);SignalDefinition d;d.id=next.makeEntity().id;affected=d.id;
        d.number=p[@"number"]?uint16_t(integer(p[@"number"],1,999)):newNumber();d.name=string(p[@"name"]?:@"New subgraph",256).UTF8String;
        const auto input=next.makeEntity().id,output=next.makeEntity().id;d.nodes={{input,SignalNodeKind::Input,"Input",40,100},{output,SignalNodeKind::Output,"Output",620,100}};d.audio={{input,output}};graph.library.push_back(std::move(d));
      } else if([method isEqual:@"graph.clone"]) {
        allowed(@[@"graph",@"name",@"number",@"dryRun"]);auto copy=definition(p[@"graph"]);copy.id=next.makeEntity().id;affected=copy.id;copy.number=p[@"number"]?uint16_t(integer(p[@"number"],1,999)):newNumber();copy.name=string(p[@"name"]?:@(copy.name.c_str()),256).UTF8String;
        std::map<uint64_t,uint64_t> mapping;for(auto &node:copy.nodes){const auto id=next.makeEntity().id;mapping[node.id]=id;node.id=id;}
        for(auto &e:copy.audio){e.source=mapping.at(e.source);e.target=mapping.at(e.target);}for(auto &e:copy.modulation){e.source=mapping.at(e.source);e.target=mapping.at(e.target);}graph.library.push_back(std::move(copy));
      } else if([method isEqual:@"graph.update"]) {
        allowed(@[@"definition",@"dryRun"]);NSMutableDictionary *replacement=[object(p[@"definition"]) mutableCopy];auto &previous=definition(replacement[@"id"]);
        NSMutableArray *nodes=[NSMutableArray array];for(id rawNode:array(replacement[@"nodes"],64)){NSMutableDictionary *node=[object(rawNode) mutableCopy];if(node[@"plugin"]){NSMutableDictionary *recipe=[object(node[@"plugin"]) mutableCopy];if(!recipe[@"state"]){const auto id=decodeNativeID(node[@"id"]);auto existing=std::find_if(previous.nodes.begin(),previous.nodes.end(),[&](const auto &n){return n.id==id;});require(existing!=previous.nodes.end(),"Unknown node identity");recipe[@"state"]=encodeSignalRecipe(existing->plugin)[@"state"];}node[@"plugin"]=recipe;}[nodes addObject:node];}replacement[@"nodes"]=nodes;
        auto updated=decodeSignalDefinition(replacement);affected=updated.id;
        // New identities are allocated by graph.node.add, never supplied by a client.
        for(const auto &node:updated.nodes)require(std::any_of(previous.nodes.begin(),previous.nodes.end(),[&](const auto &n){return n.id==node.id&&n.kind==node.kind;}),"Add nodes using graph.node.add; node identities and kinds cannot be replaced");
        previous=std::move(updated);
      } else if([method isEqual:@"graph.remove"]) {
        allowed(@[@"graph",@"dryRun"]);affected=definition(p[@"graph"]).id;
        require(std::none_of(graph.assignments.begin(),graph.assignments.end(),[&](const auto &a){return a.graph==affected;})&&std::none_of(graph.commands.begin(),graph.commands.end(),[&](const auto &c){return c.graph==affected;}),"Remove this subgraph's assignments and pattern commands before deleting it");
        std::erase_if(graph.library,[&](const auto &d){return d.id==affected;});
      } else if([method isEqual:@"graph.node.add"]) {
        allowed(@[@"graph",@"kind",@"name",@"x",@"y",@"slot",@"plugin",@"insertAfter",@"dryRun"]);auto &d=definition(p[@"graph"]);affected=d.id;
        SignalNode n;n.id=next.makeEntity().id;nodeID=n.id;n.kind=decodeSignalNodeKind(p[@"kind"]);require(n.kind!=SignalNodeKind::Input&&n.kind!=SignalNodeKind::Output,"Each subgraph already has Input and Output nodes");
        n.name=string(p[@"name"]?:signalNodeKind(n.kind),256).UTF8String;n.x=number(p[@"x"]?:@300,-100000,100000);n.y=number(p[@"y"]?:@100,-100000,100000);
        if(n.kind==SignalNodeKind::Plugin){require(bool(p[@"slot"])!=bool(p[@"plugin"]),"Supply a rack slot or a plugin recipe");
          if(p[@"slot"]){const auto slot=pluginSlot();auto source=_plugins[slot];if(!_audio->active()&&_pluginError.empty()&&!_audio->hasAutomatedState())source=_audio->pluginStates().at(slot);require(!source.descriptor.instrument&&source.descriptor.type!=kAudioUnitType_MusicDevice,"Subgraph inserts require an effect plugin");const auto &s=source.descriptor;
            n.plugin={s.format,s.name,s.path,s.classID,s.type,s.subtype,s.manufacturer,source.state,source.auxiliaryInputs,source.auxiliaryOutputs};
            n.name=string(p[@"name"]?:@(s.name.c_str()),256).UTF8String;
          }else n.plugin=decodeSignalRecipe(p[@"plugin"]);
        } else require(!p[@"slot"]&&!p[@"plugin"],"Only plugin nodes accept plugin recipes");
        if(p[@"insertAfter"]){require(n.kind==SignalNodeKind::Plugin,"Only effects can be inserted into an audio chain");const auto after=decodeNativeID(p[@"insertAfter"]);auto edge=std::find_if(d.audio.begin(),d.audio.end(),[&](const auto &e){return e.source==after&&e.output==0&&e.input==0;});require(edge!=d.audio.end()&&std::count_if(d.audio.begin(),d.audio.end(),[&](const auto &e){return e.source==after&&e.output==0;})==1,"Insert after a node with one main audio destination");edge->source=nodeID;d.audio.push_back({after,nodeID});}
        d.nodes.push_back(std::move(n));
      } else if([method isEqual:@"graph.node.remove"]) {
        allowed(@[@"graph",@"node",@"dryRun"]);auto &d=definition(p[@"graph"]);affected=d.id;nodeID=decodeNativeID(p[@"node"]);
        auto n=std::find_if(d.nodes.begin(),d.nodes.end(),[&](const auto &v){return v.id==nodeID;});require(n!=d.nodes.end()&&n->kind!=SignalNodeKind::Input&&n->kind!=SignalNodeKind::Output,"Select an existing processor or modulation node");
        d.nodes.erase(n);std::erase_if(d.audio,[&](const auto &e){return e.source==nodeID||e.target==nodeID;});std::erase_if(d.modulation,[&](const auto &e){return e.source==nodeID||e.target==nodeID;});
      } else if([method isEqual:@"graph.assign"]) {
        allowed(@[@"target",@"graph",@"amount",@"wet",@"dryRun"]);const auto target=bus(p[@"target"]);std::erase_if(graph.assignments,[&](const auto &a){return a.target==target;});
        if(p[@"graph"]!=NSNull.null){affected=definition(p[@"graph"]).id;graph.assignments.push_back({target,affected,number(p[@"amount"]?:@1,0,1),number(p[@"wet"]?:@1,0,1)});}
      } else if([method isEqual:@"graph.layout.set"]) {
        allowed(@[@"positions",@"reset",@"dryRun"]);if(p[@"reset"]&&boolean(p[@"reset"]))graph.layout.clear();for(id raw:array(p[@"positions"]?:@[],8192)){auto v=object(raw);keys(v,@[@"node",@"x",@"y"]);graph.layout[string(v[@"node"],256).UTF8String]={number(v[@"x"],0,100000),number(v[@"y"],0,100000)};}
      } else if([method isEqual:@"graph.routes.set"]) {
        allowed(@[@"inputs",@"outputs",@"dryRun"]);
        if(p[@"inputs"]){graph.inputs.clear();for(id raw:array(p[@"inputs"],128)){auto r=object(raw);keys(r,@[@"source",@"target",@"input",@"gainDB",@"preFader"]);graph.inputs.push_back({bus(r[@"source"]),bus(r[@"target"]),uint32_t(integer(r[@"input"],1,63)),number(r[@"gainDB"]?:@0,-96,12),r[@"preFader"]?boolean(r[@"preFader"]):false});}}
        if(p[@"outputs"]){graph.outputs.clear();for(id raw:array(p[@"outputs"],128)){auto r=object(raw);keys(r,@[@"source",@"target",@"output"]);graph.outputs.push_back({bus(r[@"source"]),bus(r[@"target"]),uint32_t(integer(r[@"output"],1,63))});}}
      } else if([method isEqual:@"graph.commands.set"]) {
        allowed(@[@"pattern",@"lanes",@"commands",@"dryRun"]);const auto index=uint16_t(integer(p[@"pattern"],0,UINT16_MAX));require(song.Patterns.IsValidPat(index),"Pattern does not exist");const auto pattern=next.patterns.at(index).id;
        if(p[@"lanes"])for(id rawLane:array(p[@"lanes"],240)){auto l=object(rawLane);keys(l,@[@"target",@"count"]);const auto target=bus(l[@"target"]);auto count=uint8_t(integer(l[@"count"],0,8));if(count)graph.lanes[target]=count;else graph.lanes.erase(target);}
        if(p[@"commands"]){std::erase_if(graph.commands,[&](const auto &c){return c.pattern==pattern;});for(id rawCommand:array(p[@"commands"],65536)){NSMutableDictionary *c=[object(rawCommand) mutableCopy];require(c[@"pattern"]==nil,"Pattern is supplied once for the command set");c[@"pattern"]=nativeID(pattern);if([c[@"kind"] isEqual:@"clear"]&&!c[@"graph"])c[@"graph"]=@"";graph.commands.push_back(decodeSignalCommand(c));}}
      } else throw Error(-32601,"Unknown graph method");
      try{next.validate(song);}catch(const std::invalid_argument &e){throw Error(-32602,e.what());}
      const bool changed=next!=_document->native();if(changed&&!dry){if(next.mixer!=_document->native().mixer||!sameSignalProcessing(next.signal,_document->native().signal))_audio->stop();_document->annotate([&](NativeSong &native){native=next;});}
      result=@{@"dryRun":@(dry),@"wouldChange":@(changed),@"graph":affected?nativeID(affected):@"",@"node":nodeID?nativeID(nodeID):@""};
