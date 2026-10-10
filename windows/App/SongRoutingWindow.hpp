#pragma once
#include "NativeToolWindow.hpp"
#include "SongRoutingCanvas.hpp"
#include "NativeWriteCompletion.hpp"
#include "NativeContextMenu.hpp"
namespace ScreamSeq {
class SongRoutingWindow final:public NativeToolWindow {
  using Json=Api::Json;using Node=SongRoutingCanvas::Node;
  using Request=std::function<Json(const std::string &,const Json &)>;using Context=std::function<std::pair<std::string,std::string>()>;
  enum:int {filter=3801,nodePicker,wirePicker,kind,source,destination,gain,input,output,pre,enabled,connect,update,disconnect,reload,fit,zoomOut,zoomIn,arrange,close,open,enable,page,insertPicker,effectPicker,insertAdd,insertRemove,insertUp,insertDown,graphPicker,amount,wet,assign,clear,saveLayout,verify,
    inputPorts,outputPorts,addEffect,addGroup,addReturn,showInserts,showAssignment,helpLabel,title=3900,nodeLabel,wireLabel,sourceLabel,destinationLabel,gainLabel,inputLabel,outputLabel,insertLabel,effectLabel,graphLabel,amountLabel,wetLabel,statusLabel};
  Request request_;Context context_;std::function<void(const Node &)> inspect_;
  std::function<void(const std::string &,const std::string &,const std::string &)> addEffect_;
  std::function<bool()> canRefresh_;bool contextOpen_=false;
  NativeWriteCompletion::Write write_;NativeWriteCompletion completion_;
  std::string operation_,operationDocument_,operationRevision_;Json operationParams_,report_;bool readbackNeedsReload_=false;
  bool unresolved()const noexcept{return completion_.retained();}
  void requireResolved()const{if(unresolved())throw std::runtime_error("Review the previous routing result before another action");}
  Json data_=Json::object();SongRoutingCanvas canvas_;std::string document_,revision_,selected_,filter_;int wire_=-1,page_=0;
  bool setting_=false,pending_=false,dirty_=false,layoutDirty_=false,pre_=false,enabled_=true,keyboardMove_=false;uint64_t generation_=0;
  std::map<int,std::vector<std::string>> choices_;int drag_=0;std::string dragNode_;SongRoutingCanvas::Point dragStart_,dragOrigin_,pointer_;
  std::optional<Tracker::DocumentDraft> documentDraft()const override {
    if(unresolved())return describeDraft(operationDocument_,operationRevision_,operationParams_.dump(),generation_,dirty_||layoutDirty_,pending_,!pending_);
    return describeDraft(document_,revision_,Json::array({selected_,wire_}).dump(),generation_,dirty_||layoutDirty_||drag_==3||drag_==4||drag_==5,pending_);
  }
  void status(const std::wstring &s){status_=s;set(statusLabel,s);requestPaint();}
  void error(const std::exception &e)override{status(wide(e.what())+(unresolved()?L" / Review result before another edit":L""));}
  void current()const{requireResolved();if(readbackNeedsReload_)throw std::runtime_error("Reload the reviewed routing state before another write");if(context_()!=std::pair(document_,revision_))throw std::runtime_error("Song changed / draft retained. Reload before applying");}
  void clean()const{if(dirty_||layoutDirty_)throw std::runtime_error("Apply or reload the captured draft before changing selection");}
  void changed(){if(setting_)return;if(layoutDirty_)throw std::runtime_error("Save layout or Reload before editing routes");dirty_=true;++generation_;status(L"Captured route / Apply to save · Reload discards");}
  std::string choice(int id)const{auto index=SendMessageW(controls_.at(id),CB_GETCURSEL,0,0);const auto &list=choices_.at(id);return index>=0&&size_t(index)<list.size()?list[size_t(index)]:"";}
  void choose(int id,const std::string &value){const auto &list=choices_.at(id);auto i=std::find(list.begin(),list.end(),value);ScreamSeq::NativeInputGate::present(controls_.at(id),CB_SETCURSEL,i==list.end()?-1:i-list.begin(),0);}
  void fill(int id,const std::vector<std::pair<std::wstring,std::string>> &list,const std::string &selected={}){ScreamSeq::NativeInputGate::present(controls_.at(id),CB_RESETCONTENT,0,0);auto &ids=choices_[id];ids.clear();for(const auto &[label,key]:list){ScreamSeq::NativeInputGate::present(controls_.at(id),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));ids.push_back(key);}choose(id,selected);}
  const Node &chosenNode(int control)const{auto n=canvas_.find(choice(control));if(!n)throw std::runtime_error("Select a routing node");return *n;}
  const Json &bus(const std::string &id)const{for(const auto &b:data_.at("mixer").at("buses"))if(b.at("id")==id)return b;throw std::runtime_error("Select a bus or one of its inserts");}
  static std::string busID(const Node &n){if(n.bus.empty())throw std::runtime_error("This route requires a bus or channel stage");return n.bus;}
  static std::string pluginID(const Node &n){if(n.plugin.empty())throw std::runtime_error("This route requires a plugin node");return n.plugin;}
  unsigned port(int control)const{const auto v=number(control);if(v<0||v>63||v!=std::floor(v))throw std::runtime_error("Enter a port from 0 to 63");return unsigned(v);}
  #include "SongRoutingPorts.inc"
  void inspectFields(){
    setting_=true;auto n=canvas_.find(selected_);set(title,n?n->name:L"Song routing");
    std::vector<std::pair<std::wstring,std::string>> inserts,effects,graphs{{L"Dry / no ordinary graph",""}};Json assignment=Json::object();
    if(n&&n->canEditInserts()){const auto &b=bus(n->bus);for(const auto &id:b.at("inserts")){std::wstring name=L"Unavailable insert";for(const auto &p:data_.at("plugins"))if(p.at("id")==id)name=wide(p.at("name").get<std::string>());inserts.push_back({name,id});}}
    std::set<std::string> owners;for(const auto &b:data_.at("mixer").at("buses"))for(const auto &p:b.at("inserts"))owners.insert(p);
    for(const auto &p:data_.at("plugins"))if(!p.value("isInstrument",false)&&!owners.contains(p.at("id")))effects.push_back({wide(p.at("name").get<std::string>()),p.at("id")});
    for(const auto &g:data_.at("library"))graphs.push_back({std::to_wstring(g.at("number").get<unsigned>())+L" · "+wide(g.at("name").get<std::string>()),g.at("id")});
    if(n&&n->canAssignGraph()){const auto target=n->instrument.empty()?n->bus:n->instrument;for(const auto &a:data_.at(n->instrument.empty()?"assignments":"instrumentAssignments"))if(a.at("target")==target)assignment=a;}
    if(n&&n->commandStage())assignment["graph"]=n->graph;
    set(graphLabel,n&&n->commandStage()?L"Pattern-command stage / Open edits its shared recipe":L"Each channel or sample voice gets an independent copy");
    set(insertLabel,n&&!n->canEditInserts()?L"Select the bus or an effect to edit its regular inserts":L"Explicit inserts / processed in this order");
    fill(insertPicker,inserts,inserts.empty()?"":inserts[0].second);fill(effectPicker,effects,effects.empty()?"":effects[0].second);fill(graphPicker,graphs,assignment.value("graph",std::string{}));set(amount,assignment.value("amount",1.0));set(wet,assignment.value("wet",1.0));if(n&&n->commandStage()){set(amount,L"");set(wet,L"");}setting_=false;
  }
  void rebuild(bool resetFields=true){
    const auto from=choices_.contains(source)?choice(source):"",to=choices_.contains(destination)?choice(destination):"";
    canvas_.build(data_,filter_,selected_);if(!canvas_.find(selected_))selected_.clear();wire_=-1;setting_=true;
    std::vector<std::pair<std::wstring,std::string>> nodes,filters{{L"All channels",""}},wires{{L"New connection",""}};
    for(const auto &n:canvas_.nodes)nodes.push_back({n.name+L" / "+wide(n.kind),n.id});for(const auto &b:data_.at("mixer").at("buses"))filters.push_back({wide(b.at("name").get<std::string>()),b.at("id")});
    for(size_t i=0;i<canvas_.edges.size();++i){const auto &e=canvas_.edges[i];wires.push_back({canvas_.find(e.source)->name+L" → "+canvas_.find(e.target)->name+L" / "+e.label,std::to_string(i)});}
    fill(filter,filters,filter_);fill(nodePicker,nodes,selected_);fill(source,nodes,from.empty()?selected_:from);fill(destination,nodes,to);fill(wirePicker,wires,"");
    if(resetFields){set(gain,L"0");set(input,L"1");set(output,L"0");pre_=false;enabled_=true;}
    setting_=false;inspectFields();layout();requestPaint();
  }
  void load(bool completing=false){
    if(!completing)requireResolved();if(pending_)return;const auto captured=context_();const auto generation=generation_;pending_=true;layout();
    try{auto data=request_("graph.get",{{"includeState",false}});if(context_()!=captured||generation_!=generation)throw std::runtime_error("Document or draft changed while loading routing; Reload again");data_=std::move(data);document_=captured.first;revision_=captured.second;dirty_=layoutDirty_=readbackNeedsReload_=false;++generation_;pending_=false;resetPluginPorts();rebuild();status(L"Select a bus, then Add effect to browse plugins. Drag sockets to route; plugin cables use the declared port selectors.");}catch(...){pending_=false;layout();throw;}
  }
  void finishResult(){
    const auto result=completion_.returned();if(!result)throw std::runtime_error("Routing result is uncertain / use Review result");
    auto report=Json{{"outcome","returned"},{"submission",{{"method",operation_},{"document",operationDocument_},{"revision",operationRevision_},{"params",operationParams_},{"fields",completion_.fields()}}},{"result",result->result}};
    if(context_()==std::pair(result->document,result->revision)&&generation_==completion_.generation()){if(operation_=="mixer.bus.add")selected_=result->result.at("bus").get<std::string>();load(true);if(operation_=="mixer.bus.add")canvas_.fit();}
    report_=std::move(report);completion_.finish();operation_.clear();
    status(dirty_||layoutDirty_?L"Earlier routing edit completed / newer draft retained / no write repeated":L"Routing edit completed / document Undo available");
  }
  void reviewResult(){
    if(pending_||!unresolved())return;pending_=true;layout();
    try{
      request_("synchronizeView",Json::object());pending_=false;
      if(completion_.returned())finishResult();
      else{
        const auto captured=context_();const auto generation=generation_;if(captured.first!=operationDocument_)throw std::runtime_error("Original routing song is unavailable / result retained");
        pending_=true;auto observed=request_("graph.get",{{"includeState",false}});
        if(context_()!=captured||generation_!=generation)throw std::runtime_error("Song or draft changed during Review / result retained");
        if(!observed.at("mixer").at("buses").is_array()||!observed.at("plugins").is_array())throw std::runtime_error("Malformed routing observation / result retained");
        report_={{"outcome","unverified"},{"submission",{{"method",operation_},{"document",operationDocument_},{"revision",operationRevision_},{"params",operationParams_},{"fields",completion_.fields()}}},{"observed",std::move(observed)}};
        readbackNeedsReload_=true;completion_.finish();operation_.clear();status(L"Current routing inspected / earlier outcome unverified / draft retained; Reload before another write");
      }
    }catch(...){pending_=false;layout();throw;}pending_=false;layout();
  }
  void mutate(const std::string &method,Json p,bool dry=false){
    if(pending_)return;current();p["expectedRevision"]=revision_;if(dry)p["dryRun"]=true;const auto gen=generation_;pending_=true;layout();
    try{
      if(dry){request_(method,p);status(L"Route validated / song unchanged");}
      else{operation_=method;operationParams_=std::move(p);operationDocument_=document_;operationRevision_=revision_;
        completion_.submit(write_,method,operationParams_,operationDocument_,gen,{{"selected",selected_},{"wire",wire_},{"gain",utf8(field(gain))},{"input",utf8(field(input))},{"output",utf8(field(output))}});
        pending_=false;finishResult();}
    }catch(...){if(!unresolved())operation_.clear();pending_=false;layout();throw;}pending_=false;layout();
  }
  void selectNode(std::string id){clean();selected_=id;wire_=-1;rebuild(false);choose(source,id);refreshRoutePorts();SetFocus(window_);}
  void inspectorPage(int value){clean();page_=value;NativeInputGate::present(controls_.at(page),CB_SETCURSEL,value,0);inspectFields();layout();}
  void browseEffect(){clean();current();const auto &n=chosenNode(nodePicker);if(!n.canEditInserts()||!addEffect_)throw std::runtime_error("Select a bus or one of its regular effects to add an effect");const auto target=n.bus;const auto name=bus(target).at("name").get<std::string>();addEffect_(document_,target,name);}
  void timer(UINT_PTR id)override{
    if(id!=7||!visible()||pending_||dirty_||layoutDirty_||drag_||unresolved()||readbackNeedsReload_||contextOpen_||GetCapture()||(canRefresh_&&!canRefresh_()))return;
    if(context_()==std::pair(document_,revision_))return;
    // Follow completed edits only. Raw route/layout drafts retain their target.
    // A worker may still be publishing; defer busy reads without replaying writes.
    try{const auto focus=GetFocus();load();if(focus&&owns(focus)&&IsWindowEnabled(focus))SetFocus(focus);}
    catch(const Api::ApiError &e){if(e.code!=-32002)throw;}
  }
  bool wheel(UINT message,float x,float y,WPARAM keys)override{
    if(!canvas_.viewport.contains(x,y)||drag_)return false;
    const float delta=float(GET_WHEEL_DELTA_WPARAM(keys))/WHEEL_DELTA;
    if(message==WM_MOUSEWHEEL&&(GET_KEYSTATE_WPARAM(keys)&MK_CONTROL)){
      const auto anchor=canvas_.world(x,y);canvas_.zoom=std::clamp(canvas_.zoom*std::pow(1.2f,delta),.15f,2.f);
      canvas_.panX=x-canvas_.viewport.x-anchor.x*canvas_.zoom;canvas_.panY=y-canvas_.viewport.y-anchor.y*canvas_.zoom;
    }else if(message==WM_MOUSEHWHEEL||(GET_KEYSTATE_WPARAM(keys)&MK_SHIFT))canvas_.panX+=delta*48;
    else canvas_.panY+=delta*48;
    canvas_.geometry();requestPaint();return true;
  }
  bool contextMenu(HWND target,POINT at)override{
    if(target!=window_)return false;if(pending_||unresolved()||readbackNeedsReload_||drag_||contextOpen_)return true;
    if(at.x==-1&&at.y==-1){const auto n=canvas_.find(selected_);const auto r=n?n->rect:canvas_.viewport;at={LONG((r.x+16)*GetDpiForWindow(window_)/96),LONG((r.y+16)*GetDpiForWindow(window_)/96)};ClientToScreen(window_,&at);}
    else{POINT local=at;ScreenToClient(window_,&local);const float scale=96.f/GetDpiForWindow(window_);const float x=local.x*scale,y=local.y*scale;if(!canvas_.viewport.contains(x,y))return false;
      clean();if(const int i=canvas_.nodeAt(x,y);i>=0)selectNode(canvas_.nodes[size_t(i)].id);else if(const int i=canvas_.edgeAt(x,y);i>=0)selectWire(i);else {selected_.clear();wire_=-1;rebuild(false);}}
    const auto n=canvas_.find(selected_);const bool cleanDraft=!dirty_&&!layoutDirty_;const bool active=!data_.at("mixer").at("buses").empty();
    using Item=NativeContextMenu::Item;std::vector<Item> menu;
    const auto item=[&](int id,const wchar_t *text,bool available=true){menu.push_back({id,text,available});};
    item(open,n&&!n->plugin.empty()?L"Plugin parameters…\tEnter":L"Open selected node\tEnter",cleanDraft&&n&&(!n->bus.empty()||!n->plugin.empty()||!n->graph.empty()));
    item(addEffect,L"Add effect to this bus…\tInsert",cleanDraft&&n&&n->canEditInserts()&&bool(addEffect_));
    item(showInserts,L"Edit insert chain",cleanDraft&&n&&n->canEditInserts());item(showAssignment,L"Assign graph recipe",cleanDraft&&n&&n->canAssignGraph());
    item(disconnect,L"Disconnect selected wire\tDelete",wire_>=0&&!canvas_.edges[size_t(wire_)].action.empty()&&!layoutDirty_);
    menu.push_back({0,L""});item(addGroup,L"Add group bus",cleanDraft&&active);item(addReturn,L"Add return bus",cleanDraft&&active);
    menu.push_back({0,L""});item(fit,L"Fit graph\tHome");item(arrange,L"Arrange nodes",cleanDraft);item(saveLayout,L"Save layout",layoutDirty_&&!dirty_);
    const auto before=context_();const auto generation=generation_;const auto selected=selected_;const auto wire=wire_;
    contextOpen_=true;struct Reset{bool &flag;~Reset(){flag=false;}}reset{contextOpen_};
    const auto command=NativeContextMenu::show(window_,at,menu);
    if(command){if(!visible()||pending_||context_()!=before||generation_!=generation||selected_!=selected||wire_!=wire)throw std::runtime_error("Graph changed while the menu was open / action cancelled");action(command,BN_CLICKED);}return true;
  }
  void selectWire(int index){
    clean();wire_=index;resetPluginPorts();setting_=true;choose(wirePicker,index<0?"":std::to_string(index));if(index<0){setting_=false;return;}selected_.clear();choose(nodePicker,"");set(title,L"Connection");
    const auto &e=canvas_.edges.at(size_t(index));choose(source,e.source);choose(destination,e.target);set(output,Json(e.output));set(input,Json(e.input));const auto &a=e.action;const auto type=a.value("kind",std::string{});Json settings=Json::object();
    int mode=0;if(type=="send"){mode=1;settings=bus(a.at("source")).at("sends").at(a.at("index").get<size_t>());}else if(type=="graph-input"){mode=2;settings=data_.at("inputs").at(a.at("index").get<size_t>());}else if(type=="graph-output")mode=3;else if(type=="plugin-input"){mode=4;settings=data_.at("mixer").at("sidechains").at(a.at("index").get<size_t>());}else if(type=="plugin-output")mode=5;else if(type=="plugin-connection"){mode=6;for(const auto &r:data_.at("mixer").at("pluginConnections"))if(r.at("source")==a.at("source")&&r.at("target")==a.at("target")&&r.at("output")==a.at("output")&&r.at("input")==a.at("input")){settings=r;break;}}
    ScreamSeq::NativeInputGate::present(controls_.at(kind),CB_SETCURSEL,mode,0);page_=0;ScreamSeq::NativeInputGate::present(controls_.at(page),CB_SETCURSEL,0,0);set(gain,settings.value("gainDB",0.0));pre_=settings.value("preFader",false);enabled_=settings.value("enabled",true);setting_=false;status(type.empty()?L"This wire follows the chain order / use Inserts or open the subgraph":L"Selected wire / edit its settings then Update wire");refreshRoutePorts();layout();
  }
  std::pair<std::string,Json> route(bool remove,bool updating){
    if(layoutDirty_)throw std::runtime_error("Save layout or Reload before editing routes");const auto *edge=wire_>=0?&canvas_.edges.at(size_t(wire_)):nullptr;
    const Json a=(updating||remove)&&edge?edge->action:Json::object();std::string type=a.value("kind",std::string{});
    const std::array<const char *,7> types{"output","send","graph-input","graph-output","plugin-input","plugin-output","plugin-connection"};const auto mode=SendMessageW(controls_.at(kind),CB_GETCURSEL,0,0);
    if((updating||remove)&&type.empty())throw std::runtime_error("Select an editable routing wire");if(!remove){if(mode<0||mode>=int(types.size()))throw std::runtime_error("Select a route kind");if(updating&&type!=types[size_t(mode)])throw std::runtime_error("Remove and reconnect to change the wire kind");type=types[size_t(mode)];}
    const Node *from=nullptr,*to=nullptr;if(!remove){from=&chosenNode(source);to=&chosenNode(destination);}
    if(type=="output")return {"mixer.bus.set",{{"bus",remove?a.at("source"):Json(busID(*from))},{"output",remove?Json():Json(busID(*to))}}};
    if(type=="send"){
      const auto id=remove?a.at("source").get<std::string>():busID(*from);if(updating&&a.at("source")!=id)throw std::runtime_error("Remove and reconnect to change a send's source");auto list=bus(id).at("sends");
      if(remove)list.erase(a.at("index").get<size_t>());else{Json item={{"target",busID(*to)},{"gainDB",number(gain)},{"preFader",pre_},{"enabled",enabled_}};if(updating)list.at(a.at("index").get<size_t>())=item;else list=GraphCableEdits::addSend(list,busID(*to),number(gain),pre_,enabled_);}return {"mixer.sends.set",{{"bus",id},{"sends",list}}};
    }
    if(type=="graph-input"||type=="graph-output"){
      const bool in=type=="graph-input";const auto key=in?"inputs":"outputs";auto list=data_.at(key);
      if(remove)list.erase(a.at("index").get<size_t>());else{Json item={{"source",busID(*from)},{"target",busID(*to)},{in?"input":"output",port(in?input:output)}};if(in){item["gainDB"]=number(gain);item["preFader"]=pre_;}if(updating)list.at(a.at("index").get<size_t>())=item;else{const auto same=std::find_if(list.begin(),list.end(),[&](const auto &r){return r.at("source")==item.at("source")&&r.at("target")==item.at("target")&&r.at(in?"input":"output")==item.at(in?"input":"output");});if(same==list.end())list.push_back(item);}}
      return {"graph.routes.set",{{key,list}}};
    }
    if(type=="plugin-output"){
      const auto plugin=remove?a.at("plugin").get<std::string>():pluginID(*from);const auto out=remove?a.at("output").get<unsigned>():selectedPluginPort(output);
      if(updating&&(a.at("plugin")!=plugin||a.at("output")!=out))throw std::runtime_error("Remove and reconnect to change the plugin output port");
      std::string implicitMain;for(const auto &p:data_.at("plugins"))if(p.at("id")==plugin&&p.value("isInstrument",false))for(const auto &b:data_.at("mixer").at("buses"))if(b.at("kind")=="master")implicitMain=b.at("id").get<std::string>();
      const auto oldTarget=(remove||updating)?std::optional<std::string>(edge->target):std::nullopt;
      const auto newTarget=remove?std::nullopt:std::optional<std::string>(busID(*to));
      return {"mixer.plugin.route",{{"plugin",plugin},{"output",out},{"targets",GraphCableEdits::pluginTargets(data_.at("mixer").at("instruments"),plugin,out,implicitMain,oldTarget,newTarget)}}};
    }
    if(type=="plugin-connection"){
      if(remove)return {"graph.connections.remove",{{"connections",Json::array({a})}}};
      Json params={{"source",pluginID(*from)},{"output",selectedPluginPort(output)},{"target",pluginID(*to)},{"input",selectedPluginPort(input)},{"gainDB",number(gain)},{"enabled",enabled_}};
      if(updating){auto old=a;old.erase("kind");params["replace"]=std::move(old);}return {"mixer.plugin.connection.set",std::move(params)};
    }
    if(type=="plugin-input"){
      const auto &all=data_.at("mixer").at("sidechains");const auto old=(updating||remove)?all.at(a.at("index").get<size_t>()):Json::object();const auto plugin=remove?old.at("plugin").get<std::string>():pluginID(*to);const auto in=remove?old.at("input").get<unsigned>():selectedPluginPort(input);
      if(updating&&(old.at("plugin")!=plugin||old.at("input")!=in))throw std::runtime_error("Remove and reconnect to change the sidechain destination");Json sources=Json::array();
      for(size_t i=0;i<all.size();++i)if(all[i].at("plugin")==plugin&&all[i].at("input")==in){if(remove&&i==a.at("index").get<size_t>())continue;auto item=all[i];item.erase("plugin");item.erase("input");if(updating&&i==a.at("index").get<size_t>())item={{"source",busID(*from)},{"gainDB",number(gain)},{"preFader",pre_},{"enabled",enabled_}};sources.push_back(item);}
      if(!remove&&!updating)sources.push_back({{"source",busID(*from)},{"gainDB",number(gain)},{"preFader",pre_},{"enabled",enabled_}});return {"mixer.sidechains.set",{{"plugin",plugin},{"input",in},{"sources",sources}}};
    }throw std::runtime_error("Unsupported routing wire");
  }
  void insertAction(int id){clean();const auto &n=chosenNode(nodePicker);if(!n.canEditInserts())throw std::runtime_error("Select the bus or an effect to edit regular inserts; this graph stage was preserved");auto list=bus(n.bus).at("inserts");const auto picked=choice(insertPicker);auto i=std::find(list.begin(),list.end(),picked);
    if(id==insertAdd){const auto effect=choice(effectPicker);if(effect.empty())throw std::runtime_error("Choose an unassigned effect");list.push_back(effect);}
    else{if(i==list.end())throw std::runtime_error("Choose an explicit insert");if(id==insertRemove)list.erase(i);else{const auto offset=i-list.begin(),to=offset+(id==insertUp?-1:1);if(to<0||size_t(to)>=list.size())return;std::swap(list[size_t(offset)],list[size_t(to)]);}}
    mutate("mixer.bus.set",{{"bus",n.bus},{"inserts",list}});
  }
  void assignGraph(bool clearAssignment){if(layoutDirty_)throw std::runtime_error("Save layout or Reload first");const auto &n=chosenNode(nodePicker);if(!n.canAssignGraph())throw std::runtime_error("Row and Persistent graphs are controlled by pattern commands; the Ordinary assignment was preserved");Json p={{"graph",clearAssignment||choice(graphPicker).empty()?Json():Json(choice(graphPicker))},{"amount",number(amount)},{"wet",number(wet)}};
    if(n.instrument.empty()){p["target"]=busID(n);mutate("graph.assign",p);}else{p["instrument"]=n.instrumentIndex;mutate("graph.instrument.assign",p);}}
  void savePositions(){if(dirty_)throw std::runtime_error("Apply or Reload the route draft first");Json positions=Json::array();for(const auto &n:canvas_.nodes)positions.push_back({{"node",n.id},{"x",n.x},{"y",n.y}});mutate("graph.layout.set",{{"positions",positions}});}
  void arrangeNodes(){clean();std::vector<unsigned> levels(canvas_.nodes.size());for(size_t step=0;step<canvas_.nodes.size();++step){bool changed=false;for(const auto &e:canvas_.edges){auto a=canvas_.index.at(e.source),b=canvas_.index.at(e.target);if(levels[b]<levels[a]+1){levels[b]=std::min(unsigned(canvas_.nodes.size()),levels[a]+1);changed=true;}}if(!changed)break;}
    std::map<unsigned,float> y;for(size_t i=0;i<canvas_.nodes.size();++i){auto &n=canvas_.nodes[i];n.x=std::min(100000.0f,28+levels[i]*236.0f);n.y=28+y[levels[i]];y[levels[i]]+=112;}layoutDirty_=true;++generation_;canvas_.fit();savePositions();layout();}
  void scale(float multiplier){auto centre=canvas_.world(canvas_.viewport.x+canvas_.viewport.w/2,canvas_.viewport.y+canvas_.viewport.h/2);canvas_.zoom=std::clamp(canvas_.zoom*multiplier,.15f,2.0f);canvas_.panX=canvas_.viewport.w/2-centre.x*canvas_.zoom;canvas_.panY=canvas_.viewport.h/2-centre.y*canvas_.zoom;canvas_.geometry();requestPaint();}
  void action(int id,unsigned note)override{
    if(setting_)return;if(id==close&&note==BN_CLICKED){hide();return;}
    if(note==EN_CHANGE){changed();return;}if(pending_)return;
    if(id==reload&&note==BN_CLICKED&&unresolved()){reviewResult();return;}requireResolved();
    if(note==CBN_DROPDOWN&&(id==inputPorts||id==outputPorts)){refreshPluginPorts(id==inputPorts?input:output);return;}
    if(note==CBN_SELCHANGE){
      if(id==inputPorts||id==outputPorts){choosePluginPort(id);return;}
      if(id==filter){const auto next=choice(filter);if(dirty_||layoutDirty_){choose(filter,filter_);clean();}filter_=next;rebuild();canvas_.fit();return;}
      if(id==nodePicker){const auto next=choice(id);if(dirty_||layoutDirty_){choose(id,selected_);clean();}selectNode(next);return;}
      if(id==wirePicker){const auto selected=choice(id);if(dirty_||layoutDirty_){choose(id,wire_<0?"":std::to_string(wire_));clean();}selectWire(selected.empty()?-1:std::stoi(selected));return;}
      if(id==page){const auto next=int(SendMessageW(controls_.at(page),CB_GETCURSEL,0,0));if(dirty_||layoutDirty_){ScreamSeq::NativeInputGate::present(controls_.at(page),CB_SETCURSEL,page_,0);clean();}page_=next;inspectFields();return;}
      if(id==kind||id==source||id==destination||id==graphPicker){changed();if(id!=graphPicker)refreshRoutePorts();}return;
    }
    if(note!=BN_CLICKED)return;
    if(id==reload)load();else if(id==fit){canvas_.fit();requestPaint();}else if(id==zoomIn||id==zoomOut)scale(id==zoomIn?1.25f:.8f);else if(id==arrange)arrangeNodes();else if(id==saveLayout)savePositions();
    else if(id==addEffect)browseEffect();
    else if(id==addGroup||id==addReturn){clean();mutate("mixer.bus.add",{{"kind",id==addGroup?"group":"return"}});}
    else if(id==showInserts||id==showAssignment)inspectorPage(id==showInserts?1:2);
    else if(id==enable){clean();mutate("mixer.enable",Json::object());}
    else if(id==pre||id==enabled){changed();if(id==pre)pre_=!pre_;else enabled_=!enabled_;}
    else if(id==connect||id==update||id==disconnect||id==verify){auto [method,p]=route(id==disconnect,id==update||(id==verify&&wire_>=0));mutate(method,p,id==verify);}
    else if(id>=insertAdd&&id<=insertDown)insertAction(id);
    else if(id==assign||id==clear)assignGraph(id==clear);
    else if(id==open){clean();current();inspect_(chosenNode(nodePicker));}
  }
  bool key(WPARAM key,bool ctrl,bool shift)override{
    if(key==VK_ESCAPE){keyboardMove_=false;if(pending_||unresolved())hide();else if(dirty_||layoutDirty_)load();else hide();return true;}if(pending_)return false;
    if(ctrl&&key=='R'){if(unresolved())reviewResult();else load();return true;}requireResolved();if(key==VK_F6){SetFocus(GetFocus()==window_?controls_.at(nodePicker):window_);return true;}
    if(ctrl&&key==VK_RETURN){if(layoutDirty_)savePositions();else action(page_==2?assign:wire_>=0?update:connect,BN_CLICKED);return true;}
    if(GetFocus()!=window_)return false;
    if(ctrl&&(key=='Z'||key=='Y')){clean();mutate(key=='Y'||shift?"history.redo":"history.undo",{{"domain","document"}});return true;}
    if(key==VK_INSERT){action(addEffect,BN_CLICKED);return true;}
    if(key==VK_APPS||(shift&&key==VK_F10)){contextMenu(window_,{-1,-1});return true;}
    if(key==VK_HOME){canvas_.fit();requestPaint();return true;}if(key==VK_OEM_PLUS||key==VK_ADD){scale(1.25f);return true;}if(key==VK_OEM_MINUS||key==VK_SUBTRACT){scale(.8f);return true;}
    if(key==VK_DELETE&&wire_>=0){action(disconnect,BN_CLICKED);return true;}if(key==VK_RETURN){action(open,BN_CLICKED);return true;}
    if(key>=VK_LEFT&&key<=VK_DOWN){if(dirty_)clean();if(auto n=canvas_.find(selected_)){const float amount=shift?16:4;n->x=std::clamp(n->x+(key==VK_RIGHT?amount:key==VK_LEFT?-amount:0),0.0f,100000.0f);n->y=std::clamp(n->y+(key==VK_DOWN?amount:key==VK_UP?-amount:0),0.0f,100000.0f);layoutDirty_=keyboardMove_=true;++generation_;canvas_.geometry();status(L"Moving node / release the arrow key to save one Undo step");layout();}return true;}return false;
  }
  bool keyUp(WPARAM key)override{
    if(keyboardMove_&&key>=VK_LEFT&&key<=VK_DOWN){keyboardMove_=false;if(layoutDirty_&&!pending_)savePositions();return true;}return false;
  }
  void mouse(UINT message,float x,float y,WPARAM)override{
    pointer_={x,y};if(message==WM_CAPTURECHANGED){drag_=0;return;}if(pending_||unresolved()||readbackNeedsReload_)return;
    if(message==WM_LBUTTONDBLCLK&&canvas_.viewport.contains(x,y)){clean();if(const auto i=canvas_.nodeAt(x,y);i>=0){selectNode(canvas_.nodes[size_t(i)].id);action(open,BN_CLICKED);}return;}
    if(message==WM_LBUTTONDOWN&&canvas_.viewport.contains(x,y)){SetFocus(window_);
      if(wire_>=0&&!canvas_.edges.at(size_t(wire_)).action.empty()){
        const auto &edge=canvas_.edges.at(size_t(wire_));const auto h=edge.points[28];
        if(std::hypot(x-h.x,y-h.y)<=7){clean();dragNode_=edge.source;drag_=4;SetCapture(window_);requestPaint();return;}
      }
      const auto i=canvas_.nodeAt(x,y);
      if(i>=0){const auto n=canvas_.nodes[size_t(i)];const auto cy=n.rect.y+n.rect.h/2;
        const bool out=std::abs(x-(n.rect.x+n.rect.w))<10&&std::abs(y-cy)<10;
        const bool in=std::abs(x-n.rect.x)<10&&std::abs(y-cy)<10;
        if(out||in){if(layoutDirty_)clean();}else{if(dirty_)clean();if(!layoutDirty_)selectNode(n.id);else if(n.id!=selected_)clean();}
        dragNode_=n.id;dragOrigin_={n.x,n.y};dragStart_={x,y};drag_=out?3:in?5:1;}
      else if(const auto edge=canvas_.edgeAt(x,y);edge>=0&&!layoutDirty_){selectWire(edge);return;}else{drag_=2;dragStart_={x,y};dragOrigin_={canvas_.panX,canvas_.panY};}SetCapture(window_);
    }else if(message==WM_MOUSEMOVE&&drag_){if(drag_==1){auto n=canvas_.find(dragNode_);if(n){if(!layoutDirty_&&std::hypot(x-dragStart_.x,y-dragStart_.y)<4)return;n->x=std::clamp(dragOrigin_.x+(x-dragStart_.x)/canvas_.zoom,0.0f,100000.0f);n->y=std::clamp(dragOrigin_.y+(y-dragStart_.y)/canvas_.zoom,0.0f,100000.0f);if(n->x!=dragOrigin_.x||n->y!=dragOrigin_.y){layoutDirty_=true;++generation_;}}}else if(drag_==2){canvas_.panX=dragOrigin_.x+x-dragStart_.x;canvas_.panY=dragOrigin_.y+y-dragStart_.y;}canvas_.geometry();}
    else if(message==WM_LBUTTONUP&&drag_){const auto mode=drag_;drag_=0;ReleaseCapture();if(mode==3||mode==4||mode==5){const auto target=canvas_.nodeAt(x,y);if(target>=0){
        const auto drop=canvas_.nodes[size_t(target)].id;choose(source,mode==5?drop:dragNode_);choose(destination,mode==5?dragNode_:drop);
        // Socket identity determines every new cable. Explicit compatible
        // drafts retain their controls; selecting a wire never changes what
        // a later fresh socket drag addresses.
        if(mode!=4){
          const auto &from=chosenNode(source),&to=chosenNode(destination);
          const auto previous=int(SendMessageW(controls_.at(kind),CB_GETCURSEL,0,0));
          const int routeKind=GraphCableEdits::freshSongRoute(!from.plugin.empty(),!to.plugin.empty(),!from.graph.empty(),!to.graph.empty(),previous,dirty_);
          const bool keepDraft=dirty_&&previous==routeKind;
          setting_=true;ScreamSeq::NativeInputGate::present(controls_.at(kind),CB_SETCURSEL,routeKind,0);
          wire_=-1;choose(wirePicker,"");
          if(!keepDraft){resetPluginPorts();set(gain,L"0");set(input,L"0");set(output,L"0");pre_=false;enabled_=true;}setting_=false;
        }
        dirty_=true;++generation_;refreshRoutePorts();auto [method,p]=route(false,mode==4);mutate(method,p);
      }}else if(mode==1&&layoutDirty_)savePositions();layout();}
    requestPaint();
  }
  void layout()override{
    if(!ready_)return;const auto [w,h]=size();const float x=w-322,cw=306;canvas_.viewport={12,82,w-352,h-170};canvas_.geometry();
    place(filter,12,12,220,240);place(enable,240,12,114,26);place(addEffect,362,12,124,26);place(addGroup,494,12,90,26);place(addReturn,592,12,90,26);place(reload,w-226,12,120,26);place(close,w-98,12,86,26);
    place(fit,12,46,52,26);place(zoomOut,70,46,32,26);place(zoomIn,108,46,32,26);place(arrange,148,46,82,26);place(saveLayout,238,46,108,26);place(title,x,48,cw,24);
    place(nodeLabel,x,82,cw,18);place(nodePicker,x,104,cw-78,260);place(open,x+cw-72,104,72,26);place(page,x,140,cw,240);
    for(int id:{wirePicker,kind,source,destination,gain,input,output,pre,enabled,connect,update,disconnect,verify,insertPicker,effectPicker,insertAdd,insertRemove,insertUp,insertDown,graphPicker,amount,wet,assign,clear,wireLabel,sourceLabel,destinationLabel,gainLabel,inputLabel,outputLabel,insertLabel,effectLabel,graphLabel,amountLabel,wetLabel})ShowWindow(controls_.at(id),SW_HIDE);
    if(page_==0){place(wireLabel,x,182,cw,18);place(wirePicker,x,204,cw,260);place(kind,x,240,cw,220);place(sourceLabel,x,278,cw,18);place(source,x,300,cw,240);place(destinationLabel,x,336,cw,18);place(destination,x,358,cw,240);place(gainLabel,x,397,92,18);place(outputLabel,x+104,397,92,18);place(inputLabel,x+208,397,92,18);place(gain,x,419,94,26);place(output,x+104,419,94,26);place(input,x+208,419,98,26);place(pre,x,456,146,26);place(enabled,x+154,456,152,26);place(connect,x,494,146,26);place(update,x+154,494,152,26);place(verify,x,530,146,26);place(disconnect,x+154,530,152,26);}
    for(int id:{input,output}){
      NativeInputGate::present(controls_.at(pickerForPort(id)),CB_SETDROPPEDWIDTH,WPARAM(360*GetDpiForWindow(window_)/96),0);
      const bool pluginPort=page_==0&&pluginPortMode(id);
      if(pluginPort){ShowWindow(controls_.at(id),SW_HIDE);place(pickerForPort(id),x+(id==input?208:104),419,id==input?98:94,260);}
      else ShowWindow(controls_.at(pickerForPort(id)),SW_HIDE);
    }
    if(page_==1){place(insertLabel,x,188,cw,36);place(insertPicker,x,230,cw,240);place(insertUp,x,266,72,26);place(insertDown,x+78,266,72,26);place(insertRemove,x+158,266,148,26);place(effectLabel,x,310,cw,40);place(effectPicker,x,358,cw,260);place(insertAdd,x,398,cw,26);}
    if(page_==2){place(graphLabel,x,188,cw,40);place(graphPicker,x,238,cw,260);place(amountLabel,x,283,145,18);place(wetLabel,x+158,283,145,18);place(amount,x,307,145,26);place(wet,x+158,307,148,26);place(assign,x,350,145,26);place(clear,x+158,350,148,26);}
    place(helpLabel,12,h-76,w-24,20);place(statusLabel,12,h-52,w-24,42);for(const auto &[id,control]:controls_)if(id<title&&id!=close)EnableWindow(control,!pending_);
    const auto n=canvas_.find(selected_);set(open,n&&!n->plugin.empty()?L"Params…":L"Open");EnableWindow(controls_.at(addEffect),!pending_&&!dirty_&&!layoutDirty_&&n&&n->canEditInserts()&&bool(addEffect_));
    for(int id:{addGroup,addReturn})EnableWindow(controls_.at(id),!pending_&&!dirty_&&!layoutDirty_&&!data_.value("mixer",Json::object()).value("buses",Json::array()).empty());
    EnableWindow(controls_.at(enable),!pending_&&data_.value("mixer",Json::object()).value("buses",Json::array()).empty());EnableWindow(controls_.at(saveLayout),!pending_&&layoutDirty_);EnableWindow(controls_.at(open),!pending_&&n);for(int id:{insertPicker,effectPicker,insertAdd,insertRemove,insertUp,insertDown})EnableWindow(controls_.at(id),!pending_&&n&&n->canEditInserts());for(int id:{assign,clear,graphPicker,amount,wet})EnableWindow(controls_.at(id),!pending_&&n&&n->canAssignGraph());
    const bool editable=wire_>=0&&!canvas_.edges.at(size_t(wire_)).action.empty();for(int id:{update,disconnect})EnableWindow(controls_.at(id),!pending_&&editable);ScreamSeq::NativeInputGate::text(controls_.at(pre),pre_?L"Pre-fader: on":L"Pre-fader: off");ScreamSeq::NativeInputGate::text(controls_.at(enabled),enabled_?L"Route enabled":L"Route disabled");
    const auto mode=SendMessageW(controls_.at(kind),CB_GETCURSEL,0,0);EnableWindow(controls_.at(gain),!pending_&&(mode==1||mode==2||mode==4||mode==6));EnableWindow(controls_.at(pre),!pending_&&(mode==1||mode==2||mode==4));EnableWindow(controls_.at(enabled),!pending_&&(mode==1||mode==4||mode==6));EnableWindow(controls_.at(input),!pending_&&(mode==2||mode==4||mode==6));EnableWindow(controls_.at(output),!pending_&&(mode==3||mode==5||mode==6));
    if(layoutDirty_)for(int id:{wirePicker,kind,source,destination,gain,input,output,pre,enabled,connect,update,disconnect,verify,insertPicker,effectPicker,insertAdd,insertRemove,insertUp,insertDown,graphPicker,amount,wet,assign,clear})EnableWindow(controls_.at(id),FALSE);
    if(layoutDirty_)for(int id:{inputPorts,outputPorts})EnableWindow(controls_.at(id),FALSE);
    EnableWindow(controls_.at(open),!pending_&&n&&(!n->bus.empty()||!n->plugin.empty()||!n->graph.empty()));
    if(unresolved()||readbackNeedsReload_)for(const auto &[id,control]:controls_)if(id<title&&id!=reload&&id!=close)EnableWindow(control,FALSE);
    set(reload,unresolved()?L"Review result":L"Reload");EnableWindow(controls_.at(reload),!pending_);
  }
  void paint(RenderSurface &s)override{
    const auto [w,h]=size();s.fill(0,0,w,h,0x18222d);const auto v=canvas_.viewport;s.fill(v.x,v.y,v.w,v.h,0x111b25);s.clip(v.x,v.y,v.w,v.h);
    const auto visible=[&](const WorkspaceRect &r){return r.x+r.w>=v.x&&r.x<=v.x+v.w&&r.y+r.h>=v.y&&r.y<=v.y+v.h;};
    for(size_t i=0;i<canvas_.edges.size();++i){const auto &e=canvas_.edges[i];if(!visible(e.bounds))continue;const auto type=e.action.value("kind",std::string{});const auto color=int(i)==wire_?0xffd08a:!e.enabled?0x465463:type=="plugin-input"||type=="graph-input"?0xad98df:type=="send"?0x70b5d8:0x69b8a8;
      for(size_t j=1;j<e.points.size();++j)s.line(e.points[j-1].x,e.points[j-1].y,e.points[j].x,e.points[j].y,color,int(i)==wire_?2.5f:1.25f);if(canvas_.zoom>=.55f&&!e.label.empty()){const auto p=e.points[16];s.uiText(e.label,p.x-70,p.y-18,180,color);}}
    for(const auto &n:canvas_.nodes){if(!visible(n.rect))continue;const auto r=n.rect;const auto accent=n.id==selected_?0xffd08a:n.kind=="graph"?0xad98df:n.kind=="master"?0x70d4bf:n.kind=="plugin"||n.kind=="instrument"?0x76b7e3:0x607990;
      s.fill(r.x,r.y,r.w,r.h,n.id==selected_?0x29394a:0x21303e);s.fill(r.x,r.y,3,r.h,accent);s.outline(r.x,r.y,r.w,r.h,accent);
      if(canvas_.zoom>=.45f){s.clip(r.x+6,r.y+3,std::max(1.0f,r.w-12),std::max(1.0f,r.h-6));s.uiText(n.name,r.x+9,r.y+8,r.w-18,0xe0ecf4);if(canvas_.zoom>=.7f)s.uiText(n.detail,r.x+9,r.y+32,r.w-18,0x9cb1c2);s.unclip();}
      const float cy=r.y+r.h/2;s.fill(r.x-3,cy-3,6,6,accent);s.fill(r.x+r.w-3,cy-3,6,6,accent);s.line(r.x+r.w-8,cy-3,r.x+r.w-4,cy,accent);s.line(r.x+r.w-8,cy+3,r.x+r.w-4,cy,accent);
    }
    if(wire_>=0&&!canvas_.edges.at(size_t(wire_)).action.empty()){const auto h=canvas_.edges[size_t(wire_)].points[28];s.fill(h.x-5,h.y-5,10,10,0x18222d);s.outline(h.x-5,h.y-5,10,10,0xffd08a);}
    if(drag_==3||drag_==4||drag_==5){const auto n=canvas_.find(dragNode_);if(n){const auto points=drag_==5?GraphCanvas::curve(pointer_,{n->rect.x,n->rect.y+n->rect.h/2}):GraphCanvas::curve({n->rect.x+n->rect.w,n->rect.y+n->rect.h/2},pointer_);for(size_t i=1;i<points.size();++i)s.line(points[i-1].x,points[i-1].y,points[i].x,points[i].y,0xffd08a,2);}}
    if(canvas_.nodes.empty())s.uiText(L"Enable routing to connect channels, instruments and effects",v.x+20,v.y+24,v.w-40,0x8fa8ba);s.unclip();s.outline(v.x,v.y,v.w,v.h,GetFocus()==window_?0x6edac5:0x334757);
  }
public:
  SongRoutingWindow(HWND owner,Request request,Context context,std::function<void(const Node &)> inspect,NativeWriteCompletion::Write write,std::function<void(const std::string &,const std::string &,const std::string &)> browseEffectCallback={},std::function<bool()> canRefresh={}):NativeToolWindow(owner),request_(std::move(request)),context_(std::move(context)),inspect_(std::move(inspect)),addEffect_(std::move(browseEffectCallback)),canRefresh_(std::move(canRefresh)),write_(std::move(write)){
    minimumWidth_=1040;minimumHeight_=680;create(L"ScreamSeq.SongRouting",L"Mixing graph",1280,800,true);
    for(int id:{filter,nodePicker,wirePicker,kind,source,destination,page,insertPicker,effectPicker,graphPicker,inputPorts,outputPorts})combo(id);
    for(auto name:{L"Main output",L"Send",L"Graph sidechain",L"Graph auxiliary",L"Plugin sidechain",L"Plugin output",L"Plugin cable"})ScreamSeq::NativeInputGate::present(controls_.at(kind),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name));ScreamSeq::NativeInputGate::present(controls_.at(kind),CB_SETCURSEL,0,0);
    for(auto name:{L"Connections",L"Insert chain",L"Ordinary graph"})ScreamSeq::NativeInputGate::present(controls_.at(page),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name));ScreamSeq::NativeInputGate::present(controls_.at(page),CB_SETCURSEL,0,0);
    for(int id:{gain,input,output,amount,wet})edit(id,L"",32);
    for(auto [id,name]:std::initializer_list<std::pair<int,const wchar_t *>>{{addEffect,L"Add effect…"},{addGroup,L"Add group"},{addReturn,L"Add return"},{pre,L"Pre-fader: off"},{enabled,L"Route enabled"},{connect,L"Connect"},{update,L"Update wire"},{disconnect,L"Disconnect"},{reload,L"Reload"},{fit,L"Fit"},{zoomOut,L"−"},{zoomIn,L"+"},{arrange,L"Arrange"},{close,L"Close"},{open,L"Open"},{enable,L"Enable routing"},{insertAdd,L"Append effect"},{insertRemove,L"Remove insert"},{insertUp,L"Up"},{insertDown,L"Down"},{assign,L"Assign graph"},{clear,L"Clear graph"},{saveLayout,L"Save layout"},{verify,L"Verify route"}})button(id,name);
    for(auto [id,name]:std::initializer_list<std::pair<int,const wchar_t *>>{{title,L"Song routing"},{nodeLabel,L"Selected stage"},{wireLabel,L"Connection"},{sourceLabel,L"From"},{destinationLabel,L"To"},{gainLabel,L"Gain / dB"},{inputLabel,L"Input port"},{outputLabel,L"Output port"},{insertLabel,L"Explicit inserts / processed in this order"},{effectLabel,L"Unassigned effects / otherwise processed on master"},{graphLabel,L"Each channel or sample voice gets an independent copy"},{amountLabel,L"Amount / 0…1"},{wetLabel,L"Wet / 0…1"},{statusLabel,L""}})label(id,name);
    label(helpLabel,L"Right-click: actions · Double-click / Enter: inspect · Insert: add effect · Wheel: pan · Ctrl+wheel: zoom · Home: fit");
    finish();load();canvas_.fit();
  }
  void show(){NativeToolWindow::show();SetTimer(window_,7,250,nullptr);SetFocus(window_);}
  void hide()override{KillTimer(window_,7);NativeToolWindow::hide();}
  Json snapshot()const{
    const auto catalog=[](const PortCatalog &value){return Json{{"plugin",value.plugin},{"document",value.document},{"revision",value.revision},{"error",value.error},{"ports",value.ports},{"indices",value.indices}};};
    return {{"completion",completion_.snapshot()},{"report",report_},{"readbackNeedsReload",readbackNeedsReload_},{"inputPorts",catalog(inputCatalog_)},{"outputPorts",catalog(outputCatalog_)},{"generation",generation_},{"fields",{{"gain",utf8(field(gain))},{"input",utf8(field(input))},{"output",utf8(field(output))}}},{"visible",visible()},{"document",document_},{"expectedRevision",revision_},{"stale",context_()!=std::pair(document_,revision_)},{"pending",pending_},{"draft",dirty_},{"layoutDraft",layoutDirty_},{"selected",selected_},{"wire",wire_},{"filter",filter_},{"page",page_},{"status",utf8(status_)},{"canvas",canvas_.snapshot()}};
  }
};
}
