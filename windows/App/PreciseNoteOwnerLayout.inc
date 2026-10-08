// Private class-body include. Design-DIP geometry, not native/pixel qualification.
  bool shortLayout_=false;
  std::wstring displayStatus()const{
    if(!hasTarget())return L"Choose a row / Tools → Load selection";
    if(!current())return L"Song changed / draft retained / Reload captured before Apply";
    return status_;
  }
  int pageForControl(int id)const{
    if(id==detailsText)return 3;
    if(id==repeatCount||id==endVelocity||id==fillRow||id==replaceRow||id==loadTarget||id==returnPattern||id==close)return 2;
    if(id==list||id==pitch||id==instrument||id==localEffect||id==parameter)return 1;
    return page_; // Shared fields belong to their existing page.
  }
  void layout()override{
    if(!ready_)return;const auto [w,h]=size();const auto focused=GetFocus();const bool wasShort=shortLayout_;
    shortLayout_=docked()&&h<500;
    if(shortLayout_&&!wasShort&&focused&&IsChild(window_,focused))page_=pageForControl(GetDlgCtrlID(focused));
    const float left=8,inner=std::max(1.f,w-16),gap=6,third=(inner-2*gap)/3,half=(inner-gap)/2;
    std::set<int> shown;auto put=[&](int id,float x,float y,float width,float height){shown.insert(id);place(id,x,y,width,height);};
    // Reenabling/refocusing the native Hit list can reveal its caret item.
    // Preserve the explicit viewport through this pending-focus transition;
    // selection and scroll are independent, even when the hit is partly visible.
    const auto listControl=controls_.at(list);const bool retainListEntry=pending_&&focused==listControl;
    const auto restoreListTop=retainListEntry||(!pending_&&pendingFocus_==listControl)?SendMessageW(listControl,LB_GETTOPINDEX,0,0):LB_ERR;
    const auto tabWidth=(inner-3*gap)/4;
    for(size_t i=0;i<pageIDs.size();++i)put(pageIDs[i],left+float(i)*(tabWidth+gap),4,tabWidth,26);
    put(titleLabel,left,32,inner,18);set(titleLabel,hasTarget()?L"PRECISE NOTES / P"+std::to_wstring(target_.pattern)+L" R"+std::to_wstring(target_.row)+L" CH"+std::to_wstring(target_.channel+1)+L" / "+std::to_wstring(draft_.size())+L" hits":L"PRECISE NOTES / no captured row");
    for(int i=0;i<3;++i)put(std::array<int,3>{applyDraft,checkDraft,reloadCapturedCommand}[size_t(i)],left+i*(third+gap),h-60,third,26);
    put(statusLabel,left,h-30,inner,26);set(statusLabel,displayStatus());
    canvasVisible_=page_==0;plot_={};
    if(page_==0){
      put(snap,left,52,third,230);put(addHit,left+third+gap,52,third,26);put(removeHit,left+2*(third+gap),52,third,26);
      plot_={40,98,std::max(1.f,w-48),std::max(1.f,h-196)};
      const auto y=h-92;put(offsetLabel,8,y+4,38,18);put(offset,48,y,100,26);put(units,154,y,96,160);
      put(velocityLabel,256,y+4,58,18);put(velocity,320,y,std::max(1.f,w-328),26);
    }else if(page_==1){
      const auto extra=std::max(0.f,h-300);put(list,left,52,inner,54+extra);
      put(pitchLabel,left,110+extra,178,18);put(instrumentLabel,192,110+extra,112,18);put(velocityLabel,310,110+extra,std::max(1.f,w-318),18);
      put(pitch,left,128+extra,178,260);put(instrument,192,128+extra,112,26);put(velocity,310,128+extra,std::max(1.f,w-318),26);
      put(effectLabel,left,158+extra,300,18);put(parameterLabel,314,158+extra,std::max(1.f,w-322),18);
      put(localEffect,left,176+extra,300,260);put(parameter,314,176+extra,std::max(1.f,w-322),26);
      put(offsetLabel,left,h-88,38,18);put(offset,48,h-92,140,26);put(units,194,h-92,90,160);put(snap,290,h-92,std::max(1.f,w-298),230);
    }else if(page_==2){
      put(replaceRow,left,52,inner,26);set(replaceRow,replaceLegacy_?L"Replace ordinary row note: on":L"Replace ordinary row note: off");
      put(repeatLabel,left,90,half,18);put(endLabel,left+half+gap,90,half,18);
      put(repeatCount,left,110,half,26);put(endVelocity,left+half+gap,110,half,26);put(fillRow,left,142,inner,26);
      for(int i=0;i<3;++i)put(std::array<int,3>{loadTarget,returnPattern,close}[size_t(i)],left+i*(third+gap),h-100,third,26);
    }
    if(page_==3){put(detailsText,left,52,inner,h-120);set(detailsText,details());}
    for(const auto &[id,control]:controls_)if(!shown.contains(id))NativeControls::show(control,false);
    const bool onset=selection(pitch)>=0&&selection(pitch)<120;
    for(const auto &[id,control]:controls_){const bool navigation=pageIndex(id)>=0||id==detailsText||id==close||id==returnPattern;
      bool enabled=id>=titleLabel||navigation||(!pending_&&(hasTarget()||id==loadTarget));
      if(id==removeHit||id==fillRow)enabled=enabled&&selected_>=0;
      if(id==instrument||id==velocity||id==localEffect||id==parameter||id==fillRow)enabled=enabled&&onset;
      EnableWindow(control,enabled);}
    for(size_t i=0;i<pageIDs.size();++i)NativeControls::active(controls_.at(pageIDs[i]),page_==int(i));
    // Disabling the active field can transiently clear focus. Do not let another
    // owner/layout claim that null focus; restore only if it is still unclaimed.
    if(pending_&&focused&&IsChild(window_,focused)&&!GetFocus())pendingFocus_=focused;
    if(retainListEntry&&!GetFocus()&&pendingFocus_==listControl&&restoreListTop>=0&&!draft_.empty())
      SendMessageW(listControl,LB_SETTOPINDEX,std::min<size_t>(size_t(restoreListTop),draft_.size()-1),0);
    if(!pending_&&pendingFocus_){const auto restore=pendingFocus_;pendingFocus_=nullptr;
      if(!GetFocus()&&IsWindowVisible(restore)&&IsWindowEnabled(restore)&&GetActiveWindow()==GetAncestor(window_,GA_ROOT)){
        SetFocus(restore);
        if(restore==controls_.at(list)&&GetFocus()==restore&&restoreListTop>=0&&!draft_.empty())
          SendMessageW(restore,LB_SETTOPINDEX,std::min<size_t>(size_t(restoreListTop),draft_.size()-1),0);
      }}
    if(focused&&IsChild(window_,focused)&&!IsWindowVisible(focused)&&GetActiveWindow()==GetAncestor(window_,GA_ROOT))focusPage();
  }
  void paint(RenderSurface &surface)override{
    const auto [w,h]=size();surface.fill(0,0,w,h,0x18222d);
    if(page_==2){surface.uiText(L"Load selection replaces edits after a successful read.",8,174,w-16,0x94a4b4);return;}
    if(!canvasVisible_)return;
    const auto r=plot_;constexpr UINT32 accent=0x6edac5,gold=0xdcb971;
    const auto beat=hasTarget()?L"Beat "+std::to_wstring(target_.row/rowsPerBeat_+1)+L" / row "+std::to_wstring(target_.row%rowsPerBeat_+1)+L" of "+std::to_wstring(rowsPerBeat_)+L" / 0–"+decimal(1.0/rowsPerBeat_,5)+L" beat = 1 row":L"Captured row timing";
    surface.uiText(beat,r.x,80,r.w,0x94a4b4);surface.fill(r.x,r.y,r.w,r.h,0x101820);
    for(unsigned i=0;i<=4;++i){const auto x=r.x+r.w*i/4;surface.line(x,r.y,x,r.y+r.h,i==0?accent:0x2b3a48);}
    for(unsigned i=1;i<=3;++i)surface.line(r.x,r.y+r.h*i/4,r.x+r.w,r.y+r.h*i/4,0x22313f);
    surface.clip(r.x-6,r.y-6,r.w+12,r.h+12);
    auto marker=[&](const Json &event,bool selected){const auto [x,y]=point(event);const auto color=selected?gold:event.at("note").get<unsigned>()>=128?0xc398e6:accent;
      surface.line(x,r.y+r.h,x,y,color);surface.fill(x-3,y-3,6,6,color);if(selected)surface.outline(x-5,y-5,10,10,0xf1e6c9);};
    if(draft_.size()<=512){for(size_t i=0;i<draft_.size();++i)if(int(i)!=selected_)marker(draft_[i],false);}
    else{const auto maximum=*std::max_element(density_.begin(),density_.end());for(size_t i=0;i<density_.size();++i)if(density_[i]){
      const auto height=r.h*float(density_[i])/std::max(uint32_t(1),maximum);surface.fill(r.x+float(i)*r.w/256,r.y+r.h-height,std::max(1.f,r.w/256),height,0x387a72);}}
    if(selected_>=0)marker(draft_.at(size_t(selected_)),true);surface.unclip();
    surface.outline(r.x,r.y,r.w,r.h,GetFocus()==window_?accent:0x334757);surface.uiText(L"127",8,r.y-3,30,0x94a4b4);surface.uiText(L"1",8,r.y+r.h-15,30,0x94a4b4);
  }
