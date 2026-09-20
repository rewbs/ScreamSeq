#pragma once
#import <Foundation/Foundation.h>
#include <exception>

namespace Tracker {
// Native plugins can touch AppKit/HIToolbox even without opening an editor.
// Call only from control paths; the main run loop must remain available while
// a document worker waits here. Never dispatch from the audio callback.
template <typename Function> void pluginMainCall(const Function &fn) {
  if ([NSThread isMainThread]) {
    fn();
    return;
  }
  __block std::exception_ptr error;
  dispatch_sync(dispatch_get_main_queue(), ^{
    try {
      fn();
    } catch (...) {
      error = std::current_exception();
    }
  });
  if (error)
    std::rethrow_exception(error);
}
} // namespace Tracker
