#include "../Bridge/AutomationValidation.hpp"
#include <iostream>
#include <limits>
static void check(bool okay,const char *why){if(!okay)throw std::runtime_error(why);}
int main(){@autoreleasepool{try{
  for(NSString *literal in @[@"0",@"0.0",@"0e0",@"-0.0",@"1",@"1.0",@"1e0",@"4294967295",@"4294967295.0"]) {
    NSData *data=[[NSString stringWithFormat:@"[%@]",literal] dataUsingEncoding:NSUTF8StringEncoding];
    NSArray *values=[NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
    check(values.count==1,"JSON number fixture did not parse");
    auto value=Automation::integer(values[0],0,UINT32_MAX);
    check(value==[values[0] unsignedLongLongValue],"Integral JSON representations differ");
  }
  for(id value in @[@YES,@NO,@(-1),@0.5,@"1",NSNull.null,@(0x1p64),@(INFINITY),@(NAN)]) {
    bool rejected=false;try{Automation::integer(value,0,UINT64_MAX);}catch(const Automation::Error &e){rejected=e.code==-32602;}
    check(rejected,"Invalid Foundation integer accepted");
  }
  check(Automation::integer(@(UINT64_MAX),UINT64_MAX,UINT64_MAX)==UINT64_MAX,"Exact Foundation uint64 rounded");
  check(Automation::integer(@(uint64_t(9007199254740993ULL)),9007199254740993ULL,9007199254740993ULL)==9007199254740993ULL,
    "Foundation integer above binary64 precision rounded");
  bool rejected=false;try{Automation::integer(@(0x1p53),9007199254740993ULL,UINT64_MAX);}catch(const Automation::Error &e){rejected=e.code==-32602;}
  check(rejected,"Foundation real rounded a lower bound");
  std::cout<<"PASS Foundation integer adapter and parsed JSON number representations\n";return 0;
}catch(const std::exception &e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}}
