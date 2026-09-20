#include "../Plugins/PluginLibrary.hpp"
#include "../Bridge/AutomationValidation.hpp"
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#include <iostream>
#include <functional>
using namespace Tracker;
static void check(bool ok,const char *message) { if(!ok) throw std::runtime_error(message); }
static void error(int code,const std::function<void()> &action) {
  try { action();check(false,"Invalid library operation accepted"); } catch(const Automation::Error &e) { check(e.code==code,"Library error code"); }
}
int main() { @autoreleasepool { try {
  NSString *folder=[NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString];
  NSString *path=[folder stringByAppendingPathComponent:@"prefs.json"];
  PluginLibrary first(path),second(path);
  NSDictionary *au=@{@"format":@"AU",@"type":@1,@"subtype":@2,@"manufacturer":@3,@"name":@"Same",@"isInstrument":@NO};
  NSMutableDictionary *renamed=[au mutableCopy];renamed[@"name"]=@"New name";
  NSString *id=PluginLibrary::identifier(au);
  check([id isEqual:PluginLibrary::identifier(renamed)],"Catalog identity does not depend on display name");
  renamed[@"format"]=@"Built-in";renamed[@"classID"]=@"resonance.gainer";check(![id isEqual:PluginLibrary::identifier(renamed)],"AU and built-in identities cannot collide");
  NSString *gainer=PluginLibrary::identifier(renamed);renamed[@"classID"]=@"resonance.delay";
  check(![gainer isEqual:PluginLibrary::identifier(renamed)],"Built-ins with identical component integers retain distinct effect IDs");
  NSDictionary *vst=@{@"format":@"VST3",@"path":@"/test/a/../plugin.vst3",@"classID":@"abcdef0123456789abcdef0123456789",@"name":@"Same",@"isInstrument":@YES};
  NSMutableDictionary *canonical=[vst mutableCopy];canonical[@"path"]=@"/test/plugin.vst3";canonical[@"classID"]=[vst[@"classID"] uppercaseString];
  check([PluginLibrary::identifier(vst) isEqual:PluginLibrary::identifier(canonical)],"VST3 path/class spelling is canonicalized");
  canonical[@"path"]=@"/other/plugin.vst3";check(![PluginLibrary::identifier(vst) isEqual:PluginLibrary::identifier(canonical)],"Distinct VST3 bundles retain distinct preferences");
  NSDictionary *initial=first.read();check(![NSFileManager.defaultManager fileExistsAtPath:path],"An empty library read does not write preference data");
  auto dry=first.set(initial[@"revision"],id,@{@"favorite":@YES},true);
  check([dry[@"wouldChange"] boolValue] && ![dry[@"written"] boolValue] && [initial isEqual:second.read()],"Dry run does not change disk or revision");
  auto committed=first.set(initial[@"revision"],id,@{@"favorite":@YES,@"category":@"  Favourite filters  "});
  NSData *saved=[NSData dataWithContentsOfFile:path];
  check([committed[@"written"] boolValue] && [committed[@"preferences"][@"category"] isEqual:@"Favourite filters"],"Preferences persist with normalized category");
  check([second.read()[@"revision"] isEqual:committed[@"libraryRevision"]],"Another document sees committed preferences");
  error(-32001,[&]{second.set(initial[@"revision"],id,@{@"hidden":@YES});});
  check([[NSData dataWithContentsOfFile:path] isEqual:saved],"Stale writer cannot overwrite another document");
  auto unchanged=first.set(committed[@"libraryRevision"],id,@{@"favorite":@YES});
  check(![unchanged[@"written"] boolValue] && ![unchanged[@"wouldChange"] boolValue] && [[NSData dataWithContentsOfFile:path] isEqual:saved],"No-op retains exact disk data and revision");
  for(NSDictionary *patch in @[@{},@{@"favorite":@1},@{@"hidden":@"yes"},@{@"category":@9},@{@"unknown":@YES}])
    error(-32602,[&]{first.set(committed[@"libraryRevision"],id,patch);});
  auto decorated=PluginLibrary::decorate(@[au,vst],second.read());
  check([decorated[0][@"favorite"] boolValue] && [decorated[0][@"category"] isEqual:@"Favourite filters"] && [decorated[1][@"category"] isEqual:@"Instruments"],"Decorated catalog preserves custom and default categories");
  check([decorated[0][@"name"] isEqual:au[@"name"]] && !au[@"favorite"],"Decoration leaves scanner descriptors unchanged");
  check(PluginLibrary::decorate(@[],first.read()).count==0 && [[NSData dataWithContentsOfFile:path] isEqual:saved],"Missing plugins do not lose their library preferences");
  int fd=open([[path stringByAppendingString:@".lock"] fileSystemRepresentation],O_RDWR);check(fd>=0 && flock(fd,LOCK_EX|LOCK_NB)==0,"Test acquires library lock");
  error(-32002,[&]{first.read();});flock(fd,LOCK_UN);close(fd);
  first.set(committed[@"libraryRevision"],id,@{@"favorite":@NO,@"category":@""});
  check([first.read()[@"entries"] count]==0,"Returning to defaults removes only that customization");
  NSData *invalid=[@"not json" dataUsingEncoding:NSUTF8StringEncoding];[invalid writeToFile:path atomically:YES];
  error(-32602,[&]{first.read();});check([[NSData dataWithContentsOfFile:path] isEqual:invalid],"Corrupt preferences are not silently overwritten");
  [NSFileManager.defaultManager removeItemAtPath:folder error:nil];
  std::cout<<"PASS plugin library: stable identities, favorites/category defaults, dry-run/no-op, persistent multi-document revision checks, bounded validation, nonblocking locking and corruption preservation\n";return 0;
} catch(const std::exception &e) { std::cerr<<"FAIL "<<e.what()<<'\n';return 1; } } }
