// Private class-body include. Short and floating layouts retain the same HWNDs.
  bool shortLayout_=false;
  std::wstring displayStatus()const{
    if(!hasTarget())return L"Choose a source in Graph / Tools → Load selection";
    if(hasTarget()&&!current())return L"Song changed / captured curve retained — Reload before Apply";
    if(pointFields_)return L"Point fields pending / Set point before Apply";
    return status_;
  }
  void drawControl(const DRAWITEMSTRUCT &d)override{
    if(d.CtlType!=ODT_BUTTON||d.CtlID<pageCurve||d.CtlID>pageTools){NativeToolWindow::drawControl(d);return;}
    NativeControls::recordDraw(d.hwndItem);const auto saved=SaveDC(d.hDC);
    const bool selected=int(d.CtlID)-pageCurve==page_,contrast=NativeControls::highContrast();
    NativeControls::fill(d.hDC,d.rcItem,contrast?GetSysColor(selected?COLOR_HIGHLIGHT:COLOR_WINDOW):selected?RGB(38,69,75):RGB(35,49,63));
    SetBkMode(d.hDC,TRANSPARENT);SelectObject(d.hDC,font_);
    SetTextColor(d.hDC,contrast?GetSysColor(selected?COLOR_HIGHLIGHTTEXT:COLOR_WINDOWTEXT):selected?RGB(164,240,221):RGB(218,232,241));
    RECT label=d.rcItem;label.left+=5;label.right-=5;
    static constexpr std::array<const wchar_t *,3> names={L"Curve",L"Formula",L"Tools"};
    DrawTextW(d.hDC,names[size_t(d.CtlID-pageCurve)],-1,&label,DT_SINGLELINE|DT_VCENTER|DT_CENTER|DT_NOPREFIX);
    if(selected){RECT edge=d.rcItem;edge.top=edge.bottom-std::max(1,MulDiv(2,GetDpiForWindow(window_),96));NativeControls::fill(d.hDC,edge,contrast?GetSysColor(COLOR_HIGHLIGHTTEXT):RGB(114,216,191));}
    if((d.itemState&ODS_FOCUS)&&!(d.itemState&ODS_NOFOCUSRECT)){RECT r=d.rcItem;InflateRect(&r,-3,-3);DrawFocusRect(d.hDC,&r);}RestoreDC(d.hDC,saved);
  }
  void layout()override{
    if(!ready_)return;
    if(bank_)bank_->refreshSourceState();
    if(workbench_)workbench_->refreshSourceState();
    const auto [w,h]=size();const auto focused=GetFocus();const bool wasShort=shortLayout_;
    shortLayout_=docked()&&h<500;
    // Pattern is common in floating mode, but belongs to Tools in short mode.
    // The same HWND must remain visible when its focused control crosses modes.
    if(shortLayout_&&!wasShort&&focused==controls_.at(pattern))page_=2;
    const float left=8,inner=std::max(1.f,w-16),gap=6,third=(inner-2*gap)/3,half=(inner-gap)/2;
    std::set<int> shown;
    auto put=[&](int id,float x,float y,float width,float height){shown.insert(id);place(id,x,y,width,height);};
    for(int i=0;i<3;++i)put(pageCurve+i,left+i*(third+gap),4,third,26);
    put(titleLabel,left,32,inner,18);
    set(titleLabel,title_+(hasTarget()?L" · P"+std::to_wstring(target_.pattern):L""));
    for(int i=0;i<3;++i)put(std::array<int,3>{apply,preview,reload}[size_t(i)],left+i*(third+gap),h-60,third,26);
    put(statusLabel,left,h-30,inner,26);set(statusLabel,displayStatus());
    const float shift=shortLayout_?0.f:32.f;
    if(!shortLayout_)put(pattern,left,52,inner,230);
    const auto pointFields=[&]{
      const float fieldWidth=(inner-30-46-176-5*gap)/2;const float y=h-92;float x=left;
      put(rowLabel,x,y+4,30,18);x+=30+gap;put(pointRow,x,y,fieldWidth,26);x+=fieldWidth+gap;
      put(valueLabel,x,y+4,46,18);x+=46+gap;put(pointValue,x,y,fieldWidth,26);x+=fieldWidth+gap;
      put(setPoint,x,y,88,26);x+=88+gap;put(deletePoint,x,y,88,26);
    };
    canvasVisible_=page_==0;canvas_.viewport={};
    if(page_==0){
      float x=left;for(auto [id,width]:std::initializer_list<std::pair<int,float>>{
        {kind,inner-278},{snap,82.f},{enabled,74.f},{fit,40.f},{zoomOut,26.f},{zoomIn,26.f}}){
        put(id,x,52+shift,width,id==kind||id==snap?230.f:26.f);x+=width+gap;
      }
      canvas_.viewport={46,96+shift,std::max(1.f,w-54),std::max(1.f,h-196-shift)};
      pointFields();
    }else if(page_==1){
      put(kind,left,52+shift,inner-86,230);put(enabled,w-88,52+shift,80,26);
      put(formulaLabel,left,84+shift,inner,18);put(formula,left,104+shift,inner,26);
      put(expand,left,136+shift,half,26);put(reference,left+half+gap,136+shift,half,26);
      put(helpLabel,left,168+shift,inner,std::max(18.f,h-264-shift));
      set(helpLabel,L"Scripted segments use a bounded expression. Expand keeps the original point; Guide lists supported values.");
      pointFields();
    }else{
      put(pattern,left,52,inner,230);
      for(int i=0;i<3;++i)put(std::array<int,3>{bank,ramp,clear}[size_t(i)],left+i*(third+gap),88,third,26);
      put(helpLabel,left,126,inner,std::max(1.f,h-234));
      const auto detail=hasTarget()?title_+L"\nPattern "+std::to_wstring(target_.pattern)+L" · "+std::to_wstring(rows_)+L" rows · "+std::to_wstring(rowsPerBeat_)+L" rows / beat\nReload discards edits on this target. Load selection replaces local edits only after a successful read.":L"Choose an automation source in Graph, then Load selection. Pattern and routing remain independent.";
      set(helpLabel,detail);
      for(int i=0;i<3;++i)put(std::array<int,3>{follow,returnPattern,close}[size_t(i)],left+i*(third+gap),h-100,third,26);
    }
    for(const auto &[id,control]:controls_)if(!shown.contains(id))NativeControls::show(control,false);
    set(enabled,enabled_?L"Enabled":L"Disabled");
    for(const auto &[id,control]:controls_){
      const bool label=id>=titleLabel;
      const bool navigation=(id>=pageCurve&&id<=pageTools)||id==close||id==returnPattern;
      const bool sourceFree=id==reference||id==follow;
      EnableWindow(control,label||navigation||(!pending_&&!openingChild_&&(sourceFree||hasTarget())));
    }
    EnableWindow(controls_.at(formula),!pending_&&!openingChild_&&hasTarget()&&kind_==8);
    const bool retainedWorkbench=workbench_&&(workbench_->visible()||workbench_->retainedDraft());
    EnableWindow(controls_.at(expand),!pending_&&!openingChild_&&(retainedWorkbench||(hasTarget()&&kind_==8)));
    for(int i=0;i<3;++i)NativeControls::active(controls_.at(pageCurve+i),page_==i);
    canvas_.rebuild(points_,values_);
    if(pending_&&focused&&IsChild(window_,focused)&&!GetFocus())pendingFocus_=focused;
    if(!pending_&&pendingFocus_){const auto restore=pendingFocus_;pendingFocus_=nullptr;
      if(!GetFocus()&&IsWindowVisible(restore)&&IsWindowEnabled(restore)&&GetActiveWindow()==GetAncestor(window_,GA_ROOT))SetFocus(restore);
    }
    if(focused&&IsChild(window_,focused)&&!IsWindowVisible(focused)&&GetActiveWindow()==GetAncestor(window_,GA_ROOT))focusPage();
  }
  void paint(RenderSurface &surface)override{
    const auto [w,h]=size();surface.fill(0,0,w,h,0x18222d);
    if(!canvasVisible_)return;
    const auto r=canvas_.viewport;surface.fill(r.x,r.y,r.w,r.h,0x10171f);surface.clip(r.x,r.y,r.w,r.h);
    for(int i=0;i<=4;++i){const auto y=r.y+r.h*i/4;surface.line(r.x,y,r.x+r.w,y,0x273541);}
    const auto step=std::max(256.,double(rowsPerBeat_)*256*std::ceil((canvas_.end-canvas_.start)/(double(rowsPerBeat_)*256)/16));
    for(double p=std::ceil(canvas_.start/step)*step;p<=canvas_.end;p+=step){const auto at=canvas_.screen(p,0);surface.line(at.x,r.y,at.x,r.y+r.h,0x273541);}
    for(size_t i=1;i<canvas_.curve.size();++i)surface.line(canvas_.curve[i-1].x,canvas_.curve[i-1].y,canvas_.curve[i].x,canvas_.curve[i].y,enabled_?0x68d3bc:0x63727f,2);
    for(size_t i=0;i<canvas_.handles.size();++i){const auto at=canvas_.handles[i];surface.fill(at.x-4,at.y-4,8,8,int(i)==selected_?0xf3dfb0:0x7ce5cd);}
    if(!hasTarget())surface.uiText(L"Choose a source in Graph, then Load selection",r.x+8,r.y+12,r.w-16,0xa4b7c7);
    surface.unclip();surface.outline(r.x,r.y,r.w,r.h,GetFocus()==window_?0x6edac5:0x334757);
    surface.uiText(L"100%",r.x-38,r.y-3,36,0x94a4b4);surface.uiText(L"0%",r.x-38,r.y+r.h-15,36,0x94a4b4);
    wchar_t text[128]{};swprintf_s(text,L"Rows %.2f–%.2f · 1/256 precision",canvas_.start/256,canvas_.end/256);
    surface.uiText(text,r.x,r.y-17,r.w,0x94a4b4);
  }
