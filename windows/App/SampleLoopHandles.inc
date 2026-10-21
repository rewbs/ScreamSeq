  // Native waveform presentation; LoopDraft and sample.loops.set retain the
  // complete normal/sustain pair, revision guard, history and PCM semantics.
  struct LoopHandle {int index;unsigned frame;float x,y;};
  int selectedLoopHandle_=-1;
  bool loopDragging_=false;
  uint64_t loopGestureGeneration_=0;
  unsigned loopDragFrame_=0;
  float loopDragX_=0;
  SampleWorkflow::LoopDraft loopDragBefore_;
  WorkspaceRect loopDragCanvas_;
  static const wchar_t *loopHandleName(int index){
    return std::array<const wchar_t *,4>{L"Loop start",L"Loop end",L"Sustain start",L"Sustain end"}.at(size_t(index));
  }
  std::vector<LoopHandle> loopHandles()const{
    std::vector<LoopHandle> result;
    if(!loops_.loaded()||!region_.frames||canvas_.w<120||canvas_.h<28)return result;
    try{
      const auto values=SampleWorkflow::loopParams(loops_.target(),loops_.raw(),true);
      for(int i=0;i<4;++i){
        const auto &row=values.at(i<2?"normal":"sustain");
        if(!row.at("enabled").get<bool>())continue;
        const auto frame=row.at(i%2?"end":"start").get<unsigned>();
        if(frame<region_.start||frame>region_.end)continue;
        result.push_back({i,frame,std::clamp(xAt(frame),canvas_.x+7,canvas_.x+canvas_.w-7),
          i<2?canvas_.y+10:canvas_.y+canvas_.h-10});
      }
    }catch(const std::exception &){/* Invalid raw fields retain their text, not invented handles. */}
    return result;
  }
  int hitLoopHandle(float x,float y)const{
    int hit=-1;float distance=std::numeric_limits<float>::max();
    for(const auto &h:loopHandles()){
      const float dx=std::abs(h.x-x),dy=std::abs(h.y-y),d=dx+dy;
      if(dx<=10&&dy<=12&&d<distance){hit=h.index;distance=d;}
    }
    return hit;
  }
  void requireLoopGesture()const{
    requireCurrent();
    require(loops_.target()==workflowTarget(),"Loop target changed / Reload loops before moving a handle");
    require(!fields_&&stroke_.empty(),"Finish or discard the sample fields and drawing before moving a loop handle");
  }
  void moveLoopHandle(int index,int64_t frame){
    requireLoopGesture();
    auto raw=loopFields();
    const auto parsed=SampleWorkflow::loopParams(loops_.target(),raw,true);
    const auto &saved=parsed.at(index<2?"normal":"sustain");
    require(saved.at("enabled").get<bool>(),"Enable the loop before moving its handle");
    const auto first=saved.at("start").get<unsigned>(),last=saved.at("end").get<unsigned>();
    auto &row=index<2?raw.normal:raw.sustain;
    const auto next=std::clamp(frame,index%2?int64_t(first)+1:0,
      index%2?int64_t(region_.frames):int64_t(last)-1);
    (index%2?row.end:row.start)=std::to_wstring(next);
    if(loops_.replaceRaw(std::move(raw))){
      ++generation_;report_=Json::object();invalidatePaste();syncLoops();requestPaint();
    }
    loopGestureGeneration_=generation_;
    status(std::wstring(loopHandleName(index))+L": "+std::to_wstring(next)+
      (loopDragging_?L" frames / release saves / Escape cancels":L" frames / arrows move 1, Shift+arrows 64"));
  }
  void cancelLoopGesture(){
    if(!loopDragging_)return;
    cancelAutomaticEdit();
    // A newer native edit retains its raw text and target. Only our own
    // uninterrupted drag may restore its captured pre-gesture loop draft.
    const bool unchanged=generation_==loopGestureGeneration_;
    loopDragging_=false;
    if(unchanged){loops_=loopDragBefore_;syncLoops();}
    if(GetCapture()==window_)ReleaseCapture();
    ++generation_;requestPaint();
  }
  bool loopMouse(UINT message,float x,float y){
    if(message==WM_LBUTTONDOWN&&!dragging_&&!selecting_){
      const int hit=hitLoopHandle(x,y);if(hit<0)return false;
      requireLoopGesture();require(!loops_.dirty(),"Finish or reload the loop fields before dragging a handle");
      cancelAutomaticEdit();selectedLoopHandle_=hit;loopDragBefore_=loops_;
      const auto handles=loopHandles();
      loopDragFrame_=std::find_if(handles.begin(),handles.end(),[&](const auto &h){return h.index==hit;})->frame;
      loopDragX_=x;
      loopDragCanvas_=canvas_;loopGestureGeneration_=generation_;loopDragging_=true;
      SetFocus(window_);SetCapture(window_);
      status(std::wstring(loopHandleName(hit))+L" / drag to move / release saves / Escape cancels");
      requestPaint();return true;
    }
    if(!loopDragging_||(message!=WM_MOUSEMOVE&&message!=WM_LBUTTONUP))return false;
    try{
      require(generation_==loopGestureGeneration_&&canvas_.x==loopDragCanvas_.x&&canvas_.y==loopDragCanvas_.y&&
        canvas_.w==loopDragCanvas_.w&&canvas_.h==loopDragCanvas_.h,
        "Fields or view changed during loop drag / gesture cancelled; newer fields retained");
      // Preserve the initial grab offset, including handles inset from the
      // viewport edge. A click without motion must not change a loop boundary.
      const auto delta=std::llround(double(x-loopDragX_)*(region_.end-region_.start)/std::max(1.f,canvas_.w));
      moveLoopHandle(selectedLoopHandle_,int64_t(loopDragFrame_)+delta);
      if(message==WM_LBUTTONUP){
        loopDragging_=false;ReleaseCapture();
        require(generation_==loopGestureGeneration_,"Fields changed on release / loop draft retained");
        if(loops_.dirty())editLoops(false);
        else status(L"Loop handle unchanged / no history entry");
      }
    }catch(...){cancelLoopGesture();throw;}
    return true;
  }
  bool loopKey(WPARAM key,bool ctrl,bool shift){
    if(ctrl||GetFocus()!=window_||dragging_||selecting_||loopDragging_)return false;
    const auto handles=loopHandles();
    if(key==VK_TAB&&!handles.empty()){
      const auto found=std::find_if(handles.begin(),handles.end(),[&](const auto &h){return h.index==selectedLoopHandle_;});
      const int count=int(handles.size()),at=found==handles.end()?(shift?0:-1):int(found-handles.begin());
      selectedLoopHandle_=handles[size_t((at+(shift?-1:1)+count)%count)].index;
      status(std::wstring(loopHandleName(selectedLoopHandle_))+L" / arrows move 1 frame, Shift+arrows 64 / F6 for fields");
      requestPaint();return true;
    }
    if(selectedLoopHandle_<0||(key!=VK_LEFT&&key!=VK_RIGHT))return false;
    const auto found=std::find_if(handles.begin(),handles.end(),[&](const auto &h){return h.index==selectedLoopHandle_;});
    if(found==handles.end()){selectedLoopHandle_=-1;return false;}
    requireLoopGesture();require(!loops_.dirty(),"Finish or reload the loop fields before nudging a handle");
    cancelAutomaticEdit();moveLoopHandle(selectedLoopHandle_,int64_t(found->frame)+(key==VK_LEFT?-1:1)*(shift?64:1));
    if(loops_.dirty())editLoops(false);
    return true;
  }
  void paintLoopHandles(RenderSurface &s)const{
    for(const auto &h:loopHandles()){
      const auto color=h.index<2?0xe2b86c:0xb5a0e8;
      s.fill(h.x-5,h.y-7,10,14,color);
      if(h.index==selectedLoopHandle_)s.outline(h.x-7,h.y-9,14,18,0xffffff);
      const float labelX=std::clamp(h.x+10,canvas_.x+20,canvas_.x+canvas_.w-100);
      s.uiText(loopHandleName(h.index),labelX,h.y-9,96,color);
    }
  }
  Json loopHandleSnapshot()const{
    Json result=Json::array();
    for(const auto &h:loopHandles())result.push_back({{"index",h.index},{"name",utf8(loopHandleName(h.index))},
      {"frame",h.frame},{"x",h.x},{"y",h.y},{"selected",h.index==selectedLoopHandle_}});
    return result;
  }
