#pragma once
#include "WorkspaceState.hpp"
namespace ScreamSeq {
// Screen geometry is rebuilt after draft/viewport changes, never during paint.
struct AutomationCanvas {
  struct Point {float x{},y{};};
  WorkspaceRect viewport;
  double start=0,end=16384;
  double valueLow=0,valueHigh=1;
  std::vector<Point> handles,curve;
  Point screen(double position,double value)const{return {viewport.x+float((position-start)/std::max(1.0,end-start))*viewport.w,viewport.y+float((valueHigh-value)/std::max(.0001,valueHigh-valueLow))*viewport.h};}
  double position(float x)const{return start+(x-viewport.x)*std::max(1.0,end-start)/std::max(1.0f,viewport.w);}
  double value(float y)const{return std::clamp(valueHigh-double(y-viewport.y)/std::max(1.0f,viewport.h)*(valueHigh-valueLow),0.0,1.0);}
  void rebuild(const Api::Json &points,const Api::Json &values){handles.clear();curve.clear();for(const auto &p:points)handles.push_back(screen(p.at("position").get<double>(),p.at("value").get<double>()));for(const auto &v:values)curve.push_back(screen(v[0].get<double>(),v[1].get<double>()));}
  int hit(float x,float y)const{int found=-1;double distance=9;for(size_t i=0;i<handles.size();++i){const auto &p=handles[i];const auto d=std::hypot(x-p.x,y-p.y);if(d<distance){distance=d;found=int(i);}}return found;}
  void fit(unsigned rows){start=0;end=double(rows)*256;valueLow=0;valueHigh=1;}
  void zoom(double factor,unsigned rows){const auto span=std::clamp((end-start)/factor,64.0,double(rows)*256);const auto center=(start+end)/2;start=std::clamp(center-span/2,0.0,double(rows)*256-span);end=start+span;}
  void pan(double amount,unsigned rows){const auto span=end-start;start=std::clamp(start+amount,0.0,std::max(0.0,double(rows)*256-span));end=start+span;}
  void zoomValues(double factor){const auto span=std::clamp((valueHigh-valueLow)/factor,.0001,1.0);const auto center=(valueHigh+valueLow)/2;valueLow=std::clamp(center-span/2,0.0,1-span);valueHigh=valueLow+span;}
  void panValues(double amount){const auto span=valueHigh-valueLow;valueLow=std::clamp(valueLow+amount,0.0,1-span);valueHigh=valueLow+span;}
};
}
