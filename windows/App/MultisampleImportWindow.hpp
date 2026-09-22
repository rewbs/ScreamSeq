#pragma once
#include "NativeToolWindow.hpp"

namespace ScreamSeq {
class MultisampleImportWindow final : public NativeToolWindow {
  using Json=Api::Json;
public:
  using Request=std::function<Json(const std::string &,const Json &)>;
  using Context=std::function<std::pair<std::string,std::string>()>;
private:
  enum:int{name=5601,shift,review,apply,rows,preview,stop,close,rebase,discard,heading=5700,nameLabel,shiftLabel,explanation,statusLabel};
  Request request_;Context context_;std::function<void(unsigned,const std::string &)> applied_;
  Json group_,zones_=Json::array(),reviewedParams_;std::pair<std::string,std::string> captured_;
  bool pending_=false,setting_=false,draft_=false;uint64_t generation_=0;
  void status(std::wstring text){status_=std::move(text);set(statusLabel,status_);requestPaint();}
  void error(const std::exception &e)override{status(wide(e.what()));}
  void requireCurrent(){if(context_()!=captured_)throw std::runtime_error("Song changed / reopen this family review before importing");}
  Json parameters()const{
    const auto offset=number(shift);if(offset<-4||offset>4||offset!=std::floor(offset))throw std::runtime_error("Use an octave offset from -4 to +4");
    Json sources=Json::array();for(const auto &sample:group_.at("samples")){const auto note=sample.at("semitone").get<int>()+12*int(offset)+1;if(note<1||note>120)throw std::runtime_error("A root is outside C-0 to B-9 / adjust the octave offset");sources.push_back({{"path",sample.at("path")},{"rootNote",note}});}
    return {{"name",utf8(field(name))},{"samples",std::move(sources)},{"expectedRevision",captured_.second}};
  }
  void rebuild(){SendMessageW(controls_.at(rows),LB_RESETCONTENT,0,0);for(size_t i=0;i<group_.at("samples").size();++i){const auto &sample=group_.at("samples")[i];auto text=wide(sample.at("filename").get<std::string>()+"    / "+sample.at("sourceNote").get<std::string>());if(i<zones_.size()){const auto &z=zones_[i];text+=L"    → root "+std::to_wstring(z.at("rootNote").get<unsigned>())+L"    keys "+std::to_wstring(z.at("lowNote").get<unsigned>())+L"–"+std::to_wstring(z.at("highNote").get<unsigned>());}SendMessageW(controls_.at(rows),LB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));}layout();}
  void validate(bool commit){
    if(pending_)return;requireCurrent();auto p=parameters();const auto generation=generation_;
    if(commit&&(zones_.empty()||p!=reviewedParams_))throw std::runtime_error("Check the current roots before importing");
    pending_=true;layout();p["dryRun"]=!commit;
    try{auto result=request_("instrument.importMultisample",p);pending_=false;
      if(commit){applied_(result.at("instrument"),captured_.first);draft_=false;group_=nullptr;zones_=Json::array();hide();return;}
      if(generation!=generation_||context_()!=captured_){status(L"Review target changed / reopen the family");return;}
      p.erase("dryRun");reviewedParams_=std::move(p);zones_=result.at("zones");draft_=true;rebuild();status(L"Roots checked / outside the supplied range stays unmapped / Import uses one Undo step");
    }catch(...){pending_=false;layout();throw;}layout();
  }
  void action(int id,unsigned notification)override{
    if(setting_)return;
    if(id==stop){request_("sample.library.preview.stop",Json::object());return;}
    if(id==close){hide();return;}if(pending_)return;
    if((id==name||id==shift)&&notification==EN_CHANGE){++generation_;draft_=true;zones_=Json::array();reviewedParams_=nullptr;rebuild();status(L"Root draft / Check roots validates every file and mapping");return;}
    if(notification!=BN_CLICKED)return;
    if(id==rebase){captured_=context_();++generation_;zones_=Json::array();reviewedParams_=nullptr;rebuild();status(L"Current song captured / Check roots before importing");}
    else if(id==discard){draft_=false;group_=nullptr;zones_=Json::array();hide();}
    else if(id==review)validate(false);else if(id==apply)validate(true);
    else if(id==preview){const auto row=SendMessageW(controls_.at(rows),LB_GETCURSEL,0,0);if(row>=0&&size_t(row)<group_.at("samples").size())request_("sample.library.preview",{{"path",group_.at("samples")[size_t(row)].at("path")}});}
  }
  bool key(WPARAM value,bool ctrl,bool)override{
    if(value==VK_ESCAPE){hide();return true;}if(ctrl&&value==VK_RETURN){validate(true);return true;}
    if(value==VK_RETURN&&GetFocus()==controls_.at(shift)){validate(false);return true;}
    if(value==VK_RETURN){wchar_t type[32]{};GetClassNameW(GetFocus(),type,32);if(_wcsicmp(type,L"Button")==0){action(GetDlgCtrlID(GetFocus()),BN_CLICKED);return true;}}
    return false;
  }
  void layout()override{if(!ready_)return;const auto [w,h]=size();place(heading,16,14,w-322,24);place(rebase,w-302,14,160,25);place(discard,w-134,14,118,25);place(nameLabel,16,49,w-230,20);place(name,16,73,w-230,26);place(shiftLabel,w-202,49,186,20);place(shift,w-202,73,72,26);place(review,w-122,73,106,26);place(explanation,16,111,w-32,52);place(rows,16,173,w-32,std::max(80.f,h-303));place(preview,16,h-116,94,26);place(stop,118,h-116,76,26);place(apply,w-260,h-116,164,26);place(close,w-88,h-116,72,26);place(statusLabel,16,h-75,w-32,60);for(int id:{name,shift,review,preview,rows,rebase,discard})EnableWindow(controls_.at(id),!pending_);EnableWindow(controls_.at(apply),!pending_&&!zones_.empty());}
  void paint(RenderSurface &s)override{const auto [w,h]=size();s.fill(0,0,w,h,0x18222d);}
public:
  MultisampleImportWindow(HWND owner,Request request,Context context,std::function<void(unsigned,const std::string &)> applied):NativeToolWindow(owner),request_(std::move(request)),context_(std::move(context)),applied_(std::move(applied)){
    minimumWidth_=650;minimumHeight_=440;create(L"ScreamSeq.MultisampleImport",L"Import multi-sample instrument",840,610);
    edit(name,L"",128);edit(shift,L"0",3);add(rows,L"LISTBOX",L"Reviewed sample roots",LBS_NOTIFY|LBS_NOINTEGRALHEIGHT|WS_VSCROLL);
    for(auto [id,text]:std::initializer_list<std::pair<int,const wchar_t *>>{{review,L"Check roots"},{apply,L"Import instrument"},{preview,L"Preview"},{stop,L"Stop"},{close,L"Close"},{rebase,L"Use current song"},{discard,L"Discard draft"}})button(id,text);
    for(auto [id,text]:std::initializer_list<std::pair<int,const wchar_t *>>{{heading,L"Multi-sample instrument"},{nameLabel,L"Instrument name"},{shiftLabel,L"Octave offset −4…+4"},{explanation,L""},{statusLabel,L""}})label(id,text);finish();
  }
  void open(Json group){if(visible()||draft_){NativeToolWindow::show();status(L"Retained family draft / Use current song rechecks the destination; Discard draft releases this family");return;}group_=std::move(group);captured_=context_();++generation_;zones_=Json::array();reviewedParams_=nullptr;setting_=true;set(name,group_.at("name"));set(shift,group_.at("suggestedOctaveShift"));set(explanation,group_.at("explanation"));setting_=false;rebuild();NativeToolWindow::show();status(L"Review filename octaves, then Check roots / tracker C-4 = 49 / Ctrl+Enter imports a checked draft");SetFocus(controls_.at(shift));}
  void hide()override{++generation_;request_("sample.library.preview.stop",Json::object());NativeToolWindow::hide();}
  Json snapshot()const{return {{"visible",visible()},{"pending",pending_},{"draft",draft_},{"documentId",captured_.first},{"revision",captured_.second},{"stale",!captured_.first.empty()&&captured_!=context_()},{"group",group_},{"zones",zones_},{"name",utf8(field(name))},{"octaveShift",utf8(field(shift))},{"status",utf8(status_)}};}
};
}
