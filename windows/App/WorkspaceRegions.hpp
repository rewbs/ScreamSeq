#pragma once
// Pure Windows presentation preferences and geometry. No HWNDs, targets, pins,
// draft state, callbacks or musical operations belong in this model.
#include "WorkspaceState.hpp"
#include <cmath>
#include <initializer_list>
#include <string_view>

namespace ScreamSeq::WorkspaceRegions {
using Json=Api::Json;
enum class Panel { automation, instruments };
enum class Region { right, bottom, secondary };
enum class Placement { right, bottom, secondary, floating, hidden };
enum class Mode { none, regions, tabs };
inline constexpr std::array<std::string_view,2> panelNames{"automation","instruments"};
inline constexpr std::array<std::string_view,3> regionNames{"right","bottom","secondary"};
inline constexpr std::array<std::string_view,6> mainBottomNames{"notes","samples","effects","plugins","mixer","graph"};

struct Config {
  std::array<Placement,2> locations{Placement::hidden,Placement::hidden};
  std::array<std::string,3> selected{"","graph",""};
  std::string compactSelection="pattern";
  // Desired DIPs, never rewritten by solve(). rightWidth is the native editor
  // column, not the legacy top-level Notes/Samples inspector width.
  float rightWidth=460,bottomHeight=334;
  bool operator==(const Config &)const=default;
};
struct Minimums {
  float patternWidth=350,patternHeight=180;
  float nativeWidth=440,nativeHeight=300;
  float mainBottomWidth=780,mainBottomHeight=304;
  float header=28,gap=6;
};
struct Host {
  WorkspaceRect header,body;
  std::string selected;
  bool visible=false;
};
struct Geometry {
  Mode mode=Mode::none;
  WorkspaceRect pattern,verticalDivider,horizontalDivider,tabs;
  std::array<Host,3> hosts;
  std::string compactSelection="pattern";
};

inline void need(bool condition,const char *message="Invalid saved editor regions") {
  if(!condition)throw Api::ApiError(-32602,message);
}
inline size_t index(Panel panel){return static_cast<size_t>(panel);}
inline size_t index(Region region){return static_cast<size_t>(region);}
inline bool mainBottom(std::string_view id){return std::find(mainBottomNames.begin(),mainBottomNames.end(),id)!=mainBottomNames.end();}
inline bool nativePanel(std::string_view id){return std::find(panelNames.begin(),panelNames.end(),id)!=panelNames.end();}
inline std::string_view name(Placement value) {
  switch(value){case Placement::right:return "right";case Placement::bottom:return "bottom";case Placement::secondary:return "secondary";case Placement::floating:return "float";case Placement::hidden:return "hide";}
  throw Api::ApiError(-32602,"Invalid editor placement");
}
inline Placement placement(std::string_view value) {
  if(value=="right")return Placement::right;if(value=="bottom")return Placement::bottom;
  if(value=="secondary")return Placement::secondary;if(value=="float")return Placement::floating;
  if(value=="hide")return Placement::hidden;throw Api::ApiError(-32602,"Invalid editor placement");
}
inline Placement placement(Region region){return static_cast<Placement>(index(region));}
inline bool belongs(const Config &config,Region region,std::string_view id) {
  if(region==Region::bottom&&mainBottom(id))return true;
  for(size_t i=0;i<panelNames.size();++i)if(panelNames[i]==id)return config.locations[i]==placement(region);
  return false;
}
inline std::string firstNative(const Config &config,Region region) {
  for(size_t i=0;i<panelNames.size();++i)if(config.locations[i]==placement(region))return std::string(panelNames[i]);
  return {};
}
inline bool compactAvailable(const Config &config,std::string_view id) {
  if(id=="pattern")return true;
  for(size_t i=0;i<regionNames.size();++i)if(config.selected[i]==id&&belongs(config,static_cast<Region>(i),id))return true;
  return false;
}
inline void validate(const Config &config) {
  for(auto value:config.locations)(void)name(value);
  need(std::isfinite(config.rightWidth)&&config.rightWidth>=440&&config.rightWidth<=1600);
  // Legacy lowerHeight may be only 128. Effective bodies are clamped to their
  // minima in solve; retaining the desired height preserves old preferences.
  need(std::isfinite(config.bottomHeight)&&config.bottomHeight>=128&&config.bottomHeight<=1600);
  for(size_t i=0;i<regionNames.size();++i){
    const auto region=static_cast<Region>(i);const auto &selected=config.selected[i];
    if(selected.empty())need(region!=Region::bottom&&firstNative(config,region).empty());
    else need(belongs(config,region,selected));
  }
  need(compactAvailable(config,config.compactSelection));
}
inline void keys(const Json &value,std::initializer_list<std::string_view> expected) {
  need(value.is_object()&&value.size()==expected.size());
  for(auto key:expected)need(value.contains(std::string(key)));
}
inline std::string string(const Json &value){need(value.is_string());return value.get<std::string>();}
inline float number(const Json &value,double minimum) {
  need(value.is_number());const auto result=value.get<double>();need(std::isfinite(result)&&result>=minimum&&result<=1600);
  return static_cast<float>(result);
}
inline Json encode(const Config &config) {
  validate(config);
  return {{"version",2},{"locations",{{"automation",name(config.locations[0])},{"instruments",name(config.locations[1])}}},
    {"selected",{{"right",config.selected[0]},{"bottom",config.selected[1]},{"secondary",config.selected[2]}}},
    {"compactSelection",config.compactSelection},{"rightWidth",config.rightWidth},{"bottomHeight",config.bottomHeight}};
}
// A missing editors member is the old seven-field layout. It must leave all
// current editor presentation preferences exactly unchanged, with no creation.
// The bounded saved-layout catalogue remains version 1; only editors is V2.
inline Config decodeEditors(const Json *editors,const Config &current,std::string_view legacyLowerEditor="graph",float legacyLowerHeight=210) {
  if(!editors)return current;
  const auto &value=*editors;Config result;
  if(value.is_object()&&!value.contains("version")){
    keys(value,{"locations","active","tracker"});
    need(value.at("locations").is_array()&&value.at("locations").size()==2);
    for(size_t i=0;i<2;++i){const auto text=string(value.at("locations")[i]);need(text=="right"||text=="float"||text=="hide");result.locations[i]=placement(text);}
    const auto active=string(value.at("active"));need(nativePanel(active)&&value.at("tracker").is_boolean());
    need(mainBottom(legacyLowerEditor));result.selected[1]=legacyLowerEditor;
    result.selected[0]=belongs(result,Region::right,active)?active:firstNative(result,Region::right);
    result.compactSelection=value.at("tracker").get<bool>()||result.selected[0].empty()?"pattern":result.selected[0];
    result.bottomHeight=legacyLowerHeight;
  }else{
    keys(value,{"version","locations","selected","compactSelection","rightWidth","bottomHeight"});
    need(value.at("version").is_number_integer()&&value.at("version")==2);
    keys(value.at("locations"),{"automation","instruments"});keys(value.at("selected"),{"right","bottom","secondary"});
    for(size_t i=0;i<2;++i)result.locations[i]=placement(string(value.at("locations").at(std::string(panelNames[i]))));
    for(size_t i=0;i<3;++i)result.selected[i]=string(value.at("selected").at(std::string(regionNames[i])));
    result.compactSelection=string(value.at("compactSelection"));result.rightWidth=number(value.at("rightWidth"),440);result.bottomHeight=number(value.at("bottomHeight"),128);
  }
  validate(result);return result;
}
inline Config placed(Config config,Panel panel,Placement destination,std::string_view mainFallback="graph") {
  validate(config);need(mainBottom(mainFallback));(void)name(destination);
  const auto id=std::string(panelNames.at(index(panel)));config.locations.at(index(panel))=destination;
  for(size_t i=0;i<3;++i){const auto region=static_cast<Region>(i);
    if(destination==placement(region))config.selected[i]=id;
    else if(!belongs(config,region,config.selected[i]))config.selected[i]=region==Region::bottom?std::string(mainFallback):firstNative(config,region);
  }
  if(!compactAvailable(config,config.compactSelection))config.compactSelection="pattern";
  validate(config);return config;
}
inline Config selected(Config config,Region region,std::string_view id) {
  validate(config);need(belongs(config,region,id));config.selected.at(index(region))=id;
  if(!compactAvailable(config,config.compactSelection))config.compactSelection="pattern";
  validate(config);return config;
}

// work excludes main toolbar/sidebar/footer. Both rows share one right-column
// split, and both columns share one bottom-row split. No monitor/owner resizing.
// Without any native dock this returns mode none: Main retains legacy geometry.
// A hidden/nonselected native bottom panel does not replace the selected Main
// lower panel. mainBottomVisible only collapses Main-owned lower content.
inline Geometry solve(WorkspaceRect work,const Config &config,const Minimums &minimums={},bool patternFocus=false,bool mainBottomVisible=true) {
  validate(config);
  for(auto value:{minimums.patternWidth,minimums.patternHeight,minimums.nativeWidth,minimums.nativeHeight,minimums.mainBottomWidth,minimums.mainBottomHeight,minimums.header})need(std::isfinite(value)&&value>0,"Invalid region minimums");
  need(std::isfinite(minimums.gap)&&minimums.gap>=0,"Invalid region gap");
  need(std::isfinite(work.x)&&std::isfinite(work.y)&&std::isfinite(work.w)&&std::isfinite(work.h),"Invalid workspace bounds");
  work.w=std::max(0.f,work.w);work.h=std::max(0.f,work.h);
  Geometry result;result.pattern=work;result.compactSelection=config.compactSelection;
  for(size_t i=0;i<3;++i)result.hosts[i].selected=config.selected[i];
  const bool anyDock=std::any_of(config.locations.begin(),config.locations.end(),[](Placement value){return value==Placement::right||value==Placement::bottom||value==Placement::secondary;});
  if(patternFocus||!anyDock)return result;
  const bool right=!config.selected[0].empty(),secondary=!config.selected[2].empty();
  const bool nativeBottom=nativePanel(config.selected[1]);
  const bool bottom=nativeBottom||mainBottomVisible,column=right||secondary,row=bottom||secondary;
  const auto &m=minimums;
  const float bottomWidth=nativeBottom?m.nativeWidth:m.mainBottomWidth,bottomBody=nativeBottom?m.nativeHeight:m.mainBottomHeight;
  const float leftMinimum=std::max(m.patternWidth,bottom?bottomWidth:0.f);
  const float topMinimum=std::max(m.patternHeight,right&&secondary?m.header+m.nativeHeight:0.f);
  const float lowerMinimum=m.header+std::max(bottom?bottomBody:0.f,secondary?m.nativeHeight:0.f);
  const float requiredWidth=leftMinimum+(column?m.gap+m.nativeWidth:0.f);
  const float requiredHeight=std::max(topMinimum+(row?m.gap+lowerMinimum:0.f),right?m.header+m.nativeHeight:0.f);
  if(work.w<requiredWidth||work.h<requiredHeight){
    result.mode=Mode::tabs;result.pattern={};
    result.tabs={work.x,work.y,work.w,std::min(m.header,work.h)};
    WorkspaceRect body{work.x,work.y+result.tabs.h,work.w,std::max(0.f,work.h-result.tabs.h)};
    if(mainBottom(config.compactSelection)&&!mainBottomVisible)result.compactSelection="pattern";
    if(result.compactSelection=="pattern")result.pattern=body;
    else for(size_t i=0;i<3;++i)if(config.selected[i]==result.compactSelection){auto &host=result.hosts[i];host.body=body;host.visible=body.w>0&&body.h>0;break;}
    return result;
  }
  result.mode=Mode::regions;
  const float rightWidth=column?std::clamp(config.rightWidth,m.nativeWidth,work.w-m.gap-leftMinimum):0.f;
  const float leftWidth=column?work.w-m.gap-rightWidth:work.w;
  const float lowerHeight=row?std::clamp(config.bottomHeight,lowerMinimum,work.h-m.gap-topMinimum):0.f;
  const float topHeight=row?work.h-m.gap-lowerHeight:work.h;
  const float rightX=work.x+leftWidth+m.gap,bottomY=work.y+topHeight+m.gap;
  result.pattern={work.x,work.y,right?leftWidth:work.w,row?topHeight:work.h};
  if(right&&!bottom)result.pattern.h=work.h;
  const auto host=[&](Region region,WorkspaceRect box){auto &value=result.hosts[index(region)];value.header={box.x,box.y,box.w,m.header};value.body={box.x,box.y+m.header,box.w,box.h-m.header};value.visible=true;};
  // Without a secondary neighbor the right editor can use the full height.
  if(right)host(Region::right,{rightX,work.y,rightWidth,secondary?topHeight:work.h});
  if(bottom)host(Region::bottom,{work.x,bottomY,column?leftWidth:work.w,lowerHeight});
  if(secondary)host(Region::secondary,{rightX,bottomY,rightWidth,lowerHeight});
  if(column)result.verticalDivider={work.x+leftWidth,right?work.y:bottomY,m.gap,right?work.h:lowerHeight};
  if(row){
    if(right&&!bottom)result.horizontalDivider={rightX,work.y+topHeight,rightWidth,m.gap};
    else result.horizontalDivider={work.x,work.y+topHeight,right&&!secondary?leftWidth:work.w,m.gap};
  }
  return result;
}
} // namespace ScreamSeq::WorkspaceRegions
