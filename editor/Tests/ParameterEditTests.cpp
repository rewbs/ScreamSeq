#include "editor/hosted/ParameterEdits.hpp"
#include <iostream>
#include <limits>
using namespace Tracker;
namespace {
void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F f){try{f();}catch(const std::invalid_argument &){return;}throw std::runtime_error("Invalid parameter batch was accepted");}
void run(){
  std::vector<PluginParameter> catalog={{17,"Gain",-96,24,-12,13},{999,"Mode",0,2,1,1},{42,"Read-only",0,1,.5,0}};
  catalog.back().writable=false;
  const std::vector<std::pair<uint32_t,double>> input={{999,2},{17,-6.25}};
  const auto prepared=prepareParameterEdits(catalog,input);
  check(prepared==std::vector<std::pair<uint32_t,float>>{{999,2},{17,-6.25f}},"Stable IDs, request order or native units changed");
  check(catalog[0].value==-12&&catalog[1].value==1,"Preparation changed accepted values");
  const auto attempt=[&](std::vector<std::pair<uint32_t,double>> values){return prepareParameterEdits(catalog,values);};
  rejects([&]{attempt({});});rejects([&]{attempt({{17,0},{17,1}});});
  rejects([&]{attempt({{17,0},{123,1}});});rejects([&]{attempt({{17,0},{42,1}});});
  for(const auto value:{-97.0,25.0,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()})rejects([&]{attempt({{17,value}});});
  // Reject a later value without returning a partially publishable batch.
  for(const auto value:{-1.0,3.0,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()})rejects([&]{attempt({{17,-12},{999,value}});});
  check(catalog[0].value==-12&&catalog[1].value==1,"Rejected batch altered its catalogue");
  check(attempt({{17,-96},{999,0}})[0].second==-96&&attempt({{17,24},{999,2}})[0].second==24,"Exact range endpoints were lost");
  catalog.push_back({UINT32_MAX,"Stable maximum ID",0,1,0,0});
  check(attempt({{UINT32_MAX,.25}})[0].first==UINT32_MAX,"Unsigned stable parameter ID was narrowed");
  catalog[1].min=catalog[1].max=2;check(attempt({{999,2}})[0].second==2,"Constant parameter range was rejected");
  rejects([&]{attempt({{999,1}});});
  catalog[0].min=std::numeric_limits<float>::quiet_NaN();rejects([&]{attempt({{17,0}});});
  catalog[0].min=30;rejects([&]{attempt({{17,0}});});
  catalog[0].min=-96;catalog[0].max=std::numeric_limits<float>::infinity();rejects([&]{attempt({{17,0}});});
  std::vector<PluginParameter> large;std::vector<std::pair<uint32_t,double>> many;
  for(uint32_t i=0;i<4096;++i){large.push_back({i,"Bounded",0,1,0,0});many.emplace_back(i,.5);}
  check(prepareParameterEdits(large,many).size()==4096,"Valid maximum parameter batch was rejected");
  many.emplace_back(4096,.5);rejects([&]{prepareParameterEdits(large,many);});
}
}
int main(){try{run();std::cout<<"Shared parameter edit preparation passed\n";return 0;}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
