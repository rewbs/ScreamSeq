    } else if ([method isEqual:@"mixer.meters"]) {
      allowed(@[]); result = @{@"meters": [self mixerMeters], @"playing": @(_audio->playing())};
    } else if ([method isEqual:@"mixer.get"]) {
      allowed(@[]);
      const auto &native = _document->native();
      NSMutableDictionary *data = [encodeMixer(native.mixer) mutableCopy];
      data[@"active"] = @(native.mixer.active());
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
        if (enabled && !graph.active()) {
          const auto master = candidate.makeEntity().id;
          for (const auto &[channel, track] : candidate.tracks)
            graph.buses.push_back({track.id, master, MixerBusKind::Track, track.name.empty() ? "Track " + std::to_string(channel + 1) : track.name, track.color});
          graph.buses.push_back({master, 0, MixerBusKind::Master, "Master"});
        } else if (!enabled) { graph = {}; candidate.noteTracks.clear(); candidate.signal.assignments.clear(); candidate.signal.commands.clear(); candidate.signal.lanes.clear();candidate.signal.inputs.clear();candidate.signal.outputs.clear(); }
      } else if ([method isEqual:@"mixer.bus.add"]) {
        allowed(@[@"kind", @"name", @"output", @"dryRun"]);
        const auto kind = decodeMixerKind(p[@"kind"]);
        require(kind == MixerBusKind::Group || kind == MixerBusKind::Return, "Add a group or a return; track buses follow song tracks");
        const auto output = p[@"output"] ? decodeNativeID(p[@"output"]) : masterID();
        affected = candidate.makeEntity().id;
        graph.buses.push_back({affected, output, kind, string(p[@"name"] ?: (kind == MixerBusKind::Group ? @"Group" : @"Return"), 256).UTF8String});
      } else if ([method isEqual:@"mixer.bus.set"]) {
        allowed(@[@"bus", @"name", @"color", @"output", @"preGainDB", @"prePan", @"gainDB", @"pan", @"width", @"timingMS", @"mute", @"solo", @"inserts", @"dryRun", @"preview"]);
        auto &bus = findBus(p[@"bus"]); affected = bus.id;
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
          for (id insert in array(p[@"inserts"], 32)) bus.inserts.push_back(plugin(insert, false));
        }
      } else if ([method isEqual:@"mixer.bus.remove"]) {
        allowed(@[@"bus", @"dryRun"]);
        const auto bus = findBus(p[@"bus"]); affected = bus.id;
        require(bus.kind == MixerBusKind::Group || bus.kind == MixerBusKind::Return, "Only groups and returns can be removed");
        std::erase_if(candidate.noteTracks, [&](const auto &track) { return track.bus == affected; });
        std::erase_if(candidate.signal.assignments,[&](const auto &a){return a.target==affected;});
        std::erase_if(candidate.signal.commands,[&](const auto &c){return c.target==affected;});candidate.signal.lanes.erase(affected);
        std::erase_if(candidate.signal.inputs,[&](const auto &r){return r.source==affected||r.target==affected;});
        std::erase_if(candidate.signal.outputs,[&](const auto &r){return r.source==affected||r.target==affected;});
        std::erase_if(graph.buses, [&](const auto &item) { return item.id == affected; });
        for (auto &item : graph.buses) {
          if (item.output == affected) item.output = bus.output;
          std::erase_if(item.sends, [&](const auto &send) { return send.target == affected; });
        }
        for (auto &source : graph.instruments) if (source.target == affected) source.target = bus.output;
        std::erase_if(graph.sidechains, [&](const auto &side) { return side.source == affected; });
        // Keep owned effects in processing order on the removed bus's output.
        auto &destination = findBus(nativeID(bus.output));
        destination.inserts.insert(destination.inserts.begin(), bus.inserts.begin(), bus.inserts.end());
      } else if ([method isEqual:@"mixer.sends.set"]) {
        allowed(@[@"bus", @"sends", @"dryRun"]);
        auto &bus = findBus(p[@"bus"]); affected = bus.id; bus.sends.clear();
        for (id raw in array(p[@"sends"], 16)) {
          auto send = object(raw); keys(send, @[@"target", @"gainDB", @"preFader", @"enabled"]);
          bus.sends.push_back({decodeNativeID(send[@"target"]), number(send[@"gainDB"] ?: @(-12), -96, 12),
            send[@"preFader"] ? boolean(send[@"preFader"]) : false, send[@"enabled"] ? boolean(send[@"enabled"]) : true});
        }
      } else if ([method isEqual:@"mixer.sidechains.set"]) {
        allowed(@[@"plugin", @"input", @"sources", @"dryRun"]);
        std::string targetPlugin = string(p[@"plugin"], 128).UTF8String;
        const auto input = uint32_t(integer(p[@"input"], 1, 63));
        const auto sources = array(p[@"sources"], 128);
        if (sources.count) {
          targetPlugin = plugin(p[@"plugin"], false);
          auto found = std::find_if(_plugins.begin(), _plugins.end(), [&](const auto &p) { return p.instanceID == targetPlugin; });
          const auto available = _audio->pluginBuses(size_t(found - _plugins.begin()));
          require(std::any_of(available.begin(), available.end(), [&](const auto &b) { return b.input && b.index == input && b.active && b.supported; }),
                  "Enable an available auxiliary effect input before routing a sidechain");
        }
        std::erase_if(graph.sidechains, [&](const auto &side) { return side.plugin == targetPlugin && side.input == input; });
        for (id raw : sources) {
          auto side = object(raw); keys(side, @[@"source", @"gainDB", @"preFader", @"enabled"]);
          graph.sidechains.push_back({findBus(side[@"source"]).id, targetPlugin, input, number(side[@"gainDB"] ?: @0, -96, 12),
            side[@"preFader"] ? boolean(side[@"preFader"]) : false, side[@"enabled"] ? boolean(side[@"enabled"]) : true});
        }
      } else if ([method isEqual:@"mixer.instrument.route"] || [method isEqual:@"mixer.plugin.route"]) {
        allowed(@[@"plugin", @"output", @"target", @"dryRun"]);
        std::string id = string(p[@"plugin"], 128).UTF8String;
        const auto output = uint32_t(integer(p[@"output"] ?: @0, 0, 63));
        const bool remove = p[@"target"] == NSNull.null;
        const auto target = remove ? 0 : findBus(p[@"target"]).id; affected = target;
        if (!remove) {
          auto descriptor=std::find_if(_plugins.begin(),_plugins.end(),[&](const auto &v){return v.instanceID==id;});require(descriptor!=_plugins.end(),"Plugin instance does not exist");
          const bool instrument=descriptor->descriptor.instrument||descriptor->descriptor.type==kAudioUnitType_MusicDevice;
          id = plugin(p[@"plugin"], instrument);
          require(instrument||output>0,"Effect main output follows its insert chain; select an auxiliary output");
          if(!instrument)require(std::any_of(graph.buses.begin(),graph.buses.end(),[&](const auto &b){return std::find(b.inserts.begin(),b.inserts.end(),id)!=b.inserts.end();}),"Assign the effect to a bus before routing its auxiliary output");
          auto found = std::find_if(_plugins.begin(), _plugins.end(), [&](const auto &p) { return p.instanceID == id; });
          const auto available = _audio->pluginBuses(size_t(found - _plugins.begin()));
          require(std::any_of(available.begin(), available.end(), [&](const auto &b) { return !b.input && b.index == output && b.active && b.supported; }),
                  "Enable an available plugin output before routing it");
        }
        std::erase_if(graph.instruments, [&](const auto &source) { return source.plugin == id && source.output == output; });
        if (!remove) graph.instruments.push_back({std::move(id), target, output});
      } else throw Error(-32601, "Unknown mixer method");
      std::vector<uint64_t> tracks; for (const auto &[index, track] : _document->native().tracks) tracks.push_back(track.id);
      std::vector<MixerProcessorInfo> processors;
      for (size_t slot = 0; slot < _plugins.size(); ++slot) {
        const auto &p = _plugins[slot]; uint32_t count = 1; uint64_t enabled = 1, inputs = 0;
        for (const auto &b : _audio->pluginBuses(slot)) if (!b.input) { count = std::max(count, b.index + 1); if (b.active) enabled |= uint64_t(1) << b.index; }
        for (const auto &b : _audio->pluginBuses(slot)) if (b.input && b.index && b.active) inputs |= uint64_t(1) << b.index;
        processors.push_back({p.instanceID, 0, 0, p.descriptor.instrument || p.descriptor.type == kAudioUnitType_MusicDevice, p.bypass, count, enabled, inputs});
      }
      MixerPlan plan;
      try {
        validatePluginCapacity(_plugins, graph.buses.size());
        if (!preview) candidate.validate(song);
        plan = compileMixer(graph, tracks, processors, uint32_t(_audio->sampleRate()));
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
      const bool different = graph != original;
      if (!dryRun && (different || preview || ([method isEqual:@"mixer.bus.set"] && graph.active()))) {
        if (controlsOnly && graph.active()) {
          std::vector<MixerControls> controls;
          for (size_t i = 0; i < graph.buses.size(); ++i) {
            const auto &bus = graph.buses[i]; controls.push_back({bus.preGainDB, bus.gainDB, bus.pan, bus.width, plan.nodes[i].audible, bus.prePan});
          }
          if (!_audio->mixerControls(controls)) throw Error(-32002, "Mixer control queue is busy; retry the same revision");
        } else _audio->stop();
        if (!preview) _document->annotate([&](NativeSong &n) { n = candidate; });
      }
      result = @{@"mixer": encodeMixer(graph), @"wouldChange": @(different), @"preview": @(preview),
        @"bus": affected ? (id)nativeID(affected) : (id)NSNull.null, @"controlsOnly": @(controlsOnly)};
