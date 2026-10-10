#include "../PatternDisplay.h"
#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
}
int main() {
  try {
    // F04's Start 70% and Amount 35% must agree across both native lanes.
    check(ScreamSeqGraphCommandDisplayByte(.7)==0xB2,"F04 Start 70% must display B2");
    check(ScreamSeqGraphCommandDisplayByte(.35)==0x59,"F04 Amount 35% must display 59");
    check(ScreamSeqGraphCommandDisplayByte(.5)==0x7F,"Midpoint must truncate rather than round");
    check(ScreamSeqGraphCommandDisplayByte(0)==0&&ScreamSeqGraphCommandDisplayByte(1)==255,
      "Normalized display endpoints changed");
    check(ScreamSeqGraphCommandDisplayByte(-1)==0&&ScreamSeqGraphCommandDisplayByte(2)==255,
      "Finite display values were not bounded");
    for(double value:{std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity()})
      check(ScreamSeqGraphCommandDisplayByte(value)==0,"Non-finite display value must use the neutral fallback");
    const auto metrics=ScreamSeqPatternGridMetricsMake(3,10);
    const std::array<unsigned,13> expected{2,0,0,1,0,0,1,0,0,1,2,0,1};
    for(unsigned row=0;row<expected.size();++row)
      check(ScreamSeqPatternRowAccent(metrics,row)==expected[row],"Non-four-row beat or non-divisible measure boundary changed");
    const auto legacy=ScreamSeqPatternGridMetricsMake(0,0);
    check(legacy.rowsPerBeat==4&&legacy.rowsPerMeasure==16,"Legacy unspecified metric fallback changed");
    const auto shortBar=ScreamSeqPatternGridMetricsMake(12,4);
    check(shortBar.rowsPerBeat==12&&shortBar.rowsPerMeasure==12,"Display bar cannot be shorter than its beat");
    const auto large=ScreamSeqPatternGridMetricsMake(65536,131072);
    check(ScreamSeqPatternRowAccent(large,131072)==2&&ScreamSeqPatternRowAccent(large,65536)==1,
      "Display metrics narrowed a large row boundary");
    std::cout<<"PASS shared pattern metrics and F04 graph display bytes\n";return 0;
  }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
