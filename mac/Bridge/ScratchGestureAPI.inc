    } else if ([method isEqual:@"scratch.gestures.get"]) {
      allowed(@[]);
      NSMutableArray *gestures=[NSMutableArray array],*presets=[NSMutableArray array];
      for(const auto &[id,gesture]:_document->native().scratchGestures){auto item=[encodeScratchGesture(id,gesture) mutableCopy];item[@"uses"]=@(scratchGestureUses(_document->native(),id));[gestures addObject:item];}
      for(const auto &preset:scratchPresets()){auto item=[encodeScratchGesture(1,preset.gesture) mutableCopy];item[@"id"]=@(preset.id.c_str());[presets addObject:item];}
      result=@{@"unitsPerCycle":@(scratchCycleUnits),@"gestures":gestures,@"presets":presets,
        @"limits":@{@"gestures":@255,@"pointsPerLane":@(maximumScratchPoints)}};
    } else if ([method isEqual:@"scratch.gestures.set"] || [method isEqual:@"scratch.gestures.remove"] || [method isEqual:@"scratch.gestures.clone"]) {
      try {
        const bool remove=[method isEqual:@"scratch.gestures.remove"];
        const bool clone=[method isEqual:@"scratch.gestures.clone"];
        allowed(remove?@[@"id",@"dryRun"]:clone?@[@"id",@"name",@"target",@"dryRun"]:@[@"id",@"name",@"motion",@"fader",@"preset",@"dryRun"]);
        const bool dry=p[@"dryRun"]?boolean(p[@"dryRun"]):false;
        auto next=_document->native();uint16_t id=0;
        if(p[@"id"])id=uint16_t(integer(p[@"id"],1,255));
        if(clone){
          require(id!=0,"Scratch clone requires its source id");
          std::optional<ScratchPatternCell> target;
          if(p[@"target"]){
            NSDictionary *t=object(p[@"target"]);keys(t,@[@"pattern",@"row",@"channel",@"column"]);
            const auto pattern=uint16_t(integer(t[@"pattern"],0,UINT16_MAX));require(song.Patterns.IsValidPat(pattern),"Pattern does not exist");
            target=ScratchPatternCell{pattern,uint16_t(integer(t[@"row"],0,song.Patterns[pattern].GetNumRows()-1)),uint16_t(integer(t[@"channel"],0,song.GetNumChannels()-1)),uint8_t(integer(t[@"column"],0,maximumEffectColumns-1))};
          }
          std::optional<std::string> name;if(p[@"name"])name=string(p[@"name"],256).UTF8String;
          id=cloneScratchGesture(next,id,name,target);
        }
        else if(remove){require(id&&next.scratchGestures.contains(id),"Scratch phrase no longer exists");require(!scratchGestureUses(next,id),"This scratch phrase is used in the pattern; replace or remove those SK commands first");next.scratchGestures.erase(id);}
        else {
          if(!id){for(uint16_t candidate=1;candidate<=255;++candidate)if(!next.scratchGestures.contains(candidate)){id=candidate;break;}require(id!=0,"The song holds at most 255 scratch phrases");}
          ScratchGesture gesture;
          if(auto found=next.scratchGestures.find(id);found!=next.scratchGestures.end())gesture=found->second;
          if(p[@"preset"]){const std::string name=string(p[@"preset"],64).UTF8String;const auto presets=scratchPresets();const auto found=std::find_if(presets.begin(),presets.end(),[&](const auto &v){return v.id==name;});require(found!=presets.end(),"Unknown scratch preset");gesture=found->gesture;}
          if(p[@"name"])gesture.name=string(p[@"name"],256).UTF8String;
          if(p[@"motion"]){gesture.motion.clear();for(NSObject *point:array(p[@"motion"],maximumScratchPoints))gesture.motion.push_back(decodeAutomationPoint(point,scratchCycleUnits));}
          if(p[@"fader"]){gesture.fader.clear();for(NSObject *point:array(p[@"fader"],maximumScratchPoints))gesture.fader.push_back(decodeAutomationPoint(point,scratchCycleUnits));}
          validateScratchGesture(gesture);next.scratchGestures[id]=std::move(gesture);
        }
        next.validate(song);const bool changed=next!=_document->native();
        if(changed&&!dry){
          // Reassigning an event uses the same transport policy as a pattern
          // edit. Validate the whole candidate first; never leave a spare copy
          // behind when a stale or invalid destination is rejected.
          if(clone&&p[@"target"])_audio->stop();
          auto prepared=_audio->prepareScratchUpdate(next);
          _document->annotate([&](NativeSong &n){n=next;},[&]{if(prepared&&!_audio->publishScratchUpdate(prepared))throw Error(-32002,"Scratch update is busy; retry the same revision");});
        }
        result=@{@"id":@(id),@"dryRun":@(dry),@"wouldChange":@(changed)};
      } catch(const std::invalid_argument &e){throw Error(-32602,e.what());}
