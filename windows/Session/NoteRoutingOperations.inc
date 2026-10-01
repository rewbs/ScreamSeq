    if(method.starts_with("graph.note.")) {
      auto &routing=graph.noteRouting;const auto rack=host_.rack?host_.rack():host_.cachedRack;
      auto instrument=[&](const Json &raw){const auto target=identity(raw);const auto found=std::find_if(next.instruments.begin(),next.instruments.end(),[&](const auto &v){return v.second.id==target;});
        require(found!=next.instruments.end(),"Note instrument does not exist");require(pluginInstrument(rack,found->first),"Note cables carry plugin-instrument events; assign a plugin instrument first");return target;};
      auto kind=[&](const Json &raw){const auto value=text(raw,16);require(value=="channel"||value=="instrument","Note sourceKind must be channel or instrument");return value=="channel"?NoteSourceKind::Channel:NoteSourceKind::Instrument;};
      auto validate=[&](const NoteRoute &route){
        require(std::any_of(rack.begin(),rack.end(),[&](const auto &r){return r.id==route.plugin&&(r.descriptor.value("isInstrument",false)||r.descriptor.value("type",0u)==audioUnitMusicDeviceType);}),"Note destination must be an existing plugin instrument");
        if(route.sourceKind==NoteSourceKind::Instrument)instrument(id(route.source));
      };
      if(method=="graph.note.connect") {
        keys(p,{"sourceKind","source","plugin","midiChannel","enabled","suppressAssignment","dryRun"});
        NoteRoute route;route.id=next.makeEntity().id;route.sourceKind=kind(field(p,"sourceKind"));route.source=identity(field(p,"source"));route.plugin=text(field(p,"plugin"),256);
        route.midiChannel=uint8_t(integer(p.value("midiChannel",Json(0)),0,16));route.enabled=p.contains("enabled")?boolean(p.at("enabled")):true;validate(route);
        if(p.contains("suppressAssignment")&&boolean(p.at("suppressAssignment"))){require(route.sourceKind==NoteSourceKind::Instrument,"Only an instrument cable can replace an implicit assignment");if(std::find(routing.suppressedAssignments.begin(),routing.suppressedAssignments.end(),route.source)==routing.suppressedAssignments.end())routing.suppressedAssignments.push_back(route.source);replaceNoteAssignmentGeometry(graph.presentation,route);}
        nodeID=route.id;routing.routes.push_back(std::move(route));
      } else if(method=="graph.note.update") {
        keys(p,{"id","sourceKind","source","plugin","midiChannel","enabled","dryRun"});nodeID=identity(field(p,"id"));
        auto route=std::find_if(routing.routes.begin(),routing.routes.end(),[&](const auto &r){return r.id==nodeID;});require(route!=routing.routes.end(),"Note cable no longer exists; refresh the graph");
        if(p.contains("sourceKind"))route->sourceKind=kind(p.at("sourceKind"));if(p.contains("source"))route->source=identity(p.at("source"));if(p.contains("plugin"))route->plugin=text(p.at("plugin"),256);
        if(p.contains("midiChannel"))route->midiChannel=uint8_t(integer(p.at("midiChannel"),0,16));if(p.contains("enabled"))route->enabled=boolean(p.at("enabled"));validate(*route);
      } else if(method=="graph.note.disconnect") {
        keys(p,{"id","instrument","dryRun"});require(p.contains("id")!=p.contains("instrument"),"Specify either a cable id or an implicit instrument assignment");
        if(p.contains("id")){nodeID=identity(p.at("id"));require(std::erase_if(routing.routes,[&](const auto &r){return r.id==nodeID;})==1,"Note cable no longer exists; refresh the graph");}
        else{const auto target=instrument(p.at("instrument"));if(std::find(routing.suppressedAssignments.begin(),routing.suppressedAssignments.end(),target)==routing.suppressedAssignments.end())routing.suppressedAssignments.push_back(target);}
      } else if(method=="graph.note.restoreAssignment") {
        keys(p,{"instrument","dryRun"});std::erase(routing.suppressedAssignments,instrument(field(p,"instrument")));
      } else throw Api::ApiError(-32601,"Unknown note cable method");
      reconcileNoteCableGeometry(graph.presentation,routing);
    } else
