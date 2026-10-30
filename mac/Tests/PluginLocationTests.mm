#include "../Plugins/PluginInventory.hpp"
#include <iostream>
#include <stdexcept>
#include <sys/stat.h>
using namespace Tracker;
static void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
template<typename F> void refuses(F action,const char *message){bool rejected=false;try{action();}catch(const std::exception &){rejected=true;}check(rejected,message);}
int main(){@autoreleasepool{
  NSString *folder=[NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString];
  auto files=NSFileManager.defaultManager;
  try {
    NSString *bundle=[folder stringByAppendingPathComponent:@"Moved 音.vst3"];
    NSString *contents=[bundle stringByAppendingPathComponent:@"Contents"],*code=[contents stringByAppendingPathComponent:@"MacOS/Fixture"];
    check([files createDirectoryAtPath:code.stringByDeletingLastPathComponent withIntermediateDirectories:YES attributes:nil error:nil],"Create test bundle");
    NSString *plist=[contents stringByAppendingPathComponent:@"Info.plist"];
    [@{@"CFBundleExecutable":@"Fixture",@"CFBundlePackageType":@"BNDL"} writeToFile:plist atomically:YES];
    auto writeCode=[&](NSString *text){check([text writeToFile:code atomically:YES encoding:NSUTF8StringEncoding error:nil],"Write fixture code");};
    writeCode(@"original module");
    NSString *identity=@"0123456789ABCDEF0123456789ABCDEF",*other=@"FEDCBA9876543210FEDCBA9876543210";
    auto descriptor=[&](NSString *path,NSString *id,bool instrument)->NSDictionary *{return @{@"format":@"VST3",@"path":path,@"classID":id,@"isInstrument":@(instrument),@"type":@0,@"subtype":@0,@"manufacturer":@0,@"name":@"Fixture"};};
    NSString *cache=[folder stringByAppendingPathComponent:@"cache/locations.json"];
    PluginLocations locations(cache);
    check(!locations.candidates(identity,false).count,"Discovery alone cannot verify an executable");
    NSString *hash=PluginLocations::moduleSHA256(bundle);
    check([hash isEqual:@"0d2a79321efc3dece7bde0e28729fbbe2e18cdc058f198bb6c901e48520a43b6"],"Executable hash is SHA-256 of its bytes");
    NSString *real=PluginTrust::canonical(bundle);int scans=0;
    auto scanner=[&](NSString *path)->NSArray<NSDictionary *> *{++scans;check([path isEqual:real],"Scanner receives canonical chosen path");return @[descriptor(path,identity,false),descriptor(path,other,true)];};
    auto candidates=locations.scan(bundle,identity.lowercaseString,false,scanner);
    check(scans==1 && candidates.count==1 && [candidates[0][@"moduleSHA256"] isEqual:hash],"Matching scan records exact module bytes");
    check([locations.verify(bundle,identity,false,hash) isEqual:real],"Captured hash verifies exact module");
    check(!PluginTrust::trusted(bundle,nil),"Hash verification alone cannot grant plugin execution trust");
    check(locations.candidates(other,true).count==1 && !locations.candidates(identity,true).count,"All classes retain instrument/effect identity");
    PluginLocations restarted(cache);
    check(restarted.candidates(identity,false).count==1,"Successful private scan survives restart");
    struct stat permissions{};check(lstat(cache.fileSystemRepresentation,&permissions)==0 && (permissions.st_mode&077)==0,"Scan records are private");
    NSData *saved=[NSData dataWithContentsOfFile:cache];
    refuses([&]{locations.verify(bundle,identity,false,@"bad");},"Malformed hash refused");
    refuses([&]{locations.verify(bundle,identity,false,[@"0" stringByPaddingToLength:64 withString:@"0" startingAtIndex:0]);},"Stale expected hash refused");
    refuses([&]{locations.scan(bundle,other,false,scanner);},"Different class cannot replace a verified scan");
    refuses([&]{locations.scan(bundle,identity,false,[](NSString *)->NSArray<NSDictionary *> *{throw std::runtime_error("crashed scanner");});},"Scanner failure refused");
    refuses([&]{locations.scan(bundle,identity,false,[&](NSString *)->NSArray<NSDictionary *> *{return @[descriptor(@"/different.vst3",identity,false)];});},"Wrong scanner bundle refused");
    check([[NSData dataWithContentsOfFile:cache] isEqual:saved] && locations.candidates(identity,false).count==1,"Failed scans preserve memory and disk records");
    refuses([&]{locations.scan(bundle,identity,false,[&](NSString *path)->NSArray<NSDictionary *> *{writeCode(@"changed module");return @[descriptor(path,identity,false)];});},"Changing module during scan cannot publish a receipt");
    check([[NSData dataWithContentsOfFile:cache] isEqual:saved] && !locations.candidates(identity,false).count,"Changed executable cannot be advertised using an old scan");
    refuses([&]{locations.verify(bundle,identity,false,hash);},"Changed executable refused before loading");
    writeCode(@"original module");
    check(locations.candidates(identity,false).count==1,"Restored exact executable matches its prior scan");
    NSString *alias=[folder stringByAppendingPathComponent:@"Alias.vst3"];
    check([files createSymbolicLinkAtPath:alias withDestinationPath:bundle error:nil],"Create canonical alias");
    check([locations.verify(alias,identity,false,hash) isEqual:real],"Chosen alias resolves to the verified canonical bundle");
    locations.scan(alias,identity,false,scanner);check(locations.candidates(identity,false).count==1,"Rescanning an alias replaces rather than duplicates its module");
    NSString *alternate=[contents stringByAppendingPathComponent:@"MacOS/Alternate"];
    [@"another executable" writeToFile:alternate atomically:YES encoding:NSUTF8StringEncoding error:nil];
    [@{@"CFBundleExecutable":@"Alternate"} writeToFile:plist atomically:YES];
    check(!locations.candidates(identity,false).count,"Changed Info.plist executable selection retires the scan");
    [@{@"CFBundleExecutable":@"../../escape"} writeToFile:plist atomically:YES];
    refuses([&]{PluginLocations::moduleSHA256(bundle);},"Executable path traversal rejected");
    [@{@"CFBundleExecutable":@"Fixture"} writeToFile:plist atomically:YES];
    PluginLocations unwritable([code stringByAppendingPathComponent:@"locations.json"]);
    check(unwritable.scan(bundle,identity,false,scanner).count==1 && unwritable.candidates(identity,false).count==1,"Unavailable cache keeps verified records in memory");
    chmod(cache.fileSystemRepresentation,0644);
    check(!PluginLocations(cache).candidates(identity,false).count,"Publicly readable receipts are not accepted as private scan records");
    chmod(cache.fileSystemRepresentation,0600);
    [@"{broken" writeToFile:cache atomically:YES encoding:NSUTF8StringEncoding error:nil];chmod(cache.fileSystemRepresentation,0600);
    check(!PluginLocations(cache).candidates(identity,false).count,"Malformed records cannot authorize a module");
    [files removeItemAtPath:code error:nil];
    check(!locations.candidates(identity,false).count,"Deleted executables are omitted");
    [files removeItemAtPath:folder error:nil];
    std::cout<<"PASS plugin locations: hash guards, isolated scan admission, class/role identity, changed executable/plist, aliases, restart, refusal retention and private cache\n";return 0;
  }catch(const std::exception &e){[files removeItemAtPath:folder error:nil];std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}}
