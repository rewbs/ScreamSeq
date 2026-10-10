#pragma once
#include "NativeToolWindow.hpp"
#include <set>

namespace ScreamSeq {
// Read-only provenance owned by the loaded document. No musical draft or
// acknowledgement can weaken the loader's source-overwrite protection.
class DocumentLoadReportWindow final : public NativeToolWindow {
public:
  using Json=Api::Json;
  using SaveCopy=std::function<void(const std::string &)>;
private:
  enum:int {heading=8300,reportText,statusLabel,saveCopyButton,closeButton};
  SaveCopy saveCopy_;Json context_=Json::object();HWND previousFocus_{};
  static std::wstring report(const Json &state) {
    std::wstring text;
    const auto source=state.at("sourcePath").get<std::string>(),path=state.at("path").get<std::string>();
    if(!source.empty())text=L"Source file\r\n"+wide(source)+L"\r\n\r\n";
    if(!path.empty()&&path!=source)text+=L"Current file\r\n"+wide(path)+L"\r\n\r\n";
    if(state.at("requiresSaveAs")==true)
      text+=L"The original file is protected from overwrite. Review the recovery warnings and use Save a copy to keep the recovered song.\r\n\r\n";
    if(state.at("editable")==false)text+=L"This format is available for preview only.\r\n\r\n";
    std::set<std::string> seen;unsigned count=0;
    for(const auto *key:{"warnings","issues"})for(const auto &item:state.at(key)) {
      const auto value=item.get<std::string>();if(value.empty()||!seen.insert(value).second)continue;
      text+=std::to_wstring(++count)+L". "+wide(value)+L"\r\n\r\n";
    }
    text+=count?L"Warnings describe the file as opened.":L"No load warnings or import issues were reported.";
    return text;
  }
  void saveCopy() {
    if(context_.empty()||context_.at("busy")==true||context_.at("requiresSaveAs")!=true)
      throw std::runtime_error("Wait for the current operation before saving a recovered copy");
    saveCopy_(context_.at("documentId").get<std::string>());
  }
  void layout()override {
    if(!ready_)return;const auto [w,h]=size();
    place(heading,18,14,w-36,26);place(reportText,18,52,w-36,h-152);
    place(statusLabel,18,h-90,w-36,30);
    place(saveCopyButton,18,h-48,140,28,!context_.empty()&&context_.at("requiresSaveAs")==true);
    place(closeButton,w-98,h-48,80,28);
  }
  void action(int id,unsigned notification)override {
    if(notification!=BN_CLICKED)return;
    if(id==saveCopyButton)saveCopy();else if(id==closeButton)hide();
  }
  void error(const std::exception &error)override{set(statusLabel,wide(error.what()));}
  bool key(WPARAM value,bool ctrl,bool shift)override {
    if(!visible()||!owns(GetFocus())||ctrl||(GetKeyState(VK_MENU)&0x8000))return false;
    if(value==VK_ESCAPE){hide();return true;}
    if(value==VK_RETURN&&!shift&&GetFocus()==controls_.at(saveCopyButton)){saveCopy();return true;}
    if(value==VK_RETURN&&!shift&&GetFocus()==controls_.at(closeButton)){hide();return true;}
    if(value==VK_TAB&&GetFocus()==window_){SetFocus(controls_.at(reportText));return true;}return false;
  }
public:
  DocumentLoadReportWindow(HWND owner,SaveCopy save):NativeToolWindow(owner),saveCopy_(std::move(save)) {
    minimumClientWidth_=440;minimumClientHeight_=300;
    create(L"ScreamSeq.DocumentLoadReport",L"Project load report",660,460);
    label(heading,L"Project load report");
    add(reportText,L"EDIT",L"",ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL);
    SendMessageW(controls_.at(reportText),EM_SETLIMITTEXT,0x7ffffffe,0);
    label(statusLabel,L"");button(saveCopyButton,L"Save a copy…");button(closeButton,L"Close");finish();
  }
  void update(Json context) {
    const auto text=report(context);context_=std::move(context);
    // NativeControls::text avoids WM_SETTEXT when unchanged, preserving text
    // selection and vertical scroll during busy/revision/telemetry updates.
    set(reportText,text);
    set(statusLabel,context_.at("requiresSaveAs")==true?L"Source protected / Save a copy required":L"Load report retained for this document");
    EnableWindow(controls_.at(saveCopyButton),context_.at("busy")!=true);
    layout();
  }
  void open(Json context) {
    const bool existing=visible();if(!existing)previousFocus_=GetFocus();
    update(std::move(context));show();if(!existing)SetFocus(controls_.at(reportText));
  }
  void hide()override {
    const bool focused=owns(GetFocus());NativeToolWindow::hide();
    if(focused&&IsWindow(previousFocus_)&&IsWindowVisible(previousFocus_)&&IsWindowEnabled(previousFocus_))SetFocus(previousFocus_);
  }
  Json snapshot()const{return {{"visible",visible()},{"context",context_},{"text",utf8(field(reportText))}};}
};
}
