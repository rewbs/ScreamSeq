// Private class-body include. All pointer/key state belongs to this HWND.
  void duplicate(){
    saveSelected();Json item=selected_>=0?draft_.at(size_t(selected_)):edited(-1);
    if(selected_>=0){const auto begin=item.at("position").get<unsigned>()%unitsPerRow;std::set<unsigned> used;
      for(const auto &event:draft_)used.insert(event.at("position").get<unsigned>()%unitsPerRow);
      const auto preferred=std::min(65535u,begin+4096);auto next=preferred;while(next<unitsPerRow&&used.contains(next))++next;
      if(next==unitsPerRow){next=begin+1;while(next<preferred&&used.contains(next))++next;require(next<preferred,"Move this hit earlier to leave room for another");}
      item["position"]=target_.row*unitsPerRow+next;}
    require(draft_.size()<maximumEvents,"Too many precise notes");draft_.push_back(item);++generation_;refresh(item);status(L"Hit added to draft");
  }
  void remove(){if(selected_<0)return;const auto index=size_t(selected_);draft_.erase(draft_.begin()+selected_);++generation_;
    refresh(draft_.empty()?Json():draft_[std::min(index,draft_.size()-1)]);status(L"Hit removed from draft");}
  void retrigger(){
    saveSelected();require(selected_>=0,"Select an onset to retrigger");const auto original=draft_.at(size_t(selected_));
    require(original.at("note").get<unsigned>()<128,"Select an onset to retrigger");
    const auto count=unsigned(numeric(repeatCount,2,64,true)),end=unsigned(numeric(endVelocity,1,127,true));
    const auto start=original.at("position").get<unsigned>()%unitsPerRow,remaining=unitsPerRow-start;
    require(remaining>=count,"Move the first hit earlier to fit the retriggers");require(draft_.size()-1+count<=maximumEvents,"Too many precise notes");
    Json copies=Json::array();for(unsigned n=0;n<count;++n){auto item=original;item["position"]=target_.row*unitsPerRow+start+n*remaining/count;
      item["velocity"]=unsigned(std::lround(original.at("velocity").get<double>()+(double(end)-original.at("velocity").get<double>())*n/(count-1)));
      require(!collision(item,selected_),"Retriggers overlap another hit");copies.push_back(std::move(item));}
    draft_.erase(draft_.begin()+selected_);for(const auto &item:copies)draft_.push_back(item);++generation_;
    toolsBaseline_["repeatCount"]=utf8(field(repeatCount));toolsBaseline_["endVelocity"]=utf8(field(endVelocity));
    refresh(copies[0]);status(L"Retriggers added / Apply saves them together");
  }
  void action(int id,unsigned notification)override{
    if(setting_||id==detailsText)return;
    if(const auto nextPage=pageIndex(id);nextPage>=0){if(dragging_)cancelDrag();page_=nextPage;layout();focusPage();return;}
    if(id==close){hide();return;}
    if(id==returnPattern){cancelDrag();if(return_)return_();return;}
    const bool text=id==instrument||id==velocity||id==offset||id==parameter||id==repeatCount||id==endVelocity;
    if(text){if(notification==EN_CHANGE)fieldChanged();return;}
    if(pending_&&notification==CBN_SELCHANGE&&(id==pitch||id==units||id==snap||id==localEffect)){
      ++generation_;if(id==units)selection(units,int(units_));if(id==snap)selection(snap,int(snap_));
      status(L"Newer field choice retained while the request completes");return;
    }
    try{
      require(!pending_&&!dragging_&&!context_().busy,"Precise-note operation is pending");
      if(id==loadTarget){loadSelection();return;}if(id==reloadCapturedCommand){reloadCaptured();return;}
      require(hasTarget(),"Open a precise-note row first");
      if(id==list&&notification==LBN_SELCHANGE){const auto next=SendMessageW(controls_.at(list),LB_GETCURSEL,0,0);saveSelected();if(next>=0&&size_t(next)<draft_.size())refresh(draft_[size_t(next)]);}
      else if(id==units&&notification==CBN_SELCHANGE){
        const auto next=selection(units);require(next>=0&&next<2,"Choose units");const auto amount=rowOffset();
        const bool clean=pointRaw().at("offset")==pointBaseline_.at("offset");units_=unsigned(next);
        {Setting guard(setting_);set(offset,offsetText(std::min(65535u,unsigned(std::lround(amount*unitsPerRow)))));}
        if(clean)pointBaseline_["offset"]=utf8(field(offset));++generation_;
      }else if(id==snap&&notification==CBN_SELCHANGE){const auto next=selection(snap);require(next>=0&&next<4,"Choose snap");snap_=unsigned(next);++generation_;}
      else if(id==localEffect&&notification==CBN_SELCHANGE){const auto next=selection(localEffect);require(next>=0&&size_t(next)<effects_.size(),"Choose a local effect");
        wchar_t hex[8]{};swprintf_s(hex,L"%02X",effects_[size_t(next)].at("suggestedParameter").get<unsigned>());{Setting guard(setting_);set(parameter,hex);}fieldChanged();}
      else if(id==pitch&&notification==CBN_SELCHANGE)fieldChanged();
      else if(id==replaceRow){replaceLegacy_=!replaceLegacy_;++generation_;}
      else if(id==addHit)duplicate();else if(id==removeHit)remove();else if(id==fillRow)retrigger();
      else if(id==applyDraft||id==checkDraft)apply(id==checkDraft);
    }catch(const std::exception &e){if(id==list)ScreamSeq::NativeInputGate::present(controls_.at(list),LB_SETCURSEL,selected_,0);if(id==units)selection(units,int(units_));error(e);}
  }
  unsigned snapStep()const{return snap_==0?256:std::max(1u,unitsPerRow*rowsPerBeat_/(snap_==1?16:snap_==2?32:64));}
  unsigned positionAt(float x,bool free)const{
    double value=std::clamp(double(x-plot_.x)/plot_.w,0.0,1.0)*unitsPerRow;
    if(snap_&&!free){const double base=double(target_.row)*unitsPerRow,step=snapStep();value=std::round((base+value)/step)*step-base;}
    return target_.row*unitsPerRow+unsigned(std::clamp(std::lround(value),0l,65535l));
  }
  unsigned velocityAt(float y)const{return unsigned(std::clamp(std::lround((1.0-double(y-plot_.y)/plot_.h)*127),1l,127l));}
  std::pair<float,float> point(const Json &event)const{return {plot_.x+float(event.at("position").get<unsigned>()%unitsPerRow)/unitsPerRow*plot_.w,
    plot_.y+(1-float(event.at("velocity").get<unsigned>())/127)*plot_.h};}
  void cancelDrag(){
    if(!dragging_)return;dragging_=false;draft_=std::move(dragBefore_);dragBefore_=Json::array();++generation_;
    if(GetCapture()==window_)ReleaseCapture();refresh(dragSelection_>=0&&size_t(dragSelection_)<draft_.size()?draft_[size_t(dragSelection_)]:Json());status(L"Timing drag cancelled");
  }
  void deactivate()override{cancelDrag();}
  void mouse(UINT message,float x,float y,WPARAM)override{
    if(message==WM_CAPTURECHANGED){if(dragging_)cancelDrag();return;}
    if(message==WM_LBUTTONUP&&dragging_){dragging_=false;const auto item=selected_>=0?draft_[size_t(selected_)]:Json();dragBefore_=Json::array();
      if(GetCapture()==window_)ReleaseCapture();refresh(item);status(L"Timing draft / Apply saves all hits together");return;}
    if(!canvasVisible_||!hasTarget()||pending_||context_().busy)return;
    try{
      if((message==WM_LBUTTONDOWN||message==WM_LBUTTONDBLCLK)&&plot_.contains(x,y)){
        SetFocus(window_);saveSelected();int nearest=-1;float distance=144;
        for(size_t i=0;i<draft_.size();++i){const auto [px,py]=point(draft_[i]);const auto d=(px-x)*(px-x)+(py-y)*(py-y);if(d<distance){nearest=int(i);distance=d;}}
        if(nearest<0&&message==WM_LBUTTONDBLCLK){require(draft_.size()<maximumEvents,"Too many precise notes");auto item=selected_>=0?draft_[size_t(selected_)]:edited(-1);
          item["position"]=positionAt(x,(GetKeyState(VK_MENU)&0x8000)!=0);if(item.at("note").get<unsigned>()<128)item["velocity"]=velocityAt(y);
          require(!collision(item),"Another event already occupies that offset");draft_.push_back(item);++generation_;refresh(item);status(L"Hit added / Apply saves the draft");
        }else if(nearest>=0){refresh(draft_[size_t(nearest)]);dragBefore_=draft_;dragSelection_=selected_;dragging_=true;SetCapture(window_);}
        else refresh();
      }else if(message==WM_MOUSEMOVE&&dragging_&&selected_>=0){auto item=draft_[size_t(selected_)];const auto previous=item;
        item["position"]=positionAt(x,(GetKeyState(VK_MENU)&0x8000)!=0);if(!(GetKeyState(VK_SHIFT)&0x8000)&&item.at("note").get<unsigned>()<128)item["velocity"]=velocityAt(y);
        require(!collision(item,selected_),"Another event occupies that offset");if(item!=previous){draft_[size_t(selected_)]=item;
          --density_[(previous.at("position").get<unsigned>()%unitsPerRow)/256];++density_[(item.at("position").get<unsigned>()%unitsPerRow)/256];
          ++generation_;loadFields();InvalidateRect(controls_.at(list),nullptr,FALSE);requestPaint();}}
    }catch(const std::exception &e){error(e);}
  }
  void focusPage(){SetFocus(page_==0?window_:controls_.at(page_==1?list:page_==2?repeatCount:detailsText));requestPaint();}
  bool key(WPARAM key,bool ctrl,bool shift)override{
    const auto focused=GetFocus();if(!owns(focused))return false;const bool alt=(GetKeyState(VK_MENU)&0x8000)!=0;const int id=GetDlgCtrlID(focused);
    if(key==VK_ESCAPE){if(dragging_)cancelDrag();else if(return_)return_();return true;}
    if(!ctrl&&!alt&&key==VK_F6){page_=page_==0?1:0;layout();focusPage();return true;}
    if(key==VK_RETURN&&!alt&&!ctrl){
      wchar_t kind[32]{};GetClassNameW(focused,kind,32);
      if(_wcsicmp(kind,L"Button")==0){
        if(!IsWindowVisible(focused)||!IsWindowEnabled(focused))return false;
        action(id,BN_CLICKED);layout();return true;
      }
    }
    if(id==detailsText&&key==VK_RETURN&&!ctrl)return false;
    if(key==VK_RETURN&&!alt&&id!=pitch&&id!=units&&id!=snap&&id!=localEffect){action(applyDraft,BN_CLICKED);layout();return true;}
    if(ctrl&&!alt&&key=='D'&&(focused==window_||focused==controls_.at(list))){action(addHit,BN_CLICKED);layout();return true;}
    if(focused!=window_||!canvasVisible_||!hasTarget())return false;
    if(!ctrl&&!alt&&key==VK_TAB){page_=1;layout();focusPage();return true;}
    if(!ctrl&&!alt&&key==VK_DELETE){action(removeHit,BN_CLICKED);layout();return true;}
    if(ctrl||pending_||context_().busy||selected_<0||key<VK_LEFT||key>VK_DOWN)return false;
    // Clicking a canvas can focus it before invalid field parsing rejects the
    // click. A subsequent arrow must not silently replace those raw fields.
    try{saveSelected();}catch(const std::exception &e){error(e);return true;}
    auto item=draft_[size_t(selected_)];if(key==VK_LEFT||key==VK_RIGHT){const int step=alt?1:int(snapStep()),offset=int(item.at("position").get<unsigned>()%unitsPerRow);
      item["position"]=target_.row*unitsPerRow+unsigned(std::clamp(offset+(key==VK_LEFT?-step:step),0,65535));}
    else if(item.at("note").get<unsigned>()<128)item["velocity"]=std::clamp(item.at("velocity").get<int>()+(key==VK_UP?1:-1)*(shift?8:1),1,127);
    if(collision(item,selected_)){status(L"Another event occupies that offset");return true;}
    draft_[size_t(selected_)]=item;++generation_;refresh(item);status(L"Timing draft / Apply saves all hits together");return true;
  }
  void drawControl(const DRAWITEMSTRUCT &item)override{
    if(item.CtlType==ODT_BUTTON&&pageIndex(int(item.CtlID))>=0){
      NativeControls::recordDraw(item.hwndItem);const auto saved=SaveDC(item.hDC);const bool active=pageIndex(int(item.CtlID))==page_,contrast=NativeControls::highContrast();
      NativeControls::fill(item.hDC,item.rcItem,contrast?GetSysColor(active?COLOR_HIGHLIGHT:COLOR_WINDOW):active?RGB(38,69,75):RGB(35,49,63));
      SetBkMode(item.hDC,TRANSPARENT);SelectObject(item.hDC,font_);SetTextColor(item.hDC,contrast?GetSysColor(active?COLOR_HIGHLIGHTTEXT:COLOR_WINDOWTEXT):active?RGB(164,240,221):RGB(218,232,241));
      auto bounds=item.rcItem;
      DrawTextW(item.hDC,pageNames[size_t(pageIndex(int(item.CtlID)))],-1,&bounds,DT_SINGLELINE|DT_VCENTER|DT_CENTER|DT_NOPREFIX);
      if(active){auto edge=item.rcItem;edge.top=edge.bottom-std::max(1,MulDiv(2,GetDpiForWindow(window_),96));NativeControls::fill(item.hDC,edge,contrast?GetSysColor(COLOR_HIGHLIGHTTEXT):RGB(114,216,191));}
      if((item.itemState&ODS_FOCUS)&&!(item.itemState&ODS_NOFOCUSRECT)){auto focus=item.rcItem;InflateRect(&focus,-3,-3);DrawFocusRect(item.hDC,&focus);}
      RestoreDC(item.hDC,saved);return;
    }
    if(item.CtlID!=list){NativeToolWindow::drawControl(item);return;}
    NativeControls::recordDraw(item.hwndItem);const auto saved=SaveDC(item.hDC);const bool chosen=(item.itemState&ODS_SELECTED)!=0,disabled=(item.itemState&ODS_DISABLED)!=0;
    SetDCBrushColor(item.hDC,chosen?RGB(46,79,83):RGB(24,34,45));FillRect(item.hDC,&item.rcItem,reinterpret_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    if(item.itemID<draft_.size()){
      const auto columns=hitColumns(draft_[item.itemID]);const auto scale=GetDpiForWindow(window_)/96.f;
      const int inset=int(std::lround(6*scale)),fixed=int(std::lround(194*scale));
      const LONG timeWidth=std::max(1L,(item.rcItem.right-item.rcItem.left-2*inset-fixed)/2);
      const std::array<LONG,6> widths={timeWidth,timeWidth,int(std::lround(50*scale)),int(std::lround(36*scale)),int(std::lround(40*scale)),int(std::lround(68*scale))};
      auto bounds=item.rcItem;bounds.left+=inset;SetBkMode(item.hDC,TRANSPARENT);SetTextColor(item.hDC,disabled?RGB(103,119,133):RGB(218,232,241));
      const auto old=SelectObject(item.hDC,font_);
      for(size_t column=0;column<columns.size();++column){bounds.right=std::min(item.rcItem.right-inset,bounds.left+widths[column]);
        DrawTextW(item.hDC,columns[column].c_str(),-1,&bounds,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);bounds.left=bounds.right;}
      SelectObject(item.hDC,old);}
    if(item.itemState&ODS_FOCUS)DrawFocusRect(item.hDC,&item.rcItem);RestoreDC(item.hDC,saved);
  }
