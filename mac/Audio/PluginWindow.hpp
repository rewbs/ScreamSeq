#pragma once
#import <AppKit/AppKit.h>
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
