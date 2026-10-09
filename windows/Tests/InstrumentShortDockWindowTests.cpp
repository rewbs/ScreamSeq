#include "common/stdafx.h"
#include "../App/InstrumentEnvelopeWindow.hpp"
#include "PrivateGuiTest.hpp"
#include <set>

namespace {
using Json=ScreamSeq::Api::Json;
void require(bool value,const char *message){if(!value)throw std::runtime_error(message);}
struct Owner {
  HWND window{};
  Owner(){WNDCLASSW kind{};kind.lpfnWndProc=DefWindowProcW;kind.hInstance=GetModuleHandleW(nullptr);kind.lpszClassName=L"ScreamSeq.InstrumentShort.TestOwner";RegisterClassW(&kind);
    window=CreateWindowExW(0,kind.lpszClassName,L"Owned short instrument host",WS_OVERLAPPEDWINDOW|WS_VISIBLE|WS_CLIPCHILDREN,0,0,1200,900,nullptr,nullptr,kind.hInstance,nullptr);
    require(window,"Create instrument short test host");ScreamSeq::Tests::ownGuiWindow(window);}
  ~Owner(){if(window)DestroyWindow(window);}
};
std::wstring text(HWND control){std::wstring result(size_t(GetWindowTextLengthW(control))+1,0);GetWindowTextW(control,result.data(),int(result.size()));result.resize(wcslen(result.c_str()));return result;}
std::pair<DWORD,DWORD> selection(HWND control){DWORD first=0,last=0;SendMessageW(control,EM_GETSEL,reinterpret_cast<WPARAM>(&first),reinterpret_cast<LPARAM>(&last));return {first,last};}
void sizeClient(HWND window,int width,int height){
  const auto dpi=GetDpiForWindow(window);RECT frame{0,0,MulDiv(width,dpi,96),MulDiv(height,dpi,96)};
  require(AdjustWindowRectExForDpi(&frame,DWORD(GetWindowLongPtrW(window,GWL_STYLE)),FALSE,DWORD(GetWindowLongPtrW(window,GWL_EXSTYLE)),dpi),"Calculate instrument client frame");
  require(SetWindowPos(window,nullptr,0,0,frame.right-frame.left,frame.bottom-frame.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE),"Size wide instrument fixture");
}
struct Form {
  ScreamSeq::InstrumentEnvelopeWindow::Context current{"owned-song","r1",1,1,
    Json::array({{{"index",1},{"id","instrument:1"},{"name","Held instrument"}}}),
    Json::array({{{"index",1},{"id","sample:1"},{"name","Owned sample"}}})};
  Json envelope={{"editable",true},{"maxPoints",240},{"points",Json::array({Json::array({0,32}),Json::array({16,48}),Json::array({48,0})})},
    {"enabled",true},{"sustain",false},{"loop",false},{"carry",false},{"filter",false},
    {"loopStart",0},{"loopEnd",2},{"sustainPoint",0},{"sustainEnd",0},{"releaseNode",255}};
  Json properties={{"name","Held instrument"},{"volume",64},{"pan",128},{"fadeout",256},{"nna",0},{"dct",0},{"dna",0},{"mapping",Json::array()}};
  unsigned reads=0,writes=0,auditions=0,toolCalls=0;
  Json lastTool;
  std::function<void()> duringPatch,duringRead;
  ScreamSeq::InstrumentEnvelopeWindow tool;
  explicit Form(HWND owner):tool(owner,[this](const std::string &method,const Json &parameters){return request(method,parameters);},[this]{return current;},
    [this](unsigned index,const std::string &id,const std::string &document,const std::string &revision){require(index==1&&id=="instrument:1"&&document==current.document&&revision==current.revision,"Audition redirected the captured target");++auditions;},[](unsigned,const std::string &){}){
    for(int i=0;i<128;++i)properties["mapping"].push_back(1);ScreamSeq::Tests::ownGuiWindow(tool.window());
  }
  Json request(const std::string &method,const Json &parameters){
    if(method=="instrument.envelope.get"){++reads;if(duringRead)duringRead();return envelope;}
    if(method=="instrument.get"){++reads;if(duringRead)duringRead();return properties;}
    if(method=="instrument.patch"){
      require(parameters.at("expectedRevision")==current.revision&&parameters.at("instrument")==1,"Apply lost captured revision/instrument");++writes;
      if(duringPatch)duringPatch();
      for(auto entry=parameters.at("values").begin();entry!=parameters.at("values").end();++entry){if(entry.key()=="envelope")continue;if(envelope.contains(entry.key()))envelope[entry.key()]=entry.value();else properties[entry.key()]=entry.value();}
      current.revision="r"+std::to_string(writes+1);return Json::object();
    }
    if(method=="instrument.envelope.copy")return {{"points",Json::array()}};
    if(method=="instrument.envelope.transform"){++toolCalls;lastTool=parameters;return {{"wouldChange",false}};}
    throw std::runtime_error("Unexpected short instrument request: "+method);
  }
  HWND control(int id)const{const auto found=GetDlgItem(tool.window(),id);require(found,"Original instrument control is no longer a direct retained child");return found;}
  void press(int id){require(IsWindowVisible(control(id))&&IsWindowEnabled(control(id)),"Press requires a visible enabled native control");SendMessageW(tool.window(),WM_COMMAND,MAKEWPARAM(id,BN_CLICKED),reinterpret_cast<LPARAM>(control(id)));}
  void page(int index){press(4600+index);}
  void choose(int id,int index){SendMessageW(control(id),CB_SETCURSEL,index,0);SendMessageW(tool.window(),WM_COMMAND,MAKEWPARAM(id,CBN_SELCHANGE),reinterpret_cast<LPARAM>(control(id)));}
  void field(int id,const wchar_t *value){require(SetWindowTextW(control(id),value),"Set retained instrument field");}
  void open(HWND owner){tool.openAt();tool.dock(owner);tool.dockBounds(0,0,440,300);SetActiveWindow(owner);}
};

std::set<int> geometry(Form &form){
  const auto snapshot=form.tool.snapshot();const auto window=form.tool.window();RECT client{};GetClientRect(window,&client);
  std::set<int> ids;std::vector<RECT> rectangles;
  for(const auto &entry:snapshot.at("controlBounds")){
    const auto &r=entry.at("bounds");RECT rect{r[0].get<LONG>(),r[1].get<LONG>(),r[2].get<LONG>(),r[3].get<LONG>()};
    require(rect.left>=0&&rect.top>=0&&rect.right<=client.right&&rect.bottom<=client.bottom&&rect.left<rect.right&&rect.top<rect.bottom,"Short instrument child is outside its body");
    for(const auto &other:rectangles){RECT intersection{};require(!IntersectRect(&intersection,&rect,&other),"Short instrument native controls overlap");}
    rectangles.push_back(rect);ids.insert(entry.at("id").get<int>());
  }
  for(int id:{4450,4451,4452,4453,4454})require(ids.contains(id),"Fixed Apply/Reload/Cursor/Audition/Close disappeared");
  if(snapshot.at("canvasVisible").get<bool>()){
    const auto &r=snapshot.at("canvas");const float scale=GetDpiForWindow(window)/96.f;
    RECT canvas{LONG(r[0].get<float>()*scale),LONG(r[1].get<float>()*scale),LONG((r[0].get<float>()+r[2].get<float>())*scale),LONG((r[1].get<float>()+r[3].get<float>())*scale)};
    require(r[3].get<float>()>=100&&canvas.bottom<=client.bottom,"Short envelope lost its usable canvas");
    for(const auto &rect:rectangles){RECT intersection{};require(!IntersectRect(&intersection,&rect,&canvas),"Short envelope canvas covers a native control");}
  }
  return ids;
}

void allPagesAndActions(Owner &owner){
  Form form(owner.window);form.open(owner.window);const auto native=form.tool.window();const auto generation=form.tool.snapshot().at("generation");const auto reads=form.reads;
  for(const auto [width,height]:{std::pair{440,300},std::pair{460,310},std::pair{650,499}}){
    form.tool.dockBounds(0,0,float(width),float(height));std::set<int> visited;
    for(int page=0;page<5;++page){form.page(page);if(page==2)form.choose(4429,5);const auto current=geometry(form);visited.insert(current.begin(),current.end());}
    form.page(0);form.press(4605);require(!form.tool.snapshot().at("canvasVisible").get<bool>(),"Options should not retain a hidden interactive canvas");auto options=geometry(form);visited.insert(options.begin(),options.end());
    for(int id:{4401,4402,4403,4404,4405,4406,4407,4408,4409,4410,4411,4412,4413,4414,4415,4416,4417,4418,4419,4420,4421,4422,4423,4424,4425,4426,4427,4428,4429,4430,4431,4432,4433,4434,4435,4436,4437,4438,4439,4440,4441,4442,4443,4444,4445,4446,4447,4448,4449,4450,4451,4452,4453,4454,4455,4456})
      require(visited.contains(id),"An original instrument action/field is unreachable in short pages");
    form.press(4605);geometry(form);require(form.tool.snapshot().at("canvasVisible").get<bool>(),"Curve did not return from Options");
  }
  require(form.tool.window()==native&&form.reads==reads&&form.writes==0&&form.tool.snapshot().at("generation")==generation,"Presentation navigation reloaded or edited the instrument");
  LOGFONTW font{};require(GetObjectW(reinterpret_cast<HFONT>(SendMessageW(form.control(4413),WM_GETFONT,0,0)),sizeof(font),&font),"Read short editor font");
  require(font.lfHeight==-int(13*GetDpiForWindow(native)/96.f),"Short dock shrank the normal native font");
  form.page(2);form.press(4438);require(form.toolCalls==1&&form.lastTool.at("dryRun")==true&&form.lastTool.at("operation")=="sine"&&form.lastTool.at("options").at("spacing")==4,"Five-field tool Preview is not reachable with retained controls");
  form.press(4454);require(form.auditions==1,"Short fixed Audition is unavailable");
}

void rawDraftFocusAndPlacement(Owner &owner){
  Form form(owner.window);form.tool.openAt();sizeClient(form.tool.window(),440,500);form.tool.dock(owner.window);form.tool.dockBounds(0,0,440,300);SetActiveWindow(owner.window);
  const auto tick=form.control(4413),value=form.control(4414),native=form.tool.window();
  form.field(4413,L"unfinished tick");form.field(4414,L"37.");SetFocus(tick);SendMessageW(tick,EM_SETSEL,2,7);
  const auto captured=form.tool.snapshot();const auto reads=form.reads;
  form.tool.dockBounds(0,0,460,310);require(GetFocus()==tick&&selection(tick)==std::pair<DWORD,DWORD>{2,7},"Short resize lost visible raw field focus/caret");
  form.press(4605);require(IsWindowVisible(GetFocus())&&GetFocus()!=tick,"Options left focus on a hidden field");form.press(4605);
  for(int page=1;page<5;++page){form.page(page);geometry(form);}form.page(0);
  SetFocus(tick);form.tool.floatWindow();require(!form.tool.snapshot().at("shortDock").get<bool>()&&GetFocus()==tick,"Floating lost visible field focus or kept short body");
  require(IsWindowVisible(tick)&&selection(tick)==std::pair<DWORD,DWORD>{2,7}&&text(tick)==L"unfinished tick","Compact floating view hid or replaced the raw point field/caret");
  MINMAXINFO minimum{};SendMessageW(native,WM_GETMINMAXINFO,0,reinterpret_cast<LPARAM>(&minimum));const auto dpi=GetDpiForWindow(native);RECT bounds{0,0,MulDiv(440,dpi,96),MulDiv(500,dpi,96)};
  require(AdjustWindowRectExForDpi(&bounds,DWORD(GetWindowLongPtrW(native,GWL_STYLE)),FALSE,DWORD(GetWindowLongPtrW(native,GWL_EXSTYLE)),dpi),"Calculate unchanged floating minimum");
  require(minimum.ptMinTrackSize.x==bounds.right-bounds.left&&minimum.ptMinTrackSize.y==bounds.bottom-bounds.top,"Short mode changed the floating minimum");
  form.tool.dock(owner.window);form.tool.dockBounds(0,0,440,300);form.tool.hide();form.tool.show();
  const auto after=form.tool.snapshot();require(form.tool.window()==native&&form.control(4413)==tick&&form.control(4414)==value&&form.reads==reads&&form.writes==0,"Placement replaced controls or reloaded draft");
  require(text(tick)==L"unfinished tick"&&text(value)==L"37."&&selection(tick)==std::pair<DWORD,DWORD>{2,7}&&after.at("generation")==captured.at("generation"),"Placement/pages changed raw draft or its generation");
  for(const auto *key:{"document","expectedRevision","instrument","envelope","mapping","start","end"})require(after.at(key)==captured.at(key),"Presentation redirected captured instrument data");
  form.current.document="new-song";form.current.revision="new-revision";require(!form.tool.followCursor()&&form.tool.snapshot().at("document")=="owned-song","Automatic follow discarded short draft");
}

void revealFocusedWideSection(Owner &owner){
  Form form(owner.window);form.tool.openAt();sizeClient(form.tool.window(),1200,840);form.choose(4402,2);form.choose(4429,5);
  struct FocusCase {int id;const char *page;bool options;};
  for(const auto test:std::initializer_list<FocusCase>{{4439,"properties",false},{4436,"tools",false},{4418,"points",false},{4446,"keymap",false},{4456,"keymap",false},
      {4408,"envelope",true},{4409,"envelope",true},{4404,"envelope",true},{4424,"envelope",true},{4425,"envelope",true},{4428,"envelope",true},{4413,"points",false}}){
    if(form.tool.docked())form.tool.floatWindow();sizeClient(form.tool.window(),1200,840);
    require(!form.tool.snapshot().at("compactLayout").get<bool>(),"Focused-field fixture is not in the original wide layout");
    const auto field=form.control(test.id);require(IsWindowVisible(field)&&IsWindowEnabled(field),"Wide focused-field fixture is unavailable");
    wchar_t type[32]{};GetClassNameW(field,type,32);const bool edit=_wcsicmp(type,L"EDIT")==0;
    if(edit)form.field(test.id,L"retained raw -001.");
    SetActiveWindow(form.tool.window());SetFocus(field);require(GetFocus()==field,"Focus wide instrument field");if(edit)SendMessageW(field,EM_SETSEL,3,10);
    const auto raw=text(field);const auto before=form.tool.snapshot();const auto reads=form.reads;
    form.tool.dock(owner.window);form.tool.dockBounds(0,0,440,300);const auto after=form.tool.snapshot();
    require(after.at("page")==test.page&&IsWindowVisible(field)&&GetFocus()==field&&text(field)==raw,"First short reflow hid the focused wide section or changed its field");
    if(test.options)require(after.at("shortEnvelopeOptions").get<bool>()&&!after.at("canvasVisible").get<bool>(),"Wide Options control did not reveal the retained Options view");
    if(edit)require(selection(field)==std::pair<DWORD,DWORD>{3,10},"First short reflow changed the focused raw edit selection");
    for(const auto *key:{"generation","document","expectedRevision","instrument","envelope","mapping","start","end","selectedPoint"})require(after.at(key)==before.at(key),"Focused section reveal altered captured data or its draft generation");
    require(form.reads==reads&&form.writes==0,"Focused section reveal reloaded or applied the instrument");geometry(form);
  }
}

void zoomedCanvasMouseMapping(Owner &owner){
  Form form(owner.window);form.open(owner.window);form.press(4605);form.press(4426);form.press(4427);form.press(4605);
  const auto initial=form.tool.snapshot();require(initial.at("start")==8&&initial.at("end")==40,"Zoom/pan fixture did not retain a nontrivial tick range");
  const auto canvas=initial.at("canvas");const double scale=GetDpiForWindow(form.tool.window())/96.;
  const auto pixels=[&](double x,double y){return MAKELPARAM(int(std::lround(x*scale)),int(std::lround(y*scale)));};
  const auto point=[&](double tick,double value){return pixels(canvas[0].get<double>()+(tick-8)/32*canvas[2].get<double>(),canvas[1].get<double>()+(1-value/64)*canvas[3].get<double>());};
  const auto handle=initial.at("handles").at(1);const auto window=form.tool.window();
  SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,pixels(handle.at("x").get<double>(),handle.at("y").get<double>()));
  require(form.tool.snapshot().at("selectedPoint")==1&&GetFocus()==window,"DPI-scaled zoomed handle hit selected the wrong point");
  SendMessageW(window,WM_MOUSEMOVE,MK_LBUTTON,point(20,40));SendMessageW(window,WM_LBUTTONUP,0,point(20,40));
  require(form.tool.snapshot().at("envelope").at("points").at(1)==Json::array({20,40}),"Zoomed native drag did not stage the exact envelope tick/value");
  SendMessageW(window,WM_LBUTTONDBLCLK,MK_LBUTTON,point(24,24));SendMessageW(window,WM_LBUTTONUP,0,point(24,24));
  const auto staged=form.tool.snapshot();require(staged.at("envelope").at("points").size()==4&&staged.at("envelope").at("points").at(2)==Json::array({24,24})&&staged.at("selectedPoint")==2,"Zoomed double-click did not insert the exact tick/value");
  require(staged.at("dirty").get<bool>()&&staged.at("expectedRevision")=="r1"&&staged.at("document")=="owned-song"&&form.current.revision=="r1"&&form.writes==0,"Canvas interaction committed or redirected the captured document");
  form.press(4450);require(form.writes==1&&form.current.revision=="r2"&&form.envelope==staged.at("envelope")&&!form.tool.snapshot().at("dirty").get<bool>(),"Apply did not save exactly the staged mouse edits once");
}

void stagedAndPendingEdits(Owner &owner){
  Form form(owner.window);form.open(owner.window);form.page(1);form.choose(4412,0);form.field(4414,L"37");form.press(4415);
  const auto staged=form.tool.snapshot().at("envelope");require(staged.at("points").at(0)==Json::array({0,37}),"Short point fields did not stage the envelope");
  form.page(4);form.field(4446,L"60");form.field(4447,L"63");form.choose(4448,0);form.press(4449);const auto mapping=form.tool.snapshot().at("mapping");
  for(int note=60;note<=63;++note)require(mapping[note]==0,"Short key range was not staged");
  for(int page=0;page<5;++page){form.page(page);geometry(form);require(form.tool.snapshot().at("envelope")==staged&&form.tool.snapshot().at("mapping")==mapping,"Page changed staged data");}
  form.page(0);form.press(4450);require(form.writes==1&&!form.tool.snapshot().at("dirty").get<bool>()&&form.envelope==staged&&form.properties.at("mapping")==mapping,"Fixed short Apply did not save staged envelope/mapping once");
  form.page(2);form.choose(4429,5);form.field(4436,L"unfinished spacing");const auto generation=form.tool.snapshot().at("generation");
  form.page(0);form.press(4605);form.press(4605);form.page(2);require(form.tool.snapshot().at("toolFieldDraft").get<bool>()&&form.tool.snapshot().at("generation")==generation&&text(form.control(4436))==L"unfinished spacing","Pages lost the fifth raw tool parameter");
  form.page(1);form.duringPatch=[&]{form.field(4413,L"newer pending tick");form.page(0);form.press(4605);};form.press(4450);
  require(form.writes==2&&form.tool.snapshot().at("fieldDraft").get<bool>()&&text(form.control(4413))==L"newer pending tick"&&form.tool.snapshot().at("expectedRevision")=="r2","Pumped Apply discarded/rebased a newer raw field");
  require(!form.tool.snapshot().at("pending").get<bool>()&&form.tool.snapshot().at("status").get<std::string>().find("newer draft retained")!=std::string::npos,"Pumped Apply error did not retain a usable editor");
}

void hiddenInitialization(Owner &owner){
  Form existing(owner.window);existing.open(owner.window);existing.field(4413,L"existing raw draft");
  const auto externalFocus=existing.control(4413);SetFocus(externalFocus);SendMessageW(externalFocus,EM_SETSEL,2,6);
  const auto previous=existing.tool.snapshot();const auto reject=[](auto &&operation){bool rejected=false;try{operation();}catch(const std::runtime_error &){rejected=true;}require(rejected,"Hidden initializer accepted a non-fresh editor");};
  {
    Form staged(owner.window);unsigned placements=0;staged.tool.placementChanged([&]{++placements;});
    staged.tool.initializeHidden();const auto snapshot=staged.tool.snapshot();
    require(!staged.tool.visible()&&!staged.tool.docked()&&GetFocus()==externalFocus&&selection(externalFocus)==std::pair<DWORD,DWORD>{2,6},"Hidden initialization showed a tool or stole existing focus/caret");
    require(snapshot.at("document")=="owned-song"&&snapshot.at("expectedRevision")=="r1"&&snapshot.at("instrument")=="instrument:1"&&!snapshot.at("pending").get<bool>()&&!snapshot.at("retainedDraft").get<bool>()&&staged.reads==2&&staged.writes==0&&placements==0,"Hidden initialization lost its target or issued presentation/mutation callbacks");
    reject([&]{staged.tool.initializeHidden();});require(staged.reads==2,"Rejected repeat initialization still read document state");
  }
  {
    Form shown(owner.window);shown.tool.show();reject([&]{shown.tool.initializeHidden();});require(shown.reads==0,"Visible initialization rejection performed a load");
  }
  {
    Form docked(owner.window);docked.tool.dock(owner.window);reject([&]{docked.tool.initializeHidden();});require(docked.reads==0,"Docked initialization rejection performed a load");
  }
  {
    Form raw(owner.window);raw.field(4413,L"hidden raw draft");reject([&]{raw.tool.initializeHidden();});require(raw.reads==0&&text(raw.control(4413))==L"hidden raw draft","Hidden initialization discarded raw draft");
  }
  SetFocus(externalFocus);SendMessageW(externalFocus,EM_SETSEL,2,6);HWND failedWindow{};unsigned placements=0;
  {
    auto staged=std::make_unique<Form>(owner.window);failedWindow=staged->tool.window();staged->tool.placementChanged([&]{++placements;});
    staged->duringRead=[&]{require(GetFocus()==externalFocus,"Pending hidden read stole external focus");throw std::runtime_error("owned failing read");};
    bool rejected=false;try{staged->tool.initializeHidden();}catch(const std::runtime_error &error){rejected=std::string_view(error.what())=="owned failing read";}
    require(rejected&&!staged->tool.visible()&&!staged->tool.snapshot().at("pending").get<bool>()&&staged->writes==0&&placements==0,"Throwing hidden load changed presentation or queued a mutation");
    staged.reset();
  }
  require(!IsWindow(failedWindow)&&GetFocus()==externalFocus&&selection(externalFocus)==std::pair<DWORD,DWORD>{2,6}&&text(externalFocus)==L"existing raw draft"&&existing.tool.snapshot()==previous,"Destroying a failed staged tool affected the existing editor");
}
}
int main(){try{SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);ScreamSeq::Tests::runPrivateGui(L"ScreamSeqInstrumentShortDock",[]{Owner owner;allPagesAndActions(owner);rawDraftFocusAndPlacement(owner);revealFocusedWideSection(owner);zoomedCanvasMouseMapping(owner);stagedAndPendingEdits(owner);hiddenInitialization(owner);});
  std::cout<<"PASS short instrument dock: five retained pages/options,100-DIP canvas, fixed actions, native bounds and unchanged floating minimum\n"
           <<"PASS short instrument drafts: raw/caret/generation, dock/float/hide, staged mapping/envelope and pumped Apply retention\n"
           <<"PASS short instrument interaction: focused wide detail reveal and DPI-scaled zoomed handle drag/insertion before guarded Apply\n"
           <<"PASS hidden instrument initialization: captured target, unchanged external focus, strict freshness and staged read failure\n";return 0;
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
