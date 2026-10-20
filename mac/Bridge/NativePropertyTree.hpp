#pragma once
#import <Foundation/Foundation.h>
#include "../../editor/project/BinaryPlist.hpp"
#include "../../editor/project/ProjectPreservation.hpp"

namespace ScreamSeq::NativePropertyTree {
using Project::Value;
// Keep the original typed tree outside Foundation's canonical model. Unknown
// binary/date/UID/wide-integer/NaN payloads must never pass through JSON text.
inline Value decode(NSData *data) {
  if(!data)throw std::runtime_error("Project property data is unavailable");
  return Project::decodePlist({static_cast<const std::byte *>(data.bytes),data.length});
}
inline NSData *encode(const Value &tree) {
  const auto bytes=Project::encodePlist(tree);
  return [NSData dataWithBytes:bytes.data() length:bytes.size()];
}
// The native encoder produces the known model; only that tree goes through
// Foundation. Original unknown values stay in the shared typed representation.
inline Value fromFoundation(id tree) {
  NSError *error=nil;
  NSData *data=[NSPropertyListSerialization dataWithPropertyList:tree
    format:NSPropertyListBinaryFormat_v1_0 options:0 error:&error];
  if(!data)throw std::runtime_error(error.localizedDescription.UTF8String?:"Cannot encode native property tree");
  return decode(data);
}
}
