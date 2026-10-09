#pragma once
#include "NativeToolWindow.hpp"
#include "NativeWriteCompletion.hpp"

namespace ScreamSeq {
class MultisampleImportWindow final : public NativeToolWindow {
  using Json=Api::Json;
public:
  using Request=std::function<Json(const std::string &,const Json &)>;
  using Context=std::function<std::pair<std::string,std::string>()>;
private:
  enum:int{name=5601,shift,review,apply,rows,preview,stop,close,rebase,discard,heading=5700,nameLabel,shiftLabel,explanation,statusLabel};
  NativeWriteCompletion::Write write_;NativeWriteCompletion completion_;
  Request request_;Context context_;std::function<void(unsigned,const std::string &)> applied_;
  Json group_,zones_=Json::array(),reviewedParams_;std::pair<std::string,std::string> captured_;
  bool pending_=false,setting_=false,draft_=false;uint64_t generation_=0;
  std::optional<Tracker::DocumentDraft> documentDraft()const override {
    return describeDraft(captured_.first,captured_.second,group_.is_object()?group_.value("name",std::string()):std::string(),
      generation_,draft_,pending_,!pending_&&completion_.retained());
  }
  void status(std::wstring text){status_=std::move(text);set(statusLabel,status_);requestPaint();}
  void error(const std::exception &e)override{status(wide(e.what()));}
  void requireCurrent(){if(!group_.is_object())throw std::runtime_error("Open a sample family before importing");if(context_()!=captured_)throw std::runtime_error("Song changed / family retained; choose Use current song, then Check roots");}
  Json parameters()const{
    const auto offset=number(shift);if(offset<-4||offset>4||offset!=std::floor(offset))throw std::runtime_error("Use an octave offset from -4 to +4");
    Json sources=Json::array();for(const auto &sample:group_.at("samples")){const auto note=sample.at("semitone").get<int>()+12*int(offset)+1;if(note<1||note>120)throw std::runtime_error("A root is outside C-0 to B-9 / adjust the octave offset");sources.push_back({{"path",sample.at("path")},{"rootNote",note}});}
    return {{"name",utf8(field(name))},{"samples",std::move(sources)},{"expectedRevision",captured_.second}};
  }
  void rebuild(){SendMessageW(controls_.at(rows),LB_RESETCONTENT,0,0);for(size_t i=0;i<group_.at("samples").size();++i){const auto &sample=group_.at("samples")[i];auto text=wide(sample.at("filename").get<std::string>()+"    / "+sample.at("sourceNote").get<std::string>());if(i<zones_.size()){const auto &z=zones_[i];text+=L"    → root "+std::to_wstring(z.at("rootNote").get<unsigned>())+L"    keys "+std::to_wstring(z.at("lowNote").get<unsigned>())+L"–"+std::to_wstring(z.at("highNote").get<unsigned>());}SendMessageW(controls_.at(rows),LB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));}layout();}
  void validate(bool commit){
    if(pending_)return;if(completion_.retained())throw std::runtime_error("Review the import result before checking or importing again");requireCurrent();auto p=parameters();const auto generation=generation_;const auto captured=captured_;
    if(commit&&(zones_.empty()||p!=reviewedParams_))throw std::runtime_error("Check the current roots before importing");
    pending_=true;layout();p["dryRun"]=!commit;
    try{
      if(commit){
        completion_.submit(write_,"instrument.importMultisample",p,captured.first,generation,Json::array({utf8(field(name)),utf8(field(shift))}));
        finishResult();
      }else{
        auto result=request_("instrument.importMultisample",p);
        if(generation!=generation_||context_()!=captured){status(L"Review changed / raw draft retained; Check roots again");}
        else{p.erase("dryRun");reviewedParams_=std::move(p);zones_=result.at("zones");draft_=true;rebuild();status(L"Roots checked / outside the supplied range stays unmapped / Import uses one Undo step");}
      }
    }catch(...){pending_=false;layout();throw;}
    pending_=false;layout();
  }
  void finishResult(){
    const auto returned=completion_.returned();
    if(!returned)throw std::runtime_error("Import outcome is still unknown / this family cannot be imported again until the operation is reconciled");
    const auto instrument=returned->result.at("instrument").get<unsigned>();
    const auto now=context_();
    if(now.first==returned->document&&now.second==returned->revision)applied_(instrument,returned->document);
    request_("sample.library.preview.stop",Json::object());
    zones_=Json::array();reviewedParams_=nullptr;
    if(generation_==completion_.generation()){
      draft_=false;group_=nullptr;
      status(L"Instrument imported / result reviewed without repeating the import");
      NativeToolWindow::hide();
    }else status(L"Instrument imported / newer family draft retained; Use current song and Check roots before another import");
    completion_.finish();
  }
  void reviewResult(){
    if(pending_||!completion_.retained())return;
    pending_=true;layout();
    try{request_("synchronizeView",Json::object());finishResult();}
    catch(...){pending_=false;layout();throw;}
    pending_=false;layout();
  }
  void action(int id,unsigned notification)override{
    if(setting_)return;
    if((id==name||id==shift)&&notification==EN_CHANGE&&group_.is_object()){++generation_;draft_=true;zones_=Json::array();reviewedParams_=nullptr;rebuild();status(L"Root draft / Check roots validates every file and mapping");return;}
    if(id==close&&notification==BN_CLICKED){hide();return;}if(pending_)return;
    if(notification!=BN_CLICKED)return;
    if(id==stop){request_("sample.library.preview.stop",Json::object());return;}
    if(completion_.retained()&&(id==rebase||id==discard||id==review))throw std::runtime_error("Review the import result before changing its captured family");
    if(id==rebase){captured_=context_();++generation_;zones_=Json::array();reviewedParams_=nullptr;rebuild();status(L"Current song captured / Check roots before importing");}
    else if(id==discard){const auto generation=generation_;hide();if(generation==generation_){++generation_;draft_=false;group_=nullptr;zones_=Json::array();reviewedParams_=nullptr;}else status(L"Newer family draft retained while closing");}
    else if(id==review)validate(false);else if(id==apply){if(completion_.retained())reviewResult();else validate(true);}
    else if(id==preview){const auto row=SendMessageW(controls_.at(rows),LB_GETCURSEL,0,0);if(row>=0&&size_t(row)<group_.at("samples").size())request_("sample.library.preview",{{"path",group_.at("samples")[size_t(row)].at("path")}});}
  }
  bool key(WPARAM value,bool ctrl,bool)override{
    if(value==VK_ESCAPE){hide();return true;}if(ctrl&&value==VK_RETURN){action(apply,BN_CLICKED);return true;}
    if(value==VK_RETURN&&GetFocus()==controls_.at(shift)){validate(false);return true;}
    if(value==VK_RETURN){wchar_t type[32]{};GetClassNameW(GetFocus(),type,32);if(_wcsicmp(type,L"Button")==0){action(GetDlgCtrlID(GetFocus()),BN_CLICKED);return true;}}
    return false;
  }
  void layout()override{if(!ready_)return;const auto [w,h]=size();place(heading,16,14,w-322,24);place(rebase,w-302,14,160,25);place(discard,w-134,14,118,25);place(nameLabel,16,49,w-230,20);place(name,16,73,w-230,26);place(shiftLabel,w-202,49,186,20);place(shift,w-202,73,72,26);place(review,w-122,73,106,26);place(explanation,16,111,w-32,52);place(rows,16,173,w-32,std::max(80.f,h-303));place(preview,16,h-116,94,26);place(stop,118,h-116,76,26);place(apply,w-260,h-116,164,26);place(close,w-88,h-116,72,26);place(statusLabel,16,h-75,w-32,60);for(int id:{name,shift,review,preview,rows,rebase,discard})EnableWindow(controls_.at(id),!pending_);for(int id:{review,rebase,discard})EnableWindow(controls_.at(id),!pending_&&!completion_.retained());set(apply,completion_.retained()?L"Review result":L"Import instrument");EnableWindow(controls_.at(apply),!pending_&&(completion_.retained()||!zones_.empty()));}
  void paint(RenderSurface &s)override{const auto [w,h]=size();s.fill(0,0,w,h,0x18222d);}
public:
  MultisampleImportWindow(HWND owner,Request request,Context context,std::function<void(unsigned,const std::string &)> applied,NativeWriteCompletion::Write write):NativeToolWindow(owner),write_(std::move(write)),request_(std::move(request)),context_(std::move(context)),applied_(std::move(applied)){
    minimumWidth_=650;minimumHeight_=440;create(L"ScreamSeq.MultisampleImport",L"Import multi-sample instrument",840,610);
    edit(name,L"",128);edit(shift,L"0",3);add(rows,L"LISTBOX",L"Reviewed sample roots",LBS_NOTIFY|LBS_NOINTEGRALHEIGHT|WS_VSCROLL);
    for(auto [id,text]:std::initializer_list<std::pair<int,const wchar_t *>>{{review,L"Check roots"},{apply,L"Import instrument"},{preview,L"Preview"},{stop,L"Stop"},{close,L"Close"},{rebase,L"Use current song"},{discard,L"Discard draft"}})button(id,text);
    for(auto [id,text]:std::initializer_list<std::pair<int,const wchar_t *>>{{heading,L"Multi-sample instrument"},{nameLabel,L"Instrument name"},{shiftLabel,L"Octave offset −4…+4"},{explanation,L""},{statusLabel,L""}})label(id,text);finish();
  }
  void open(Json group){if(visible()||draft_||pending_||completion_.retained()){NativeToolWindow::show();status(L"Retained family draft / Use current song rechecks the destination; Discard draft releases this family");return;}group_=std::move(group);captured_=context_();++generation_;draft_=true;zones_=Json::array();reviewedParams_=nullptr;setting_=true;set(name,group_.at("name"));set(shift,group_.at("suggestedOctaveShift"));set(explanation,group_.at("explanation"));setting_=false;rebuild();NativeToolWindow::show();status(L"Review filename octaves, then Check roots / tracker C-4 = 49 / Ctrl+Enter imports a checked draft");SetFocus(controls_.at(shift));}
  void hide()override{if(pending_){status(L"Wait for the import request to finish before closing");return;}pending_=true;layout();try{request_("sample.library.preview.stop",Json::object());NativeToolWindow::hide();}catch(...){pending_=false;layout();throw;}pending_=false;layout();}
  Json snapshot()const{return {{"visible",visible()},{"pending",pending_},{"completion",completion_.snapshot()},{"draft",draft_},{"documentId",captured_.first},{"revision",captured_.second},{"stale",!captured_.first.empty()&&captured_!=context_()},{"group",group_},{"zones",zones_},{"name",utf8(field(name))},{"octaveShift",utf8(field(shift))},{"status",utf8(status_)}};}
};
}
