    } else if(parameterRecording) {
      using Json=nlohmann::json;
      const auto summary=[&] {
        auto value=Tracker::recordedGestureSummary(_parameterTake?&_parameterTake->take():nullptr,
          _parameterTake&&_parameterTake->compatible());
        if(_parameterTake) {
          const auto &take=_parameterTake->take();
          value["transportPlaying"]=bool(self.playing);
          value["discardable"]=!take.completed()&&(take.endTime().has_value()||!self.playing);
          value["discardCleanupPending"]=_parameterTake->awaitingDiscardCleanup();
        }
        value["releaseRequiresStoppedTransport"]=true;
        return value;
      };
      const auto requireTake=[&] {
        const auto token=std::string(string(p[@"take"],80).UTF8String);
        if(!_parameterTake||token!=_parameterTake->take().token())throw Error(-32001,"Parameter take changed; read automation.recording.get");
      };
      Json value;
      try {
        if([method isEqual:@"automation.recording.get"]) {
          allowed(@[]);std::string warning;
          try{[self pollParameterTake:NO];}catch(const std::exception &e){warning=e.what();}
          value=summary();value["hostTime"]=std::to_string(mach_absolute_time());value["clock"]="mach-absolute-time";
          mach_timebase_info_data_t timebase{};mach_timebase_info(&timebase);
          value["timebaseNumerator"]=timebase.numer;value["timebaseDenominator"]=timebase.denom;
          if(!warning.empty())value["intakeError"]=std::move(warning);
        } else if([method isEqual:@"automation.recording.events"]) {
          allowed(@[@"take",@"plugin",@"parameter",@"offset",@"limit",@"includeTiming"]);requireTake();
          value=Tracker::recordedGestureEvents(_parameterTake->take(),string(p[@"plugin"],128).UTF8String,
            uint32_t(integer(p[@"parameter"],0,UINT32_MAX)),size_t(integer(p[@"offset"]?:@0,0,SIZE_MAX)),
            size_t(integer(p[@"limit"]?:@128,1,512)),p[@"includeTiming"]?boolean(p[@"includeTiming"]):false);
        } else if([method isEqual:@"automation.recording.start"]) {
          allowed(@[@"plugins"]);
          [self startParameterTake:array(p[@"plugins"],maximumNativePlugins) expectedRevision:p[@"expectedRevision"]];
          value=summary();
        } else if([method isEqual:@"automation.recording.capture"]) {
          allowed(@[@"take",@"plugin",@"parameter",@"value",@"timestamp"]);requireTake();
          const auto plugin=string(p[@"plugin"],128);const auto parameter=integer(p[@"parameter"],0,UINT32_MAX);
          const auto nativeValue=number(p[@"value"],-std::numeric_limits<double>::max(),std::numeric_limits<double>::max());
          NSString *stamp=string(p[@"timestamp"],20);uint64_t timestamp=0;
          require(stamp.length>0,"Timestamp must be a decimal host-clock string");
          for(NSUInteger i=0;i<stamp.length;++i){const auto c=[stamp characterAtIndex:i];require(c>='0'&&c<='9'&&timestamp<=(UINT64_MAX-(c-'0'))/10,"Invalid host timestamp");timestamp=timestamp*10+c-'0';}
          require(timestamp&&timestamp<=mach_absolute_time(),"Control timestamp must be an original past host time");
          const bool accepted=[self captureParameterTake:plugin parameter:NSInteger(parameter) value:nativeValue timestamp:timestamp];
          std::string warning;try{[self pollParameterTake:NO];}catch(const std::exception &e){warning=e.what();}
          value=summary();value["accepted"]=accepted;
          if(!warning.empty())value["intakeError"]=std::move(warning);
        } else if([method isEqual:@"automation.recording.stop"]) {
          allowed(@[@"take"]);requireTake();std::string warning;
          try{[self pollParameterTake:YES];}catch(const std::exception &e){warning=e.what();}
          value=summary();if(!warning.empty())value["intakeError"]=std::move(warning);
        } else {
          const bool commit=[method isEqual:@"automation.recording.commit"];
          allowed(commit?@[@"take",@"acknowledgeLoss"]:@[@"take"]);requireTake();
          const bool acknowledge=commit&&p[@"acknowledgeLoss"]?boolean(p[@"acknowledgeLoss"]):false;
          if(!commit&&_parameterTake->take().completed())throw Error(-32002,"This take is already kept; retry Commit to finish native restoration, then use Undo to change the music");
          if(!_parameterTake->take().endTime()&&(commit||self.playing))throw Error(-32002,"Stop the parameter take before committing or discarding it");
          bool changed=false;
          if(commit) {
            const auto committed=[self commitParameterTake:acknowledge];
            if(!committed.accepted)throw Error(-32002,"Parameter take could not commit; input retained");
            changed=committed.changed;
          }
          // Retain the successful take's identity/completion when release
          // removes its owner. On failure, report the current cleanup stage.
          value=summary();value["changed"]=changed;
          std::string restorationError;
          try {
            const bool released=commit?[self releaseStoppedParameterTake:NO]:[self discardStoppedParameterTake];
            if(released){value["retained"]=false;value["capturing"]=false;value["discardCleanupPending"]=false;}
            else restorationError="Native restoration is busy; retry this retained take";
          }catch(const std::exception &e){restorationError=e.what();}
          if(!restorationError.empty()) {value=summary();value["changed"]=changed;value["restorationError"]=std::move(restorationError);}
          // Publication remains successful even when release needs stopped
          // transport or native cleanup. completed prevents duplicate history.
        }
      }catch(const Error &){throw;}
      catch(const std::invalid_argument &e){throw Error(-32602,e.what());}
      catch(const std::runtime_error &e){throw Error(-32002,e.what());}
      result=ScreamSeq::NativePropertyTree::modelProjection(value);
