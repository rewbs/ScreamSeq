#include "../App/WorkspaceRegions.hpp"
#include <iostream>
#include <limits>
#include <vector>

namespace {
namespace Regions=ScreamSeq::WorkspaceRegions;
using Regions::Config;using Regions::Json;using Regions::Panel;using Regions::Region;using Regions::Placement;
using ScreamSeq::WorkspaceRect;
void check(bool condition,const char *message){if(!condition)throw std::runtime_error(message);}
bool close(float a,float b){return std::abs(a-b)<.001f;}
bool same(WorkspaceRect a,WorkspaceRect b){return close(a.x,b.x)&&close(a.y,b.y)&&close(a.w,b.w)&&close(a.h,b.h);}
Config fourPanes(){auto result=Regions::placed(Config{},Panel::automation,Placement::secondary);return Regions::placed(result,Panel::instruments,Placement::right);}
void reject(const Json &value,const Config &current){
  const auto before=current;
  try{(void)Regions::decodeEditors(&value,current);throw std::runtime_error("Invalid region preferences were accepted");}
  catch(const ScreamSeq::Api::ApiError &error){check(error.code==-32602,"Malformed preferences used the wrong error code");}
  check(current==before,"Rejected decode changed the live presentation model");
}
void legacyMigration(){
  auto current=fourPanes();current.compactSelection="automation";current.rightWidth=523;current.bottomHeight=411;
  check(Regions::decodeEditors(nullptr,current,"samples",128)==current,"Seven-field layout changed current independent editor placements");
  unsigned cases=0;
  for(const auto a:{"right","float","hide"})for(const auto b:{"right","float","hide"})
    for(const auto active:{"automation","instruments"})for(const bool tracker:{false,true})for(const auto lower:Regions::mainBottomNames){
      const Json old={{"locations",Json::array({a,b})},{"active",active},{"tracker",tracker}};
      const auto migrated=Regions::decodeEditors(&old,current,lower,128);
      const auto expected=std::string_view(active)=="automation"&&std::string_view(a)=="right"?"automation":
        std::string_view(active)=="instruments"&&std::string_view(b)=="right"?"instruments":
        std::string_view(a)=="right"?"automation":std::string_view(b)=="right"?"instruments":"";
      check(migrated.locations==std::array{Regions::placement(a),Regions::placement(b)},"Legacy placement changed");
      check(migrated.selected[0]==expected&&migrated.selected[1]==lower&&migrated.selected[2].empty(),"Legacy active/lower selection migration changed effective tabs");
      check(migrated.compactSelection==(tracker||std::string_view(expected).empty()?"pattern":expected),"Legacy tracker selection was lost");
      check(migrated.rightWidth==460&&migrated.bottomHeight==128,"Legacy short desired height was rejected or silently enlarged");
      const auto encoded=Regions::encode(migrated);check(Regions::decodeEditors(&encoded,current)==migrated,"Migrated V2 did not round-trip");++cases;
    }
  check(cases==216,"Legacy migration did not cover all 36 editor states and six lower editors");
  const Json bad={{"locations",Json::array({"bottom","hide"})},{"active","automation"},{"tracker",false}};reject(bad,current);
  for(auto badOld:{Json{{"locations",{"right"}},{"active","automation"},{"tracker",false}},Json{{"locations",{"right","hide"}},{"active","graph"},{"tracker",false}},Json{{"locations",{"right","hide"}},{"active","automation"},{"tracker",0}}})reject(badOld,current);
}
void strictVersionAndMembership(){
  const auto current=fourPanes();const auto valid=Regions::encode(current);
  for(const auto *field:{"version","locations","selected","compactSelection","rightWidth","bottomHeight"}){auto bad=valid;bad.erase(field);reject(bad,current);}
  for(const auto *field:{"pins","targets","origin","drafts"}){auto bad=valid;bad[field]=Json::object();reject(bad,current);}
  for(const auto value:{Json(1),Json(2.0),Json(true),Json("2")}){auto bad=valid;bad["version"]=value;reject(bad,current);}
  for(const auto value:{Json::array(),Json(),Json("right")}){auto bad=valid;bad["locations"]=value;reject(bad,current);}
  for(const auto *field:{"locations","selected"}){auto bad=valid;bad[field]["unknown"]="automation";reject(bad,current);}
  for(const auto *place:{"left","window","bottom-right",""}){auto bad=valid;bad["locations"]["automation"]=place;reject(bad,current);}
  for(const auto *id:{"graph","automation","unknown",""}){auto bad=valid;bad["selected"]["right"]=id;reject(bad,current);}
  for(const auto *id:{"unknown","instruments",""}){auto bad=valid;bad["selected"]["bottom"]=id;reject(bad,current);}
  for(const auto *id:{"notes","samples","unknown",""}){auto bad=valid;bad["compactSelection"]=id;reject(bad,current);}
  auto graphChoice=valid;graphChoice["compactSelection"]="graph";check(Regions::decodeEditors(&graphChoice,current).compactSelection=="graph","Selected Main lower panel cannot be compact choice");
  for(const auto *field:{"rightWidth","bottomHeight"}){
    const double minimum=std::string_view(field)=="rightWidth"?440:128;
    for(const auto value:{Json(true),Json("440"),Json(minimum-.000001),Json(1600.000001),Json(std::numeric_limits<double>::infinity()),Json(std::numeric_limits<double>::quiet_NaN())}){auto bad=valid;bad[field]=value;reject(bad,current);}
  }
  for(const auto destination:{Placement::right,Placement::bottom,Placement::secondary,Placement::floating,Placement::hidden}){
    const auto moved=Regions::placed(current,Panel::automation,destination,"plugins");const auto encoded=Regions::encode(moved);
    check(Regions::decodeEditors(&encoded,Config{})==moved,"V2 native placement did not round-trip");
  }
  check(valid.size()==6&&!valid.contains("pins")&&!valid.contains("targets"),"Region preferences unexpectedly persist live editor state");
}
void independentSelections(){
  auto config=fourPanes();config.compactSelection="automation";
  const auto before=config;
  config=Regions::placed(config,Panel::automation,Placement::right);
  check(config.selected[0]=="automation"&&config.selected[1]=="graph"&&config.selected[2].empty(),"Move did not update only source/destination region selection");
  check(config.compactSelection=="automation","Moving an active compact editor lost its selection");
  config=Regions::selected(config,Region::right,"instruments");
  check(config.compactSelection=="pattern"&&config.selected[0]=="instruments","Selecting another tab left an unavailable compact choice");
  config=Regions::placed(config,Panel::instruments,Placement::hidden);
  check(config.selected[0]=="automation","Hiding selected native panel did not reveal remaining panel");
  config=Regions::placed(config,Panel::automation,Placement::bottom);
  check(config.selected[0].empty()&&config.selected[1]=="automation","Move to bottom did not remove duplicate host selection");
  config=Regions::selected(config,Region::bottom,"plugins");
  check(config.locations[0]==Placement::bottom&&config.selected[1]=="plugins","Selecting a Main lower panel moved a retained native editor");
  config=Regions::placed(config,Panel::automation,Placement::floating,"samples");
  check(config.selected[1]=="plugins","Moving an unselected native editor overwrote selected Main panel");
  auto original=fourPanes();original.compactSelection="automation";check(before==original,"Value operations changed their source model");
}
void checkGeometry(WorkspaceRect work,const Regions::Geometry &geometry){
  std::vector<std::pair<WorkspaceRect,bool>> boxes;
  const auto add=[&](WorkspaceRect box,bool divider=false){
    check(std::isfinite(box.x)&&std::isfinite(box.y)&&std::isfinite(box.w)&&std::isfinite(box.h)&&box.w>=0&&box.h>=0,"Geometry contains invalid dimensions");
    if(!box.w||!box.h)return;
    check(box.x>=work.x-.001f&&box.y>=work.y-.001f&&box.x+box.w<=work.x+std::max(0.f,work.w)+.001f&&box.y+box.h<=work.y+std::max(0.f,work.h)+.001f,"Region escaped the existing workspace bounds");
    // The two resize hit regions deliberately share the split junction. Each
    // must still be disjoint from every body/header and inside the workspace.
    for(const auto &[other,otherDivider]:boxes)if(!divider||!otherDivider)
      check(std::min(box.x+box.w,other.x+other.w)-std::max(box.x,other.x)<=.001f||std::min(box.y+box.h,other.y+other.h)-std::max(box.y,other.y)<=.001f,"Region bodies/headers/dividers overlap");
    boxes.emplace_back(box,divider);
  };
  add(geometry.pattern);add(geometry.tabs);add(geometry.verticalDivider,true);add(geometry.horizontalDivider,true);
  for(const auto &host:geometry.hosts)if(host.visible){add(host.header);add(host.body);}
}
void alignedGeometryAndFallback(){
  const auto config=fourPanes();const Regions::Minimums minimums;
  const float fitWidth=minimums.mainBottomWidth+minimums.gap+minimums.nativeWidth;
  const float fitHeight=minimums.header+minimums.nativeHeight+minimums.gap+minimums.header+minimums.mainBottomHeight;
  for(const float dw:{-1.f,0.f,1.f})for(const float dh:{-1.f,0.f,1.f}){
    const WorkspaceRect work{170,88,fitWidth+dw,fitHeight+dh};const auto result=Regions::solve(work,config);
    check(result.mode==(dw<0||dh<0?Regions::Mode::tabs:Regions::Mode::regions),"Four-pane threshold does not follow meaningful body minima");checkGeometry(work,result);
  }
  const WorkspaceRect work{170,88,1262,686};const auto result=Regions::solve(work,config);
  check(result.mode==Regions::Mode::regions,"1440-DIP client fixture did not fit four panes");
  const auto &right=result.hosts[0],&bottom=result.hosts[1],&secondary=result.hosts[2];
  check(close(right.body.x,secondary.body.x)&&close(right.body.w,secondary.body.w)&&close(bottom.header.y,secondary.header.y),"Four-pane columns or bottom row are misaligned");
  const auto v=result.verticalDivider,h=result.horizontalDivider;
  check(close(std::min(v.x+v.w,h.x+h.w)-std::max(v.x,h.x),minimums.gap)&&close(std::min(v.y+v.h,h.y+h.h)-std::max(v.y,h.y),minimums.gap),"Aligned divider junction is not exactly one gap square");
  check(right.body.w>=440&&right.body.h>=300&&secondary.body.w>=440&&secondary.body.h>=300&&bottom.body.h>=304,"Visible independent bodies fell below their minimums");checkGeometry(work,result);
  auto copied=config;copied.compactSelection="automation";copied.rightWidth=1600;copied.bottomHeight=128;const auto desired=copied;
  for(const WorkspaceRect bounds:{WorkspaceRect{170,88,870,488},WorkspaceRect{170,88,1,1},WorkspaceRect{170,88,0,0},WorkspaceRect{170,88,-1,-1},work,WorkspaceRect{170,88,3000,2000}}){
    const auto view=Regions::solve(bounds,copied);checkGeometry(bounds,view);check(copied==desired,"Resize silently rewrote desired layout or selected panels");
    if(view.mode==Regions::Mode::tabs&&bounds.h>28)check(view.hosts[2].visible&&!view.hosts[0].visible&&!view.hosts[1].visible&&!view.pattern.h,"Compact fallback exposed the wrong retained selection");
  }
  const auto focus=Regions::solve(work,config,{},true);check(focus.mode==Regions::Mode::none&&same(focus.pattern,work)&&config==fourPanes(),"Pattern focus changed desired dock config");
}
void absentAndMainOwnedNeighbors(){
  const WorkspaceRect work{170,88,1262,686};
  for(const auto a:{Placement::floating,Placement::hidden})for(const auto b:{Placement::floating,Placement::hidden}){
    Config config;config.locations={a,b};const auto result=Regions::solve(work,config);
    check(result.mode==Regions::Mode::none&&same(result.pattern,work)&&!result.hosts[0].visible&&!result.hosts[1].visible&&!result.hosts[2].visible,"No native dock incorrectly replaced legacy geometry");
  }
  auto config=Regions::placed(fourPanes(),Panel::automation,Placement::hidden);auto result=Regions::solve(work,config);
  check(close(result.hosts[0].header.h+result.hosts[0].body.h,work.h)&&!result.hosts[2].visible,"Absent secondary did not let right editor extend to full height");checkGeometry(work,result);
  result=Regions::solve(work,fourPanes(),{},false,false);
  check(close(result.pattern.h,work.h)&&close(result.horizontalDivider.x,result.hosts[2].body.x)&&close(result.horizontalDivider.w,result.hosts[2].body.w),"Collapsed Main bottom left a divider across the full-height pattern");checkGeometry(work,result);
  config=Regions::placed(Config{},Panel::automation,Placement::bottom);config=Regions::selected(config,Region::bottom,"graph");result=Regions::solve(work,config);
  check(result.hosts[1].visible&&result.hosts[1].selected=="graph"&&close(result.hosts[1].body.w,work.w),"Unselected native bottom placement replaced the Main-owned graph");checkGeometry(work,result);
  result=Regions::solve(work,config,{},false,false);check(!result.hosts[1].visible&&same(result.pattern,work),"Collapsed legacy bottom still consumed geometry");
  config=Regions::selected(config,Region::bottom,"automation");result=Regions::solve(work,config,{},false,false);check(result.hosts[1].visible,"Legacy Main collapse also hid an independently selected native bottom editor");checkGeometry(work,result);
  config=fourPanes();config.compactSelection="graph";result=Regions::solve({170,88,800,500},config,{},false,false);
  check(result.mode==Regions::Mode::tabs&&result.compactSelection=="pattern"&&result.pattern.h>0&&config.compactSelection=="graph","Collapsed Main fallback mutated desired selection or exposed hidden content");
  for(const auto a:{Placement::right,Placement::bottom,Placement::secondary,Placement::floating,Placement::hidden})for(const auto b:{Placement::right,Placement::bottom,Placement::secondary,Placement::floating,Placement::hidden}){
    auto candidate=Regions::placed(Regions::placed(Config{},Panel::automation,a),Panel::instruments,b);
    for(const bool mainVisible:{false,true})for(const WorkspaceRect bounds:{work,WorkspaceRect{170,88,600,420}})checkGeometry(bounds,Regions::solve(bounds,candidate,{},false,mainVisible));
  }
}
}
int main(){try{
  legacyMigration();strictVersionAndMembership();independentSelections();alignedGeometryAndFallback();absentAndMainOwnedNeighbors();
  std::cout<<"PASS workspace regions: 216 legacy migrations, strict V2 and immutable restore values, independent selections, aligned bounded geometry, compact fallback and legacy/Main-owned host semantics\n";return 0;
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
