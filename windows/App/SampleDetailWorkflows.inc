// Retained sample workflows. Musical changes still use the shared document API.
  SampleWorkflow::Target workflowTarget()const{return {captured_.document,id_,captured_.revision,slot_,region_.frames};}
  const char *pageName()const{static constexpr const char *names[]={"draw","process","loops","clipboard","snap"};return names[page_];}
  bool loopField(int id)const{return id==normalStart||id==normalEnd||id==normalDirection||id==sustainStart||id==sustainEnd||id==sustainDirection;}
  bool pasteField(int id)const{return id==pasteMode||id==pasteRate||id==pasteSourceGain||id==pasteDestinationGain;}
  bool snapField(int id)const{return id==snapMode||id==snapDirection||id==snapSize||id==snapOrigin;}
  void invalidatePaste(){pasteSignature_=Json::object();pasteClipboard_.clear();}
  SampleWorkflow::LoopsRaw loopFields()const{
    auto raw=loops_.raw();
    for(int sustain=0;sustain<2;++sustain){auto &row=sustain?raw.sustain:raw.normal;row.start=field(sustain?sustainStart:normalStart);row.end=field(sustain?sustainEnd:normalEnd);const auto direction=choice(sustain?sustainDirection:normalDirection);row.pingpong=direction==1;row.reverse=direction==2;}
    return raw;
  }
  void syncLoops(){
    setting_=true;for(int sustain=0;sustain<2;++sustain){const auto &row=sustain?loops_.raw().sustain:loops_.raw().normal;set(sustain?sustainStart:normalStart,row.start);set(sustain?sustainEnd:normalEnd,row.end);set(sustain?sustainEnabled:normalEnabled,row.enabled?L"Enabled":L"Disabled");choose(sustain?sustainDirection:normalDirection,row.reverse?2:row.pingpong?1:0);}setting_=false;
  }
  void resetLoops(){loops_.reset(workflowTarget(),info_);syncLoops();}
  void loopChanged(){loops_.replaceRaw(loopFields());++generation_;report_=Json::object();invalidatePaste();queueSampleEdit();requestPaint();}
  void useSelectionForLoop(bool sustain){
    requireCurrent();applyRange();require(region_.first<region_.last,"Select a nonempty range for the loop");
    auto raw=loopFields();auto &row=sustain?raw.sustain:raw.normal;row.start=std::to_wstring(region_.first);row.end=std::to_wstring(region_.last);row.enabled=true;const auto direction=choice(sustain?sustainDirection:normalDirection);row.pingpong=direction==1;row.reverse=direction==2;
    loops_.replaceRaw(std::move(raw));syncLoops();++generation_;report_=Json::object();invalidatePaste();page_=2;status(L"Saving loop from selection…");queueSampleEdit();
  }
  void toggleLoop(bool sustain){
    auto raw=loopFields();auto &row=sustain?raw.sustain:raw.normal;row.enabled=!row.enabled;
    loops_.replaceRaw(std::move(raw));syncLoops();++generation_;report_=Json::object();invalidatePaste();status(L"Saving loop changes…");queueSampleEdit();
  }
  void editLoops(bool dry){
    cancelAutomaticEdit();
    requireCurrent();loops_.replaceRaw(loopFields());const auto submittedTarget=loops_.target();const auto submittedRaw=loops_.raw();
    auto params=loops_.params(workflowTarget(),dry);mutate("sample.loops.set",std::move(params),dry,false,false,true);
    if(!dry){require(loops_.acceptApplied(submittedTarget,submittedRaw,workflowTarget()),"Loop draft changed during Apply / newer fields retained");syncLoops();}
    const auto changed=report_.value("loopsChanged",false);status(dry?(changed?L"Preview: normal and sustain loops change together / PCM and history unchanged":L"Preview: both loops already match the saved sample"):(changed?L"Both loops applied / one Undo restores them / PCM unchanged":L"Loops unchanged / no history entry created"));
  }
  Json snapParams(unsigned first,unsigned last)const{
    const bool grid=choice(snapMode)==1;Json p={{"sample",slot_},{"positions",Json::array({first,last})},{"mode",grid?"grid":"zero"},{"direction",choice(snapDirection)==1?"before":choice(snapDirection)==2?"after":"nearest"}};
    if(grid){const auto step=integerField(snapSize,268435456);require(step>0,"Grid step must be at least one frame");p["step"]=step;p["origin"]=integerField(snapOrigin,region_.frames);}else{p["radius"]=integerField(snapSize,65536);p["channels"]=channelName();}return p;
  }
  void snap(int loopTarget=-1){
    requireCurrent();require(stroke_.empty()&&!dragging_,"Apply or discard the drawing before snapping");
    unsigned first=0,last=0;auto raw=loopFields();
    if(loopTarget<0){applyRange();first=region_.first;last=region_.last;}
    else{loops_.replaceRaw(raw);const auto values=loops_.params(workflowTarget(),true).at(loopTarget?"sustain":"normal");first=values.at("start");last=values.at("end");}
    const auto params=snapParams(first,last);const auto token=generation_;const auto selection=selectionVersion_;Json result;
    busy([&]{result=request_("sample.snap.get",params);requireCurrent();require(token==generation_&&selection==selectionVersion_&&raw==loopFields()&&params==snapParams(first,last),"Selection, loops or snap settings changed / result discarded");});
    const unsigned start=result.at("positions")[0].at("after"),end=result.at("positions")[1].at("after");require(start<=end&&end<=region_.frames,"Snapping would reverse the boundaries");
    if(loopTarget<0){region_.first=start;region_.last=end;syncFields();++selectionVersion_;}
    else{auto &row=loopTarget?raw.sustain:raw.normal;require(!row.enabled||start<end,"Snapping would collapse the enabled loop / widen its boundaries");row.start=std::to_wstring(start);row.end=std::to_wstring(end);loops_.replaceRaw(std::move(raw));syncLoops();}
    ++generation_;invalidatePaste();report_=std::move(result);unsigned matched=0;for(const auto &v:report_.at("positions"))matched+=v.at("matched").get<bool>();
    status(L"Matched "+std::to_wstring(matched)+L"/2 boundaries · "+std::to_wstring(start)+L" → "+std::to_wstring(end)+(loopTarget<0?L" / selection only":L" / saving loop changes…"));if(loopTarget>=0)queueSampleEdit();
  }
  Json pasteParams(){
    applyRange();static constexpr const char *modes[]={"insert","overwrite","mix","replace"};const auto mode=choice(pasteMode);require(mode>=0&&mode<4,"Choose a paste mode");
    const auto gain=number(pasteSourceGain);require(gain>=-96&&gain<=24,"Clipboard gain must be between -96 and +24 dB");require((mode!=0&&mode!=3)||channel_==0,"Insert and Replace change time in both channels / select Both channels");
    Json p={{"sample",slot_},{"at",region_.first},{"mode",modes[mode]},{"channels",channelName()},{"rateMode",choice(pasteRate)==1?"keep-frames":"resample"},{"sourceGainDB",gain}};
    if(mode==3)p["end"]=region_.last;if(mode==2){const auto destination=number(pasteDestinationGain);require(destination>=-96&&destination<=24,"Existing audio gain must be between -96 and +24 dB");p["destinationGainDB"]=destination;}return p;
  }
  bool pasteReady()const{return !pasteClipboard_.empty()&&current()&&pasteTarget_==workflowTarget();}
  void pasteAudio(bool dry){
    requireCurrent();auto params=pasteParams();const auto signature=params;const auto target=workflowTarget();
    if(dry){invalidatePaste();const auto token=generation_;Json clip;busy([&]{clip=request_("sample.clipboard.get",Json::object());requireCurrent();require(token==generation_,"Paste fields changed / preview cancelled");});require(clip.value("available",false),"The sample clipboard is empty / copy a selection first");params["clipboardId"]=clip.at("clipboardId");}
    else{require(pasteReady()&&signature==pasteSignature_,"Preview paste with the current selection and options before applying");params["clipboardId"]=pasteClipboard_;}
    params["dryRun"]=dry;try{mutate("sample.paste",params,dry);}catch(...){invalidatePaste();throw;}
    if(dry){pasteSignature_=signature;pasteTarget_=target;pasteClipboard_=params.at("clipboardId");}
    else{invalidatePaste();const unsigned start=report_.at("start"),frames=report_.at("insertedFrames");region_.first=std::min(start,region_.frames);region_.last=std::min(region_.frames,start+frames);syncFields();++selectionVersion_;}
    std::wstring text=dry?L"Paste preview: ":L"Pasted: ";text+=std::to_wstring(report_.at("insertedFrames").get<unsigned>())+L" inserted · "+std::to_wstring(report_.at("removedFrames").get<unsigned>())+L" removed · "+std::to_wstring(report_.at("resultFrames").get<unsigned>())+L" result frames";
    if(report_.value("clippedSamples",uint64_t(0)))text+=L" · "+std::to_wstring(report_.at("clippedSamples").get<uint64_t>())+L" clipped values";status(text+(dry?L"\nReview the result, then choose Paste audio. The reviewed clipboard and song revision are retained.":L"\nOne Undo restores the previous audio and loop positions."));layout();
  }
  void clipboard(int id){
    if(id==paste){pasteAudio(false);return;}auto p=rangeParams();require(region_.first<region_.last,"Select a nonempty range first");
    if(id==copy||id==copyNew)p["channels"]=channelName();else require(channel_==0,"Cut and Delete remove time from both channels / select Both channels");
    mutate(id==copy?"sample.clipboard.copy":id==cut?"sample.cut":id==erase?"sample.delete":"sample.copyToNew",std::move(p));invalidatePaste();
  }
  Json loopSnapshot()const{
    const auto row=[](const SampleWorkflow::LoopRaw &v){return Json{{"start",utf8(v.start)},{"end",utf8(v.end)},{"enabled",v.enabled},{"pingpong",v.pingpong},{"reverse",v.reverse}};};
    return {{"dirty",loops_.dirty()},{"stale",loops_.loaded()&&(!current()||loops_.target()!=workflowTarget())},{"normal",row(loops_.raw().normal)},{"sustain",row(loops_.raw().sustain)}};
  }
  void createWorkflowControls(){
    for(auto [id,text]:std::initializer_list<std::pair<int,const wchar_t *>>{{pageDraw,L"Drawing"},{pageProcess,L"Process"},{pageLoops,L"Loops"},{pageClipboard,L"Paste"},{pageSnap,L"Snap"},{normalEnabled,L"Disabled"},{sustainEnabled,L"Disabled"},{snapNormal,L"Snap normal"},{snapSustain,L"Snap sustain"},{previewLoops,L"Preview loops"},{applyLoops,L"Apply loops"},{reloadLoops,L"Reload loops"},{previewPaste,L"Preview paste"},{autoSnap,L"Snap after selecting: Off"}})button(id,text);
    for(auto [id,text]:std::initializer_list<std::pair<int,const wchar_t *>>{{normalStart,L"0"},{normalEnd,L"0"},{sustainStart,L"0"},{sustainEnd,L"0"},{pasteSourceGain,L"0"},{pasteDestinationGain,L"0"},{snapOrigin,L"0"}})edit(id,text,24);
    auto options=[&](int id,std::initializer_list<const wchar_t *> values){combo(id);for(auto text:values)ScreamSeq::NativeInputGate::present(controls_.at(id),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text));choose(id,0);};
    options(normalDirection,{L"Forward",L"Ping pong",L"Reverse"});options(sustainDirection,{L"Forward",L"Ping pong",L"Reverse"});options(pasteRate,{L"Resample to target",L"Keep frame count"});
    for(auto [id,text]:std::initializer_list<std::pair<int,const wchar_t *>>{{normalTitle,L"NORMAL"},{sustainTitle,L"SUSTAIN"},{loopStartLabel,L"Start frame"},{loopEndLabel,L"End (exclusive)"},{loopDirectionLabel,L"Direction"},{loopHelp,L"Apply saves both loops in one Undo step.\nCrossfade uses saved forward loops."},{pasteModeLabel,L"Paste mode"},{pasteRateLabel,L"Rate conversion"},{pasteSourceLabel,L"Clipboard gain (dB)"},{pasteDestinationLabel,L"Existing audio (dB)"},{pasteHelp,L"Insert adds time. Overwrite replaces frames in place. Mix combines audio. Replace substitutes the selection.\nResample preserves pitch; Keep frame count preserves the clipboard's number of frames."},{snapModeLabel,L"Snap mode"},{snapDirectionLabel,L"Direction"},{snapSizeLabel,L"Search radius (frames)"},{snapOriginLabel,L"Grid origin (frame)"},{snapHelp,L"Snap moves selection or loop draft boundaries; audio stays unchanged.\nZero crossings use the selected channels. Grid positions are measured from the origin."},{amountLabel,L"Amount"},{exponentLabel,L"Exponent"},{curveLabel,L"Fade curve"},{processHelp,L"Amount is gain / target dB, or an odd smoothing window (3–255 frames).\nExponent applies to exponential and logarithmic fades. Preview leaves audio and Undo history unchanged."},{crossFramesLabel,L"Frames"},{drawHelp,L"Zoom to individual frames to draw in the waveform, or stage exact frame / amplitude pairs.\nApply drawing saves the full stroke in one Undo step. Escape cancels the current gesture."}})label(id,text);
  }
  void workflowLayout(){
    const auto [w,h]=size();const auto dpi=GetDpiForWindow(window_);
    const auto previousFocus=GetFocus();
    ShowWindow(controls_.at(applySettings),automaticEditFailed_&&settingsDraft()?SW_SHOWNA:SW_HIDE);
    ShowWindow(controls_.at(applyLoops),automaticEditFailed_&&loops_.dirty()&&page_==2?SW_SHOWNA:SW_HIDE);
    // Edits must retain their focus, selection and raw strings while a request
    // refreshes its result. Reflow only for actual geometry/page changes.
    const bool geometry=w!=layoutWidth_||h!=layoutHeight_||dpi!=layoutDpi_||page_!=layoutPage_;
    if(geometry){
      layoutWidth_=w;layoutHeight_=h;layoutDpi_=dpi;layoutPage_=page_;
      const auto oldWidth=canvas_.w;canvas_={14,180,w-28,std::max(90.f,h-524)};
      std::set<int> shown;auto at=[&](int id,float x,float y,float width,float height,bool show=true){if(show)shown.insert(id);place(id,x,y,width,height,show);};
      at(heading,14,12,132,23);at(sample,150,8,w-646,240);at(fromCursor,w-488,8,104,28);at(reload,w-378,8,86,28);at(undo,w-286,8,78,28);at(redo,w-202,8,78,28);at(close,w-118,8,104,28);
      at(targetLabel,14,44,w-28,23);
      at(nameLabel,14,76,36,22);at(sampleName,54,72,w-730,26);const float p=w-668;
      at(rateLabel,p,76,50,22);at(sampleRate,p+54,72,74,26);at(volumeLabel,p+138,76,28,22);at(sampleVolume,p+170,72,46,26);at(panLabel,p+224,76,28,22);at(samplePan,p+258,72,52,26);at(applySettings,p+320,72,122,26);at(discardSettings,p+450,72,88,26);at(replaceSample,p+546,72,108,26);
      float x=14;for(auto [id,width]:std::initializer_list<std::pair<int,float>>{{fit,50.f},{zoomIn,38.f},{zoomOut,38.f},{zoomSelection,116.f},{panLeft,56.f},{panRight,56.f},{channels,120.f},{drawMode,84.f},{audition,92.f},{createInstrument,154.f}}){at(id,x,110,width,id==channels?180:28);x+=width+6;}
      at(viewLabel,14,150,48,20);at(viewStart,66,146,88,26);at(viewEnd,160,146,88,26);at(setView,254,146,84,26);at(helpLabel,350,148,w-(record_?512:364),24);at(recordSample,w-148,146,134,26,bool(record_));
      const auto selectionY=h-334;at(rangeLabel,14,selectionY+3,64,22);at(rangeStart,82,selectionY,88,26);at(rangeEnd,176,selectionY,88,26);at(setRange,270,selectionY,86,26);at(all,362,selectionY,52,26);
      at(copy,434,selectionY,66,26);at(cut,506,selectionY,60,26);at(erase,572,selectionY,70,26);at(copyNew,648,selectionY,118,26);
      const auto tabsY=h-296;const auto tabWidth=(w-52)/5;for(int index=0;index<5;++index)at(pageDraw+index,14+index*(tabWidth+6),tabsY,tabWidth,30);
      const auto y=h-252;
      if(page_==0){
        at(pointLabel,14,y+4,74,24);at(pointFrame,94,y,100,28);at(pointValue,202,y,100,28);at(stagePoint,310,y,112,28);at(interpolation,430,y,128,180);at(applyDraw,566,y,138,28);at(discardDraw,712,y,w-726,28);at(drawHelp,14,y+48,w-28,58);
      }else if(page_==1){
        at(processLabel,14,y,156,23);at(curveLabel,186,y,140,23);at(amountLabel,342,y,104,23);at(exponentLabel,460,y,104,23);
        at(operation,14,y+24,158,260);at(fadeCurve,186,y+24,142,180);at(amount,342,y+24,104,28);at(exponent,460,y+24,104,28);at(previewProcess,580,y+24,138,28);at(applyProcess,726,y+24,w-740,28);at(processHelp,14,y+70,w-28,58);
      }else if(page_==2){
        at(loopStartLabel,194,y,106,22);at(loopEndLabel,306,y,114,22);at(loopDirectionLabel,426,y,126,22);
        for(int sustain=0;sustain<2;++sustain){const auto rowY=y+24+sustain*34;at(sustain?sustainTitle:normalTitle,14,rowY+4,78,23);at(sustain?sustainEnabled:normalEnabled,98,rowY,88,28);at(sustain?sustainStart:normalStart,194,rowY,104,28);at(sustain?sustainEnd:normalEnd,306,rowY,112,28);at(sustain?sustainDirection:normalDirection,426,rowY,126,180);at(sustain?sustainLoop:normalLoop,560,rowY,132,28);at(sustain?snapSustain:snapNormal,700,rowY,w-714,28);}
        at(previewLoops,98,y+94,136,28);at(applyLoops,242,y+94,132,28);at(reloadLoops,382,y+94,132,28);at(loopHelp,528,y+92,w-542,38);
        at(crossLabel,14,y+150,78,22);at(crossLoop,98,y+144,112,180);at(crossMode,218,y+144,138,180);at(crossCurve,364,y+144,106,180);at(crossFramesLabel,478,y+150,46,22);at(crossFrames,530,y+144,64,28);at(previewCross,602,y+144,136,28);at(applyCross,746,y+144,w-760,28);
      }else if(page_==3){
        at(pasteModeLabel,14,y,158,22);at(pasteRateLabel,188,y,174,22);at(pasteSourceLabel,378,y,156,22);at(pasteDestinationLabel,550,y,158,22,choice(pasteMode)==2);
        at(pasteMode,14,y+24,158,180);at(pasteRate,188,y+24,174,180);at(pasteSourceGain,378,y+24,156,28);at(pasteDestinationGain,550,y+24,158,28,choice(pasteMode)==2);
        at(previewPaste,14,y+68,158,30);at(paste,188,y+68,174,30);at(pasteHelp,14,y+116,w-28,64);
      }else{
        at(snapModeLabel,14,y,154,22);at(snapDirectionLabel,184,y,124,22);at(snapSizeLabel,324,y,174,22);at(snapOriginLabel,514,y,154,22,choice(snapMode)==1);
        at(snapMode,14,y+24,154,180);at(snapDirection,184,y+24,124,180);at(snapSize,324,y+24,174,28);at(snapOrigin,514,y+24,154,28,choice(snapMode)==1);
        at(snapRange,14,y+68,154,30);at(snapNormal,184,y+68,154,30);at(snapSustain,354,y+68,154,30);at(autoSnap,524,y+68,252,30);at(snapHelp,14,y+116,w-28,64);
      }
      at(statusLabel,14,h-60,w-28,48);
      for(auto [id,control]:controls_)if(!shown.contains(id)){if(GetFocus()==control)SetFocus(window_);ShowWindow(control,SW_HIDE);}
      if(ready_&&oldWidth!=canvas_.w)SetTimer(window_,3,70,nullptr);
    }
    for(auto [id,control]:controls_){wchar_t type[24]{};GetClassNameW(control,type,24);if(_wcsicmp(type,L"STATIC"))EnableWindow(control,!pending_||id==close);}
    EnableWindow(controls_.at(paste),!pending_&&pasteReady());
    if(mutationFrozen()){
      for(auto [id,control]:controls_){wchar_t type[24]{};GetClassNameW(control,type,24);if(_wcsicmp(type,L"STATIC"))EnableWindow(control,id==close||(id>=pageDraw&&id<=pageSnap));}
      EnableWindow(controls_.at(reload),!pending_&&!mutationWorking_);
      EnableWindow(controls_.at(fromCursor),!pending_&&!mutationWorking_&&mutationNeedsAcknowledgement_);
    }
    set(reload,mutationCompletion_.retained()?L"Review":L"Reload");set(fromCursor,mutationCompletion_.retained()?L"Accept state":L"From cursor");
    // Disabling native controls clears their focus during worker requests.
    // Restore only that lost focus, never a newer choice or inactive window.
    if(pending_&&previousFocus&&IsChild(window_,previousFocus)&&!GetFocus())pendingFocus_=previousFocus;
    if(!pending_&&!mutationFrozen()&&pendingFocus_){const auto restore=pendingFocus_;pendingFocus_=nullptr;if(!GetFocus()&&IsWindowVisible(restore)&&IsWindowEnabled(restore)&&GetActiveWindow()==GetAncestor(window_,GA_ROOT))SetFocus(restore);}
    layoutPending_=pending_;
  }
