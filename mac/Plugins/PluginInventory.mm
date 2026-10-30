#include "PluginInventory.hpp"
#include <cmath>
#include <cerrno>
#include <climits>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdlib>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <sys/utsname.h>
#include <CommonCrypto/CommonDigest.h>
#include <array>

namespace Tracker {
namespace {
struct InventoryLocation {std::mutex mutex;NSString *path=nil;};
InventoryLocation &inventoryLocation(){static InventoryLocation *value=new InventoryLocation;return *value;}
NSString *architecture() {
#if defined(__arm64__)
  return @"arm64";
#elif defined(__x86_64__)
  return @"x86_64";
#else
  struct utsname info{};
  uname(&info);
  return @(info.machine);
#endif
}
bool integer(id value) {
  if (![value isKindOfClass:NSNumber.class] || CFGetTypeID((__bridge CFTypeRef)value) == CFBooleanGetTypeID())
    return false;
  auto number = [value doubleValue];
  return std::isfinite(number) && number >= 0 && number <= UINT32_MAX && std::floor(number) == number;
}
bool string(id value, NSUInteger maximum) {
  return [value isKindOfClass:NSString.class] && [value length] <= maximum;
}
NSArray<NSDictionary *> *validated(id value) {
  if (![value isKindOfClass:NSArray.class] || [value count] > 4096)
    return nil;
  NSMutableArray *result = [NSMutableArray array];
  for (id item in value) {
    if (![item isKindOfClass:NSDictionary.class] || !integer(item[@"type"]) || !integer(item[@"subtype"]) ||
        !integer(item[@"manufacturer"]) || !string(item[@"name"], 1024) ||
        ![item[@"isInstrument"] isKindOfClass:NSNumber.class] ||
        CFGetTypeID((__bridge CFTypeRef)item[@"isInstrument"]) != CFBooleanGetTypeID())
      return nil;
    NSString *format = item[@"format"];
    if (![format isEqual:@"AU"] && ![format isEqual:@"VST3"])
      return nil;
    if ([format isEqual:@"VST3"]) {
      NSString *path = item[@"path"], *classID = item[@"classID"];
      if (!string(path, 8192) || !path.isAbsolutePath || ![path.pathExtension.lowercaseString isEqual:@"vst3"] ||
          !string(classID, 32) || classID.length != 32 ||
          [classID
              rangeOfCharacterFromSet:[[NSCharacterSet characterSetWithCharactersInString:@"0123456789abcdefABCDEF"]
                                          invertedSet]]
                  .location != NSNotFound)
        return nil;
    }
    [result addObject:[item copy]];
  }
  return [result copy];
}
// The inventory and the trust store both decide which bundles may be loaded,
// so both are read only from a regular file, not a link, that belongs to this
// user in a folder that belongs to this user. The trust store must be closed
// to everyone else. The inventory cache predates these rules and may be
// readable by others, but never writable; reading never changes permissions.
enum class Access { Private, NotWritableByOthers };
bool permitted(mode_t mode, Access access) {
  return (mode & (access == Access::Private ? 077 : 022)) == 0;
}
bool ownedDirectory(NSString *directory, Access access, bool create = false) {
  if (create)
    [NSFileManager.defaultManager createDirectoryAtPath:directory withIntermediateDirectories:YES
                                             attributes:@{NSFilePosixPermissions : @0700} error:nil];
  struct stat info{};
  if (lstat(directory.fileSystemRepresentation, &info) != 0 || info.st_uid != geteuid() || !S_ISDIR(info.st_mode))
    return false;
  if (permitted(info.st_mode, access))
    return true;
  // Only a folder being prepared for writing is tightened, and only if we own it.
  return create && chmod(directory.fileSystemRepresentation, 0700) == 0 &&
         lstat(directory.fileSystemRepresentation, &info) == 0 && S_ISDIR(info.st_mode) &&
         info.st_uid == geteuid() && permitted(info.st_mode, access);
}
NSData *readOwnedFile(NSString *path, NSUInteger maximum, Access access) {
  if (!path.isAbsolutePath || !ownedDirectory(path.stringByDeletingLastPathComponent, access))
    return nil;
  const int fd = open(path.fileSystemRepresentation, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
  if (fd < 0)
    return nil;
  struct stat info{};
  const bool accepted = fstat(fd, &info) == 0 && S_ISREG(info.st_mode) && info.st_uid == geteuid() &&
                        info.st_nlink == 1 && permitted(info.st_mode, access) && info.st_size > 0 &&
                        NSUInteger(info.st_size) <= maximum;
  NSMutableData *data = accepted ? [NSMutableData dataWithLength:NSUInteger(info.st_size)] : nil;
  size_t position = 0;
  while (data && position < data.length) {
    const auto count = read(fd, static_cast<char *>(data.mutableBytes) + position, data.length - position);
    if (count < 0 && errno == EINTR)
      continue;
    if (count <= 0)
      data = nil;
    else
      position += size_t(count);
  }
  close(fd);
  return data;
}
bool writePrivateFile(NSString *path, NSData *data, const char *pattern) {
  std::string temporary = [path.stringByDeletingLastPathComponent stringByAppendingPathComponent:@(pattern)].UTF8String;
  const int fd = mkstemp(temporary.data()); // Created with mode 0600.
  if (fd < 0)
    return false;
  bool written = fchmod(fd, 0600) == 0;
  size_t position = 0;
  while (written && position < data.length) {
    const auto count = write(fd, static_cast<const char *>(data.bytes) + position, data.length - position);
    if (count < 0 && errno == EINTR)
      continue;
    if (count <= 0)
      written = false;
    else
      position += size_t(count);
  }
  written = written && fsync(fd) == 0;
  close(fd);
  written = written && rename(temporary.c_str(), path.fileSystemRepresentation) == 0;
  if (!written)
    unlink(temporary.c_str());
  return written;
}
NSArray<NSDictionary *> *readInventory(NSString *path) {
  NSData *data = readOwnedFile(path, 8 * 1024 * 1024, Access::NotWritableByOthers);
  id root = data ? [NSJSONSerialization JSONObjectWithData:data options:0 error:nil] : nil;
  if ([root isKindOfClass:NSDictionary.class] && integer(root[@"version"]) && [root[@"version"] isEqual:@1] &&
      [root[@"architecture"] isEqual:architecture()])
    return validated(root[@"plugins"]);
  return nil;
}
} // namespace
NSString *PluginInventory::defaultPath() {
  auto &location=inventoryLocation();std::lock_guard lock(location.mutex);if(location.path)return location.path;
  NSString *root = NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES).firstObject;
  return [[root stringByAppendingPathComponent:@"org.resonance"]
      stringByAppendingPathComponent:[NSString stringWithFormat:@"plugins-v1-%@.json", architecture()]];
}
void PluginInventory::setStorePath(NSString *path){auto &location=inventoryLocation();std::lock_guard lock(location.mutex);location.path=path.isAbsolutePath?[path copy]:nil;}
NSArray<NSDictionary *> *PluginInventory::cached() {
  if (!cached_)
    cached_ = readInventory(path_);
  return cached_;
}
namespace {
struct ProcessTrust {
  std::mutex mutex;
  std::set<std::string> bundles, folders;
};
ProcessTrust &processTrust() {
  static ProcessTrust *value = new ProcessTrust; // Outlives static destruction during Quit.
  return *value;
}
bool bundleName(NSString *path) {
  return [path.pathExtension.lowercaseString isEqual:@"vst3"];
}
bool inside(NSString *path, NSString *folder) {
  if (!path.length || !folder.length)
    return false;
  return [path hasPrefix:[folder hasSuffix:@"/"] ? folder : [folder stringByAppendingString:@"/"]];
}
constexpr NSUInteger maximumStoredBundles = 1024, maximumStoreBytes = 1024 * 1024, maximumStoredPath = 4096;
struct StoreLocation {
  std::mutex mutex;
  NSString *override = nil;
};
StoreLocation &storeLocation() {
  static StoreLocation *value = new StoreLocation;
  return *value;
}
bool storedEntry(id value) {
  if (![value isKindOfClass:NSString.class] || ![value length] || [value length] > maximumStoredPath ||
      ![value isAbsolutePath] || !bundleName(value))
    return false;
  // Only a location that is still its own canonical path: no links, no "..".
  return [PluginTrust::canonical(value) isEqual:value];
}
// Any unreadable, foreign, oversized or malformed store is an empty store.
NSArray<NSString *> *readStore(NSString *path) {
  NSData *data = path ? readOwnedFile(path, maximumStoreBytes, Access::Private) : nil;
  id root = data ? [NSJSONSerialization JSONObjectWithData:data options:0 error:nil] : nil;
  if (![root isKindOfClass:NSDictionary.class] || [root count] != 2 || ![root[@"version"] isEqual:@1] ||
      ![root[@"bundles"] isKindOfClass:NSArray.class] || [root[@"bundles"] count] > maximumStoredBundles)
    return @[];
  NSMutableArray *result = [NSMutableArray array];
  for (id entry in root[@"bundles"])
    if (storedEntry(entry) && ![result containsObject:entry])
      [result addObject:entry];
  return result;
}
bool writeStore(NSString *path, NSArray<NSString *> *bundles) {
  NSData *data = [NSJSONSerialization dataWithJSONObject:@{@"version" : @1, @"bundles" : bundles}
                                                 options:NSJSONWritingSortedKeys error:nil];
  return data && data.length <= maximumStoreBytes && writePrivateFile(path, data, ".trusted-plugins.XXXXXX");
}
} // namespace
NSString *PluginTrust::defaultStorePath() {
  // Durable application data, beside Recovery and SampleLibrary; not a purgeable cache.
  NSString *root = NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES).firstObject;
  return [[root stringByAppendingPathComponent:@"Resonance/Plugins"] stringByAppendingPathComponent:@"trusted-plugins-v1.json"];
}
NSString *PluginTrust::storePath() {
  auto &location = storeLocation();
  std::lock_guard lock(location.mutex);
  if (location.override)
    return location.override;
  return NSBundle.mainBundle.bundleIdentifier.length ? defaultStorePath() : nil;
}
void PluginTrust::setStorePath(NSString *path) {
  auto &location = storeLocation();
  std::lock_guard lock(location.mutex);
  location.override = path.isAbsolutePath ? [path copy] : nil;
}
void PluginTrust::resetProcessTrust() {
  auto &trust = processTrust();
  std::lock_guard lock(trust.mutex);
  trust.bundles.clear();
  trust.folders.clear();
}
bool PluginTrust::persist(NSString *bundle) {
  NSString *real = canonical(bundle);
  if (!real || !bundleName(real) || !trust(real))
    return false;
  // Persistence is best effort: a failure leaves the bundle trusted until exit.
  NSString *path = storePath();
  if (!path || real.length > maximumStoredPath)
    return true;
  auto &location = storeLocation();
  std::lock_guard lock(location.mutex);
  if (!ownedDirectory(path.stringByDeletingLastPathComponent, Access::Private, true))
    return true;
  NSMutableArray *bundles = [readStore(path) mutableCopy];
  if ([bundles containsObject:real])
    return true;
  [bundles addObject:real];
  while (bundles.count > maximumStoredBundles)
    [bundles removeObjectAtIndex:0];
  writeStore(path, bundles);
  return true;
}
NSString *PluginTrust::canonical(NSString *path) {
  if (![path isKindOfClass:NSString.class] || !path.isAbsolutePath || path.length > 8192)
    return nil;
  char resolved[PATH_MAX];
  if (!realpath(path.fileSystemRepresentation, resolved))
    return nil;
  return [NSFileManager.defaultManager stringWithFileSystemRepresentation:resolved length:strlen(resolved)];
}
bool PluginTrust::trust(NSString *location) {
  NSString *real = canonical(location);
  if (!real)
    return false;
  auto &trust = processTrust();
  std::lock_guard lock(trust.mutex);
  (bundleName(real) ? trust.bundles : trust.folders).insert(real.UTF8String);
  return true;
}
bool PluginTrust::trusted(NSString *bundle, NSArray<NSDictionary *> *inventory) {
  if (![bundle isKindOfClass:NSString.class] || !bundleName(bundle))
    return false;
  NSString *real = canonical(bundle);
  if (!real)
    return false;
  for (NSString *folder in @[
         @"/Library/Audio/Plug-Ins/VST3", [@"~/Library/Audio/Plug-Ins/VST3" stringByExpandingTildeInPath],
         @"/Network/Library/Audio/Plug-Ins/VST3"
       ])
    if (inside(real, canonical(folder)))
      return true;
  {
    auto &trust = processTrust();
    std::lock_guard lock(trust.mutex);
    if (trust.bundles.contains(real.UTF8String))
      return true;
    for (const auto &folder : trust.folders)
      if (inside(real, @(folder.c_str())))
        return true;
  }
  for (NSDictionary *entry in inventory)
    if ([entry[@"format"] isEqual:@"VST3"] && [canonical(entry[@"path"]) isEqual:real])
      return true;
  return [readStore(storePath()) containsObject:real];
}
void PluginTrust::revoke(NSString *location) {
  NSString *real = canonical(location) ?: location;
  if (!real)
    return;
  auto &trust = processTrust();
  std::lock_guard lock(trust.mutex);
  trust.bundles.erase(real.UTF8String);
  trust.folders.erase(real.UTF8String);
}
NSString *PluginTrust::loadable(NSString *bundle) {
  NSString *real = canonical(bundle);
  return real && bundleName(real) && trusted(real, PluginInventory().cached()) ? real : nil;
}
NSString *PluginTrust::resolve(NSString *hint, NSString *classID, NSArray<NSDictionary *> *inventory) {
  // Always hand back the location that was checked, never the stored string:
  // a link inside the hint could point elsewhere by the time it is loaded.
  NSString *found = trusted(hint, inventory) ? canonical(hint) : nil;
  if (!found && [classID isKindOfClass:NSString.class] && classID.length == 32)
    for (NSDictionary *entry in inventory)
      if ([entry[@"format"] isEqual:@"VST3"] && [entry[@"classID"] caseInsensitiveCompare:classID] == NSOrderedSame &&
          bundleName(entry[@"path"]) && (found = canonical(entry[@"path"])))
        break;
  // The loader consults the stored inventory; a bundle accepted only through
  // this session's newer scan must remain loadable there too.
  if (found && bundleName(found) && !loadable(found))
    trust(found);
  return found && bundleName(found) ? found : nil;
}
NSArray<NSDictionary *> *PluginInventory::load(bool rescan, const std::function<NSArray<NSDictionary *> *()> &scan) {
  if (!rescan && cached_)
    return cached_;
  if (!rescan && (cached_ = readInventory(path_)))
    return cached_;
  // Commit only a completed, well-formed scan. Failure preserves the last good
  // list both in memory and on disk, so Cancel + Add Plugin still works.
  NSArray *next = validated(scan());
  if (!next)
    throw std::runtime_error("The plugin scanner returned an invalid inventory");
  NSData *data = [NSJSONSerialization dataWithJSONObject:@{
    @"version" : @1,
    @"architecture" : architecture(),
    @"plugins" : next
  }
                                                 options:0
                                                   error:nil];
  if (!data || data.length > 8 * 1024 * 1024)
    throw std::runtime_error("The plugin inventory exceeds its size limit");
  cached_ = next;
  // A read-only/full cache directory must not prevent adding a plugin. Keep
  // the in-memory cache and retry persistence after the next explicit scan.
  if (ownedDirectory(path_.stringByDeletingLastPathComponent, Access::NotWritableByOthers, true))
    writePrivateFile(path_, data, ".plugins.XXXXXX");
  return cached_;
}
namespace {
bool moduleHash(id value) {
  return string(value,64) && [value length]==64 &&
    [value rangeOfCharacterFromSet:[[NSCharacterSet characterSetWithCharactersInString:@"0123456789abcdef"] invertedSet]].location==NSNotFound;
}
StoreLocation &moduleStoreLocation(){static StoreLocation *location=new StoreLocation;return *location;}
bool sameClass(NSDictionary *descriptor, NSString *classID, bool instrument) {
  return [descriptor[@"format"] isEqual:@"VST3"] &&
    [descriptor[@"classID"] caseInsensitiveCompare:classID]==NSOrderedSame &&
    [descriptor[@"isInstrument"] boolValue]==instrument;
}
bool sameFile(const struct stat &a,const struct stat &b) {
  return a.st_dev==b.st_dev && a.st_ino==b.st_ino && a.st_size==b.st_size &&
    a.st_mtimespec.tv_sec==b.st_mtimespec.tv_sec && a.st_mtimespec.tv_nsec==b.st_mtimespec.tv_nsec &&
    a.st_ctimespec.tv_sec==b.st_ctimespec.tv_sec && a.st_ctimespec.tv_nsec==b.st_ctimespec.tv_nsec;
}
struct ModuleFile {
  int fd;
  explicit ModuleFile(NSString *path):fd(open(path.fileSystemRepresentation,O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK)){}
  ~ModuleFile(){if(fd>=0)close(fd);}
};
NSString *moduleExecutable(NSString *bundle) {
  NSString *real=PluginTrust::canonical(bundle);
  if(!real || !bundleName(real))throw std::invalid_argument("Choose an existing macOS VST3 bundle");
  // Read the on-disk plist directly. CFBundle may retain metadata for a bundle
  // already loaded in this process, including its former executable name.
  NSString *info=[real stringByAppendingPathComponent:@"Contents/Info.plist"];
  ModuleFile file(info);struct stat before{},after{},current{};
  if(file.fd<0 || fstat(file.fd,&before)!=0 || !S_ISREG(before.st_mode) || before.st_size<=0 || before.st_size>1024*1024)
    throw std::invalid_argument("VST3 bundle has no readable Info.plist within the size limit");
  NSMutableData *bytes=[NSMutableData dataWithLength:size_t(before.st_size)];size_t position=0;
  while(position<bytes.length){auto count=read(file.fd,static_cast<char *>(bytes.mutableBytes)+position,bytes.length-position);
    if(count<0 && errno==EINTR)continue;
    if(count<=0)throw std::runtime_error("VST3 bundle metadata changed while reading");position+=size_t(count);}
  if(fstat(file.fd,&after)!=0 || lstat(info.fileSystemRepresentation,&current)!=0 || !sameFile(before,after) || !sameFile(after,current))
    throw std::runtime_error("VST3 bundle metadata changed while reading");
  id root=[NSPropertyListSerialization propertyListWithData:bytes options:0 format:nil error:nil];
  id name=[root isKindOfClass:NSDictionary.class]?root[@"CFBundleExecutable"]:nil;
  if(!string(name,512) || ![name length] || ![name isEqual:[name lastPathComponent]] || [name isEqual:@"."] || [name isEqual:@".."])
    throw std::invalid_argument("VST3 bundle has an invalid executable name");
  NSString *executable=PluginTrust::canonical([[real stringByAppendingPathComponent:@"Contents/MacOS"] stringByAppendingPathComponent:name]);
  if(!executable || !inside(executable,real))throw std::invalid_argument("VST3 executable must exist inside its bundle");
  return executable;
}
}
NSString *PluginLocations::defaultPath() {
  auto &location=moduleStoreLocation();std::lock_guard lock(location.mutex);if(location.override)return location.override;
  return [PluginInventory::defaultPath().stringByDeletingLastPathComponent
    stringByAppendingPathComponent:[NSString stringWithFormat:@"verified-vst3-v1-%@.json",architecture()]];
}
void PluginLocations::setStorePath(NSString *path){auto &location=moduleStoreLocation();std::lock_guard lock(location.mutex);location.override=path.isAbsolutePath?[path copy]:nil;}
NSArray<NSDictionary *> *PluginLocations::records() {
  if(records_)return records_;
  records_=@[];
  NSData *bytes=readOwnedFile(path_,8*1024*1024,Access::Private);
  id root=bytes?[NSJSONSerialization JSONObjectWithData:bytes options:0 error:nil]:nil;
  if(![root isKindOfClass:NSDictionary.class] || !integer(root[@"version"]) || ![root[@"version"] isEqual:@1] ||
     ![root[@"architecture"] isEqual:architecture()] || ![root[@"modules"] isKindOfClass:NSArray.class] || [root[@"modules"] count]>4096)return records_;
  for(id item in root[@"modules"]){
    if(![item isKindOfClass:NSDictionary.class] || [item count]!=2 || !moduleHash(item[@"moduleSHA256"]) ||
       ![item[@"descriptor"] isKindOfClass:NSDictionary.class] || !validated(@[item[@"descriptor"]]) ||
       ![item[@"descriptor"][@"format"] isEqual:@"VST3"])return records_;
  }
  records_=[root[@"modules"] copy];return records_;
}
NSString *PluginLocations::moduleSHA256(NSString *bundle) {
  NSString *path=moduleExecutable(bundle);
  ModuleFile file(path);struct stat before{},after{},current{};
  if(file.fd<0 || fstat(file.fd,&before)!=0 || !S_ISREG(before.st_mode) || before.st_size<=0 || uint64_t(before.st_size)>2ull*1024*1024*1024)
    throw std::invalid_argument("VST3 executable must be a readable regular file within 2 GiB");
  CC_SHA256_CTX digest;CC_SHA256_Init(&digest);std::array<unsigned char,65536> buffer;uint64_t bytes=0;
  while(bytes<uint64_t(before.st_size)) {
    auto count=read(file.fd,buffer.data(),std::min<uint64_t>(buffer.size(),uint64_t(before.st_size)-bytes));
    if(count<0 && errno==EINTR)continue;
    if(count<=0)throw std::runtime_error("VST3 executable changed while hashing");
    CC_SHA256_Update(&digest,buffer.data(),CC_LONG(count));bytes+=uint64_t(count);
  }
  if(fstat(file.fd,&after)!=0 || lstat(path.fileSystemRepresentation,&current)!=0 || !sameFile(before,after) || !sameFile(after,current) ||
     ![moduleExecutable(bundle) isEqual:path])throw std::runtime_error("VST3 executable changed while hashing; scan again");
  std::array<unsigned char,CC_SHA256_DIGEST_LENGTH> hash;CC_SHA256_Final(hash.data(),&digest);
  NSMutableString *result=[NSMutableString string];for(auto byte:hash)[result appendFormat:@"%02x",byte];return result;
}
NSArray<NSDictionary *> *PluginLocations::scan(NSString *bundle,NSString *classID,bool instrument,
    const std::function<NSArray<NSDictionary *> *(NSString *)> &scanner) {
  if(!string(classID,32) || classID.length!=32)throw std::invalid_argument("Choose a VST3 class identity");
  NSString *real=PluginTrust::canonical(bundle),*hash=moduleSHA256(bundle);
  NSArray *found=validated(scanner(real));
  if(!found || !found.count)throw std::runtime_error("Scanner returned no valid VST3 descriptors");
  bool matched=false;NSMutableArray *next=[NSMutableArray array];
  for(NSDictionary *descriptor in found) {
    if(![descriptor[@"format"] isEqual:@"VST3"] || ![PluginTrust::canonical(descriptor[@"path"]) isEqual:real])
      throw std::runtime_error("Scanner returned a different VST3 bundle");
    matched=matched||sameClass(descriptor,classID,instrument);
    NSMutableDictionary *canonical=[descriptor mutableCopy];canonical[@"path"]=real;
    [next addObject:@{@"descriptor":canonical,@"moduleSHA256":hash}];
  }
  if(!matched)throw std::invalid_argument("Scanned module has a different plugin class; choose a matching VST3");
  if(![PluginTrust::canonical(bundle) isEqual:real] || ![moduleSHA256(real) isEqual:hash])
    throw std::runtime_error("VST3 module changed during the scan; scan again");
  for(NSDictionary *item in records())if(![item[@"descriptor"][@"path"] isEqual:real])[next addObject:item];
  if(next.count>4096)throw std::runtime_error("Too many verified VST3 classes");
  NSData *data=[NSJSONSerialization dataWithJSONObject:@{@"version":@1,@"architecture":architecture(),@"modules":next}
    options:NSJSONWritingSortedKeys error:nil];
  if(!data || data.length>8*1024*1024)throw std::runtime_error("Verified VST3 inventory exceeds its size limit");
  records_=[next copy];
  if(ownedDirectory(path_.stringByDeletingLastPathComponent,Access::Private,true))writePrivateFile(path_,data,".verified-vst3.XXXXXX");
  return candidates(classID,instrument);
}
NSArray<NSDictionary *> *PluginLocations::candidates(NSString *classID,bool instrument) {
  if(!string(classID,32) || classID.length!=32)throw std::invalid_argument("Choose a VST3 class identity");
  NSMutableArray *result=[NSMutableArray array];NSMutableDictionary *hashes=[NSMutableDictionary dictionary];
  for(NSDictionary *item in records()) {
    NSDictionary *descriptor=item[@"descriptor"];if(!sameClass(descriptor,classID,instrument))continue;
    NSString *path=descriptor[@"path"],*hash=hashes[path];
    if(!hash){try{hash=moduleSHA256(path);}catch(const std::exception &){hash=@"";}hashes[path]=hash;}
    if([PluginTrust::canonical(path) isEqual:path] && [hash isEqual:item[@"moduleSHA256"]])[result addObject:item];
  }
  return result;
}
NSString *PluginLocations::verify(NSString *bundle,NSString *classID,bool instrument,NSString *expected) {
  if(expected && !moduleHash(expected))throw std::invalid_argument("Expected the module SHA-256 from plugin.path.get");
  NSString *real=PluginTrust::canonical(bundle);
  for(NSDictionary *item in candidates(classID,instrument))if([item[@"descriptor"][@"path"] isEqual:real]) {
    if(expected && ![item[@"moduleSHA256"] isEqual:expected])throw std::invalid_argument("The scanned VST3 module changed; read its current hash again");
    return real;
  }
  throw std::invalid_argument("No matching verified VST3 module at this path; scan it first");
}
} // namespace Tracker
