#pragma once
#include "NativeToolWindow.hpp"
#include "NativeWriteCompletion.hpp"
#include "../../editor/MixerGesture.hpp"

namespace ScreamSeq {
// Native strips share the existing mixer API and history; Details remains a
// separate retained owner. A small page of stock controls bounds HWND count.
class MixerStripsWindow final:public NativeToolWindow {
public:
  using Json=Api::Json;
  using Read=std::function<Json(const std::string &,const Json &)>;
  using Context=std::function<std::pair<std::string,std::string>()>;
private:
  enum {previous=10,next=11,cancel=12,review=13,accept=14,base=100,stride=8};
  enum {name=0,fader=1,gain=2,pan=3,mute=4,solo=5,details=6};
  Read request_;Context context_;NativeWriteCompletion::Write write_;
  std::function<void()> admit_,reveal_;std::function<void(const std::string &)> details_;
  NativeWriteCompletion completion_;Tracker::MixerGesture gesture_;
  Json data_=Json::object();std::string document_,revision_;
  std::vector<std::string> bindings_;size_t first_=0;
  std::map<std::string,Tracker::MixerMeter> meters_;
  bool setting_=false,pending_=false,captureLost_=false,rawDirty_=false,observed_=false,previewBlocked_=false;
  int capturedControl_=0;
  static constexpr size_t maximumStrips=16;
  bool retained()const noexcept{return gesture_.active()||pending_||completion_.retained();}
  const Json *bus(const std::string &id)const {
    if(!data_.contains("buses"))return nullptr;
    for(const auto &value:data_.at("buses"))if(value.at("id")==id)return &value;
    return nullptr;
  }
  int slot(int id)const {return id>=base&&size_t((id-base)/stride)<bindings_.size()?(id-base)/stride:-1;}
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
    capturedControl_=id;rawDirty_=false;observed_=false;previewBlocked_=false;
    enableEdits();
  }
  void enableEdits() {
    for(size_t i=0;i<bindings_.size();++i)for(int part=fader;part<=solo;++part) {
      const int id=base+int(i)*stride+part;
      EnableWindow(controls_.at(id),!pending_&&!completion_.retained()&&(!gesture_.active()||id==capturedControl_));
    }
  }
  void checkCurrent()const {
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
    reload();gesture_.finish();rawDirty_=false;captureLost_=false;completion_.finish();observed_=false;
    status_=L"Mixer edit applied / Undo restores the previous value";
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
    Pending guard(pending_);reload();
    if(document_==gesture_.context().document)if(const auto *saved=bus(gesture_.context().bus)) {
      const auto &v=saved->at(Tracker::mixerControlKey(gesture_.control()));
      const auto value=v.is_boolean()?(v.get<bool>()?1.:0.):v.get<double>();
      request_("mixer.bus.set",params(value,true,{document_,revision_,gesture_.context().bus}));
    }
    gesture_.finish();completion_.finish();rawDirty_=false;captureLost_=false;observed_=false;
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
    try {request_("mixer.bus.set",params(value,true,gesture_.context()));}
    catch(...){previewBlocked_=true;throw;}
    gesture_.previewAccepted(value);
  }
  void createStrip(size_t index) {
    const int id=base+int(index)*stride;
    try {
    label(id+name,L"");
    add(id+fader,TRACKBAR_CLASSW,L"Gain / dB",TBS_VERT|TBS_NOTICKS);
    NativeInputGate::present(controls_.at(id+fader),TBM_SETRANGE,TRUE,MAKELPARAM(0,1200));
    NativeInputGate::present(controls_.at(id+fader),TBM_SETPAGESIZE,0,30);
    edit(id+gain,L"0",32);add(id+pan,TRACKBAR_CLASSW,L"Balance",TBS_HORZ|TBS_NOTICKS);
    NativeInputGate::present(controls_.at(id+pan),TBM_SETRANGE,TRUE,MAKELPARAM(0,200));
    button(id+mute,L"Mute");button(id+solo,L"Solo");button(id+details,L"Details");
    for(int part=0;part<=details;++part)if(font_)SendMessageW(controls_.at(id+part),WM_SETFONT,reinterpret_cast<WPARAM>(font_),FALSE);
    bindings_.push_back({});
    } catch(...) {
      for(int part=0;part<=details;++part)if(const auto found=controls_.find(id+part);found!=controls_.end()) {
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
    bool focused=false;for(int part=0;part<=details;++part)focused|=focus==controls_.at(id+part);
    if(focused&&bindings_[index]!=identity)return;
    bindings_[index]=identity;setting_=true;
    try {
      set(id+name,value.at("name"));set(id+gain,value.at("gainDB"));
      NativeInputGate::present(controls_.at(id+fader),TBM_SETPOS,TRUE,LPARAM(std::lround((24-value.at("gainDB").get<double>())*10)));
      NativeInputGate::present(controls_.at(id+pan),TBM_SETPOS,TRUE,LPARAM(std::lround((value.at("pan").get<double>()+1)*100)));
      set(id+mute,value.value("mute",false)?L"Mute on":L"Mute");set(id+solo,value.value("solo",false)?L"Solo on":L"Solo");
    }catch(...){setting_=false;throw;}
    setting_=false;
  }
  void page(int direction) {
    if(retained())throw std::runtime_error("Finish or cancel the captured gesture before changing the mixer page");
    SetFocus(window_);const size_t count=std::max(size_t(1),visibleCount());
    if(direction<0)first_=first_>count?first_-count:0;
    else if(data_.contains("buses")&&first_+count<data_["buses"].size())first_+=count;
    layout();requestPaint();
  }
  size_t visibleCount()const {return std::clamp(size_t(std::max(1.0f,std::floor(size().first/132))),size_t(1),maximumStrips);}
  void layout()override {
    const auto [w,h]=size();const size_t total=data_.contains("buses")?data_["buses"].size():0;
    if(!retained()&&first_>=total)first_=0;
    // Freeze bindings during capture/raw editing; resizing can clip the page but
    // cannot silently put a different bus under the mouse or keyboard focus.
    const size_t count=retained()?bindings_.size():std::min(visibleCount(),total-first_);
    while(bindings_.size()<count)createStrip(bindings_.size());
    place(previous,8,4,72,24);place(next,84,4,72,24);place(cancel,164,4,76,24);
    place(review,246,4,112,24,completion_.retained());place(accept,364,4,110,24,completion_.retained());
    EnableWindow(controls_.at(previous),first_>0&&!retained());EnableWindow(controls_.at(next),first_+count<total&&!retained());
    EnableWindow(controls_.at(cancel),gesture_.active()&&!pending_&&!completion_.retained());
    EnableWindow(controls_.at(review),!pending_);EnableWindow(controls_.at(accept),observed_&&!pending_);
    const float stripWidth=132,sliderHeight=std::max(32.0f,h-172);
    for(size_t i=0;i<bindings_.size();++i) {
      const int id=base+int(i)*stride;const bool show=i<count&&first_+i<total;
      if(show)bind(i,data_["buses"][first_+i]);
      const float x=8+float(i)*stripWidth;
      place(id+name,x,34,124,19,show);place(id+fader,x+6,57,32,sliderHeight,show);
      place(id+gain,x+52,57,67,24,show);place(id+pan,x+52,92,67,25,show);
      place(id+mute,x,62+sliderHeight,60,24,show);place(id+solo,x+64,62+sliderHeight,60,24,show);
      place(id+details,x,90+sliderHeight,124,24,show);
    }
    enableEdits();
  }
  void paint(RenderSurface &s)override {
    const auto [w,h]=size();s.fill(0,0,w,h,0x18222d);
    s.uiText(status_.empty()?L"Drag gain or balance / Enter commits a typed gain / Escape cancels":status_,8,h-24,w-16,0xabbacb);
    if(data_.contains("buses")&&data_["buses"].empty())s.uiText(L"Enable the mixer to show channel and Master strips",12,54,w-24,0xabbacb);
    const float height=std::max(32.0f,h-172);
    for(size_t i=0;i<bindings_.size();++i) {
      const auto found=meters_.find(bindings_[i]);const float x=8+float(i)*132;
      for(int channel=0;channel<2;++channel) {
        const float value=found==meters_.end()?0:(channel?found->second.right:found->second.left);
        const float amount=std::clamp(value,0.0f,1.0f)*height;
        s.fill(x+40+channel*5,57,3,height,0x303b49);
        s.fill(x+40+channel*5,57+height-amount,3,amount,value>1?0xe87c77:0x6edac5);
      }
      s.uiText(L"dB",x+100,81,24,0xabbacb);
    }
  }
  void action(int id,unsigned notification)override {
    if(setting_||pending_)return;
    if(id==previous||id==next){page(id==previous?-1:1);return;}
    if(id==cancel){restoreCurrent();return;}if(id==review){reviewResult();return;}
    if(id==accept){if(!observed_)throw std::runtime_error("Inspect the result first");restoreCurrent(true);return;}
    const int index=slot(id);if(index<0)return;const int part=(id-base)%stride;
    if(part==details){details_(bindings_[size_t(index)]);return;}
    if(part==gain&&notification==EN_CHANGE){begin(id,Tracker::MixerControl::Gain);rawDirty_=true;gesture_.rawChanged();return;}
    if(part==mute||part==solo){const bool continuing=gesture_.active();begin(id,part==mute?Tracker::MixerControl::Mute:Tracker::MixerControl::Solo);if(!continuing)gesture_.update(gesture_.value()==0?1:0);commit();}
  }
  bool controlScroll(UINT,WPARAM event,HWND control)override {
    const int id=GetDlgCtrlID(control),index=slot(id);if(index<0)return false;
    const int part=(id-base)%stride;if(part!=fader&&part!=pan)return false;
    if(setting_||pending_||completion_.retained())return true;
    const auto position=double(SendMessageW(control,TBM_GETPOS,0,0));
    if(!gesture_.active()) {
      if(LOWORD(event)==TB_ENDTRACK)return true;
      const auto *saved=bus(bindings_[size_t(index)]);if(!saved)return true;
      const auto rounded=part==fader?std::lround((24-saved->at("gainDB").get<double>())*10):std::lround((saved->at("pan").get<double>()+1)*100);
      if(position==double(rounded))return true; // Clicking a rounded thumb is not an edit.
    }
    begin(id,part==fader?Tracker::MixerControl::Gain:Tracker::MixerControl::Pan);
    gesture_.update(part==fader?24-position/10:position/100-1);
    previewBlocked_=false;
    if(part==fader){setting_=true;try{set(base+index*stride+gain,Json(gesture_.value()));}catch(...){setting_=false;throw;}setting_=false;}
    if(LOWORD(event)==TB_ENDTRACK){captureLost_=false;commit();layout();}
    requestPaint();return true;
  }
  void controlCaptureChanged(HWND control)override {
    if(gesture_.active()&&GetDlgCtrlID(control)==capturedControl_){captureLost_=true;SetTimer(window_,3,1,nullptr);}
  }
  void timer(UINT_PTR id)override {
    if(id!=3)return;
    KillTimer(window_,3);
    if(pending_){SetTimer(window_,3,50,nullptr);return;}
    if(captureLost_&&gesture_.active()&&!completion_.retained()){captureLost_=false;restoreCurrent();layout();requestPaint();}
  }
  bool key(WPARAM key,bool,bool)override {
    if(key==VK_ESCAPE&&gesture_.active()){restoreCurrent();layout();return true;}
    if(key==VK_RETURN&&gesture_.active()){commit();layout();return true;}
    return false;
  }
  bool keyUp(WPARAM key)override {
    if(gesture_.active()&&!rawDirty_&&(key==VK_LEFT||key==VK_RIGHT||key==VK_UP||key==VK_DOWN||key==VK_HOME||key==VK_END||key==VK_PRIOR||key==VK_NEXT)) {
      commit();layout();return true;
    }
    return false;
  }
  bool wheel(UINT,float,float,WPARAM value)override {page(GET_WHEEL_DELTA_WPARAM(value)>0?-1:1);return true;}
  void reviewDocumentDraft()override {reveal_();show();SetFocus(window_);}
public:
  MixerStripsWindow(HWND owner,Read read,Context context,NativeWriteCompletion::Write write,
      std::function<void()> admit,std::function<void(const std::string &)> details,std::function<void()> reveal)
    :NativeToolWindow(owner),request_(std::move(read)),context_(std::move(context)),write_(std::move(write)),
     admit_(std::move(admit)),reveal_(std::move(reveal)),details_(std::move(details)) {
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_BAR_CLASSES};
    if(!InitCommonControlsEx(&common))throw std::runtime_error("Cannot initialize native mixer sliders");
    create(L"ScreamSeqMixerStrips",L"Mixer strips",900,330);
    button(previous,L"Previous");button(next,L"Next");button(cancel,L"Cancel");button(review,L"Review result");button(accept,L"Use current");
    finish();
  }
  std::optional<Tracker::DocumentDraft> documentDraft()const override {
    const auto &c=gesture_.context();return describeDraft(c.document,c.revision,c.bus,gesture_.generation(),
      gesture_.active(),pending_,completion_.retained());
  }
  bool hasGesture()const noexcept{return retained();}
  void hide()override {
    // Reset after the current native notification stack unwinds. Do not call
    // the worker from workspace layout or wait for a visible-only meter timer.
    if(gesture_.active()&&!rawDirty_&&!completion_.retained()) {
      captureLost_=true;SetTimer(window_,3,1,nullptr);
      if(owns(GetCapture()))ReleaseCapture();
    }
    NativeToolWindow::hide();
  }
  void meters(const Json &buses,const std::vector<Tracker::MixerMeter> &values) {
    meters_.clear();for(size_t i=0;i<std::min(buses.size(),values.size());++i)
      meters_.emplace(buses[i].at("id").get<std::string>(),values[i]);
    requestPaint();
  }
  void update() {
    if(retired()||!visible()||pending_)return;
    try {
      if(captureLost_&&gesture_.active()&&!completion_.retained()){captureLost_=false;restoreCurrent();}
      else if(gesture_.active())preview();
      else if(!completion_.retained()&&context_()!=std::pair(document_,revision_)){Pending guard(pending_);reload();}
      layout();requestPaint();
    }catch(const std::exception &e){error(e);}
  }
};
}
