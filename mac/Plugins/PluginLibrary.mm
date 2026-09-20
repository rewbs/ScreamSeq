#include "PluginLibrary.hpp"
#include "../Bridge/AutomationValidation.hpp"
#include <CommonCrypto/CommonDigest.h>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
namespace Tracker {
namespace {
using namespace Automation;
class Lock {
  int fd_ = -1;
public:
  explicit Lock(NSString *path) {
    NSError *error = nil;
    if (![NSFileManager.defaultManager createDirectoryAtPath:path.stringByDeletingLastPathComponent withIntermediateDirectories:YES attributes:@{NSFilePosixPermissions:@0700} error:&error])
      throw std::runtime_error("Cannot create the plugin library folder");
    fd_ = open([[path stringByAppendingString:@".lock"] fileSystemRepresentation], O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (fd_ < 0) throw std::runtime_error("Cannot open the plugin library lock");
    // Never wait behind a different process on the document worker. Its owner
    // should finish shortly; callers can retry with the same library revision.
    if (flock(fd_, LOCK_EX | LOCK_NB) != 0) { close(fd_); fd_ = -1; throw Error(-32002,"Plugin library is busy; retry shortly"); }
  }
  ~Lock() { if (fd_ >= 0) { flock(fd_,LOCK_UN); close(fd_); } }
};
void validID(NSString *id) {
  string(id,65); require(id.length == 65 && [id hasPrefix:@"p"], "Invalid catalog identity");
  require([[id substringFromIndex:1] rangeOfCharacterFromSet:[[NSCharacterSet characterSetWithCharactersInString:@"0123456789abcdef"] invertedSet]].location == NSNotFound, "Invalid catalog identity");
}
NSDictionary *preferences(id raw) {
  auto value = object(raw); keys(value,@[@"favorite",@"hidden",@"category"]);
  return @{@"favorite":@(boolean(value[@"favorite"])), @"hidden":@(boolean(value[@"hidden"])), @"category":string(value[@"category"],80)};
}
NSDictionary *readFile(NSString *path) {
  auto attributes = [NSFileManager.defaultManager attributesOfItemAtPath:path error:nil];
  if (!attributes) {
    require(![NSFileManager.defaultManager fileExistsAtPath:path], "Cannot read plugin library preferences");
    return @{@"version":@1,@"revision":@"library:0",@"entries":@{}};
  }
  require(attributes.fileSize <= 2 * 1024 * 1024, "Plugin library preferences exceed 2 MB");
  NSData *bytes = [NSData dataWithContentsOfFile:path];
  auto root = object(bytes ? [NSJSONSerialization JSONObjectWithData:bytes options:0 error:nil] : nil);
  keys(root,@[@"version",@"revision",@"entries"]); integer(root[@"version"],1,1);
  require(string(root[@"revision"],80).length > 0, "Invalid plugin library revision");
  auto entries = object(root[@"entries"]);require(entries.count<=4096,"Plugin library exceeds 4096 entries");
  for (id key in entries) { validID(key); preferences(entries[key]); }
  return root;
}
}
NSString *PluginLibrary::defaultPath() {
  NSString *test = NSProcessInfo.processInfo.environment[@"RESONANCE_AUTOMATION_TEST_DIRECTORY"];
  if (test.isAbsolutePath) return [test stringByAppendingPathComponent:@"plugin-library.json"];
  NSString *root = NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory,NSUserDomainMask,YES).firstObject;
  NSString *bundle = NSBundle.mainBundle.bundleIdentifier ?: @"org.resonance.tracker";
  return [[root stringByAppendingPathComponent:bundle] stringByAppendingPathComponent:@"plugin-library-v1.json"];
}
NSString *PluginLibrary::identifier(NSDictionary *descriptor) {
  using namespace Automation;
  NSString *format = string(descriptor[@"format"],16);
  require([@[@"AU",@"VST3",@"Built-in"] containsObject:format],"Unknown plugin format");
  NSArray *identity;
  if ([format isEqual:@"VST3"]) {
    NSString *path = string(descriptor[@"path"],8192), *classID = string(descriptor[@"classID"],32).uppercaseString;
    require(path.isAbsolutePath && classID.length==32 && [classID rangeOfCharacterFromSet:[[NSCharacterSet characterSetWithCharactersInString:@"0123456789ABCDEF"] invertedSet]].location==NSNotFound,"Invalid VST3 catalog identity");
    identity = @[format,path.stringByStandardizingPath,classID];
  } else if ([format isEqual:@"Built-in"]) {
    NSString *identifier = string(descriptor[@"classID"],128);
    require(identifier.length>0,"Built-in catalog identity requires its effect ID");
    identity = @[format,identifier];
  } else identity = @[format,@(integer(descriptor[@"type"],0,UINT32_MAX)),@(integer(descriptor[@"subtype"],0,UINT32_MAX)),@(integer(descriptor[@"manufacturer"],0,UINT32_MAX))];
  NSData *data = [NSJSONSerialization dataWithJSONObject:identity options:0 error:nil];
  unsigned char digest[CC_SHA256_DIGEST_LENGTH];CC_SHA256(data.bytes,CC_LONG(data.length),digest);
  NSMutableString *result = [NSMutableString stringWithString:@"p"];
  for (auto byte : digest) [result appendFormat:@"%02x",byte];return result;
}
NSDictionary *PluginLibrary::read() { Lock lock(path_); return readFile(path_); }
NSDictionary *PluginLibrary::set(NSString *revision, NSString *id, NSDictionary *patch, bool dryRun) {
  using namespace Automation;
  require(string(revision,80).length>0,"Provide the current library revision");validID(id);object(patch);keys(patch,@[@"favorite",@"hidden",@"category"]);require(patch.count>0,"Provide a library preference");
  Lock lock(path_);auto before = readFile(path_);
  if (![revision isEqual:before[@"revision"]]) throw Error(-32001,"Plugin library changed; reload before editing preferences");
  NSDictionary *defaults = @{@"favorite":@NO,@"hidden":@NO,@"category":@""};
  NSDictionary *old = before[@"entries"][id] ?: defaults;
  NSMutableDictionary *next = [old mutableCopy];
  if (patch[@"favorite"]) next[@"favorite"] = @(boolean(patch[@"favorite"]));
  if (patch[@"hidden"]) next[@"hidden"] = @(boolean(patch[@"hidden"]));
  if (patch[@"category"]) next[@"category"] = [string(patch[@"category"],80) stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
  const bool different = ![old isEqual:next];
  NSMutableDictionary *result = [before mutableCopy], *entries = [before[@"entries"] mutableCopy];
  if ([next isEqual:defaults]) [entries removeObjectForKey:id];else entries[id]=next;
  require(entries.count<=4096,"Plugin library exceeds 4096 customized entries");
  result[@"entries"]=entries;
  if (different && !dryRun) {
    result[@"revision"]=[@"library:" stringByAppendingString:NSUUID.UUID.UUIDString];
    NSData *bytes = [NSJSONSerialization dataWithJSONObject:result options:NSJSONWritingSortedKeys error:nil];
    require(bytes && bytes.length<=2*1024*1024,"Plugin library exceeds 2 MB");
    NSError *error=nil;
    if (![bytes writeToFile:path_ options:NSDataWritingAtomic error:&error]) throw std::runtime_error("Cannot save plugin library preferences; existing preferences are retained");
    [NSFileManager.defaultManager setAttributes:@{NSFilePosixPermissions:@0600} ofItemAtPath:path_ error:nil];
  }
  return @{@"libraryRevision":result[@"revision"],@"wouldChange":@(different),@"written":@(different && !dryRun),@"catalogID":id,@"preferences":next};
}
NSArray<NSDictionary *> *PluginLibrary::decorate(NSArray<NSDictionary *> *plugins, NSDictionary *library) {
  NSMutableArray *result=[NSMutableArray array];
  for (NSDictionary *plugin in plugins) {
    NSString *id = identifier(plugin);NSDictionary *p = library[@"entries"][id];
    NSMutableDictionary *entry=[plugin mutableCopy];entry[@"catalogID"]=id;
    NSMutableDictionary *descriptor=[NSMutableDictionary dictionary];
    for (NSString *key in @[@"type",@"subtype",@"manufacturer",@"name",@"format",@"path",@"classID",@"isInstrument"])
      if (plugin[key]) descriptor[key]=plugin[key];
    entry[@"descriptor"]=descriptor;
    entry[@"favorite"]=p[@"favorite"] ?: @NO;entry[@"hidden"]=p[@"hidden"] ?: @NO;
    NSString *custom = p[@"category"] ?: @"";entry[@"customCategory"]=custom;
    entry[@"category"]=custom.length ? custom : ([plugin[@"isInstrument"] boolValue] ? @"Instruments" : @"Effects");
    [result addObject:entry];
  }
  return result;
}
}
