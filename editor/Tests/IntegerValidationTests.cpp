#include "editor/IntegerValidation.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
using Tracker::boundedUnsignedInteger;
namespace {
void check(bool okay,const char *why){if(!okay)throw std::runtime_error(why);}
void run(){
  for(const uint64_t value:{0ULL,1ULL,127ULL,65535ULL,4294967295ULL,9007199254740991ULL}) {
    check(boundedUnsignedInteger(value,0,value)==value,"Exact unsigned endpoint lost");
    check(boundedUnsignedInteger(int64_t(value),0,value)==value,"Exact signed endpoint lost");
    check(boundedUnsignedInteger(double(value),0,value)==value,"Integral real rejected");
    check(!boundedUnsignedInteger(value,value+1,UINT64_MAX),"Unsigned lower bound lost");
    check(!boundedUnsignedInteger(double(value),value+1,UINT64_MAX),"Real lower bound lost");
    if(value) {
      check(!boundedUnsignedInteger(value,0,value-1),"Unsigned upper bound lost");
      check(!boundedUnsignedInteger(double(value),0,value-1),"Real upper bound lost");
    }
  }
  check(boundedUnsignedInteger(-0.0,0,1)==0,"Negative zero is a valid zero coordinate");
  for(const double value:{-.5,-1.,.5,1.25,std::numeric_limits<double>::infinity(),
      -std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN(),0x1p64})
    check(!boundedUnsignedInteger(value,0,UINT64_MAX),"Invalid real converted to an integer");
  check(!boundedUnsignedInteger(int64_t(-1),0,UINT64_MAX),"Negative signed value wrapped");
  check(!boundedUnsignedInteger(INT64_MIN,0,UINT64_MAX),"Signed minimum wrapped");
  check(boundedUnsignedInteger(UINT64_MAX,UINT64_MAX,UINT64_MAX)==UINT64_MAX,"Maximum exact integer rounded");
  check(boundedUnsignedInteger(uint64_t(9007199254740993ULL),9007199254740993ULL,9007199254740993ULL)==9007199254740993ULL,
    "Integer above binary64 consecutive precision rounded");
  check(!boundedUnsignedInteger(0x1p53,9007199254740993ULL,UINT64_MAX),"Rounded real lower bound accepted");
  const double below=std::nextafter(0x1p64,0.);
  const uint64_t exact=UINT64_MAX-2047;
  check(boundedUnsignedInteger(below,exact,exact)==exact,"Largest representable uint64 real lost");
  check(!boundedUnsignedInteger(below,exact+1,UINT64_MAX),"Large lower bound rounded");
  check(!boundedUnsignedInteger(below,0,exact-1),"Large upper bound rounded");
  check(!boundedUnsignedInteger(uint64_t(7),8,6),"Inverted range accepted");
}
}
int main(){try{run();std::cout<<"PASS integer validation: exact storage, integral reals, bounds and safe conversion\n";return 0;}
catch(const std::exception &e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
