#include "PluginPreset.hpp"
#include "../Bridge/AutomationValidation.hpp"
#include <CommonCrypto/CommonDigest.h>
#include <cerrno>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace Tracker {
namespace {
using namespace Automation;
constexpr NSUInteger maximumFileBytes = PluginPreset::maximumStateBytes + 65536;
struct File {
  int fd;
  explicit File(int value) : fd(value) { if (fd >= 0) fcntl(fd, F_SETFD, FD_CLOEXEC); }
  ~File() { if (fd >= 0) close(fd); }
};
void pathCheck(NSString *path) {
  string(path, 8192);
  require(path.isAbsolutePath && [@[@"screamseq-preset", @"resonance-preset"] containsObject:path.pathExtension.lowercaseString],
          "Use an absolute .screamseq-preset (or legacy .resonance-preset) path");
}
NSDictionary *descriptor(id raw) {
  auto value = object(raw);
  keys(value, @[@"type", @"subtype", @"manufacturer", @"name", @"format", @"path", @"classID", @"isInstrument"]);
  for (NSString *key in @[@"type", @"subtype", @"manufacturer"]) integer(value[key], 0, UINT32_MAX);
  string(value[@"name"], 1024); string(value[@"path"], 8192); string(value[@"classID"], 128);
  boolean(value[@"isInstrument"]);
  NSString *format = string(value[@"format"], 16);
  require([@[@"AU", @"VST3", @"Built-in"] containsObject:format], "Unsupported preset plugin format");
  if ([format isEqual:@"VST3"]) {
    NSString *classID = [value[@"classID"] uppercaseString];
    require(classID.length == 32 && [classID rangeOfCharacterFromSet:[[NSCharacterSet characterSetWithCharactersInString:@"0123456789ABCDEF"] invertedSet]].location == NSNotFound,
            "Invalid preset VST3 class identity");
  }
  if ([format isEqual:@"Built-in"])
    require([value[@"classID"] length] > 0 && ![value[@"type"] unsignedIntValue] && ![value[@"subtype"] unsignedIntValue] &&
      ![value[@"manufacturer"] unsignedIntValue] && ![value[@"isInstrument"] boolValue] && ![value[@"path"] length], "Invalid built-in preset identity");
  return value;
}
NSDictionary *decode(NSData *bytes) {
  auto root = object([NSPropertyListSerialization propertyListWithData:bytes options:0 format:nil error:nil]);
  keys(root, @[@"format", @"version", @"name", @"plugin", @"state"]);
  require([@[@"ScreamSeq plugin preset", @"Resonance plugin preset"] containsObject:root[@"format"]], "Not a ScreamSeq plugin preset");
  integer(root[@"version"], 1, 1); string(root[@"name"], 200); descriptor(root[@"plugin"]);
  require([root[@"state"] isKindOfClass:NSData.class] && [root[@"state"] length] <= PluginPreset::maximumStateBytes,
          "Preset state exceeds 16 MiB or has an invalid type");
  unsigned char digest[CC_SHA256_DIGEST_LENGTH]; CC_SHA256(bytes.bytes, CC_LONG(bytes.length), digest);
  NSMutableString *revision = [NSMutableString stringWithString:@"preset:"];
  for (auto byte : digest) [revision appendFormat:@"%02x", byte];
  NSMutableDictionary *result = [root mutableCopy]; result[@"presetRevision"] = revision;
  return result;
}
}
NSDictionary *PluginPreset::read(NSString *path) {
  pathCheck(path);
  File file(open(path.fileSystemRepresentation, O_RDONLY | O_CLOEXEC | O_NONBLOCK));
  struct stat info{};
  require(file.fd >= 0 && fstat(file.fd, &info) == 0 && S_ISREG(info.st_mode) && info.st_size > 0 && uint64_t(info.st_size) <= maximumFileBytes,
          "Preset must be a readable regular file within the size limit");
  NSMutableData *bytes = [NSMutableData dataWithLength:NSUInteger(info.st_size)];
  size_t position = 0;
  while (position < bytes.length) {
    const auto count = ::read(file.fd, static_cast<char *>(bytes.mutableBytes) + position, bytes.length - position);
    if (count < 0 && errno == EINTR) continue;
    require(count > 0, "Preset changed or could not be read completely"); position += size_t(count);
  }
  char extra;
  require(::read(file.fd, &extra, 1) == 0, "Preset changed while reading; inspect it again");
  return decode(bytes);
}
NSDictionary *PluginPreset::summary(NSDictionary *preset) {
  return @{@"name": preset[@"name"], @"descriptor": preset[@"plugin"],
           @"presetRevision": preset[@"presetRevision"], @"stateBytes": @([preset[@"state"] length]), @"presetVersion": @1};
}
bool PluginPreset::matches(NSDictionary *a, NSDictionary *b) {
  a = descriptor(a); b = descriptor(b);
  if (![a[@"format"] isEqual:b[@"format"]] || ![a[@"isInstrument"] isEqual:b[@"isInstrument"]]) return false;
  if ([a[@"format"] isEqual:@"AU"])
    return [a[@"type"] isEqual:b[@"type"]] && [a[@"subtype"] isEqual:b[@"subtype"]] && [a[@"manufacturer"] isEqual:b[@"manufacturer"]];
  // Bundle paths and display names can differ across computers or installations.
  if ([a[@"format"] isEqual:@"VST3"]) return [[a[@"classID"] uppercaseString] isEqual:[b[@"classID"] uppercaseString]];
  return [a[@"classID"] isEqual:b[@"classID"]];
}
NSDictionary *PluginPreset::write(NSString *path, NSDictionary *plugin, NSData *state, NSString *name, bool overwrite, bool dryRun) {
  pathCheck(path); descriptor(plugin); string(name, 200);
  require([state isKindOfClass:NSData.class] && state.length <= maximumStateBytes, "Preset state exceeds 16 MiB");
  BOOL isDirectory = NO;
  require([NSFileManager.defaultManager fileExistsAtPath:path.stringByDeletingLastPathComponent isDirectory:&isDirectory] && isDirectory,
          "Preset directory does not exist");
  struct stat existing{};
  if (lstat(path.fileSystemRepresentation, &existing) == 0) {
    require(overwrite, "Preset exists; use overwrite:true to replace it");
    require(S_ISREG(existing.st_mode), "Existing preset destination must be a regular file");
  } else require(errno == ENOENT, "Cannot inspect preset destination");
  NSData *bytes = [NSPropertyListSerialization dataWithPropertyList:@{@"format": @"Resonance plugin preset", @"version": @1,
    @"name": name, @"plugin": plugin, @"state": state} format:NSPropertyListBinaryFormat_v1_0 options:0 error:nil];
  require(bytes && bytes.length <= maximumFileBytes, "Cannot encode preset within the size limit");
  NSMutableDictionary *result = [summary(decode(bytes)) mutableCopy]; result[@"path"] = path; result[@"written"] = @(!dryRun);
  if (dryRun) return result;
  std::string temporary = [path.stringByDeletingLastPathComponent stringByAppendingPathComponent:@".resonance-preset.XXXXXX"].UTF8String;
  File file(mkstemp(temporary.data()));
  if (file.fd < 0) throw std::runtime_error("Cannot create preset staging file");
  try {
    size_t position = 0;
    while (position < bytes.length) {
      const auto count = ::write(file.fd, static_cast<const char *>(bytes.bytes) + position, bytes.length - position);
      if (count < 0 && errno == EINTR) continue;
      if (count <= 0) throw std::runtime_error("Cannot write complete preset"); position += size_t(count);
    }
    if (fsync(file.fd) != 0) throw std::runtime_error("Cannot flush preset data");
    const int published = overwrite ? rename(temporary.c_str(), path.fileSystemRepresentation) : link(temporary.c_str(), path.fileSystemRepresentation);
    if (published != 0) throw std::runtime_error("Cannot publish preset; existing destination was retained");
    unlink(temporary.c_str());
  } catch (...) { unlink(temporary.c_str()); throw; }
  return result;
}
}
