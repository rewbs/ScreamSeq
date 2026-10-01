    } else if ([method isEqual:@"pattern.notes.get"]) {
      allowed(@[@"pattern"]);
      const auto index=uint16_t(integer(p[@"pattern"],0,UINT16_MAX));require(song.Patterns.IsValidPat(index),"Pattern does not exist");
      const auto &native=_document->native();const auto pattern=native.patterns.at(index).id;
      NSMutableArray *events=[NSMutableArray array];
      for(const auto &note:native.preciseNotes)if(note.pattern==pattern) {
        const auto track=std::find_if(native.tracks.begin(),native.tracks.end(),[&](const auto &v){return v.second.id==note.track;});
        NSMutableDictionary *item=[encodePreciseNote(note) mutableCopy];item[@"channel"]=@(track->first);[events addObject:item];
      }
      result=@{@"pattern":@(index),@"patternID":nativeID(pattern),@"rows":@(song.Patterns[index].GetNumRows()),@"unitsPerRow":@(performanceUnitsPerRow),@"rowsPerBeat":@(preciseRowsPerBeat(song,index)),@"effects":preciseNoteEffects(song.GetType()),@"events":events};
    } else if ([method isEqual:@"pattern.notes.set"]) {
      allowed(@[@"pattern",@"events",@"clearLegacy",@"clearRows",@"clearRowEffects",@"dryRun"]);
      const auto index=uint16_t(integer(p[@"pattern"],0,UINT16_MAX));require(song.Patterns.IsValidPat(index),"Pattern does not exist");
      const bool dry=p[@"dryRun"]?boolean(p[@"dryRun"]):false,clear=p[@"clearLegacy"]?boolean(p[@"clearLegacy"]):false;
      const bool clearEffects=p[@"clearRowEffects"]?boolean(p[@"clearRowEffects"]):false;
      auto native=_document->native();const auto pattern=native.patterns.at(index).id;
      const auto end=uint32_t(song.Patterns[index].GetNumRows())*performanceUnitsPerRow;
      std::vector<PreciseNote> replacement;
      std::set<std::pair<uint16_t,uint16_t>> cells;std::vector<Edit> edits;
      for(id raw:array(p[@"events"],maximumPreciseNotes)) {
        const auto e=object(raw);keys(e,@[@"channel",@"position",@"row",@"offsetRows",@"offsetBeats",@"note",@"instrument",@"velocity",@"effect",@"parameter"]);
        const auto channel=uint16_t(integer(e[@"channel"],0,song.GetNumChannels()-1));const auto note=uint8_t(integer(e[@"note"],1,255));
        uint32_t position=0;
        if(e[@"position"]) {
          require(!e[@"row"]&&!e[@"offsetRows"]&&!e[@"offsetBeats"],"Use position, or row with one offset unit, not both");
          position=uint32_t(integer(e[@"position"],0,end-1));
        } else {
          const auto row=uint32_t(integer(e[@"row"],0,song.Patterns[index].GetNumRows()-1));
          require(bool(e[@"offsetRows"])!=bool(e[@"offsetBeats"]),"Specify exactly one of offsetRows or offsetBeats");
          const double offset=e[@"offsetRows"]?number(e[@"offsetRows"],0,1):number(e[@"offsetBeats"],0,1)*preciseRowsPerBeat(song,index);
          require(offset<1,"A precise note offset must be inside its row, before the end boundary");
          position=row*performanceUnitsPerRow+std::min<uint32_t>(performanceUnitsPerRow-1,uint32_t(std::lround(offset*performanceUnitsPerRow)));
        }
        replacement.push_back({pattern,native.tracks.at(channel).id,position,uint16_t(integer(e[@"instrument"]?:@0,0,255)),note,uint8_t(integer(e[@"velocity"]?:@127,1,127)),
          uint8_t(integer(e[@"effect"]?:@0,0,255)),uint8_t(integer(e[@"parameter"]?:@0,0,255))});
        if(clear&&cells.emplace(uint16_t(position/performanceUnitsPerRow),channel).second) {
          const auto row=uint16_t(position/performanceUnitsPerRow);auto cell=_document->cell(index,row,channel);
          cell.note=cell.instrument=0;if(cell.volumeCommand==VOLCMD_VOLUME)cell.volumeCommand=cell.volume=0;
          if(clearEffects)cell.effect=cell.parameter=0;
          edits.push_back({index,row,channel,{},cell});
        }
      }
      if(p[@"clearRows"])for(id raw:array(p[@"clearRows"],8192)) {
        const auto item=object(raw);keys(item,@[@"row",@"channel"]);
        const auto row=uint16_t(integer(item[@"row"],0,song.Patterns[index].GetNumRows()-1)),channel=uint16_t(integer(item[@"channel"],0,song.GetNumChannels()-1));
        if(cells.emplace(row,channel).second){auto cell=_document->cell(index,row,channel);cell.note=cell.instrument=0;
          if(cell.volumeCommand==VOLCMD_VOLUME)cell.volumeCommand=cell.volume=0;if(clearEffects)cell.effect=cell.parameter=0;edits.push_back({index,row,channel,{},cell});}
      }
      replacePreciseNotesForPattern(native.preciseNotes,pattern,std::move(replacement));
      try{native.validate(song);_document->validateEdits(edits);}catch(const std::invalid_argument &e){throw Error(-32602,e.what());}
      const bool changed=native!=_document->native()||std::any_of(edits.begin(),edits.end(),[&](const auto &e){return _document->cell(e.pattern,e.row,e.channel)!=e.after;});
      if(changed&&!dry){_audio->stop();_document->editNative(std::move(native),edits);}
      result=@{@"dryRun":@(dry),@"wouldChange":@(changed),@"events":@([p[@"events"] count]),@"clearedRows":@(cells.size())};
