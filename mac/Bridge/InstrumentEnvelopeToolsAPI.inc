    } else if ([method hasPrefix:@"instrument.envelope."]) {
      const bool read=[method isEqual:@"instrument.envelope.get"],copy=[method isEqual:@"instrument.envelope.copy"];
      allowed(read ? @[@"instrument",@"envelope"] : copy ? @[@"instrument",@"envelope",@"start",@"end"] :
        @[@"instrument",@"envelope",@"operation",@"start",@"end",@"options",@"dryRun"]);
      const auto identity=decodeNativeID(p[@"instrument"]);
      const auto found=std::find_if(_document->native().instruments.begin(),_document->native().instruments.end(),
        [&](const auto &item){return item.second.id==identity;});
      require(found!=_document->native().instruments.end()&&song.Instruments[found->first],"Instrument no longer exists");
      const auto index=found->first;NSString *kind=string(p[@"envelope"],16);
      const auto before=instrumentEnvelope(*song.Instruments[index],kind);
      const auto maximum=song.GetModSpecifications().envelopePointsMax;
      const bool supported=maximum>0&&!([kind isEqual:@"pitch"]&&song.GetType()==MOD_TYPE_XM);
      auto info=[&](const InstrumentEnvelope &envelope){auto value=[instrumentEnvelopeInfo(envelope,maximum) mutableCopy];
        value[@"instrument"]=nativeID(identity);value[@"index"]=@(index);value[@"envelope"]=kind;
        value[@"name"]=songString(song,song.GetInstrumentName(index));value[@"editable"]=@(supported&&_document->editable());
        value[@"units"]=@"ticks";value[@"maxTick"]=@65535;value[@"maxValue"]=@64;return value;};
      if(read) result=info(before);
      else {
        AutomationTool tool;
        tool.start=uint32_t(integer(p[@"start"]?:@0,0,65535));
        tool.end=uint32_t(integer(p[@"end"]?:@(before.empty()?49:uint32_t(before.back().tick)+1),1,65536));
        try {
          if(copy){const auto clip=copyInstrumentEnvelope(before,tool.start,tool.end);NSMutableArray *points=[NSMutableArray array];
            for(const auto &point:clip.points)[points addObject:@[@(point.position),@(std::lround(point.value*64))]];
            result=@{@"span":@(clip.span),@"points":points,@"units":@"ticks"};
          } else {
            require(supported,"This module format does not store this instrument envelope");
            tool.operation=string(p[@"operation"],32).UTF8String;NSDictionary *options=object(p[@"options"]?:@{});
            if(tool.operation=="shift"){keys(options,@[@"amount"]);const auto amount=number(options[@"amount"],-65535,65535);require(std::floor(amount)==amount,"Shift uses whole ticks");tool.shift=int32_t(amount);}
            else if(tool.operation=="flip-time"||tool.operation=="flip-values")keys(options,@[]);
            else if(tool.operation=="scale"){keys(options,@[@"amount",@"offset"]);tool.amount=number(options[@"amount"],-16,16);tool.offset=number(options[@"offset"]?:@0,-64,64)/64;}
            else if(tool.operation=="ramp"){keys(options,@[@"from",@"to"]);tool.from=number(options[@"from"]?:@0,0,64)/64;tool.to=number(options[@"to"]?:@64,0,64)/64;}
            else if(tool.operation=="sine"){keys(options,@[@"amplitude",@"center",@"cycles",@"phase",@"spacing"]);
              tool.amount=number(options[@"amplitude"]?:@32,0,64)/64;tool.offset=number(options[@"center"]?:@32,0,64)/64;
              tool.cycles=number(options[@"cycles"]?:@1,0.000001,1024);tool.phase=number(options[@"phase"]?:@0,-360,360);
              tool.spacing=uint32_t(integer(options[@"spacing"]?:@4,1,65536));
            } else if(tool.operation=="humanize"){keys(options,@[@"amount",@"jitter",@"seed"]);tool.amount=number(options[@"amount"]?:@3,0,64)/64;
              tool.jitter=uint32_t(integer(options[@"jitter"]?:@0,0,65536));tool.seed=uint32_t(integer(options[@"seed"],0,UINT32_MAX));
            } else if(tool.operation=="paste"||tool.operation=="insert"){
              keys(options,@[@"clip",@"repeats"]);require(!p[@"end"],"Paste extent comes from clipboard span; omit end");tool.end=65536;
              tool.repeats=uint32_t(integer(options[@"repeats"]?:@1,1,4096));const auto clip=object(options[@"clip"]);keys(clip,@[@"span",@"points",@"units"]);
              require([string(clip[@"units"]) isEqual:@"ticks"],"Instrument clipboard uses ticks");tool.clip.span=uint32_t(integer(clip[@"span"],1,65536));
              for(id raw:array(clip[@"points"],240)){const auto point=array(raw,2);require(point.count==2,"Envelope point needs tick and value");
                tool.clip.points.push_back({uint32_t(integer(point[0],0,tool.clip.span-1)),double(integer(point[1],0,64))/64,AutomationCurve::Linear});}
            } else require(false,"Unknown instrument envelope tool");
            const bool dry=p[@"dryRun"]?boolean(p[@"dryRun"]):false;
            const auto transformed=transformInstrumentEnvelope(before,maximum,tool);const bool changed=!sameInstrumentEnvelope(before,transformed.envelope);
            result=@{@"before":info(before),@"after":info(transformed.envelope),@"wouldChange":@(changed),@"dryRun":@(dry),
              @"clippedValues":@(transformed.clipped),@"roundedValues":@(transformed.rounded),@"reanchoredMarkers":@(transformed.reanchored)};
            if(changed&&!dry){_audio->stop();_document->transaction([&](CSoundFile &current){instrumentEnvelope(*current.Instruments[index],kind)=transformed.envelope;});}
          }
        }catch(const std::invalid_argument &ex){throw Error(-32602,ex.what());}
      }
