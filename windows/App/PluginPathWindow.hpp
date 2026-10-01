#pragma once
#include "NativeToolWindow.hpp"
#include <commdlg.h>
#include <filesystem>
namespace ScreamSeq {
class PluginPathWindow final : public NativeToolWindow {
  using Json=Api::Json;
  using Request=std::function<Json(const std::string &,const Json &)>;
  using Context=std::function<std::pair<std::string,std::string>()>;
  enum : int {candidate=3601,preview,reconnect,reload,scan,browse,rescan,close,savedPath,chosenPath,manualPath,
    heading=3700,savedLabel,candidateLabel,manualLabel,statusLabel};
  Request request_;Context context_;Json target_,candidates_=Json::array();
  std::string document_,revision_,prefix_;int selected_=-1;bool pending_=false,setting_=false;
  void status(std::wstring message){status_=std::move(message);set(statusLabel,status_);requestPaint();}
  void error(const std::exception &e)override{status(wide(e.what()));}
  void current(){if(context_()!=std::pair(document_,revision_))throw std::runtime_error("Song or plugin changed / selection retained; Reload before reconnecting");}
  void chosen(){set(chosenPath,selected_>=0?wide(candidates_.at(size_t(selected_)).at("descriptor").at("path").get<std::string>()):L"");}
  void load(){
    if(pending_)return;const auto document=context_().first;pending_=true;layout();
    try{auto data=request_(prefix_+"get",target_);pending_=false;if(context_().first!=document)throw std::runtime_error("Document changed while reading plugin locations");
      const auto previous=selected_>=0?candidates_.at(size_t(selected_)).at("descriptor").at("path").get<std::string>():"";
      candidates_=data.at("candidates");document_=document;revision_=context_().second;setting_=true;
      set(heading,L"Reconnect "+wide(data.at("descriptor").at("name").get<std::string>()));set(savedPath,data.at("descriptor").at("path"));SendMessageW(controls_.at(candidate),CB_RESETCONTENT,0,0);selected_=-1;
      for(size_t i=0;i<candidates_.size();++i){const auto &d=candidates_[i].at("descriptor");auto label=wide(d.at("name").get<std::string>()+" / "+d.at("path").get<std::string>());SendMessageW(controls_.at(candidate),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));if(d.at("path")==previous)selected_=int(i);}
      if(selected_<0&&!candidates_.empty())selected_=0;SendMessageW(controls_.at(candidate),CB_SETCURSEL,selected_,0);chosen();setting_=false;
      status(data.at("moduleVerified").get<bool>()?L"Saved module matches its scan / choose a different location only if needed":wide(data.at("reason").get<std::string>())+L" / choose or scan the matching Windows VST3");
    }catch(...){pending_=false;layout();throw;}layout();
  }
  void apply(bool dry){
    if(pending_||selected_<0)return;current();const auto &choice=candidates_.at(size_t(selected_));Json p=target_;p["expectedRevision"]=revision_;p["path"]=choice.at("descriptor").at("path");p["expectedModuleSHA256"]=choice.at("moduleSHA256");p["dryRun"]=dry;pending_=true;layout();
    try{const auto result=request_(prefix_+"set",p);pending_=false;if(context_().first!=document_)throw std::runtime_error("Document changed while reconnecting / reload to inspect the result");revision_=context_().second;
      if(!dry)set(savedPath,result.at("path"));status(dry?L"Module identity verified / Reconnect checks the saved sound and creates one Undo step":result.at("wouldChange").get<bool>()?L"Plugin reconnected / saved sound, routes and identity retained / Undo available":L"This location is already saved");
    }catch(...){pending_=false;layout();throw;}layout();
  }
  void scanPath(){
    if(pending_)return;current();Json p=target_;p["expectedRevision"]=revision_;p["path"]=utf8(field(manualPath));pending_=true;layout();
    try{request_(prefix_+"scan",p);pending_=false;current();load();status(L"Matching module scanned / choose it and Reconnect");}catch(...){pending_=false;layout();throw;}layout();
  }
  void chooseFile(){
    if(pending_)return;current();const auto captured=context_();pending_=true;layout();
    try{std::vector<wchar_t> path(32768);OPENFILENAMEW choice{};choice.lStructSize=sizeof(choice);choice.hwndOwner=window_;choice.lpstrTitle=L"Choose Windows VST3 module";choice.lpstrFilter=L"VST3 module\0*.vst3\0\0";choice.lpstrFile=path.data();choice.nMaxFile=DWORD(path.size());choice.Flags=OFN_EXPLORER|OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR|OFN_DONTADDTORECENT;
      const bool picked=GetOpenFileNameW(&choice)!=FALSE;pending_=false;if(!picked){if(CommDlgExtendedError())throw std::runtime_error("Could not open the plugin file chooser");layout();return;}
      if(context_()!=captured)throw std::runtime_error("Song changed while choosing a plugin / Reload before scanning");set(manualPath,std::wstring(path.data()));scanPath();
    }catch(...){pending_=false;layout();throw;}layout();
  }
  void scanInstalled(){
    if(pending_)return;current();pending_=true;layout();try{request_("plugin.discover",{{"format","VST3"},{"rescan",true}});pending_=false;current();load();}catch(...){pending_=false;layout();throw;}layout();
  }
  void action(int id,unsigned notification)override{
    if(setting_)return;if(id==close&&notification==BN_CLICKED){hide();return;}if(pending_)return;
    if(id==candidate&&notification==CBN_SELCHANGE){selected_=int(SendMessageW(controls_.at(candidate),CB_GETCURSEL,0,0));chosen();return;}
    if(notification!=BN_CLICKED)return;
    if(id==preview||id==reconnect)apply(id==preview);else if(id==reload)load();else if(id==scan)scanPath();else if(id==browse)chooseFile();else if(id==rescan)scanInstalled();
  }
  bool key(WPARAM value,bool ctrl,bool)override{
    if(value==VK_ESCAPE){hide();return true;}if(ctrl&&value=='R'){load();return true;}if(ctrl&&value==VK_RETURN){apply(false);return true;}
    if(value==VK_F6){SetFocus(controls_.at(GetFocus()==controls_.at(candidate)?manualPath:candidate));return true;}
    if(value==VK_RETURN){if(GetFocus()==controls_.at(manualPath)){scanPath();return true;}wchar_t name[32]{};GetClassNameW(GetFocus(),name,32);if(_wcsicmp(name,L"Button")==0){action(GetDlgCtrlID(GetFocus()),BN_CLICKED);return true;}}return false;
  }
  void layout()override{
    if(!ready_)return;const auto [w,h]=size();place(heading,16,14,w-144,25);place(reload,w-120,14,104,26);place(savedLabel,16,49,w-32,20);place(savedPath,16,73,w-32,26);
    place(candidateLabel,16,112,w-32,20);place(candidate,16,137,w-32,240);place(chosenPath,16,173,w-32,26);place(preview,16,213,144,26);place(reconnect,168,213,144,26);
    place(manualLabel,16,260,w-32,20);place(manualPath,16,284,w-174,26);place(browse,w-150,284,134,26);place(scan,16,321,144,26);place(rescan,168,321,168,26);place(close,w-120,321,104,26);place(statusLabel,16,362,w-32,std::max(48.0f,h-378));
    for(int id:{candidate,reload,scan,browse,rescan,manualPath})EnableWindow(controls_.at(id),!pending_);for(int id:{preview,reconnect})EnableWindow(controls_.at(id),!pending_&&selected_>=0);
  }
  void paint(RenderSurface &surface)override{const auto [w,h]=size();surface.fill(0,0,w,h,0x18222d);}
public:
  PluginPathWindow(HWND owner,Json target,Request request,Context context):NativeToolWindow(owner),request_(std::move(request)),context_(std::move(context)),target_(std::move(target)){
    prefix_=target_.contains("graph")?"graph.plugin.path.":"plugin.path.";minimumWidth_=700;minimumHeight_=480;create(L"ScreamSeq.PluginPath",L"Reconnect Windows plugin",820,540);
    combo(candidate);for(int id:{savedPath,chosenPath})add(id,L"EDIT",L"",ES_AUTOHSCROLL|ES_READONLY);edit(manualPath,L"",8192);
    for(auto [id,name]:std::initializer_list<std::pair<int,const wchar_t *>>{{preview,L"Verify module"},{reconnect,L"Reconnect"},{reload,L"Reload"},{scan,L"Scan path"},{browse,L"Browse module…"},{rescan,L"Rescan installed"},{close,L"Close"}})button(id,name);
    for(auto [id,name]:std::initializer_list<std::pair<int,const wchar_t *>>{{heading,L"Reconnect plugin"},{savedLabel,L"Saved location"},{candidateLabel,L"Scanned Windows modules with the same plugin class"},{manualLabel,L"Or scan an absolute VST3 bundle or module path"},{statusLabel,L""}})label(id,name);
    finish();load();
  }
  const Json &target()const{return target_;}
  void show(){NativeToolWindow::show();SetFocus(controls_.at(candidate));}
  Json snapshot()const{return {{"visible",visible()},{"target",target_},{"document",document_},{"expectedRevision",revision_},{"stale",context_()!=std::pair(document_,revision_)},{"pending",pending_},{"selected",selected_},{"candidates",candidates_},{"savedPath",utf8(field(savedPath))},{"manualPath",utf8(field(manualPath))},{"status",utf8(status_)}};}
};
}
