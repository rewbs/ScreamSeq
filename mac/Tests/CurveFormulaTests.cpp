#include "editor/MusicalAutomation.hpp"
#include "GraphRealtimeAudit.hpp"
#include <iostream>
using namespace Tracker;
static void check(bool ok,const char *why){if(!ok)throw std::runtime_error(why);}
int main(){try {
  CurveFormula::Context c{.2,.8,.5,8,2,1,2,1,3};
  for(const auto &[text,expected]:std::vector<std::pair<std::string,double>>{{"mix(start,end,t^3)",.275},{"L",.5},{"beatOffset",.5},{"sin(pi/2)*end",.8},{"if(t<0.5,0.1,0.9)",.9},{"t<0.6 ? .7 : .2",.7},{"1/0",.5},{"sqrt(-1)",.5},{"exp(9999)",.5},{"10",1},{"-4",0},{"-2^2+4.5",.5},{"2^-2",.25},{"0 ? 1/0 : .6",.6},{"clamp(0.8,0.1,0.4)",.4},{"(beat-startBeat)/duration",.5}}) {
    CurveFormula formula(text);uint64_t a,f,l;tracker_audit_begin();const double actual=formula.evaluate(c);tracker_audit_end(&a,&f,&l);
    check(a+f+l==0,"Formula evaluation allocates/locks nothing");if(std::abs(actual-expected)>1e-10){std::cerr<<text<<" = "<<actual<<" expected "<<expected<<'\n';throw std::runtime_error("Formula math");}
  }
  for(const std::string text:std::vector<std::string>{"", "process.exit()", "foo", "sin(1,2)", "mix(1,2)", "1 +", "start = 3", std::string(40,'(')+"1"+std::string(40,')'),std::string(2049,'1')}){bool rejected=false;try{CurveFormula f(text);}catch(const std::invalid_argument &){rejected=true;}check(rejected,"Invalid/unbounded formula must reject");}
  std::vector<AutomationPoint> p{{256,.2,AutomationCurve::Scripted,CurveFormula("mix(start,end,t^2)")},{512,.8,AutomationCurve::Scripted,CurveFormula("start*(1-t)")}};
  check(automationValue(p,0,1024)==.2,"Before first anchor holds");check(std::abs(automationValue(p,384,1024)-.35)<1e-12,"Segment normalized progress");check(std::abs(automationValue(p,768,1024)-.4)<1e-12,"Last scripted node runs through envelope end");check(automationValue(p,1024,1024)==0,"Last node has t=1 at envelope end");
  CurveFormula noise("noise(beats*2,17)");check(noise.evaluate(c)==noise.evaluate(c),"Noise is deterministic");
  std::cout<<"Bounded expression compiler and scripted segments passed\n";return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
