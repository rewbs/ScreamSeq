#pragma once
#include "NativeToolWindow.hpp"
#include "NativeWriteCompletion.hpp"
#include "../../editor/MixerGesture.hpp"

namespace ScreamSeq {
// Native strips share the existing mixer API and history; Details remains a
// separate retained owner. Visible neighbors and captured controls bound HWNDs.
class MixerStripsWindow final:public NativeToolWindow {
public:
  using Json=Api::Json;
  using Read=std::function<Json(const std::string &,const Json &)>;
  using Context=std::function<std::pair<std::string,std::string>()>;
private:
  enum {previous=10,next=11,cancel=12,review=13,accept=14,base=100,stride=16};
  enum {name=0,fader=1,gain=2,pan=3,mute=4,solo=5,details=6,preGain=7,prePan=8,width=9,
    preGainLabel=10,prePanLabel=11,widthLabel=12,lastPart=widthLabel};
  Read request_;Context context_;NativeWriteCompletion::Write write_;
  std::function<void()> admit_,reveal_;std::function<void(const std::string &)> details_;
  NativeWriteCompletion completion_;Tracker::MixerGesture gesture_;
  Json data_=Json::object(),report_=Json::object();std::string document_,revision_;
  std::vector<std::string> bindings_;std::vector<size_t> positions_;size_t first_=0,lastVisibleCount_=0,viewportTotal_=0;
  std::map<std::string,Tracker::MixerMeter> meters_;
  bool setting_=false,pending_=false,captureLost_=false,rawDirty_=false,observed_=false,previewBlocked_=false,resetPresentation_=false;
  bool previewInFlight_=false,deferredCommit_=false,completedWithNewerInput_=false;
  int capturedControl_=0;
  float stripTop_=34,sliderTop_=57,sliderHeight_=32;
  int scrollOffset_=0,contentHeight_=0,wheelHorizontal_=0,wheelVertical_=0;bool layingOut_=false;
  HWND lastLayoutFocus_{};
  // 240 visible strips, two neighbors and distinct focus/gesture reservations.
  static constexpr size_t maximumVisibleStrips=240,maximumStrips=maximumVisibleStrips+4;
  bool retained()const noexcept{return gesture_.active()||pending_||completion_.retained();}
  const Json *bus(const std::string &id)const {
    if(!data_.contains("buses"))return nullptr;
    for(const auto &value:data_.at("buses"))if(value.at("id")==id)return &value;
    return nullptr;
  }
  int slot(int id)const {return id>=base&&size_t((id-base)/stride)<bindings_.size()?(id-base)/stride:-1;}
  bool pinned(size_t index)const {
    const auto focus=GetFocus();
    return (gesture_.active()&&bindings_[index]==gesture_.context().bus)||
      (GetParent(focus)==window_&&slot(GetDlgCtrlID(focus))==int(index));
  }
  void scrollBusesTo(size_t index) {
    const size_t total=viewportTotal_;
    first_=std::min(index,total>visibleCount()?total-visibleCount():0);
    layout();requestPaint();
  }
  void scrollTo(int offset) {
    scrollOffset_=std::clamp(offset,0,std::max(0,contentHeight_-int(size().second)));
    layout();requestPaint();
  }
  void revealControl(HWND control) {
    const int index=slot(GetDlgCtrlID(control));
    if(index>=0&&positions_[size_t(index)]!=SIZE_MAX) {
      const auto at=positions_[size_t(index)];
      if(at<first_)scrollBusesTo(at);
      else if(at>=first_+visibleCount())scrollBusesTo(at+1-visibleCount());
    }
    RECT bounds{};if(!GetWindowRect(control,&bounds))return;
    MapWindowPoints(nullptr,window_,reinterpret_cast<POINT *>(&bounds),2);
    const float scale=96.0f/GetDpiForWindow(window_);
    const auto height=size().second;
    if(bounds.top*scale<0)scrollTo(scrollOffset_+int(std::floor(bounds.top*scale)));
    else if(bounds.bottom*scale>height)scrollTo(scrollOffset_+int(std::ceil(bounds.bottom*scale-height)));
  }
  void installReveal(HWND control) {
    if(!SetWindowSubclass(control,[](HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR data)->LRESULT {
      auto &self=*reinterpret_cast<MixerStripsWindow *>(data);
      if(m==WM_SETFOCUS||m==WM_KEYDOWN)try{self.revealControl(h);}catch(const std::exception &e){self.error(e);}
      // Wheel navigation over a strip must not alter the control under it.
      // Trackbars otherwise consume the wheel without an end-track notification.
      if(m==WM_MOUSEWHEEL||m==WM_MOUSEHWHEEL) {
        try{self.wheel(m,0,0,w);}catch(const std::exception &e){self.error(e);}
        return 0;
      }
      return DefSubclassProc(h,m,w,l);
    },2,reinterpret_cast<DWORD_PTR>(this)))throw std::runtime_error("Cannot install mixer focus scrolling");
  }
  Json params(double value,bool preview,const Tracker::MixerGesture::Context &context)const {
    Json result={{"bus",context.bus},{"expectedRevision",context.revision}};
    const auto control=gesture_.control();
    result[Tracker::mixerControlKey(control)]=(control==Tracker::MixerControl::Mute||control==Tracker::MixerControl::Solo)?Json(value!=0):Json(value);
    if(preview)result["preview"]=true;
    return result;
  }
  void begin(int id,Tracker::MixerControl control) {
    if(pending_||completion_.retained())throw std::runtime_error("Review the previous mixer result first");
    const auto index=slot(id);if(index<0||bindings_[size_t(index)].empty())return;
    const auto &target=bindings_[size_t(index)];
    if(gesture_.active()) {
      if(gesture_.context().bus!=target||gesture_.control()!=control)
        throw std::runtime_error("Finish or cancel the captured mixer control first");
      return;
    }
    admit_();const auto now=context_();
    if(now.first!=document_||now.second!=revision_)throw std::runtime_error("Song changed; refresh the mixer before editing");
    const auto *saved=bus(target);if(!saved)throw std::runtime_error("Mixer bus is unavailable");
    const auto &value=saved->at(Tracker::mixerControlKey(control));
    gesture_.begin({document_,revision_,target},control,value.is_boolean()?(value.get<bool>()?1.:0.):value.get<double>());
    capturedControl_=id;rawDirty_=false;observed_=false;previewBlocked_=false;deferredCommit_=false;completedWithNewerInput_=false;
    enableEdits();
  }
  void enableEdits() {
    for(size_t i=0;i<bindings_.size();++i)for(int part:{fader,gain,pan,mute,solo,preGain,prePan,width}) {
      const int id=base+int(i)*stride+part;
      const bool available=bus(bindings_[i])!=nullptr;
      const bool capturedInput=gesture_.active()&&id==capturedControl_&&
        (part==gain||part==preGain||pending_);
      EnableWindow(controls_.at(id),capturedInput||(!pending_&&!completion_.retained()&&
        (gesture_.active()?id==capturedControl_:available)));
    }
    for(size_t i=0;i<bindings_.size();++i)
      EnableWindow(controls_.at(base+int(i)*stride+details),bus(bindings_[i])!=nullptr&&!retained());
  }
  void checkCurrent()const {
    if(completedWithNewerInput_)throw std::runtime_error("Earlier edit completed / newer input retained; Cancel reloads current saved state");
    const auto now=context_();
    if(!gesture_.current({now.first,now.second,gesture_.context().bus}))
      throw std::runtime_error("Song changed / captured gesture retained; Cancel restores the current saved value");
  }
  void reload() {
    const auto before=context_();const auto generation=gesture_.generation();
    auto data=request_("mixer.get",Json::object());
    if(context_()!=before||gesture_.generation()!=generation)throw std::runtime_error("Mixer changed during readback");
    data_=std::move(data);document_=before.first;revision_=before.second;
  }
  struct Pending {
    bool &value;explicit Pending(bool &v):value(v){if(value)throw std::runtime_error("Mixer operation pending");value=true;}
    ~Pending(){value=false;}
  };
  void finishReadback() {
    const auto returned=completion_.returned();
    if(!returned)throw std::runtime_error("Mixer result is uncertain / Review result before continuing");
    auto report=Json{{"outcome","returned"},{"method",returned->method},{"documentId",returned->document},
      {"revision",returned->revision},{"result",returned->result},{"fields",completion_.fields()}};
    reload();
    const bool unchanged=gesture_.generation()==completion_.generation()&&
      std::pair(document_,revision_)==std::pair(returned->document,returned->revision);
    deferredCommit_=false;captureLost_=false;completedWithNewerInput_=!unchanged;
    if(unchanged){gesture_.finish();rawDirty_=false;captureLost_=false;resetPresentation_=true;}
    else previewBlocked_=true;
    report_=std::move(report);completion_.finish();observed_=false;
    status_=unchanged?L"Mixer edit applied / Undo restores the previous value":
      L"Earlier mixer edit completed / newer input retained. Cancel reloads the current saved value";
  }
  void commit() {
    if(!gesture_.active()||pending_||completion_.retained())return;
    checkCurrent();admit_();
    if(rawDirty_)gesture_.update(number(capturedControl_));
    Pending guard(pending_);
    const auto request=params(gesture_.value(),false,gesture_.context());
    completion_.submit(write_,"mixer.bus.set",request,gesture_.context().document,gesture_.generation(),request);
    finishReadback();
  }
  // Reset against a fresh saved frame, including while the renderer is paused.
  // Never replay the captured baseline over a newer accepted edit.
  void restoreCurrent(bool acknowledge=false) {
    if(!gesture_.active()||pending_)return;
    if(completion_.retained()&&!acknowledge)throw std::runtime_error("Review the uncertain mixer result before cancelling");
    const auto generation=gesture_.generation();Pending guard(pending_);reload();
    if(document_==gesture_.context().document)if(const auto *saved=bus(gesture_.context().bus)) {
      const auto &v=saved->at(Tracker::mixerControlKey(gesture_.control()));
      const auto value=v.is_boolean()?(v.get<bool>()?1.:0.):v.get<double>();
      request_("mixer.bus.set",params(value,true,{document_,revision_,gesture_.context().bus}));
    }
    if(gesture_.generation()!=generation||context_()!=std::pair(document_,revision_))
      throw std::runtime_error("Song or input changed while restoring / newer input retained; Cancel again to reload");
    gesture_.finish();completion_.finish();rawDirty_=false;captureLost_=false;observed_=false;resetPresentation_=true;deferredCommit_=false;completedWithNewerInput_=false;
    status_=acknowledge?L"Current saved state accepted / the edit was not repeated":L"Gesture cancelled / current saved value restored";
  }
  void reviewResult() {
    if(pending_||!completion_.retained())return;
    Pending guard(pending_);
    request_("synchronizeView",Json::object());
    if(completion_.returned()){finishReadback();return;}
    reload();observed_=true;
    status_=L"Result uncertain / saved state inspected. Use current acknowledges it without repeating the edit";
  }
  void preview() {
    if(pending_||completion_.retained()||rawDirty_||previewBlocked_||!gesture_.needsPreview())return;
    checkCurrent();admit_();Pending guard(pending_);const auto value=gesture_.value();
    previewInFlight_=true;
    struct PreviewGuard{bool &flag;~PreviewGuard(){flag=false;}}previewGuard{previewInFlight_};
    try {request_("mixer.bus.set",params(value,true,gesture_.context()));}
    catch(...){previewBlocked_=true;deferredCommit_=false;throw;}
    gesture_.previewAccepted(value);
  }
  void createStrip(size_t index) {
    const int id=base+int(index)*stride;
    bindings_.reserve(index+1);positions_.reserve(index+1);
    try {
    label(id+name,L"");
    add(id+fader,TRACKBAR_CLASSW,L"Gain / dB",TBS_VERT|TBS_NOTICKS);
    NativeInputGate::present(controls_.at(id+fader),TBM_SETRANGE,TRUE,MAKELPARAM(0,1200));
    NativeInputGate::present(controls_.at(id+fader),TBM_SETPAGESIZE,0,30);
    edit(id+gain,L"0",32);add(id+pan,TRACKBAR_CLASSW,L"Balance",TBS_HORZ|TBS_NOTICKS);
    NativeInputGate::present(controls_.at(id+pan),TBM_SETRANGE,TRUE,MAKELPARAM(0,200));
    button(id+mute,L"Mute");button(id+solo,L"Solo");button(id+details,L"Details");
    edit(id+preGain,L"0",32);label(id+preGainLabel,L"Pre dB");
    label(id+prePanLabel,L"Pre balance");label(id+widthLabel,L"Width / %");
    for(int part:{prePan,width}) {
      add(id+part,TRACKBAR_CLASSW,part==prePan?L"Balance before effects":L"Stereo width / percent",TBS_HORZ|TBS_NOTICKS);
      NativeInputGate::present(controls_.at(id+part),TBM_SETRANGE,TRUE,MAKELPARAM(0,200));
    }
    for(int part=0;part<=lastPart;++part) {
      installReveal(controls_.at(id+part));
      if(font_)SendMessageW(controls_.at(id+part),WM_SETFONT,reinterpret_cast<WPARAM>(font_),FALSE);
    }
    bindings_.push_back({});positions_.push_back(SIZE_MAX);
    } catch(...) {
      for(int part=0;part<=lastPart;++part)if(const auto found=controls_.find(id+part);found!=controls_.end()) {
        DestroyWindow(found->second);controls_.erase(found);
      }
      throw;
    }
  }
  void bind(size_t index,const Json &value) {
    const int id=base+int(index)*stride;const auto identity=value.at("id").get<std::string>();
    // Never rebind or rewrite the HWND that owns raw text or an active gesture.
    if(gesture_.active()&&bindings_[index]==gesture_.context().bus)return;
    const auto focus=GetFocus();
    bool focused=false;for(int part=0;part<=lastPart;++part)focused|=focus==controls_.at(id+part);
    if(focused&&!resetPresentation_)return;
    bindings_[index]=identity;setting_=true;
    try {
      set(id+name,value.at("name"));set(id+gain,value.at("gainDB"));
      set(id+preGain,value.at("preGainDB"));
      NativeInputGate::present(controls_.at(id+fader),TBM_SETPOS,TRUE,LPARAM(std::lround((24-value.at("gainDB").get<double>())*10)));
      NativeInputGate::present(controls_.at(id+pan),TBM_SETPOS,TRUE,LPARAM(std::lround((value.at("pan").get<double>()+1)*100)));
      NativeInputGate::present(controls_.at(id+prePan),TBM_SETPOS,TRUE,LPARAM(std::lround((value.at("prePan").get<double>()+1)*100)));
      NativeInputGate::present(controls_.at(id+width),TBM_SETPOS,TRUE,LPARAM(std::lround(value.at("width").get<double>()*100)));
      const auto title=wide(value.at("name").get<std::string>());
      set(id+fader,title+L" gain / dB");
      set(id+pan,title+L" balance / 0 left, 100 center, 200 right");
      set(id+prePan,title+L" pre balance / 0 left, 100 center, 200 right");
      set(id+width,title+L" stereo width / percent");
      set(id+mute,value.value("mute",false)?L"Mute on":L"Mute");set(id+solo,value.value("solo",false)?L"Solo on":L"Solo");
    }catch(...){setting_=false;throw;}
    setting_=false;
  }
  void page(int direction) {
    const size_t count=std::max(size_t(1),visibleCount());
    scrollBusesTo(direction<0?(first_>count?first_-count:0):first_+count);
  }
  size_t visibleCount()const {return std::clamp(size_t(std::max(1.0f,std::floor((size().first-8)/132))),size_t(1),maximumVisibleStrips);}
  void bindViewport(size_t total) {
    const auto count=visibleCount();
    viewportTotal_=total;
    std::vector<size_t> next( maximumStrips,SIZE_MAX );
    std::vector<bool> used(maximumStrips,false),protectedSlot(maximumStrips,false);
    // Existing focused/captured HWNDs are reserved before choosing recycle slots.
    // A deleted pinned bus occupies its former place as unavailable until review
    // or focus departure; it cannot become the replacement at that index.
    for(size_t i=0;i<bindings_.size();++i)if(pinned(i)) {
      protectedSlot[i]=used[i]=true;auto at=positions_[i];
      for(size_t j=0;j<total;++j)if(data_["buses"][j].at("id")==bindings_[i]){at=j;break;}
      next[i]=at;
      if(at!=SIZE_MAX)viewportTotal_=std::max(viewportTotal_,at+1);
      if(at!=SIZE_MAX&&(lastVisibleCount_!=count||at!=positions_[i])) {
        if(at<first_)first_=at;
        else if(at>=first_+count)first_=at+1-count;
      }
    }
    first_=std::min(first_,viewportTotal_>count?viewportTotal_-count:0);
    const auto begin=first_?first_-1:0,end=std::min(total,first_+count+1);
    // Reserve surviving neighbors before recycling anything. A leftward scroll
    // must not steal the next desired bus's HWND for its newly exposed neighbor.
    for(size_t at=begin;at<end;++at) {
      bool reserved=false;
      for(size_t i=0;i<bindings_.size();++i)if(used[i]&&next[i]==at){reserved=true;break;}
      if(reserved)continue;
      for(size_t i=0;i<bindings_.size();++i)if(!used[i]&&bindings_[i]==data_["buses"][at].at("id").get<std::string>()) {
        used[i]=true;next[i]=at;break;
      }
    }
    for(size_t at=begin;at<end;++at) {
      const auto &value=data_["buses"][at];const auto identity=value.at("id").get<std::string>();
      bool reserved=false;
      for(size_t i=0;i<bindings_.size();++i)if(used[i]&&next[i]==at){if(!protectedSlot[i])bind(i,value);reserved=true;break;}
      if(reserved)continue;
      size_t index=bindings_.size();
      for(size_t i=0;i<bindings_.size();++i)if(!used[i]&&bindings_[i]==identity){index=i;break;}
      if(index==bindings_.size())for(size_t i=0;i<bindings_.size();++i)if(!used[i]&&!protectedSlot[i]){index=i;break;}
      if(index==bindings_.size()) {
        if(index>=maximumStrips)throw std::runtime_error("Mixer visible control capacity exceeded");
        createStrip(index);
      }
      used[index]=true;next[index]=at;bind(index,value);
    }
    for(size_t i=0;i<bindings_.size();++i) {
      positions_[i]=next[i];
      if(protectedSlot[i]) {
        if(const auto *saved=bus(bindings_[i]))bind(i,*saved);
        else set(base+int(i)*stride+name,L"Bus unavailable");
      }
    }
    lastVisibleCount_=count;
    SCROLLINFO scroll{sizeof(scroll),SIF_RANGE|SIF_PAGE|SIF_POS|SIF_DISABLENOSCROLL};
    scroll.nMin=0;scroll.nMax=int(viewportTotal_?viewportTotal_-1:0);scroll.nPage=UINT(count);scroll.nPos=int(first_);
    SetScrollInfo(window_,SB_HORZ,&scroll,TRUE);
  }
  void layout()override {
    if(layingOut_)return;
    struct LayoutGuard {bool &value;LayoutGuard(bool &v):value(v){value=true;}~LayoutGuard(){value=false;}}guard(layingOut_);
    const auto [w,clientHeight]=size();const size_t total=data_.contains("buses")?data_["buses"].size():0;
    contentHeight_=std::max(329,int(clientHeight));const float h=float(contentHeight_);
    scrollOffset_=std::clamp(scrollOffset_,0,std::max(0,contentHeight_-int(clientHeight)));
    SCROLLINFO scroll{sizeof(scroll),SIF_RANGE|SIF_PAGE|SIF_POS|SIF_DISABLENOSCROLL};
    scroll.nMin=0;scroll.nMax=contentHeight_-1;scroll.nPage=UINT(std::max(1.0f,clientHeight));scroll.nPos=scrollOffset_;
    SetScrollInfo(window_,SB_VERT,&scroll,TRUE);
    const auto position=[&](int id,float x,float y,float width,float height,bool show=true){place(id,x,y-float(scrollOffset_),width,height,show);};
    bindViewport(total);
    // Reconciliation replaces navigation while it is disabled anyway. Both
    // actions remain reachable in a narrow dock without another toolbar row.
    position(previous,8,4,72,24,!completion_.retained());position(next,84,4,72,24,!completion_.retained());
    position(cancel,164,4,76,24,!completion_.retained());
    position(review,8,4,112,24,completion_.retained());position(accept,126,4,110,24,completion_.retained());
    stripTop_=34;sliderTop_=57;
    EnableWindow(controls_.at(previous),first_>0);EnableWindow(controls_.at(next),first_+visibleCount()<viewportTotal_);
    EnableWindow(controls_.at(cancel),gesture_.active()&&!pending_&&!completion_.retained());
    EnableWindow(controls_.at(review),!pending_);EnableWindow(controls_.at(accept),observed_&&!pending_);
    // Keep the two horizontal controls clear of the button row at short dock
    // heights. The previous h-172 rule overlapped Balance and Mute at 226 DIPs.
    const float stripWidth=132;
    sliderHeight_=std::max(60.0f,h-sliderTop_-212);
    for(size_t i=0;i<bindings_.size();++i) {
      const int id=base+int(i)*stride;const bool show=positions_[i]!=SIZE_MAX;
      const float x=show?8+(float(positions_[i])-float(first_))*stripWidth:0;
      position(id+name,x,stripTop_,124,19,show);position(id+fader,x+6,sliderTop_,32,sliderHeight_,show);
      position(id+gain,x+52,sliderTop_,67,24,show);position(id+pan,x+52,sliderTop_+35,67,25,show);
      position(id+mute,x,sliderTop_+sliderHeight_+5,60,24,show);position(id+solo,x+64,sliderTop_+sliderHeight_+5,60,24,show);
      position(id+details,x,sliderTop_+sliderHeight_+33,124,24,show);
      const float controlsTop=sliderTop_+sliderHeight_+64;
      position(id+preGainLabel,x,controlsTop+4,48,18,show);position(id+preGain,x+52,controlsTop,67,24,show);
      position(id+prePanLabel,x,controlsTop+30,124,18,show);position(id+prePan,x,controlsTop+48,124,24,show);
      position(id+widthLabel,x,controlsTop+78,124,18,show);position(id+width,x,controlsTop+96,124,24,show);
    }
    // Win32 dialog traversal follows sibling Z order, not recycled slot IDs.
    // Order the native controls by the current musical bus order without
    // recreating HWNDs or disturbing the focused edit's text/selection.
    std::vector<size_t> ordered;
    for(size_t i=0;i<bindings_.size();++i)if(positions_[i]!=SIZE_MAX)ordered.push_back(i);
    std::stable_sort(ordered.begin(),ordered.end(),[&](size_t a,size_t b){return positions_[a]<positions_[b];});
    for(int id:{previous,next,cancel,review,accept})
      SetWindowPos(controls_.at(id),HWND_BOTTOM,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
    for(const auto i:ordered)for(int part=0;part<=lastPart;++part)
      SetWindowPos(controls_.at(base+int(i)*stride+part),HWND_BOTTOM,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
    enableEdits();
    resetPresentation_=false;
    lastLayoutFocus_=GetFocus();
  }
  void paint(RenderSurface &s)override {
    const auto [w,h]=size();s.fill(0,0,w,h,0x18222d);
    s.uiText(status_.empty()?L"Drag gain or balance / Enter commits a typed gain / Escape cancels":status_,8,float(contentHeight_-scrollOffset_-24),w-16,0xabbacb);
    if(data_.contains("buses")&&data_["buses"].empty())s.uiText(L"Enable the mixer to show channel and Master strips",12,54,w-24,0xabbacb);
    const float height=sliderHeight_;
    const float top=sliderTop_-float(scrollOffset_);
    for(size_t i=0;i<bindings_.size();++i) {
      if(positions_[i]==SIZE_MAX)continue;
      const float x=8+(float(positions_[i])-float(first_))*132;
      if(x>=w||x+124<=0)continue;
      if(const auto *saved=bus(bindings_[i]))if(const auto color=saved->value("color",0u))
        s.fill(x,stripTop_-float(scrollOffset_)+20,124,2,color);
      const auto found=meters_.find(bindings_[i]);
      for(int channel=0;channel<2;++channel) {
        const float value=found==meters_.end()?0:(channel?found->second.right:found->second.left);
        const float amount=std::clamp(value,0.0f,1.0f)*height;
        s.fill(x+40+channel*5,top,3,height,0x303b49);
        s.fill(x+40+channel*5,top+height-amount,3,amount,value>1?0xe87c77:0x6edac5);
      }
      if(found==meters_.end())s.uiText(L"—",x+38,top+height/2,18,0xabbacb);
      s.uiText(L"dB",x+100,top+24,24,0xabbacb);
    }
  }
  void error(const std::exception &e)override {
    NativeToolWindow::error(e);
    // A failed pumped operation may have laid out controls while pending.
    // Restore Review/Cancel availability after its pending guard unwinds.
    try{layout();}catch(...){}
  }
  void action(int id,unsigned notification)override {
    if(setting_)return;
    // EDIT mutates before EN_CHANGE. A pending completion must observe newer
    // text already accepted by the captured native control.
    if(gesture_.active()&&id==capturedControl_&&notification==EN_CHANGE&&
       ((id-base)%stride==gain||(id-base)%stride==preGain)) {
      rawDirty_=true;gesture_.rawChanged();return;
    }
    if(pending_)return;
    if(id==previous||id==next){page(id==previous?-1:1);return;}
    if(id==cancel){restoreCurrent();return;}if(id==review){reviewResult();return;}
    if(id==accept){if(!observed_)throw std::runtime_error("Inspect the result first");restoreCurrent(true);return;}
    const int index=slot(id);if(index<0)return;const int part=(id-base)%stride;
    if(part==details){details_(bindings_[size_t(index)]);return;}
    if((part==gain||part==preGain)&&notification==EN_CHANGE){begin(id,part==gain?Tracker::MixerControl::Gain:Tracker::MixerControl::PreGain);rawDirty_=true;gesture_.rawChanged();return;}
    if(part==mute||part==solo){const bool continuing=gesture_.active();begin(id,part==mute?Tracker::MixerControl::Mute:Tracker::MixerControl::Solo);if(!continuing)gesture_.update(gesture_.value()==0?1:0);commit();}
  }
  bool controlScroll(UINT message,WPARAM event,HWND control)override {
    if(!control&&message==WM_HSCROLL) {
      SCROLLINFO info{sizeof(info),SIF_TRACKPOS};GetScrollInfo(window_,SB_HORZ,&info);
      int target=int(first_);const int count=int(visibleCount());
      switch(LOWORD(event)) {
        case SB_LEFT:target=0;break;case SB_RIGHT:target=int(viewportTotal_);break;
        case SB_LINELEFT:--target;break;case SB_LINERIGHT:++target;break;
        case SB_PAGELEFT:target-=count;break;case SB_PAGERIGHT:target+=count;break;
        case SB_THUMBTRACK:case SB_THUMBPOSITION:target=info.nTrackPos;break;default:return true;
      }
      scrollBusesTo(size_t(std::max(0,target)));return true;
    }
    if(!control&&message==WM_VSCROLL) {
      SCROLLINFO info{sizeof(info),SIF_TRACKPOS};GetScrollInfo(window_,SB_VERT,&info);
      int target=scrollOffset_;const int pageSize=std::max(24,int(size().second)-24);
      switch(LOWORD(event)) {
        case SB_TOP:target=0;break;case SB_BOTTOM:target=contentHeight_;break;
        case SB_LINEUP:target-=24;break;case SB_LINEDOWN:target+=24;break;
        case SB_PAGEUP:target-=pageSize;break;case SB_PAGEDOWN:target+=pageSize;break;
        case SB_THUMBTRACK:case SB_THUMBPOSITION:target=info.nTrackPos;break;
        default:return true;
      }
      scrollTo(target);return true;
    }
    const int id=GetDlgCtrlID(control),index=slot(id);if(index<0)return false;
    const int part=(id-base)%stride;if(part!=fader&&part!=pan&&part!=prePan&&part!=width)return false;
    if(setting_||(completion_.retained()&&!pending_))return true;
    if(pending_&&(!gesture_.active()||id!=capturedControl_))return true;
    const auto position=double(SendMessageW(control,TBM_GETPOS,0,0));
    if(!gesture_.active()) {
      if(LOWORD(event)==TB_ENDTRACK)return true;
      const auto *saved=bus(bindings_[size_t(index)]);if(!saved)return true;
      const auto rounded=part==fader?std::lround((24-saved->at("gainDB").get<double>())*10):
        part==width?std::lround(saved->at("width").get<double>()*100):std::lround((saved->at(part==pan?"pan":"prePan").get<double>()+1)*100);
      if(position==double(rounded))return true; // Clicking a rounded thumb is not an edit.
    }
    if(!pending_)begin(id,part==fader?Tracker::MixerControl::Gain:part==pan?Tracker::MixerControl::Pan:
      part==prePan?Tracker::MixerControl::PrePan:Tracker::MixerControl::Width);
    gesture_.update(part==fader?24-position/10:part==width?position/100:position/100-1);
    previewBlocked_=false;
    if(part==fader){setting_=true;try{set(base+index*stride+gain,Json(gesture_.value()));}catch(...){setting_=false;throw;}setting_=false;}
    if(LOWORD(event)==TB_ENDTRACK){
      captureLost_=false;
      if(pending_){if(previewInFlight_)deferredCommit_=true;}
      else {commit();layout();}
    }
    requestPaint();return true;
  }
  void controlCaptureChanged(HWND control)override {
    if(gesture_.active()&&!completedWithNewerInput_&&GetDlgCtrlID(control)==capturedControl_){captureLost_=true;SetTimer(window_,3,1,nullptr);}
  }
  void timer(UINT_PTR id)override {
    if(id!=3)return;
    KillTimer(window_,3);
    if(pending_){SetTimer(window_,3,50,nullptr);return;}
    if(captureLost_&&gesture_.active()&&!completion_.retained()){captureLost_=false;restoreCurrent();layout();requestPaint();}
  }
  bool key(WPARAM key,bool control,bool shift)override {
    if(key==VK_TAB&&GetFocus()==window_) {
      if(auto target=GetNextDlgTabItem(window_,nullptr,shift))SetFocus(target);
      return true;
    }
    if(control&&(key==VK_PRIOR||key==VK_NEXT)){page(key==VK_PRIOR?-1:1);return true;}
    if(key==VK_ESCAPE&&gesture_.active()){restoreCurrent();layout();return true;}
    if(key==VK_RETURN&&gesture_.active()){commit();layout();return true;}
    return false;
  }
  bool keyUp(WPARAM key)override {
    if(gesture_.active()&&!rawDirty_&&(key==VK_LEFT||key==VK_RIGHT||key==VK_UP||key==VK_DOWN||key==VK_HOME||key==VK_END||key==VK_PRIOR||key==VK_NEXT)) {
      if(pending_){if(previewInFlight_)deferredCommit_=true;return true;}
      commit();layout();return true;
    }
    return false;
  }
  bool wheel(UINT message,float,float,WPARAM value)override {
    const bool vertical=message==WM_MOUSEWHEEL&&!(GET_KEYSTATE_WPARAM(value)&MK_SHIFT)&&contentHeight_>size().second;
    auto &remainder=vertical?wheelVertical_:wheelHorizontal_;
    remainder+=GET_WHEEL_DELTA_WPARAM(value)*(message==WM_MOUSEWHEEL&&!vertical?-1:1);
    const auto delta=remainder/WHEEL_DELTA;remainder%=WHEEL_DELTA;
    if(!delta)return true;
    if(vertical)scrollTo(scrollOffset_-delta*48);
    else scrollBusesTo(size_t(std::max(0,int(first_)+delta)));
    return true;
  }
  void reviewDocumentDraft()override {reveal_();show();SetFocus(window_);}
public:
  MixerStripsWindow(HWND owner,Read read,Context context,NativeWriteCompletion::Write write,
      std::function<void()> admit,std::function<void(const std::string &)> details,std::function<void()> reveal)
    :NativeToolWindow(owner),request_(std::move(read)),context_(std::move(context)),write_(std::move(write)),
     admit_(std::move(admit)),reveal_(std::move(reveal)),details_(std::move(details)) {
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_BAR_CLASSES};
    if(!InitCommonControlsEx(&common))throw std::runtime_error("Cannot initialize native mixer sliders");
    create(L"ScreamSeqMixerStrips",L"Mixer strips",900,330);
    minimumClientWidth_=280;minimumClientHeight_=205;
    SetWindowLongPtrW(window_,GWL_STYLE,GetWindowLongPtrW(window_,GWL_STYLE)|WS_VSCROLL|WS_HSCROLL);
    SetWindowPos(window_,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);
    button(previous,L"Previous");button(next,L"Next");button(cancel,L"Cancel");button(review,L"Review result");button(accept,L"Use current");
    for(const auto id:{previous,next,cancel,review,accept})installReveal(controls_.at(id));
    finish();
  }
  std::optional<Tracker::DocumentDraft> documentDraft()const override {
    const auto &c=gesture_.context();return describeDraft(c.document,c.revision,c.bus,gesture_.generation(),
      gesture_.active(),pending_,completion_.retained());
  }
  bool hasGesture()const noexcept{return retained();}
  Json snapshot()const {
    Json strips=Json::array();
    for(size_t i=0;i<bindings_.size();++i)if(positions_[i]!=SIZE_MAX)
      strips.push_back({{"bus",bindings_[i]},{"controlBase",base+int(i)*stride},{"position",positions_[i]},
        {"pinned",pinned(i)},{"inViewport",positions_[i]>=first_&&positions_[i]<first_+visibleCount()}});
    return {{"visible",visible()},{"firstBus",first_},{"visibleCapacity",visibleCount()},{"allocatedStrips",bindings_.size()},
      {"strips",strips},{"gesture",gesture_.active()},{"generation",gesture_.generation()},{"pending",pending_},
      {"completion",completion_.snapshot()},{"report",report_}};
  }
  void hide()override {
    // Reset after the current native notification stack unwinds. Do not call
    // the worker from workspace layout or wait for a visible-only meter timer.
    if(gesture_.active()&&!rawDirty_&&!completion_.retained()&&!completedWithNewerInput_) {
      captureLost_=true;SetTimer(window_,3,1,nullptr);
      if(owns(GetCapture()))ReleaseCapture();
    }
    NativeToolWindow::hide();
  }
  void meters(const Tracker::MixerMeterReading &reading) {
    meters_.clear();if(reading.fresh)for(const auto &bus:reading.buses)
      meters_.emplace("n"+std::to_string(bus.id),bus.level);
    requestPaint();
  }
  void update() {
    if(retired()||!visible()||pending_)return;
    try {
      bool relayout=GetFocus()!=lastLayoutFocus_;
      if(deferredCommit_&&gesture_.active()&&!completion_.retained()){deferredCommit_=false;commit();relayout=true;}
      else if(captureLost_&&gesture_.active()&&!completion_.retained()){captureLost_=false;restoreCurrent();relayout=true;}
      else if(gesture_.active())preview();
      else if(!completion_.retained()&&context_()!=std::pair(document_,revision_)){Pending guard(pending_);reload();relayout=true;}
      // A release/key-up can arrive while preview() pumps the worker wait.
      // Submit its newest value only after the preview's pending guard unwinds.
      if(deferredCommit_&&gesture_.active()&&!completion_.retained()){deferredCommit_=false;commit();relayout=true;}
      if(relayout||resetPresentation_)layout();requestPaint();
    }catch(const std::exception &e){error(e);}
  }
};
}
