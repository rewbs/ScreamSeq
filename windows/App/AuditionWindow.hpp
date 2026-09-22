#pragma once
#include "NativeToolWindow.hpp"
#include "editor/TrackerDocument.hpp"

namespace ScreamSeq {
class AuditionWindow final : public NativeToolWindow {
  using Json=Api::Json;
public:
  struct Context {std::string document,revision;unsigned sample=0,instrument=0;uint64_t epoch=0;Json samples,instruments;};
private:
  using Request=std::function<Json(const std::string &,const Json &)>;
  enum:int {kind=5101,asset,note,velocity,noteOn,noteOff,panic,fromCursor,reload,close,octaveDown,octaveUp,heading=5150,targetLabel,statusLabel,helpLabel,noteLabel,velocityLabel};
  struct Held {unsigned note;uint64_t epoch;bool pending;};
  Request request_;std::function<Context()> context_;Context captured_;
  std::map<WPARAM,Held> held_;std::string identity_;unsigned index_=0,firstNote_=49;
  bool sample_=true,setting_=false,preparing_=false;WorkspaceRect keyboard_;
  std::vector<WorkspaceRect> white_,black_;std::vector<unsigned> whiteNotes_,blackNotes_;
  static constexpr WPARAM buttonKey=0x1000,mouseKey=0x1001;
  void require(bool ok,const char *why)const{if(!ok)throw std::runtime_error(why);}
  const Json &catalog(const Context &c)const{return sample_?c.samples:c.instruments;}
  bool targetExists(const Context &c)const{return c.document==captured_.document&&std::any_of(catalog(c).begin(),catalog(c).end(),[&](const auto &v){return v.at("id")==identity_&&v.at("index")==index_;});}
  bool current()const{const auto c=context_();return targetExists(c)&&c.revision==captured_.revision;}
  void status(std::wstring text){status_=std::move(text);set(statusLabel,status_);requestPaint();}
  void error(const std::exception &e)override{status(wide(e.what()));}
  unsigned integer(int id,unsigned low,unsigned high)const{const auto v=number(id);require(v>=low&&v<=high&&v==std::floor(v),"Enter a whole note from 1 to 120 and velocity from 1 to 127");return unsigned(v);}
  Json parameters(unsigned pitch,bool on,const Context &c)const{return {{"expectedRevision",c.revision},{"note",pitch},{"velocity",on?integer(velocity,1,127):100},{"on",on},{sample_?"sample":"instrument",index_}};}
  void release(WPARAM key){
    const auto it=held_.find(key);if(it==held_.end())return;const auto held=it->second;held_.erase(it);const auto now=context_();
    const bool other=std::any_of(held_.begin(),held_.end(),[&](const auto &v){return v.second.note==held.note&&(v.second.pending||v.second.epoch==now.epoch);});
    if(!other&&targetExists(now)&&(held.pending||held.epoch==now.epoch))request_("transport.note",parameters(held.note,false,now));requestPaint();
  }
  void releaseAll()noexcept{while(!held_.empty())try{release(held_.begin()->first);}catch(...){/* The entry was retired before a possibly failing delivery. */}if(GetCapture()==window_)ReleaseCapture();requestPaint();}
  void begin(WPARAM key,unsigned pitch){
    if(held_.contains(key))return;require(current(),"Song changed / Reload the captured audition target");require(pitch>=1&&pitch<=120,"Note is outside 1 to 120");const auto p=parameters(pitch,true,captured_);
    const auto now=context_();
    const auto other=std::find_if(held_.begin(),held_.end(),[&](const auto &v){return v.second.note==pitch&&(v.second.pending||v.second.epoch==now.epoch);});
    if(other!=held_.end()){held_.emplace(key,other->second);requestPaint();return;}
    held_.emplace(key,Held{pitch,now.epoch,true});const bool wasPreparing=preparing_;preparing_=true;
    try{const auto result=request_("transport.note",p);preparing_=wasPreparing;const auto after=context_();require(targetExists(after),"Audition target changed while preparing");captured_.revision=after.revision;captured_.epoch=after.epoch;
      for(auto &[k,value]:held_)if(value.note==pitch&&value.pending){value.pending=false;value.epoch=result.value("epoch",after.epoch);}status(L"Audition uses saved sound settings / release the key to release its voice");
    }catch(...){preparing_=wasPreparing;std::erase_if(held_,[&](const auto &v){return v.second.note==pitch&&v.second.pending;});throw;}requestPaint();
  }
  void choices(){setting_=true;SendMessageW(controls_.at(kind),CB_SETCURSEL,sample_?0:1,0);SendMessageW(controls_.at(asset),CB_RESETCONTENT,0,0);const auto &values=catalog(captured_);for(size_t i=0;i<values.size();++i){const auto &v=values[i];const auto text=std::to_wstring(v.at("index").get<unsigned>())+L" · "+wide(v.at("name").get<std::string>());SendMessageW(controls_.at(asset),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));if(v.at("id")==identity_)SendMessageW(controls_.at(asset),CB_SETCURSEL,i,0);}setting_=false;}
  void load(bool follow,std::string requested={}){
    require(!preparing_,"Audition preparation is still running");const auto now=context_();require(follow||now.document==captured_.document,"Document replaced / use From cursor");const auto &values=catalog(now);
    auto it=std::find_if(values.begin(),values.end(),[&](const auto &v){return !requested.empty()?v.at("id")==requested:follow?v.at("index")==unsigned(sample_?now.sample:now.instrument):v.at("id")==identity_;});
    if(follow&&requested.empty()&&it==values.end())it=values.begin();require(it!=values.end(),sample_?"No sample is available / choose From cursor":"Create or choose an instrument first");
    releaseAll();captured_=now;identity_=it->at("id");index_=it->at("index");choices();set(targetLabel,(sample_?L"Sample ":L"Instrument ")+std::to_wstring(index_)+L" · "+wide(it->at("name").get<std::string>())+L" · saved settings");status(L"Click a piano key, use Note on/off, or press F6 for musical typing");
  }
  static bool sharp(unsigned pitch){const auto n=(pitch-1)%12;return n==1||n==3||n==6||n==8||n==10;}
  void geometry(){white_.clear();black_.clear();whiteNotes_.clear();blackNotes_.clear();const unsigned last=std::min(120u,firstNote_+24);unsigned count=0;for(unsigned n=firstNote_;n<=last;++n)count+=!sharp(n);const float width=keyboard_.w/std::max(1u,count);unsigned at=0;for(unsigned n=firstNote_;n<=last;++n){if(sharp(n)){black_.push_back({keyboard_.x+at*width-width*.32f,keyboard_.y,width*.64f,keyboard_.h*.61f});blackNotes_.push_back(n);}else{white_.push_back({keyboard_.x+at*width,keyboard_.y,width-2,keyboard_.h});whiteNotes_.push_back(n);++at;}}}
  unsigned hit(float x,float y)const{for(size_t i=0;i<black_.size();++i)if(black_[i].contains(x,y))return blackNotes_[i];for(size_t i=0;i<white_.size();++i)if(white_[i].contains(x,y))return whiteNotes_[i];return 0;}
  void action(int id,unsigned notification)override{
    if(setting_)return;if(notification==EN_CHANGE)return;if(notification==CBN_SELCHANGE){
      if(id==kind){releaseAll();const auto previous=sample_;sample_=SendMessageW(controls_.at(kind),CB_GETCURSEL,0,0)==0;try{load(true);}catch(...){sample_=previous;choices();throw;}}
      else if(id==asset){const auto chosen=SendMessageW(controls_.at(asset),CB_GETCURSEL,0,0);const auto &values=catalog(captured_);require(chosen>=0&&size_t(chosen)<values.size(),"Choose an audition target");const auto target=values[size_t(chosen)].at("id").get<std::string>();try{load(false,target);}catch(...){choices();throw;}}return;
    }
    if(notification!=BN_CLICKED)return;if(id==close){hide();return;}if(id==noteOff){releaseAll();return;}
    if(id==panic){releaseAll();const auto now=context_();request_("transport.panic",{{"expectedRevision",now.revision}});status(L"All preview voices released / song playback continues");return;}
    if(id==fromCursor){load(true);return;}if(id==reload){load(false);return;}if(id==noteOn){begin(buttonKey,integer(note,1,120));return;}
    if(id==octaveDown||id==octaveUp){releaseAll();firstNote_=unsigned(std::clamp(int(firstNote_)+(id==octaveDown?-12:12),1,97));geometry();requestPaint();}
  }
  bool key(WPARAM value,bool ctrl,bool)override{
    if(value==VK_ESCAPE){releaseAll();return true;}if(value==VK_F6){SetFocus(GetFocus()==window_?controls_.at(note):window_);return true;}
    if(ctrl||GetKeyState(VK_MENU)&0x8000||GetFocus()!=window_)return false;
    if(value==VK_SPACE){begin(value,integer(note,1,120));return true;}const std::string lower="ZSXDCVGBHNJM",upper="Q2W3ER5T6Y7UI";const auto low=lower.find(char(value)),high=upper.find(char(value));
    if(low!=std::string::npos){begin(value,std::min(120u,firstNote_+unsigned(low)));return true;}if(high!=std::string::npos){begin(value,std::min(120u,firstNote_+12+unsigned(high)));return true;}return false;
  }
  bool keyUp(WPARAM key)override{if(!held_.contains(key))return false;release(key);return true;}
  void deactivate()override{releaseAll();}
  void mouse(UINT m,float x,float y,WPARAM flags)override{
    if(m==WM_CAPTURECHANGED){release(mouseKey);return;}
    if(m==WM_LBUTTONDOWN){if(const auto pitch=hit(x,y)){SetFocus(window_);begin(mouseKey,pitch);SetCapture(window_);}}
    else if(m==WM_MOUSEMOVE&&(flags&MK_LBUTTON)&&GetCapture()==window_){const auto pitch=hit(x,y);auto it=held_.find(mouseKey);if(it!=held_.end()&&it->second.note==pitch)return;release(mouseKey);if(pitch)begin(mouseKey,pitch);}
    else if(m==WM_LBUTTONUP){release(mouseKey);if(GetCapture()==window_)ReleaseCapture();}
  }
  void layout()override{const auto [w,h]=size();place(heading,14,12,108,22);place(kind,128,8,114,160);place(asset,250,8,w-516,240);place(fromCursor,w-258,8,104,28);place(reload,w-146,8,70,28);place(close,w-70,8,56,28);place(targetLabel,14,44,w-28,24);
    place(noteLabel,14,80,62,24);place(note,80,76,68,28);place(velocityLabel,158,80,62,24);place(velocity,224,76,68,28);place(noteOn,306,76,90,28);place(noteOff,404,76,90,28);place(panic,502,76,100,28);place(octaveDown,w-158,76,68,28);place(octaveUp,w-82,76,68,28);
    keyboard_={14,120,w-28,std::max(70.f,h-218)};geometry();place(helpLabel,14,h-88,w-28,22);place(statusLabel,14,h-60,w-28,46);
  }
  void paint(RenderSurface &s)override{const auto [w,h]=size();s.fill(0,0,w,h,0x18222d);auto active=[&](unsigned pitch){return std::any_of(held_.begin(),held_.end(),[&](const auto &v){return v.second.note==pitch;});};for(size_t i=0;i<white_.size();++i){const auto r=white_[i];s.fill(r.x,r.y,r.w,r.h,active(whiteNotes_[i])?0x6edac5:0xc7d4db);if((whiteNotes_[i]-1)%12==0)s.uiText(L"C"+std::to_wstring((whiteNotes_[i]-1)/12),r.x+5,r.y+r.h-22,r.w-10,0x162330);}for(size_t i=0;i<black_.size();++i){const auto r=black_[i];s.fill(r.x,r.y,r.w,r.h,active(blackNotes_[i])?0x58b5a6:0x0c141c);}s.outline(keyboard_.x,keyboard_.y,keyboard_.w,keyboard_.h,GetFocus()==window_?0x6edac5:0x344757);}
public:
  AuditionWindow(HWND owner,Request request,std::function<Context()> context):NativeToolWindow(owner),request_(std::move(request)),context_(std::move(context)){
    minimumWidth_=800;minimumHeight_=380;create(L"ScreamSeq.Audition",L"Sample & instrument audition",980,450);combo(kind);combo(asset);for(auto text:{L"Sample",L"Instrument"})SendMessageW(controls_.at(kind),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text));edit(note,L"49",3);edit(velocity,L"100",3);
    for(auto [id,text]:std::initializer_list<std::pair<int,const wchar_t *>>{{noteOn,L"Note on"},{noteOff,L"Note off"},{panic,L"Panic"},{fromCursor,L"From cursor"},{reload,L"Reload"},{close,L"Close"},{octaveDown,L"Octave −"},{octaveUp,L"Octave +"}})button(id,text);
    for(auto [id,text]:std::initializer_list<std::pair<int,const wchar_t *>>{{heading,L"AUDITION"},{targetLabel,L""},{noteLabel,L"Note / 120"},{velocityLabel,L"Velocity"},{statusLabel,L""},{helpLabel,L"F6: piano focus · ZSXDCVGBHNJM / Q2W3ER5T6Y7UI · Space: chosen note · Escape: release"}})label(id,text);finish();
  }
  ~AuditionWindow(){releaseAll();}
  void openAt(bool sample=true,std::string identity={}){if(preparing_){show();return;}if(identity_.empty()||!identity.empty()){releaseAll();sample_=sample;load(true,std::move(identity));}show();SetFocus(window_);}
  bool releaseKey(WPARAM key){return keyUp(key);}
  Json snapshot()const{Json held=Json::array(),keys=Json::array();for(const auto &[key,value]:held_)held.push_back({{"key",key},{"note",value.note},{"epoch",value.epoch},{"pending",value.pending}});for(size_t i=0;i<white_.size();++i){const auto r=white_[i];keys.push_back({{"note",whiteNotes_[i]},{"black",false},{"x",r.x},{"y",r.y},{"width",r.w},{"height",r.h}});}for(size_t i=0;i<black_.size();++i){const auto r=black_[i];keys.push_back({{"note",blackNotes_[i]},{"black",true},{"x",r.x},{"y",r.y},{"width",r.w},{"height",r.h}});}return {{"visible",visible()},{"kind",sample_?"sample":"instrument"},{"index",index_},{"id",identity_},{"document",captured_.document},{"expectedRevision",captured_.revision},{"stale",!current()},{"preparing",preparing_},{"held",held},{"keys",keys},{"firstNote",firstNote_},{"note",utf8(field(note))},{"velocity",utf8(field(velocity))},{"status",utf8(status_)}};}
};
}
