#include "editor/AutomationTools.hpp"
#include "editor/TrackerDocument.hpp"
#include <iostream>
#include <limits>
using namespace Tracker;
static void check(bool ok, const char *why) { if (!ok) throw std::runtime_error(why); }
template<class F> static void rejects(F f) { bool caught=false; try { f(); } catch (const std::invalid_argument &) { caught=true; } check(caught,"Invalid operation rejected"); }
int main() { try {
  size_t comparisons=0;
  for (uint8_t curve=0;curve<8;++curve) {
    std::vector<AutomationPoint> original{{0,.13,AutomationCurve(curve)},{129,.86,AutomationCurve(curve)},{512,.29,AutomationCurve(curve)},{1023,.7,AutomationCurve::Smooth}};
    AutomationTool flip;flip.operation="flip-time";flip.end=1024;
    const auto reversed=transformAutomationPoints(original,1024,flip).points;
    for (int i=0;i<=8184;++i) {
      const double x=i/8.0;
      check(std::abs(automationValue(original,1023-x)-automationValue(reversed,x))<2e-13,"Time flip preserves the independently evaluated reversed curve");++comparisons;
    }
    check(transformAutomationPoints(reversed,1024,flip).points==original,"Time flip is an exact point/curve involution");
  }
  const std::vector<AutomationPoint> points{{0,.1,AutomationCurve::Linear},{256,.5,AutomationCurve::Exponential},{512,.9,AutomationCurve::Step}};
  const auto clip=copyAutomationPoints(points,2048,128,768);
  check(clip.span==640 && clip.points.size()==2 && clip.points[0].position==128 && clip.points[1].position==384,"Clipboard uses selected control points and exact relative range");
  AutomationTool paste;paste.operation="paste";paste.start=768;paste.end=2048;paste.clip=clip;paste.repeats=2;
  auto pasted=transformAutomationPoints(points,2048,paste).points;
  check(pasted.size()==7 && pasted[3].position==896 && pasted[4].position==1152 && pasted[5].position==1536 && pasted[6].position==1792,"Repeated paste tiles the exact clip span");
  paste.operation="insert";paste.start=256;paste.repeats=1;
  auto inserted=transformAutomationPoints(points,2048,paste).points;
  check(inserted.size()==5 && inserted[1].position==384 && inserted[2].position==640 && inserted[3].position==896 && inserted[4].position==1152,"Insert shifts all following points and keeps clipboard offsets");
  rejects([&]{transformAutomationPoints(points,1024,paste);});
  AutomationTool t;t.operation="shift";t.start=256;t.end=512;t.shift=-256;
  rejects([&]{transformAutomationPoints(points,2048,t);});
  t.shift=64;auto shifted=transformAutomationPoints(points,2048,t).points;
  check(shifted[0]==points[0] && shifted[1].position==320 && shifted[2]==points[2],"Shift preserves points outside the selected range");
  t.operation="scale";t.start=0;t.end=2048;t.amount=2;
  const auto scaled=transformAutomationPoints(points,2048,t);
  check(scaled.clipped==1 && scaled.points[0].value==.2 && scaled.points[1].value==1 && scaled.points[2].value==1,"Scaling reports clipping exactly");
  t.operation="humanize";t.amount=.1;t.seed=0;t.jitter=0;
  const auto human=transformAutomationPoints(points,2048,t).points;
  check(human==transformAutomationPoints(points,2048,t).points,"Seeded humanization is repeatable");
  check(human[0].value==.1+.1*(2*(double(1013904223u)/UINT32_MAX)-1),"Seed zero has specified nondegenerate first noise sample");
  t.amount=0;check(transformAutomationPoints(points,2048,t).points==points,"Zero humanization is exact no-op");
  t.operation="sine";t.start=1024;t.end=1537;t.spacing=128;t.amount=.4;t.offset=.5;t.cycles=1;t.phase=0;
  const auto sine=transformAutomationPoints(points,2048,t).points;
  const double values[]{.5,.9,.5,.1,.5};
  for(size_t i=0;i<5;++i)check(sine[3+i].position==1024+i*128 && std::abs(sine[3+i].value-values[i])<1e-14,"Generated sine matches independent quadrature values");
  t.operation="ramp";t.start=255;t.end=257;t.from=.2;t.to=.8;t.curve=AutomationCurve::LogarithmicReverse;
  const auto ramp=transformAutomationPoints(points,2048,t).points;
  check(ramp.size()==4 && ramp[1].position==255 && ramp[2].position==256 && ramp[1].value==.2 && ramp[2].value==.8 && ramp.back()==points.back(),"Ramp replaces a half-open range, retaining exterior points");
  t.end=256;check(transformAutomationPoints(points,2048,t).points[1].value==.2,"Single-unit ramp retains its first value");
  t.operation="humanize";t.amount=std::numeric_limits<double>::quiet_NaN();rejects([&]{transformAutomationPoints(points,2048,t);});
  t.operation="unknown";rejects([&]{transformAutomationPoints(points,2048,t);});
  rejects([&]{copyAutomationPoints(points,2048,600,700);});
  rejects([&]{copyAutomationPoints(points,2048,0,2049);});
  auto corrupt=points;corrupt[1].position=0;rejects([&]{validateAutomationPoints(corrupt,2048);});
  for(auto type:{MOD_TYPE_MOD,MOD_TYPE_S3M,MOD_TYPE_XM,MOD_TYPE_IT,MOD_TYPE_MPT}) {
    Document d(type);auto native=d.native();
    auto lane=MusicalAutomationLane{native.makeEntity().id,native.patterns.at(0).id,"fixture",7,true,points};
    native.automation.push_back(lane);d.annotate([&](NativeSong &n){n=native;});
    auto before=d.native();t={};t.operation="flip-time";t.end=1024;
    d.annotate([&](NativeSong &n){n.automation[0].points=transformAutomationPoints(n.automation[0].points,2048,t).points;});
    const auto after=d.native();check(after.automation[0].id==before.automation[0].id,"Lane identity survives transform");
    d.undo();check(d.native()==before,"Native transform Undo is exact");d.redo();check(d.native()==after,"Native transform Redo is exact");
  }
  std::cout<<"PASS "<<comparisons<<" reversed-curve reference values; clipboard/repeat/insert, scoped transforms, deterministic humanization, collisions, bounds and five-format native history\n";
  return 0;
} catch(const std::exception &e) {std::cerr<<"FAIL "<<e.what()<<'\n';return 1;} }
