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
// A model-reading projection only. Foundation's binary plist reader rejects
// some valid opaque UID/wide-integer encodings. These values have no supported
// musical meaning, so expose NSNull to field validation rather than narrowing
// them or pretending their bytes are ordinary NSData. Preserve the complete
// original typed tree separately; never serialize this projection as a copy.
inline id modelProjection(const Value &tree) {
  if(tree.is_object()) {
    NSMutableDictionary *result=[NSMutableDictionary dictionaryWithCapacity:tree.size()];
    for(auto item=tree.begin();item!=tree.end();++item) {
      NSString *key=[[NSString alloc] initWithBytes:item.key().data() length:item.key().size() encoding:NSUTF8StringEncoding];
      if(!key)throw std::runtime_error("Invalid UTF-8 project property name");
      result[key]=modelProjection(item.value());
    }
    return result;
  }
  if(tree.is_array()) {
    NSMutableArray *result=[NSMutableArray arrayWithCapacity:tree.size()];
    for(const auto &item:tree)[result addObject:modelProjection(item)];
    return result;
  }
  if(tree.is_binary()) {
    const auto &bytes=tree.get_binary();
    if(bytes.has_subtype())return NSNull.null;
    return [NSData dataWithBytes:bytes.data() length:bytes.size()];
  }
  if(tree.is_boolean())return @(tree.get<bool>());
  if(tree.is_number_unsigned())return @(tree.get<uint64_t>());
  if(tree.is_number_integer())return @(tree.get<int64_t>());
  if(tree.is_number_float())return @(tree.get<double>());
  if(tree.is_string()) {
    const auto &text=tree.get_ref<const std::string &>();
    NSString *result=[[NSString alloc] initWithBytes:text.data() length:text.size() encoding:NSUTF8StringEncoding];
    if(!result)throw std::runtime_error("Invalid UTF-8 project property string");
    return result;
  }
  return NSNull.null;
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
