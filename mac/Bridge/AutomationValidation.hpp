#pragma once
#import <Foundation/Foundation.h>
#include <cmath>
#include <set>
#include <stdexcept>
#include <tuple>

namespace Automation {
struct Error : std::runtime_error {
  int code;
  Error(int code, const char *message) : std::runtime_error(message), code(code) {}
};
inline void require(bool condition, const char *message) {
  if (!condition)
    throw Error(-32602, message);
}
inline NSDictionary *object(id value) {
  require([value isKindOfClass:NSDictionary.class], "Expected an object");
  return value;
}
inline NSArray *array(id value, NSUInteger maximum) {
  require([value isKindOfClass:NSArray.class] && [value count] <= maximum, "Invalid or oversized array");
  return value;
}
inline NSString *string(id value, NSUInteger maximum = 4096) {
  require([value isKindOfClass:NSString.class] && [value length] <= maximum, "Expected a bounded string");
  require([value rangeOfString:[NSString stringWithFormat:@"%C", unichar(0)]].location == NSNotFound,
          "Strings cannot contain NUL");
  return value;
}
inline double number(id value, double minimum, double maximum) {
  require([value isKindOfClass:NSNumber.class] && CFGetTypeID((__bridge CFTypeRef)value) != CFBooleanGetTypeID(),
          "Expected a number, not a boolean");
  double n = [value doubleValue];
  require(std::isfinite(n) && n >= minimum && n <= maximum, "Number outside its allowed range");
  return n;
}
inline uint64_t integer(id value, uint64_t minimum, uint64_t maximum) {
  double n = number(value, double(minimum), double(maximum));
  require(std::floor(n) == n, "Expected an integer");
  return uint64_t(n);
}
inline bool boolean(id value) {
  require(value && CFGetTypeID((__bridge CFTypeRef)value) == CFBooleanGetTypeID(), "Expected a boolean");
  return [value boolValue];
}
inline void keys(NSDictionary *value, NSArray<NSString *> *allowed) {
  object(value);
  for (id key in value)
    require([allowed containsObject:key], "Unknown parameter or field");
}
inline NSData *base64(id value, NSUInteger maximum) {
  NSString *text = string(value, ((maximum + 2) / 3) * 4);
  NSData *data = [[NSData alloc] initWithBase64EncodedString:text options:0];
  require(data && data.length <= maximum, "Invalid or oversized base64 data");
  return data;
}
inline void succeeded(BOOL result, NSError *error) {
  if (!result)
    throw Error(-32003, error.localizedDescription.UTF8String ?: "Operation failed");
}
} // namespace Automation
