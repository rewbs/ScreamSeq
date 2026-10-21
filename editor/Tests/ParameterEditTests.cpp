#include "editor/hosted/ParameterEdits.hpp"
#include "editor/hosted/RecipeParameterEdits.hpp"
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
void recipeEdits(){
  std::vector<PluginParameter> catalog={{17,"Gain",-96,24,-12,13},{999,"Mode",0,2,1,1},{42,"Read-only",0,1,.5,0}};
  catalog.back().writable=false;
  SignalDefinition recipe;recipe.id=7;recipe.name="Independent recipe";
  SignalNode processor;processor.id=10;processor.plugin.state={std::byte{0x63},std::byte{0x81}};
  processor.plugin.parameters[999]=1;processor.plugin.inputs={1};recipe.nodes.push_back(processor);
  processor.id=20;recipe.nodes.push_back(processor);
  recipe.audio={{10,20,0,1,.75}};
  recipe.modulation={{30,10,17,-.2,.3,.123456789,true},{31,10,17,-.1,.1,.123456789,false},{32,20,17,0,.1,.6,true}};
  const auto original=recipe;unsigned accepted=0;
  const auto apply=[&](std::vector<std::pair<uint32_t,double>> values){editSignalParameters(recipe,10,catalog,values,[&](uint32_t,float){++accepted;return true;});};
  const auto manual=signalManualParameterValue(recipe,10,17,-96,24,-12);
  apply({{17,manual}});apply({{17,float(manual)}});apply({});
  check(recipe==original&&accepted==0,"Recipe no-op rewrote an override, disabled base or opaque state");
  rejects([&]{apply({{17,-6},{42,.2}});});rejects([&]{apply({{17,-6},{999,3}});});
  rejects([&]{apply({{17,-6},{17,-3}});});rejects([&]{apply({{17,-6},{123,0}});});
  check(recipe==original&&accepted==0,"Invalid recipe batch reached a provider or partly edited the model");
  const std::vector<std::pair<uint32_t,double>> batch{{17,-6},{999,2}};
  rejects([&]{editSignalParameters(recipe,10,catalog,batch,[&](uint32_t,float){return ++accepted<2;});});
  check(accepted==2&&recipe==original,"Provider refusal partially updated the candidate recipe");
  accepted=0;apply(batch);
  check(accepted==2&&recipe.nodes[0].plugin.parameters.at(17)==-6&&recipe.nodes[0].plugin.parameters.at(999)==2,"Recipe edit lost native units or stable IDs");
  check(recipe.modulation[0].base==.75&&recipe.modulation[1].base==.75&&recipe.modulation[2]==original.modulation[2],"Manual edit did not align its contributions or touched another node");
  auto expected=original;expected.nodes[0].plugin.parameters={{17,-6},{999,2}};expected.modulation[0].base=expected.modulation[1].base=.75;
  check(recipe==expected,"Recipe parameter edit changed unrelated ports, routes, identities or opaque bytes");
  const auto saved=recipe;apply(batch);check(recipe==saved&&accepted==2,"Repeating a recipe edit changed its saved representation or called the provider");
  const std::vector<std::pair<uint32_t,double>> none;
  rejects([&]{editSignalParameters(recipe,999,catalog,none,[](uint32_t,float){return true;});});
  recipe.modulation[0].base=std::numeric_limits<double>::quiet_NaN();
  rejects([&]{apply({{17,-6}});});check(accepted==2,"Invalid manual baseline reached the provider");recipe=saved;
  const std::vector<PluginParameter> wide{{17,"Wide finite range",-3e38f,3e38f,0,0}};
  const std::vector<std::pair<uint32_t,double>> wideValue{{17,1e38}};
  recipe.nodes[0].plugin.parameters[17]=0;recipe.modulation[0].base=.5;
  editSignalParameters(recipe,10,wide,wideValue,[](uint32_t,float){return true;});
  const auto normalized=(double(float(1e38))-double(wide[0].min))/(double(wide[0].max)-double(wide[0].min));
  check(std::isfinite(normalized)&&normalized>.5&&recipe.modulation[0].base==normalized&&recipe.modulation[1].base==normalized,"Recipe range subtraction overflowed before normalization");
  std::vector<std::pair<uint32_t,double>> tooMany(1025,{17,0});rejects([&]{apply(tooMany);});
}
}
int main(){try{run();recipeEdits();std::cout<<"Shared rack and recipe parameter preparation passed\n";return 0;}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
