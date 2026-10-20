// Private class-body include. No Main command dispatch, timer or drag state.
  void focusPage(){
    HWND target=page_==0?window_:controls_.at(page_==1?formula:pattern);
    if(!IsWindowVisible(target)||!IsWindowEnabled(target))target=controls_.at(pageCurve+page_);
    SetFocus(target);requestPaint();
  }
  void choosePage(int page,bool focus=true){
    if(dragging_)cancelDrag();page_=std::clamp(page,0,2);layout();if(focus)focusPage();requestPaint();
  }
  void action(int id,unsigned notification)override{
    if(setting_)return;
    if(notification==EN_CHANGE&&(id==pointRow||id==pointValue||id==formula)){
      pointFields_=true;++generation_;if(selected_>=0)queueCurveEdit();status(selected_>=0?L"Saving point changes…":L"Enter point fields, then Set point");return;
    }
    if(id>=pageCurve&&id<=pageTools&&notification==BN_CLICKED){choosePage(id-pageCurve);return;}
    if(id==close&&notification==BN_CLICKED){if(dragging_)cancelDrag();hide();return;}
    if(id==returnPattern&&notification==BN_CLICKED){if(dragging_)cancelDrag();if(return_)return_();return;}
    if(id==reference&&notification==BN_CLICKED){openFormulaReference();return;}
    if(notification==CBN_SELCHANGE&&(id==pattern||id==kind||id==snap)){
      require(!pending_&&!dragging_&&!openingChild_,"Curve editor is busy");
      if(id==pattern){
        const auto chosen=selection(pattern);
        try{flushAutomaticEdit();}catch(...){restorePatternChoice();throw;}
        if(retainedDraft()){restorePatternChoice();throw std::runtime_error("Apply or Reload this curve and finish its child draft before changing pattern");}
        require(chosen>=0&&size_t(chosen)<patterns_.size(),"Choose an available pattern");
        auto target=target_;target.patternID=patterns_[size_t(chosen)].at("id");
        const auto now=context_();require(now.document==document_,"Document changed / Load selection to capture the new song");
        try{load(now,resolve(now,std::move(target)),false);}catch(...){restorePatternChoice();throw;}return;
      }
      if(id==kind){const auto chosen=selection(kind);require(chosen>=0&&chosen<int(curves.size()),"Choose a curve kind");kind_=chosen;pointFields_=true;++generation_;if(selected_>=0)queueCurveEdit();status(selected_>=0?L"Saving point changes…":L"Enter point fields, then Set point");return;}
      const auto chosen=selection(snap);require(chosen>=0&&chosen<4,"Choose a snap interval");snapIndex_=chosen;snapUnits_=std::array<unsigned,4>{256,128,64,1}[size_t(snapIndex_)];return;
    }
    if(notification!=BN_CLICKED)return;
    require(!pending_&&!dragging_&&!openingChild_,"Curve editor is busy");
    if(id==reload){reloadCaptured();return;}
    if(id==follow){
      // Explicit Load selection may replace local edits after a successful read.
      // Automatic/root followSelection remains blocked by retained drafts.
      // Child windows keep their text and reject Use through the old token.
      captureSelection({},false);status(L"Selected source loaded / earlier local curve replaced");return;
    }
    require(hasTarget(),"Select an automation source in Graph first");
    if(id==bank){openBank();return;}if(id==expand){openFormula();return;}
    if(id==fit||id==zoomIn||id==zoomOut){if(id==fit)canvas_.fit(rows_);else canvas_.zoom(id==zoomIn?2:.5,rows_);schedule();rebuild();return;}
    if(id==setPoint){replacePoint(pointFromFields());return;}
    if(id==preview){if(pointFields_)replacePoint(pointFromFields());previewNow();return;}
    if(id==apply){commit();return;}
    require(!pointFields_,"Set the point or press Escape before changing the curve");
    if(id==enabled){enabled_=!enabled_;changed();return;}
    if(id==deletePoint){if(selected_>=0&&size_t(selected_)<points_.size()){points_.erase(points_.begin()+selected_);selected_=points_.empty()?-1:std::min(selected_,int(points_.size())-1);changed();showPoint();}return;}
    if(id==clear||id==ramp){
      points_=id==clear?Json::array():Json::array({{{"position",0},{"value",0},{"curve","linear"}},{{"position",uint64_t(rows_)*256-1},{"value",1},{"curve","linear"}}});
      selected_=points_.empty()?-1:0;changed();showPoint();return;
    }
  }
  uint32_t positionAt(float x)const{return uint32_t(std::clamp(std::round(canvas_.position(x)/snapUnits_)*snapUnits_,0.0,double(rows_)*256-1));}
  void cancelDrag(){
    if(!dragging_)return;dragging_=false;points_=dragBefore_;dirty_=dragDirty_;selected_=dragSelection_;++generation_;
    values_=Json::array();schedule();showPoint();rebuild();if(GetCapture()==window_)ReleaseCapture();
  }
  void deactivate()override{cancelDrag();}
  void mouse(UINT message,float x,float y,WPARAM buttons)override{
    if(message==WM_CAPTURECHANGED){cancelDrag();return;}
    if(message==WM_LBUTTONUP&&dragging_){dragging_=false;dragBefore_=Json::array();if(GetCapture()==window_)ReleaseCapture();schedule();if(dirty_)queueCurveEdit(1);return;}
    if(!canvasVisible_||pending_||openingChild_)return;
    const auto r=canvas_.viewport;const WorkspaceRect hit{r.x-7,r.y-7,r.w+14,r.h+14};
    if(message==WM_LBUTTONDOWN&&hit.contains(x,y)&&hasTarget()){
      require(!pointFields_,"Set or discard the point fields before selecting another point");
      SetFocus(window_);dragBefore_=points_;dragDirty_=dirty_;dragSelection_=selected_;selected_=canvas_.hit(x,y);
      if(selected_<0){Json point={{"position",positionAt(x)},{"value",canvas_.value(y)},{"curve",curves[size_t(kind_)]}};if(kind_==8)point["formula"]=utf8(field(formula));replacePoint(std::move(point));}
      else showPoint();dragging_=true;SetCapture(window_);return;
    }
    if(message==WM_MOUSEMOVE&&dragging_&&(buttons&MK_LBUTTON)&&selected_>=0){
      auto point=points_[size_t(selected_)];point["position"]=positionAt(x);point["value"]=canvas_.value(y);
      try{replacePoint(std::move(point));}catch(const std::exception &e){error(e);}
    }
  }
  bool wheel(UINT message,float x,float y,WPARAM buttons)override{
    if(!canvasVisible_||!canvas_.viewport.contains(x,y)||dragging_)return false;
    const auto delta=double(GET_WHEEL_DELTA_WPARAM(buttons))/WHEEL_DELTA;
    if(GET_KEYSTATE_WPARAM(buttons)&MK_CONTROL)canvas_.zoom(std::pow(2.,delta),rows_);
    else canvas_.pan((message==WM_MOUSEHWHEEL?1:-1)*delta*(canvas_.end-canvas_.start)*.125,rows_);
    schedule();rebuild();return true;
  }
  bool key(WPARAM key,bool ctrl,bool shift)override{
    const bool alt=(GetKeyState(VK_MENU)&0x8000)!=0;
    if(alt||!owns(GetFocus()))return false;
    if(key==VK_ESCAPE){if(dragging_)cancelDrag();else if(pointFields_){++generation_;showPoint();}else hide();return true;}
    if(ctrl&&(key==VK_PRIOR||key==VK_NEXT)){choosePage((page_+(key==VK_PRIOR?2:1))%3);return true;}
    if(key==VK_F6){if(page_==0&&hasTarget())SetFocus(GetFocus()==window_?controls_.at(pointRow):window_);else focusPage();return true;}
    if(ctrl&&key=='R'){action(reload,BN_CLICKED);return true;}
    if(ctrl&&key==VK_RETURN){action(apply,BN_CLICKED);return true;}
    const auto id=GetDlgCtrlID(GetFocus());
    if(key==VK_RETURN&&(id==pointRow||id==pointValue||id==formula)){action(setPoint,BN_CLICKED);return true;}
    if(key==VK_RETURN){wchar_t name[32]{};GetClassNameW(GetFocus(),name,32);if(_wcsicmp(name,L"Button")==0){action(id,BN_CLICKED);return true;}}
    if(GetFocus()!=window_||!canvasVisible_||pending_||ctrl)return false;
    if(key==VK_DELETE){action(deletePoint,BN_CLICKED);return true;}
    if(key==VK_HOME){action(fit,BN_CLICKED);return true;}
    if(key==VK_OEM_PLUS||key==VK_ADD){action(zoomIn,BN_CLICKED);return true;}
    if(key==VK_OEM_MINUS||key==VK_SUBTRACT){action(zoomOut,BN_CLICKED);return true;}
    if(pointFields_)return false;
    if(key==VK_TAB){if(!points_.empty()){selected_=(selected_+(shift?-1:1)+int(points_.size()))%int(points_.size());showPoint();requestPaint();}return true;}
    if(selected_>=0&&(key==VK_LEFT||key==VK_RIGHT||key==VK_UP||key==VK_DOWN)){
      auto point=points_[size_t(selected_)];
      if(key==VK_LEFT||key==VK_RIGHT)point["position"]=uint32_t(std::clamp(point.at("position").get<double>()+(key==VK_LEFT?-1:1)*(shift?1.:double(snapUnits_)),0.,double(rows_)*256-1));
      else point["value"]=std::clamp(point.at("value").get<double>()+(key==VK_UP?1:-1)*(shift?.001:.01),0.,1.);
      replacePoint(std::move(point));return true;
    }
    return false;
  }
  void resumeVisiblePresentation()noexcept override{if(previewNeeded_)SetTimer(window_,3,120,nullptr);}
  void timer(UINT_PTR id)override{
    if(id!=3)return;KillTimer(window_,3);if(!visible())return;
    if(pending_||dragging_||openingChild_||context_().busy){if(previewNeeded_)SetTimer(window_,3,120,nullptr);return;}
    if(previewNeeded_)previewNow();
  }
