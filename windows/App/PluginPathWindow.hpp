#pragma once
#include "NativeToolWindow.hpp"
#include "NativeWriteCompletion.hpp"
#include <commdlg.h>
#include <filesystem>
namespace ScreamSeq {
class PluginPathWindow final : public NativeToolWindow {
  using Json=Api::Json;
  using Request=std::function<Json(const std::string &,const Json &)>;
  using Context=std::function<std::pair<std::string,std::string>()>;
  enum : int {candidate=3601,preview,reconnect,reload,scan,browse,rescan,close,savedPath,chosenPath,manualPath,discardDraft,
    heading=3700,savedLabel,candidateLabel,manualLabel,statusLabel};
  Request request_;Context context_;Json target_,candidates_=Json::array();
  NativeWriteCompletion::Write write_;NativeWriteCompletion completion_;Json report_=Json::object();
  struct ScanIntent {std::string method;Json params,fields;uint64_t generation;};
  std::optional<ScanIntent> scanIntent_;bool readbackNeedsReload_=false;
  std::string document_,revision_,prefix_;int selected_=-1;bool pending_=false,setting_=false,loaded_=false;uint64_t generation_=0;
  Json baseline_=Json::array({"",""});
  std::string selectedPath()const{return selected_>=0&&size_t(selected_)<candidates_.size()?candidates_.at(size_t(selected_)).at("descriptor").at("path").get<std::string>():"";}
  Json raw()const{return Json::array({selectedPath(),utf8(field(manualPath))});}
  bool unresolved()const noexcept{return completion_.retained()||bool(scanIntent_);}
  std::optional<Tracker::DocumentDraft> documentDraft()const override{return describeDraft(document_,revision_,target_.dump(),generation_,raw()!=baseline_,pending_,!pending_&&unresolved());}
  void status(std::wstring message){status_=std::move(message);set(statusLabel,status_);requestPaint();}
  void error(const std::exception &e)override{auto message=wide(e.what());if(unresolved())message+=L" / Review result before another reconnect or scan";status(std::move(message));}
  void current(){if(readbackNeedsReload_||context_()!=std::pair(document_,revision_))throw std::runtime_error("Song or plugin changed / selection retained; Reload before reconnecting");}
  void requireResolved()const{if(unresolved())throw std::runtime_error("Review the retained reconnect or scan result before checking, scanning or discarding this draft");}
  void chosen(){set(chosenPath,selected_>=0?wide(candidates_.at(size_t(selected_)).at("descriptor").at("path").get<std::string>()):L"");}
  void load(bool finishingScan=false){
    if(pending_)return;if(!finishingScan)requireResolved();const auto captured=context_();const auto generation=generation_;
    if(loaded_&&captured.first!=document_)throw std::runtime_error("Original song is no longer open / plugin path draft retained");
    pending_=true;layout();
    try{auto data=request_(prefix_+"get",target_);if(context_()!=captured||generation!=generation_)throw std::runtime_error("Song or path draft changed while reading / retained; Reload again when ready");
      const auto previous=selectedPath();
      candidates_=data.at("candidates");document_=captured.first;revision_=captured.second;setting_=true;
      set(heading,L"Reconnect "+wide(data.at("descriptor").at("name").get<std::string>()));set(savedPath,data.at("descriptor").at("path"));ScreamSeq::NativeInputGate::present(controls_.at(candidate),CB_RESETCONTENT,0,0);selected_=-1;
      for(size_t i=0;i<candidates_.size();++i){const auto &d=candidates_[i].at("descriptor");auto label=wide(d.at("name").get<std::string>()+" / "+d.at("path").get<std::string>());ScreamSeq::NativeInputGate::present(controls_.at(candidate),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));if(d.at("path")==previous)selected_=int(i);}
      if(selected_<0&&!candidates_.empty())selected_=0;ScreamSeq::NativeInputGate::present(controls_.at(candidate),CB_SETCURSEL,selected_,0);chosen();setting_=false;
      if(!loaded_)baseline_=raw();loaded_=true;readbackNeedsReload_=false;++generation_;
      status(data.at("moduleVerified").get<bool>()?L"Saved module matches its scan / choose a different location only if needed":wide(data.at("reason").get<std::string>())+L" / choose or scan the matching Windows VST3");
    }catch(...){setting_=false;pending_=false;layout();throw;}pending_=false;layout();
  }
  void apply(bool dry){
    if(!dry&&unresolved()){reviewResult();return;}requireResolved();
    if(pending_||selected_<0)return;current();const auto generation=generation_;const auto fields=raw();const auto &choice=candidates_.at(size_t(selected_));Json p=target_;p["expectedRevision"]=revision_;p["path"]=choice.at("descriptor").at("path");p["expectedModuleSHA256"]=choice.at("moduleSHA256");p["dryRun"]=dry;pending_=true;layout();
    try{
      if(dry){request_(prefix_+"set",p);current();status(generation!=generation_?L"Module checked / newer path draft retained":L"Module identity verified / Reconnect checks the saved sound and creates one Undo step");}
      else{completion_.submit(write_,prefix_+"set",p,document_,generation,fields);finishResult();}
    }catch(...){pending_=false;layout();throw;}pending_=false;layout();
  }
  void finishResult(){
    const auto returned=completion_.returned();
    if(!returned)throw std::runtime_error("Reconnect outcome is still unknown / captured path is retained; do not reconnect again until reconciled");
    report_=returned->result;const auto &fields=completion_.fields();
    auto message=report_.at("wouldChange").get<bool>()?L"Plugin reconnected / saved sound, routes and identity retained / Undo available":L"This location was already saved";
    std::wstring text=message;
    const bool currentResult=context_()==std::pair(returned->document,returned->revision);
    if(currentResult)set(savedPath,report_.at("path"));
    else text+=L" / song changed since completion; current location was not refreshed";
    // Baseline is the submitted choice, even if newer input returned to the
    // previous saved path. Never consume a manual path that was not submitted.
    baseline_[0]=fields[0];if(fields[1]==fields[0]||fields[1]=="")baseline_[1]=fields[1];
    if(generation_==completion_.generation()&&currentResult)revision_=returned->revision;
    else if(generation_!=completion_.generation())text+=L" / newer path draft retained; Reload before reconnecting";
    status(text);completion_.finish();
  }
  void reviewResult(){
    if(pending_||!unresolved())return;pending_=true;layout();
    try{request_("synchronizeView",Json::object());if(completion_.returned())finishResult();else reviewObservation();}
    catch(...){pending_=false;layout();throw;}pending_=false;layout();
  }
  void reviewObservation(){
    const auto captured=context_();
    if(captured.first!=document_)throw std::runtime_error("Original song is no longer open / retained result cannot be reconciled against another song");
    auto data=request_(prefix_+"get",target_);
    if(context_()!=captured)throw std::runtime_error("Song changed during result review / retained; Review again");
    // Validate the complete observation before touching fields or releasing
    // intent. In particular, a rack index cannot substitute for stable identity.
    for(auto i=target_.begin();i!=target_.end();++i)if(data.at(i.key())!=i.value())throw std::runtime_error("Location readback belongs to another plugin");
    const auto observedPath=data.at("descriptor").at("path").get<std::string>();
    (void)data.at("descriptor").at("name").get<std::string>();
    (void)data.at("moduleVerified").get<bool>();(void)data.at("reason").get<std::string>();
    const auto &candidates=data.at("candidates");
    if(!candidates.is_array())throw std::runtime_error("Invalid scanned candidate readback");
    for(const auto &entry:candidates){(void)entry.at("descriptor").at("path").get<std::string>();(void)entry.at("descriptor").at("name").get<std::string>();(void)entry.at("moduleSHA256").get<std::string>();}
    auto report=Json{{"outcome","unverified"},{"observedRevision",captured.second},{"observed",std::move(data)}};
    if(scanIntent_)report["submission"]={{"method",scanIntent_->method},{"params",scanIntent_->params},{"fields",scanIntent_->fields},{"generation",scanIntent_->generation}};
    else{report["submission"]=completion_.snapshot();report["submission"]["fields"]=completion_.fields();}
    // Observing the desired path does not prove which request set it or that
    // an Undo entry was created. Keep all raw fields and their old baseline.
    set(savedPath,observedPath);report_=std::move(report);readbackNeedsReload_=true;
    status(L"Current location and scan candidates inspected / earlier outcome unverified / draft retained; Reload before reconnecting or scanning");
    completion_.finish();scanIntent_.reset();
  }
  void runScan(const std::string &method,Json params){
    const auto generation=generation_;scanIntent_=ScanIntent{method,std::move(params),raw(),generation};
    pending_=true;layout();bool returned=false;
    try{
      request_(method,scanIntent_->params);returned=true;current();
      if(generation!=generation_)throw std::runtime_error("Scan finished / newer path draft retained; Review result before Reload");
      pending_=false;load(true);scanIntent_.reset();
    }catch(const Api::ApiError &e){
      if(!returned&&e.outcome&&e.outcome->state==Tracker::CommitOutcome::NotCommitted)scanIntent_.reset();
      pending_=false;layout();throw;
    }catch(...){pending_=false;layout();throw;}
    pending_=false;layout();
  }
  void scanPath(){
    if(pending_)return;requireResolved();current();Json p=target_;p["expectedRevision"]=revision_;p["path"]=utf8(field(manualPath));
    runScan(prefix_+"scan",std::move(p));status(L"Matching module scanned / choose it and Reconnect");
  }
  void chooseFile(){
    if(pending_)return;requireResolved();current();const auto captured=context_();const auto generation=generation_;pending_=true;layout();
    try{std::vector<wchar_t> path(32768);OPENFILENAMEW choice{};choice.lStructSize=sizeof(choice);choice.hwndOwner=window_;choice.lpstrTitle=L"Choose Windows VST3 module";choice.lpstrFilter=L"VST3 module\0*.vst3\0\0";choice.lpstrFile=path.data();choice.nMaxFile=DWORD(path.size());choice.Flags=OFN_EXPLORER|OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR|OFN_DONTADDTORECENT;
      const bool picked=GetOpenFileNameW(&choice)!=FALSE;pending_=false;if(!picked){if(CommDlgExtendedError())throw std::runtime_error("Could not open the plugin file chooser");layout();return;}
      if(context_()!=captured||generation!=generation_)throw std::runtime_error("Song or path draft changed while choosing a plugin / retained; Reload before scanning");set(manualPath,std::wstring(path.data()));scanPath();
    }catch(...){pending_=false;layout();throw;}layout();
  }
  void scanInstalled(){
    if(pending_)return;requireResolved();current();runScan("plugin.discover",{{"format","VST3"},{"rescan",true}});
  }
  void action(int id,unsigned notification)override{
    if(setting_)return;
    if(id==manualPath&&notification==EN_CHANGE){++generation_;return;}
    if(id==candidate&&notification==CBN_SELCHANGE){selected_=int(SendMessageW(controls_.at(candidate),CB_GETCURSEL,0,0));++generation_;chosen();return;}
    if(id==close&&notification==BN_CLICKED){hide();return;}if(pending_)return;
    if(notification!=BN_CLICKED)return;
    if(id==discardDraft){requireResolved();setting_=true;set(manualPath,L"");setting_=false;baseline_=raw();++generation_;hide();return;}
    if(id==preview||id==reconnect)apply(id==preview);else if(id==reload)load();else if(id==scan)scanPath();else if(id==browse)chooseFile();else if(id==rescan)scanInstalled();
  }
  bool key(WPARAM value,bool ctrl,bool)override{
    if(value==VK_ESCAPE){hide();return true;}if(ctrl&&value=='R'){load();return true;}if(ctrl&&value==VK_RETURN){apply(false);return true;}
    if(value==VK_F6){SetFocus(controls_.at(GetFocus()==controls_.at(candidate)?manualPath:candidate));return true;}
    if(value==VK_RETURN){if(GetFocus()==controls_.at(manualPath)){scanPath();return true;}wchar_t name[32]{};GetClassNameW(GetFocus(),name,32);if(_wcsicmp(name,L"Button")==0){action(GetDlgCtrlID(GetFocus()),BN_CLICKED);return true;}}return false;
  }
  void layout()override{
    if(!ready_)return;const auto [w,h]=size();place(heading,16,14,w-144,25);place(reload,w-120,14,104,26);place(savedLabel,16,49,w-32,20);place(savedPath,16,73,w-32,26);
    place(candidateLabel,16,112,w-32,20);place(candidate,16,137,w-32,240);place(chosenPath,16,173,w-32,26);place(preview,16,213,144,26);place(reconnect,168,213,144,26);place(discardDraft,320,213,144,26);
    place(manualLabel,16,260,w-32,20);place(manualPath,16,284,w-174,26);place(browse,w-150,284,134,26);place(scan,16,321,144,26);place(rescan,168,321,168,26);place(close,w-120,321,104,26);place(statusLabel,16,362,w-32,std::max(48.0f,h-378));
    for(int id:{candidate,manualPath})EnableWindow(controls_.at(id),!pending_);
    for(int id:{reload,scan,browse,rescan,discardDraft})EnableWindow(controls_.at(id),!pending_&&!unresolved());
    EnableWindow(controls_.at(preview),!pending_&&!unresolved()&&selected_>=0);
    set(reconnect,unresolved()?L"Review result":L"Reconnect");EnableWindow(controls_.at(reconnect),!pending_&&(unresolved()||selected_>=0));
  }
public:
  PluginPathWindow(HWND owner,Json target,Request request,Context context,NativeWriteCompletion::Write write):NativeToolWindow(owner),request_(std::move(request)),context_(std::move(context)),target_(std::move(target)),write_(std::move(write)){
    prefix_=target_.contains("graph")?"graph.plugin.path.":"plugin.path.";minimumWidth_=700;minimumHeight_=480;create(L"ScreamSeq.PluginPath",L"Reconnect Windows plugin",820,540);
    combo(candidate);for(int id:{savedPath,chosenPath})add(id,L"EDIT",L"",ES_AUTOHSCROLL|ES_READONLY);edit(manualPath,L"",8192);
    for(auto [id,name]:std::initializer_list<std::pair<int,const wchar_t *>>{{preview,L"Verify module"},{reconnect,L"Reconnect"},{reload,L"Reload"},{scan,L"Scan path"},{browse,L"Browse module…"},{rescan,L"Rescan installed"},{close,L"Close"},{discardDraft,L"Discard draft"}})button(id,name);
    for(auto [id,name]:std::initializer_list<std::pair<int,const wchar_t *>>{{heading,L"Reconnect plugin"},{savedLabel,L"Saved location"},{candidateLabel,L"Scanned Windows modules with the same plugin class"},{manualLabel,L"Or scan an absolute VST3 bundle or module path"},{statusLabel,L""}})label(id,name);
    const auto captured=context_();document_=captured.first;revision_=captured.second;finish();load();
  }
  const Json &target()const{return target_;}
  bool retainedDraft()const{return pending_||unresolved()||raw()!=baseline_;}
  bool matchesTarget(const Json &target)const{return target_==target&&document_==context_().first;}
  void show(){NativeToolWindow::show();SetFocus(controls_.at(candidate));}
  Json snapshot()const{return {{"visible",visible()},{"target",target_},{"document",document_},{"expectedRevision",revision_},{"stale",readbackNeedsReload_||context_()!=std::pair(document_,revision_)},{"pending",pending_},{"completion",completion_.snapshot()},{"scanReview",scanIntent_?Json{{"method",scanIntent_->method},{"params",scanIntent_->params},{"fields",scanIntent_->fields},{"generation",scanIntent_->generation}}:Json()},{"readbackNeedsReload",readbackNeedsReload_},{"report",report_},{"draft",raw()!=baseline_},{"generation",generation_},{"selected",selected_},{"candidates",candidates_},{"savedPath",utf8(field(savedPath))},{"manualPath",utf8(field(manualPath))},{"status",utf8(status_)}};}
};
}
