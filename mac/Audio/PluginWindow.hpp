#pragma once
#import <AppKit/AppKit.h>
#include <vector>
@interface RSPluginWindowDelegate : NSObject <NSWindowDelegate>
@property(nonatomic, copy) void (^onClose)(void);
@end

// The host owns the window's content view. AU views keep their own bounds and
// transforms; their frame size, in AppKit points, determines the window size.
@interface RSPluginEditorContainer : NSView
- (instancetype)initWithPluginView:(NSView *)view;
- (void)synchronizePluginFrame;
@end
bool RSValidPluginEditorSize(NSSize size);
NSWindow *RSCreatePluginEditorWindow(NSSize size, NSString *title);
void RSResizePluginEditorWindow(NSWindow *window, NSSize size);

namespace Tracker {
// Native-main-owner lease. The shared capture owner still retains unexpected
// programmatic vendor notifications; this gates ordinary host-window input.
class MacPluginInputBarrier final {
  struct Control {NSControl *control;bool enabled;};
  NSWindow *window_=nil;
  std::vector<Control> controls_;
  void release()noexcept;
public:
  explicit MacPluginInputBarrier(NSWindow *);
  ~MacPluginInputBarrier();
  MacPluginInputBarrier(const MacPluginInputBarrier &)=delete;
  MacPluginInputBarrier &operator=(const MacPluginInputBarrier &)=delete;
};
}
