      if([method hasPrefix:@"graph.note."]) {
        auto &routing=graph.noteRouting;
        auto instrument=[&](id raw){const auto id=decodeNativeID(raw);auto found=std::find_if(next.instruments.begin(),next.instruments.end(),[&](const auto &v){return v.second.id==id;});require(found!=next.instruments.end(),"Note instrument does not exist");
          require(std::any_of(_plugins.begin(),_plugins.end(),[&](const auto &plugin){const auto assignments=pluginAssignments(plugin);return std::any_of(assignments.begin(),assignments.end(),[&](const auto &a){return a.instrument==found->first;});}),"Note cables carry plugin-instrument events; assign a plugin instrument first");return id;};
        auto sourceKind=[&](id raw){NSString *kind=string(raw,16);require([kind isEqual:@"channel"]||[kind isEqual:@"instrument"],"Note sourceKind must be channel or instrument");return [kind isEqual:@"channel"]?NoteSourceKind::Channel:NoteSourceKind::Instrument;};
        auto validateRoute=[&](const NoteRoute &r){require(std::any_of(_plugins.begin(),_plugins.end(),[&](const auto &plugin){return plugin.instanceID==r.plugin&&(plugin.descriptor.instrument||plugin.descriptor.type==kAudioUnitType_MusicDevice);}),"Note destination must be an existing plugin instrument");if(r.sourceKind==NoteSourceKind::Instrument)instrument(nativeID(r.source));};
        if([method isEqual:@"graph.note.connect"]) {
          allowed(@[@"sourceKind",@"source",@"plugin",@"midiChannel",@"enabled",@"suppressAssignment",@"dryRun"]);
          NoteRoute r;r.id=next.makeEntity().id;r.sourceKind=sourceKind(p[@"sourceKind"]);r.source=decodeNativeID(p[@"source"]);r.plugin=string(p[@"plugin"],256).UTF8String;r.midiChannel=uint8_t(integer(p[@"midiChannel"]?:@0,0,16));r.enabled=p[@"enabled"]?boolean(p[@"enabled"]):true;validateRoute(r);
          if(p[@"suppressAssignment"]&&boolean(p[@"suppressAssignment"])){require(r.sourceKind==NoteSourceKind::Instrument,"Only an instrument cable can replace an implicit assignment");if(std::find(routing.suppressedAssignments.begin(),routing.suppressedAssignments.end(),r.source)==routing.suppressedAssignments.end())routing.suppressedAssignments.push_back(r.source);replaceNoteAssignmentGeometry(graph.presentation,r);}
          nodeID=r.id;routing.routes.push_back(std::move(r));
        } else if([method isEqual:@"graph.note.update"]) {
          allowed(@[@"id",@"sourceKind",@"source",@"plugin",@"midiChannel",@"enabled",@"dryRun"]);nodeID=decodeNativeID(p[@"id"]);
          auto found=std::find_if(routing.routes.begin(),routing.routes.end(),[&](const auto &r){return r.id==nodeID;});require(found!=routing.routes.end(),"Note cable no longer exists; refresh the graph");
          if(p[@"sourceKind"])found->sourceKind=sourceKind(p[@"sourceKind"]);if(p[@"source"])found->source=decodeNativeID(p[@"source"]);if(p[@"plugin"])found->plugin=string(p[@"plugin"],256).UTF8String;if(p[@"midiChannel"])found->midiChannel=uint8_t(integer(p[@"midiChannel"],0,16));if(p[@"enabled"])found->enabled=boolean(p[@"enabled"]);validateRoute(*found);
        } else if([method isEqual:@"graph.note.disconnect"]) {
          allowed(@[@"id",@"instrument",@"dryRun"]);require(bool(p[@"id"])!=bool(p[@"instrument"]),"Specify either a cable id or an implicit instrument assignment");
          if(p[@"id"]){nodeID=decodeNativeID(p[@"id"]);require(std::erase_if(routing.routes,[&](const auto &r){return r.id==nodeID;})==1,"Note cable no longer exists; refresh the graph");}
          else{const auto id=instrument(p[@"instrument"]);if(std::find(routing.suppressedAssignments.begin(),routing.suppressedAssignments.end(),id)==routing.suppressedAssignments.end())routing.suppressedAssignments.push_back(id);}
        } else if([method isEqual:@"graph.note.restoreAssignment"]) {
          allowed(@[@"instrument",@"dryRun"]);std::erase(routing.suppressedAssignments,instrument(p[@"instrument"]));
        } else throw Error(-32601,"Unknown note cable method");
        reconcileNoteCableGeometry(graph.presentation,routing);
      } else
