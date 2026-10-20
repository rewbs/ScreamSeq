#include "../Bridge/NativePropertyTree.hpp"
#include <iostream>
#include <bit>
using namespace ScreamSeq;
static void check(bool okay,const char *why){if(!okay)throw std::runtime_error(why);}
int main(){@autoreleasepool{try{
  const auto before=NativePropertyTree::fromFoundation(@{@"version":@17,
    @"tracks":@[@[@0,@{@"id":@"n1",@"name":@"First"}],@[@1,@{@"id":@"n2",@"name":@"Second"}]],
    @"knownNumber":@1.0,@"knownBytes":[NSData dataWithBytes:"abc" length:3]});
  auto original=before;
  original["opaqueData"]=Project::Value::binary({0,255,42});
  original["opaqueDate"]=Project::Value::binary({0x3f,0xf0,0,0,0,0,0,0},uint64_t(Project::OpaqueType::Date));
  original["opaqueUID"]=Project::Value::binary({0,1,0,0,0,0,0,1},uint64_t(Project::OpaqueType::UID));
  std::vector<uint8_t> wide(16,0xff);wide.back()=0;
  original["opaqueWideInteger"]=Project::Value::binary(wide,uint64_t(Project::OpaqueType::WideInteger));
  original["opaqueNaN"]=Project::Value::binary({0x7f,0xc0,0,7},uint64_t(Project::OpaqueType::Real32NaN));
  original["unicode"]="\xF0\x9F\x8E\xB5";
  original["negativeZero"]=-0.0;
  original["tracks"][0][1]["future"]=Project::Value::binary({11,22});
  NSData *captured=NativePropertyTree::encode(original);
  auto loaded=NativePropertyTree::decode(captured);
  check(Project::sameStoredValue(loaded,original),"Native data bridge changed typed opaque values");
  NSDictionary *projection=NativePropertyTree::modelProjection(loaded);
  check(projection[@"opaqueUID"]==NSNull.null&&projection[@"opaqueWideInteger"]==NSNull.null&&
    projection[@"opaqueNaN"]==NSNull.null&&[projection[@"knownBytes"] isKindOfClass:NSData.class]&&
    [projection[@"knownNumber"] doubleValue]==1.0&&[projection[@"tracks"] count]==2,
    "Model projection narrowed opaque values or lost supported musical properties");
  check(Project::sameStoredValue(loaded,original),"Model projection mutated retained original properties");
  const auto after=NativePropertyTree::fromFoundation(@{@"version":@17,
    @"tracks":@[@[@0,@{@"id":@"n2",@"name":@"Second"}],@[@1,@{@"id":@"n1",@"name":@"Edited"}]],
    @"knownNumber":@2.0,@"knownBytes":[NSData dataWithBytes:"def" length:3]});
  const auto merged=Project::mergePreserved(loaded,before,after);
  check(merged["tracks"][1][1]["future"]==original["tracks"][0][1]["future"]&&
    !merged["tracks"][0][1].contains("future"),"Native bridge retargeted an opaque entity extension");
  const auto saved=NativePropertyTree::decode(NativePropertyTree::encode(merged));
  check(Project::sameStoredValue(saved,merged),"Native encode/reopen lost typed metadata");
  for(const auto key:{"opaqueData","opaqueDate","opaqueUID","opaqueWideInteger","opaqueNaN","unicode","negativeZero"})
    check(Project::sameStoredValue(saved.at(key),original.at(key)),"Known edit changed an unknown property");
  check(saved.at("knownNumber")==2.0&&saved.at("knownBytes")==after.at("knownBytes"),"Known native edits were not applied");
  check(Project::sameStoredValue(NativePropertyTree::decode(captured),original),"Save mutated captured source bytes");
  bool rejected=false;try{(void)NativePropertyTree::decode([NSData dataWithBytes:"bad" length:3]);}catch(const std::runtime_error &){rejected=true;}
  check(rejected,"Native bridge accepted corrupt property data");
  std::cout<<"PASS native property bridge: typed opaque values, stable identities, edit/save/reopen and immutable source\n";return 0;
}catch(const std::exception &e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}}
