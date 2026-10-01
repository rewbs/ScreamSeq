    } else if ([method hasPrefix:@"recording."]) {
      if([method isEqual:@"recording.start"]) {
        allowed(@[@"channels",@"instrument",@"quantization",@"latencyMS"]);
        require(!_recording,"Commit or discard the previous take before starting another recording");
        std::vector<uint16_t> channels;for(id channel:array(p[@"channels"],127))channels.push_back(uint16_t(integer(channel,0,song.GetNumChannels()-1)));
        const auto instrument=uint16_t(integer(p[@"instrument"],1,255));const auto quantum=uint32_t(integer(p[@"quantization"]?:@0,0,performanceUnitsPerRow));
        const auto latency=number(p[@"latencyMS"]?:@0,-500,500);
        auto take=std::make_unique<NoteRecording>(_document->native(),song,std::move(channels),instrument,quantum);
        _recording=std::move(take);_recordingRevision=self.automationRevision;_recordingID=NSUUID.UUID.UUIDString;
        _recordingLatencyMS=latency;_recordingLastTimestamp=0;_recordingStartTimestamp=mach_absolute_time();
      } else if([method isEqual:@"recording.get"]) {
        allowed(@[]);
      } else {
        require(_recording!=nullptr,"No recording take exists");
        require([string(p[@"take"],80) isEqual:_recordingID],"Recording take changed; read recording.get again");
        if([method isEqual:@"recording.capture"]) {
          allowed(@[@"take",@"events"]);require(_recording->capturing,"This recording has stopped");
          struct Event{uint64_t timestamp;uint8_t status,note,velocity;};std::vector<Event> input;
          for(id raw:array(p[@"events"],1024)) {
            const auto event=object(raw);keys(event,@[@"timestamp",@"status",@"note",@"velocity"]);
            NSString *stamp=string(event[@"timestamp"],20);uint64_t timestamp=0;
            require(stamp.length>0,"Timestamp must be a decimal host-clock string");
            for(NSUInteger i=0;i<stamp.length;++i){const auto c=[stamp characterAtIndex:i];require(c>='0'&&c<='9'&&timestamp<=(UINT64_MAX-(c-'0'))/10,"Invalid host timestamp");timestamp=timestamp*10+c-'0';}
            const auto status=uint8_t(integer(event[@"status"],0x80,0xbf));require((status&0xf0)==0x80||(status&0xf0)==0x90||(status&0xf0)==0xb0,"Capture accepts note-on/off and all-notes-off controls");
            const auto note=uint8_t(integer(event[@"note"],0,127));require((status&0xf0)!=0xb0||note==120||note==123,"Capture accepts only all-notes-off controls");
            input.push_back({timestamp,status,note,uint8_t(integer(event[@"velocity"],0,127))});
          }
          std::stable_sort(input.begin(),input.end(),[](const auto &a,const auto &b){return a.timestamp<b.timestamp;});
          for(const auto &event:input)[self captureRecordingEvent:event.timestamp status:event.status note:event.note velocity:event.velocity];
        } else if([method isEqual:@"recording.stop"]) {
          allowed(@[@"take"]);[self stopRecordingCapture];
        } else if([method isEqual:@"recording.discard"]) {
          allowed(@[@"take"]);_recording.reset();_recordingID=nil;_recordingRevision=nil;
        } else if([method isEqual:@"recording.commit"]) {
          allowed(@[@"take",@"replaceRows",@"dryRun"]);
          require(!_recording->capturing,"Stop the take before committing it");
          require([_recordingRevision isEqual:self.automationRevision],"Song changed during recording; the take is retained in recording.get for review and manual placement");
          const bool dry=p[@"dryRun"]?boolean(p[@"dryRun"]):false,replace=p[@"replaceRows"]?boolean(p[@"replaceRows"]):false;
          auto native=_document->native();std::set<std::tuple<uint64_t,uint64_t,uint32_t>> rows;
          for(const auto &event:_recording->events)rows.emplace(event.pattern,event.track,event.position/performanceUnitsPerRow);
          if(replace)std::erase_if(native.preciseNotes,[&](const auto &n){return rows.contains({n.pattern,n.track,n.position/performanceUnitsPerRow});});
          std::map<std::tuple<uint64_t,uint32_t,uint64_t,bool>,PreciseNote> merged;
          for(const auto &n:native.preciseNotes)merged[{n.pattern,n.position,n.track,n.note<128}]=n;
          for(const auto &n:_recording->events)merged[{n.pattern,n.position,n.track,n.note<128}]=n;
          native.preciseNotes.clear();for(const auto &[key,n]:merged)native.preciseNotes.push_back(n);
          std::vector<Edit> edits;
          if(replace)for(const auto &[pattern,track,row]:rows) {
            const auto p=std::find_if(native.patterns.begin(),native.patterns.end(),[&](const auto &v){return v.second.id==pattern;});
            const auto c=std::find_if(native.tracks.begin(),native.tracks.end(),[&](const auto &v){return v.second.id==track;});
            require(p!=native.patterns.end()&&c!=native.tracks.end(),"Recorded pattern or channel disappeared");
            auto cell=_document->cell(p->first,row,c->first);cell.note=cell.instrument=0;if(cell.volumeCommand==VOLCMD_VOLUME)cell.volumeCommand=cell.volume=0;
            edits.push_back({p->first,uint16_t(row),c->first,{},cell});
          }
          native.validate(song);_document->validateEdits(edits);
          const bool changed=native!=_document->native()||std::any_of(edits.begin(),edits.end(),[&](const auto &e){return _document->cell(e.pattern,e.row,e.channel)!=e.after;});
          if(!dry){if(changed){_audio->stop();_document->editNative(std::move(native),edits);}_recording.reset();_recordingID=nil;_recordingRevision=nil;}
          result=@{@"dryRun":@(dry),@"wouldChange":@(changed)};
        }
      }
      if(![method isEqual:@"recording.commit"]) {
        NSMutableArray *events=[NSMutableArray array];
        if(_recording&&[method isEqual:@"recording.get"])for(const auto &n:_recording->events)[events addObject:@{@"patternID":nativeID(n.pattern),@"track":nativeID(n.track),@"position":@(n.position),@"note":@(n.note),@"instrument":@(n.instrument),@"velocity":@(n.velocity)}];
        result=@{@"take":_recordingID?:@"",@"capturing":@(_recording&&_recording->capturing),@"baseRevision":_recordingRevision?:@"",@"events":events,@"eventCount":@(_recording?_recording->events.size():0),
          @"missingTime":@(_recording?_recording->missingTime:0),@"exhaustedVoices":@(_recording?_recording->exhaustedVoices:0),@"overflow":@(_recording?_recording->overflow:0),
          @"hostTime":[NSString stringWithFormat:@"%llu",(unsigned long long)mach_absolute_time()]};
      }
