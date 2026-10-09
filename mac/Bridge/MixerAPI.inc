    } else if ([method isEqual:@"mixer.meters"]) {
      allowed(@[]); result = @{@"meters": [self mixerMeters], @"playing": @(_audio->playing())};
    } else if ([method isEqual:@"mixer.get"]) {
      allowed(@[@"includeImplicit"]);
      const auto &native = _document->native();
      NativeSong implicit;
      const bool showImplicit = p[@"includeImplicit"] && boolean(p[@"includeImplicit"]) && !native.mixer.active();
      if (showImplicit) { implicit.tracks = native.tracks; implicit.nextID = native.nextID; implicit.masterID = native.masterID; implicit.mixer=native.mixer; implicit.ensureMixer(); }
      NSMutableDictionary *data = [encodeMixer(showImplicit ? implicit.mixer : native.mixer) mutableCopy];
      data[@"active"] = @(native.mixer.active());
      data[@"implicit"] = @(showImplicit);
      NSMutableArray *available = [NSMutableArray array];
      for (size_t i = 0; i < _plugins.size(); ++i) {
        const auto &plugin = _plugins[i];
        const auto audioBuses = _audio->pluginBuses(i);
        uint32_t count = 0; for (const auto &b : audioBuses) if (!b.input) ++count;
        [available addObject:@{@"id": @(plugin.instanceID.c_str()), @"slot": @(i), @"name": @(plugin.descriptor.name.c_str()),
          @"instrument": @(plugin.descriptor.instrument || plugin.descriptor.type == kAudioUnitType_MusicDevice),
          @"bypass": @(plugin.bypass), @"outputBuses": @(count), @"audioBuses": pluginBusDictionaries(audioBuses)}];
      }
      data[@"meters"] = [self mixerMeters]; data[@"plugins"] = available; data[@"sampleRate"] = @(_audio->sampleRate());
      data[@"latencySeconds"] = @(_audio->active() ? _audio->pluginLatency() : 0);
      data[@"playing"] = @(_audio->playing());
      result = data;
    } else if ([method hasPrefix:@"mixer."]) {
      const bool preview = p[@"preview"] ? boolean(p[@"preview"]) : false;
      NativeSong candidate;
      // Drag previews need only a bounded routing/control snapshot; copying
      // every pattern envelope at pointer-event frequency would stall the UI.
      if (preview) candidate.mixer = _document->native().mixer;
      else candidate = _document->native();
      auto &graph = candidate.mixer;
      const auto &original = _document->native().mixer;
      if (![method isEqual:@"mixer.enable"] && ![method isEqual:@"mixer.inserts.detach"] && !preview) candidate.ensureMixer();
      auto findBus = [&](id identifier) -> MixerBus & {
        auto id = decodeNativeID(identifier);
        auto found = std::find_if(graph.buses.begin(), graph.buses.end(), [&](const auto &bus) { return bus.id == id; });
        require(found != graph.buses.end(), "Mixer bus does not exist"); return *found;
      };
      auto masterID = [&]() {
        auto found = std::find_if(graph.buses.begin(), graph.buses.end(), [](const auto &bus) { return bus.kind == MixerBusKind::Master; });
        require(found != graph.buses.end(), "Enable the mixer first"); return found->id;
      };
      auto plugin = [&](id identifier, bool instrument) -> std::string {
        auto text = string(identifier, 128);
        auto found = std::find_if(_plugins.begin(), _plugins.end(), [&](const auto &p) { return p.instanceID == text.UTF8String; });
        require(found != _plugins.end(), "Plugin instance does not exist");
        require((found->descriptor.instrument || found->descriptor.type == kAudioUnitType_MusicDevice) == instrument,
                instrument ? "Select an instrument plugin" : "Insert chains require effect plugins");
        return text.UTF8String;
      };
      uint64_t affected = 0;
      if ([method isEqual:@"mixer.enable"]) {
        allowed(@[@"enabled", @"dryRun"]);
        bool enabled = p[@"enabled"] ? boolean(p[@"enabled"]) : true;
        if (enabled) candidate.ensureMixer();
        else { require(graph.detachedChains.empty()&&graph.disconnectedMainInputs.empty()&&graph.pluginConnections.empty()&&candidate.signal.stageConnections.empty()&&std::none_of(candidate.signal.songSources.begin(),candidate.signal.songSources.end(),[](const auto &s){return s.audioStage!=0;})&&!graph.masterOutputDisconnected,"Reconnect cut cables and place detached chains before disabling routing"); auto detached=std::move(graph.detached); graph = {}; graph.detached=std::move(detached); candidate.noteTracks.clear(); candidate.signal.assignments.clear(); candidate.signal.commands.clear(); candidate.signal.lanes.clear();candidate.signal.inputs.clear();candidate.signal.outputs.clear(); }
      } else if ([method isEqual:@"mixer.bus.add"]) {
        allowed(@[@"kind", @"name", @"output", @"sendFrom", @"position", @"dryRun"]);
        const auto kind = decodeMixerKind(p[@"kind"]);
        require(kind == MixerBusKind::Group || kind == MixerBusKind::Return, "Add a group or a return; track buses follow song tracks");
        const auto output = p[@"output"] ? decodeNativeID(p[@"output"]) : masterID();
        affected = candidate.makeEntity().id;
        graph.buses.push_back({affected, output, kind, string(p[@"name"] ?: (kind == MixerBusKind::Group ? @"Group" : @"Return"), 256).UTF8String});
        if(p[@"sendFrom"]) {require(kind==MixerBusKind::Return,"An automatic send requires a return bus");auto &source=findBus(p[@"sendFrom"]);require(source.kind!=MixerBusKind::Master,"Master cannot send upstream");source.sends.push_back({affected,-96,false,false});}
        if(p[@"position"]) {auto position=object(p[@"position"]);keys(position,@[@"x",@"y"]);candidate.signal.layout[std::string(nativeID(affected).UTF8String)]={number(position[@"x"],0,100000),number(position[@"y"],0,100000)};}
      } else if ([method isEqual:@"mixer.bus.set"]) {
        allowed(@[@"bus", @"name", @"color", @"output", @"preGainDB", @"prePan", @"gainDB", @"pan", @"width", @"timingMS", @"mute", @"solo", @"inserts", @"mainOutputConnected", @"dryRun", @"preview"]);
        auto &bus = findBus(p[@"bus"]); affected = bus.id;
        if(p[@"mainOutputConnected"]){require(bus.kind==MixerBusKind::Master,"Only Master has the final output cable");graph.masterOutputDisconnected=!boolean(p[@"mainOutputConnected"]);}
        if (p[@"name"]) bus.name = string(p[@"name"], 256).UTF8String;
        if (p[@"color"]) bus.color = uint32_t(integer(p[@"color"], 0, 0xffffff));
        if (p[@"output"]) bus.output = p[@"output"] == NSNull.null ? 0 : decodeNativeID(p[@"output"]);
        if (p[@"preGainDB"]) bus.preGainDB = number(p[@"preGainDB"], -96, 24);
        if (p[@"prePan"]) bus.prePan = number(p[@"prePan"], -1, 1);
        if (p[@"gainDB"]) bus.gainDB = number(p[@"gainDB"], -96, 24);
        if (p[@"pan"]) bus.pan = number(p[@"pan"], -1, 1);
        if (p[@"width"]) bus.width = number(p[@"width"], 0, 2);
        if (p[@"timingMS"]) bus.timingMS = number(p[@"timingMS"], -500, 500);
        if (p[@"mute"]) bus.mute = boolean(p[@"mute"]);
        if (p[@"solo"]) bus.solo = boolean(p[@"solo"]);
        if (p[@"inserts"]) {
          bus.inserts.clear();
          for (id insert in array(p[@"inserts"], 32)) {auto id=plugin(insert,false);bus.inserts.push_back(id);std::erase(graph.detached,id);for(auto &chain:graph.detachedChains)std::erase(chain.plugins,id);std::erase(graph.disconnectedMainInputs,id);}
          std::erase_if(graph.detachedChains,[](const auto &c){return c.plugins.empty();});for(const auto &chain:graph.detachedChains)std::erase(graph.disconnectedMainInputs,chain.plugins.front());
        }
      } else if ([method isEqual:@"mixer.inserts.move"] || [method isEqual:@"mixer.inserts.detach"]) {
        const bool detach=[method isEqual:@"mixer.inserts.detach"];
        allowed(detach ? @[@"plugins",@"positions",@"dryRun"] : @[@"plugins", @"target", @"before", @"positions", @"dryRun"]);
        std::vector<std::string> rack, moving;
        for(const auto &item : _plugins) if(!item.descriptor.instrument && item.descriptor.type != kAudioUnitType_MusicDevice) rack.push_back(item.instanceID);
        for(id raw : array(p[@"plugins"],32)) moving.push_back(plugin(raw,false));
        require(!moving.empty(),"Select an effect chain");
        if(detach && std::find(graph.detached.begin(),graph.detached.end(),moving.front())==graph.detached.end())candidate.ensureMixer();
        if(!detach)affected = decodeNativeID(p[@"target"]);
        const std::string before = p[@"before"] && p[@"before"] != NSNull.null ? string(p[@"before"],128).UTF8String : "";
        try { if(detach){const auto previousMixer=graph;const auto previousSignal=candidate.signal;
            detachMixerInserts(graph,rack,moving,[&]{return candidate.makeEntity().id;});
            preserveSongGroupDetachment(candidate.signal,previousSignal,previousMixer,graph,rack);
          }else moveMixerInserts(graph,rack,moving,affected,before); }
        catch(const std::invalid_argument &e) { throw Error(-32602,e.what()); }
        for(id raw:array(p[@"positions"]?:@[],64)){auto v=object(raw);keys(v,@[@"node",@"x",@"y"]);const std::string key=string(v[@"node"],256).UTF8String;auto group=std::find_if(candidate.signal.groups.begin(),candidate.signal.groups.end(),[&](const auto &g){return key=="n"+std::to_string(g.id);});
          if(group!=candidate.signal.groups.end()){const auto members=songSignalGroupNodes(candidate.signal,group->id);require(std::any_of(members.begin(),members.end(),[](const auto &m){return m.starts_with("plugin:");})&&std::all_of(members.begin(),members.end(),[&](const auto &m){return !m.starts_with("plugin:")||std::find(moving.begin(),moving.end(),m.substr(7))!=moving.end();}),"Move every group processor together");moveSongSignalGroup(candidate.signal,group->id,number(v[@"x"],0,100000),number(v[@"y"],0,100000));}
          else {require(std::any_of(moving.begin(),moving.end(),[&](const auto &id){return key=="plugin:"+id;}),"Position must belong to a moved insert");candidate.signal.layout[key]={number(v[@"x"],0,100000),number(v[@"y"],0,100000)};}}

      } else if ([method isEqual:@"mixer.bus.remove"]) {
        allowed(@[@"bus", @"dryRun"]);
        const auto bus = findBus(p[@"bus"]); affected = bus.id;
        require(bus.kind == MixerBusKind::Group || bus.kind == MixerBusKind::Return, "Only groups and returns can be removed");
        std::erase_if(candidate.noteTracks, [&](const auto &track) { return track.bus == affected; });
        std::erase_if(candidate.signal.assignments,[&](const auto &a){return a.target==affected;});
        std::erase_if(candidate.signal.commands,[&](const auto &c){return c.target==affected;});candidate.signal.lanes.erase(affected);
        std::erase_if(candidate.signal.inputs,[&](const auto &r){return r.source==affected||r.target==affected;});
        std::erase_if(candidate.signal.stageConnections,[&](const auto &r){return r.source.stage==affected||r.target.stage==affected;});
        for(auto &s:candidate.signal.songSources)if(s.audioStage==affected){s.audioStage=0;s.output=0;s.preFader=false;}
        std::erase_if(candidate.signal.outputs,[&](const auto &r){return r.source==affected||r.target==affected;});
        std::erase_if(graph.buses, [&](const auto &item) { return item.id == affected; });
        for (auto &item : graph.buses) {
          if (item.output == affected) item.output = bus.output;
          std::erase_if(item.sends, [&](const auto &send) { return send.target == affected; });
        }
        for (auto &source : graph.instruments) if (source.target == affected) source.target = bus.output ? bus.output : masterID();
        std::erase_if(graph.sidechains, [&](const auto &side) { return side.source == affected; });
        // Keep owned effects in processing order on the removed bus's output.
        auto &destination = findBus(nativeID(bus.output ? bus.output : masterID()));
        destination.inserts.insert(destination.inserts.begin(), bus.inserts.begin(), bus.inserts.end());
      } else if ([method isEqual:@"mixer.sends.set"]) {
        allowed(@[@"bus", @"sends", @"dryRun"]);
        auto &bus = findBus(p[@"bus"]); affected = bus.id; bus.sends.clear();
        for (id raw in array(p[@"sends"], 16)) {
          auto send = object(raw); keys(send, @[@"target", @"gainDB", @"preFader", @"enabled"]);
          bus.sends.push_back({decodeNativeID(send[@"target"]), number(send[@"gainDB"] ?: @(-12), -96, 12),
            send[@"preFader"] ? boolean(send[@"preFader"]) : false, send[@"enabled"] ? boolean(send[@"enabled"]) : true});
        }
      } else if ([method isEqual:@"mixer.plugin.connection.set"]) {
        allowed(@[@"source",@"output",@"target",@"input",@"gainDB",@"enabled",@"replace",@"dryRun"]);
        auto endpoint=[&](NSDictionary *v){MixerPluginConnection r;r.source=string(v[@"source"],128).UTF8String;r.output=uint32_t(integer(v[@"output"],0,63));r.target=string(v[@"target"],128).UTF8String;r.input=uint32_t(integer(v[@"input"],0,63));return r;};
        auto value=endpoint(p);std::optional<MixerPluginConnection> old;
        if(p[@"replace"]){auto v=object(p[@"replace"]);keys(v,@[@"source",@"output",@"target",@"input"]);old=endpoint(v);}
        auto previous=std::find_if(graph.pluginConnections.begin(),graph.pluginConnections.end(),[&](const auto &r){const auto &key=old?*old:value;return r.source==key.source&&r.output==key.output&&r.target==key.target&&r.input==key.input;});
        if(previous!=graph.pluginConnections.end()){value.gainDB=previous->gainDB;value.enabled=previous->enabled;}
        if(p[@"gainDB"])value.gainDB=number(p[@"gainDB"],-96,12);if(p[@"enabled"])value.enabled=boolean(p[@"enabled"]);
        for(bool input:{false,true}){const auto &id=input?value.target:value.source;auto found=std::find_if(_plugins.begin(),_plugins.end(),[&](const auto &p){return p.instanceID==id;});require(found!=_plugins.end(),"Plugin endpoint no longer exists");
          const bool instrument=found->descriptor.instrument||found->descriptor.type==kAudioUnitType_MusicDevice;
          const auto ports=_audio->pluginBuses(size_t(found-_plugins.begin()));const auto port=input?value.input:value.output;require(std::any_of(ports.begin(),ports.end(),[&](const auto &b){return b.input==input&&b.index==port&&b.supported;}),"Choose an available physical plugin audio port");
          if(!instrument)rootDetachedMixerPlugin(graph,id,[&]{return candidate.makeEntity().id;});
        }
        setMixerPluginConnection(graph,value,old?&*old:nullptr);
        if(old){const SignalRouteIdentity before{"plugin-connection","plugin:"+old->source,"plugin:"+old->target,{},"post-gain",old->input,old->output},after{"plugin-connection","plugin:"+value.source,"plugin:"+value.target,{},"post-gain",value.input,value.output};for(auto &g:candidate.signal.groups)for(auto &r:g.dryRoutes){if(r.input==before)r.input=after;if(r.output==before)r.output=after;}}
        if(old)for(auto &path:candidate.signal.presentation.cables)if(!path.modulation&&path.source=="plugin:"+old->source&&path.target=="plugin:"+old->target&&path.output==old->output&&path.input==old->input){path.source="plugin:"+value.source;path.target="plugin:"+value.target;path.output=value.output;path.input=value.input;}
      } else if ([method isEqual:@"mixer.sidechains.set"]) {
        allowed(@[@"plugin", @"input", @"sources", @"dryRun"]);
        std::string targetPlugin = string(p[@"plugin"], 128).UTF8String;
        const auto input = uint32_t(integer(p[@"input"], 0, 63));
        const auto sources = array(p[@"sources"], 128);
        if (sources.count) {
          auto known=std::find_if(_plugins.begin(),_plugins.end(),[&](const auto &v){return v.instanceID==targetPlugin;});require(known!=_plugins.end(),"Plugin endpoint no longer exists");
          if(!known->descriptor.instrument&&known->descriptor.type!=kAudioUnitType_MusicDevice)rootDetachedMixerPlugin(graph,targetPlugin,[&]{return candidate.makeEntity().id;});
          auto found = std::find_if(_plugins.begin(), _plugins.end(), [&](const auto &p) { return p.instanceID == targetPlugin; });
          const auto available = _audio->pluginBuses(size_t(found - _plugins.begin()));
          require(std::any_of(available.begin(), available.end(), [&](const auto &b) { return b.input && b.index == input && b.supported; }),
                  "Select an available plugin input; connected auxiliary ports enable on playback");
        }
        std::erase_if(graph.sidechains, [&](const auto &side) { return side.plugin == targetPlugin && side.input == input; });
        for (id raw : sources) {
          auto side = object(raw); keys(side, @[@"source", @"gainDB", @"preFader", @"enabled"]);
          graph.sidechains.push_back({findBus(side[@"source"]).id, targetPlugin, input, number(side[@"gainDB"] ?: @0, -96, 12),
            side[@"preFader"] ? boolean(side[@"preFader"]) : false, side[@"enabled"] ? boolean(side[@"enabled"]) : true});
        }
      } else if ([method isEqual:@"mixer.instrument.route"] || [method isEqual:@"mixer.plugin.route"]) {
        allowed(@[@"plugin", @"output", @"target", @"targets", @"disconnected", @"dryRun"]);
        std::string id = string(p[@"plugin"], 128).UTF8String;
        const auto output = uint32_t(integer(p[@"output"] ?: @0, 0, 63));
        const bool disconnected = p[@"disconnected"] ? boolean(p[@"disconnected"]) : false;
        require(!disconnected || p[@"target"] == NSNull.null,"A disconnected output requires a null target");
        require(bool(p[@"targets"]) != bool(p[@"target"]), "Supply target or targets, not both");
        require(!p[@"targets"] || !p[@"disconnected"], "Use an empty targets array to disconnect");
        std::vector<uint64_t> targets;
        if(p[@"targets"])for(auto raw:array(p[@"targets"],128)){auto id=findBus(raw).id;require(std::find(targets.begin(),targets.end(),id)==targets.end(),"Duplicate output destination");targets.push_back(id);}
        const bool remove = p[@"target"] == NSNull.null && !disconnected;
        const auto target = remove || disconnected || p[@"targets"] ? 0 : findBus(p[@"target"]).id; affected = target;
        if (!remove) {
          auto descriptor=std::find_if(_plugins.begin(),_plugins.end(),[&](const auto &v){return v.instanceID==id;});require(descriptor!=_plugins.end(),"Plugin instance does not exist");
          const bool instrument=descriptor->descriptor.instrument||descriptor->descriptor.type==kAudioUnitType_MusicDevice;
          id = plugin(p[@"plugin"], instrument);

          if(!instrument){rootDetachedMixerPlugin(graph,id,[&]{return candidate.makeEntity().id;});const auto projected=projectMixerDetachedChains(graph);require(std::any_of(projected.buses.begin(),projected.buses.end(),[&](const auto &b){return std::find(b.inserts.begin(),b.inserts.end(),id)!=b.inserts.end();}),"Effect has no processing owner");}
          auto found = std::find_if(_plugins.begin(), _plugins.end(), [&](const auto &p) { return p.instanceID == id; });
          const auto available = _audio->pluginBuses(size_t(found - _plugins.begin()));
          require(std::any_of(available.begin(), available.end(), [&](const auto &b) { return !b.input && b.index == output && b.supported; }),
                  "Select an available plugin output; connected auxiliary ports enable on playback");
        }
        // Keep unchanged destinations in place. Repeating a target set (even
        // in another order) must not reorder unrelated cables or create Undo.
        if(!p[@"targets"] || targets.empty()) {
          targets.clear();
          if(!remove) targets.push_back(target);
        }
        std::erase_if(graph.instruments, [&](const auto &source) {
          return source.plugin == id && source.output == output
            && std::find(targets.begin(), targets.end(), source.target) == targets.end();
        });
        for(auto bus : targets) {
          const auto existing = std::find_if(graph.instruments.begin(), graph.instruments.end(), [&](const auto &source) {
            return source.plugin == id && source.output == output && source.target == bus;
          });
          if(existing == graph.instruments.end()) graph.instruments.push_back({id, bus, output});
        }
      } else throw Error(-32601, "Unknown mixer method");
      std::vector<uint64_t> tracks; for (const auto &[index, track] : _document->native().tracks) tracks.push_back(track.id);
      // Validate the complete projected DAG, including channel graph stages.
      // A routing preview retains the current signal graph without copying its envelopes.
      const auto &signal = preview ? _document->native().signal : candidate.signal;
      const auto routingGraph = signalRoutingGraph(graph, signal);
      std::vector<MixerProcessorInfo> processors;
      for (size_t slot = 0; slot < _plugins.size(); ++slot) {
        const auto &p = _plugins[slot]; uint32_t count = 1; uint64_t enabled = 1, inputs = 0;
        for (const auto &b : _audio->pluginBuses(slot)) if (b.supported) {
          if (b.input) inputs |= uint64_t(1) << b.index;
          else { count = std::max(count, b.index + 1); enabled |= uint64_t(1) << b.index; }
        }
        // Host bypass retains and clocks the processor; it is not a missing endpoint.
        processors.push_back({p.instanceID, 0, 0, p.descriptor.instrument || p.descriptor.type == kAudioUnitType_MusicDevice, false, count, enabled, inputs});
        processors.back().scheduledSource = processors.back().instrument;
      }
      for (const auto &bus : graph.buses) {
        const auto in = signalStagePorts(signal, bus.id, true), out = signalStagePorts(signal, bus.id, false);
        const bool used = std::any_of(signal.assignments.begin(), signal.assignments.end(), [&](const auto &a) { return a.target == bus.id; }) ||
          std::any_of(signal.commands.begin(), signal.commands.end(), [&](const auto &c) { return c.target == bus.id && (c.kind == SignalCommandKind::Row || c.kind == SignalCommandKind::Start); });
        if (in.empty() && out.empty() && !used) continue;
        uint64_t inputs = 0, outputs = 1; uint32_t count = 1;
        for (auto p : in) inputs |= uint64_t(1) << p;
        for (auto p : out) { outputs |= uint64_t(1) << p; count = std::max(count, p + 1); }
        processors.push_back({signalBusIdentity(bus.id), 0, 0, false, false, count, outputs, inputs});
      }
      MixerPlan plan;
      try {
        validatePluginCapacity(_plugins, graph.buses.size());
        if (!preview) candidate.validate(song);
        plan = compileMixer(routingGraph, tracks, processors, uint32_t(_audio->sampleRate()));
      } catch (const std::invalid_argument &e) { throw Error(-32602, e.what()); }
      auto structural = graph;
      if (structural.buses.size() == original.buses.size()) for (size_t i = 0; i < structural.buses.size(); ++i) {
        auto &bus = structural.buses[i]; const auto &old = original.buses[i];
        bus.preGainDB = old.preGainDB; bus.prePan = old.prePan; bus.gainDB = old.gainDB; bus.pan = old.pan; bus.width = old.width;
        bus.mute = old.mute; bus.solo = old.solo; bus.name = old.name; bus.color = old.color;
      }
      const bool controlsOnly = structural == original;
      const bool dryRun = p[@"dryRun"] ? boolean(p[@"dryRun"]) : false;
      require(!preview || (controlsOnly && graph.active()), "Only mixer gain, balance, width, mute and solo can be previewed live");
      const bool different = graph != original || (!preview && candidate.signal.layout != _document->native().signal.layout);
      if (!dryRun && (different || preview || ([method isEqual:@"mixer.bus.set"] && graph.active()))) {
        std::unique_ptr<MixerTransition::Plan> routing;
        if (controlsOnly && graph.active()) {
          std::vector<MixerControls> controls;
          const auto projected=projectMixerDetachedChains(graph);
          for (size_t i = 0; i < projected.buses.size(); ++i) {
            const auto &bus = projected.buses[i]; controls.push_back({bus.preGainDB, bus.gainDB, bus.pan, bus.width, plan.nodes[i].audible, bus.prePan});
          }
          if (!_audio->mixerControls(controls)) throw Error(-32002, "Mixer control queue is busy; retry the same revision");
        } else if(!controlsOnly) {
          if(!_audio->mixerRoutingReady())throw Error(-32002,"Routing transition is busy; retry the same revision");
          routing=_audio->prepareMixerRouting(candidate);
          if(!routing && _audio->active())throw Error(-32002,"This routing change needs a stopped transport; playback and routing were preserved");
        }
        auto publish=[&]{if(routing && !_audio->publishMixerRouting(routing))throw Error(-32002,"Routing publication was refused; retry the same revision");};
        if (!preview) _document->annotate([&](NativeSong &n) { n = candidate; },publish);
        else publish();
      }
      result = @{@"mixer": encodeMixer(graph), @"wouldChange": @(different), @"preview": @(preview),
        @"bus": affected ? (id)nativeID(affected) : (id)NSNull.null, @"controlsOnly": @(controlsOnly)};
