#import "mac/Audio/PluginWindow.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

@interface RSInputProtectionProbe : NSView
@property(nonatomic) NSUInteger keyCount;
@end
@implementation RSInputProtectionProbe
- (BOOL)acceptsFirstResponder{return YES;}
- (void)keyDown:(NSEvent *)event{++self.keyCount;}
@end
static void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
static void inputProtection(){
  NSWindow *window=RSCreatePluginEditorWindow(NSMakeSize(400,200),@"Owned input protection");
  NSWindow *other=RSCreatePluginEditorWindow(NSMakeSize(200,100),@"Unrelated editor");
  window.contentView=[[RSInputProtectionProbe alloc] initWithFrame:window.contentView.frame];
  other.contentView=[[RSInputProtectionProbe alloc] initWithFrame:other.contentView.frame];
  __block unsigned closeCount=0;
  RSPluginWindowDelegate *delegate=[RSPluginWindowDelegate new];delegate.onClose=^{++closeCount;};window.delegate=delegate;
  NSSlider *slider=[[NSSlider alloc] initWithFrame:NSMakeRect(10,10,200,24)];slider.doubleValue=.375;
  NSButton *disabled=[[NSButton alloc] initWithFrame:NSMakeRect(10,50,100,24)];disabled.enabled=NO;
  NSTextField *text=[[NSTextField alloc] initWithFrame:NSMakeRect(10,90,200,24)];text.stringValue=@"uncommitted raw text";
  [window.contentView addSubview:slider];[window.contentView addSubview:disabled];[window.contentView addSubview:text];
  check([window makeFirstResponder:window.contentView]&&[other makeFirstResponder:other.contentView],"Owned key responder setup failed");
  NSResponder *responder=window.firstResponder;const auto frame=window.frame;
  NSEvent *key=[NSEvent keyEventWithType:NSEventTypeKeyDown location:NSZeroPoint modifierFlags:0 timestamp:0
    windowNumber:window.windowNumber context:nil characters:@"x" charactersIgnoringModifiers:@"x" isARepeat:NO keyCode:7];
  check(![window performKeyEquivalent:key]&&![other performKeyEquivalent:key],"Neutral key must have no shortcut before protection");
  [window sendEvent:key];check(((RSInputProtectionProbe *)window.contentView).keyCount==1,"Owned key event failed before protection");
  {
    Tracker::MacPluginInputBarrier barrier(window);
    check(!slider.enabled&&!text.enabled&&!disabled.enabled,"Protected editor retained enabled native input");
    check([window performKeyEquivalent:key]&&![other performKeyEquivalent:key],"Input protection leaked to another editor");
    [window sendEvent:key];[other sendEvent:key];
    check(((RSInputProtectionProbe *)window.contentView).keyCount==1&&((RSInputProtectionProbe *)other.contentView).keyCount==1,
      "Protected key reached the vendor or blocked unrelated input");
    check(slider.doubleValue==.375&&[text.stringValue isEqualToString:@"uncommitted raw text"]&&
      NSEqualRects(window.frame,frame)&&window.firstResponder==responder,"Protection changed values, geometry or native focus");
    bool nested=false;try{Tracker::MacPluginInputBarrier again(window);}catch(const std::exception &){nested=true;}
    check(nested,"Nested protection accepted the same native window");
    // Mutate neither focus nor the key window during cleanup, even if the
    // musician moved to another owned window while the transaction was held.
    [other makeFirstResponder:other.contentView];NSResponder *otherResponder=other.firstResponder;
    [window performClose:nil];[window close];
    check(!closeCount&&window.contentView==slider.superview&&other.firstResponder==otherResponder,"Protected close detached controls or changed unrelated focus");
  }
  [window sendEvent:key];check(((RSInputProtectionProbe *)window.contentView).keyCount==2,"Release did not restore owned key dispatch");
  check(slider.enabled&&text.enabled&&!disabled.enabled&&![window performKeyEquivalent:key],"Release failed to restore exact enabled states and key routing");
  check(window.firstResponder==responder&&slider.doubleValue==.375&&[text.stringValue isEqualToString:@"uncommitted raw text"],
    "Release changed raw input or native focus");
  // A field editor owns the raw draft/IME state. Refuse before disabling any
  // control, rather than committing or cancelling that draft to obtain a lease.
  NSTextView *fieldEditor=[[NSTextView alloc] initWithFrame:NSZeroRect];fieldEditor.fieldEditor=YES;
  [window.contentView addSubview:fieldEditor];check([window makeFirstResponder:fieldEditor],"Owned field editor focus setup failed");
  bool draftRefused=false;try{Tracker::MacPluginInputBarrier barrier(window);}catch(const std::exception &){draftRefused=true;}
  check(draftRefused&&slider.enabled&&text.enabled&&!disabled.enabled&&window.firstResponder==fieldEditor,
    "Active text draft was mutated or accepted for baseline restoration");
  [window makeFirstResponder:window.contentView];[fieldEditor removeFromSuperview];
  [window addChildWindow:other ordered:NSWindowAbove];
  bool childRefused=false;try{Tracker::MacPluginInputBarrier barrier(window);}catch(const std::exception &){childRefused=true;}
  check(childRefused&&slider.enabled,"Vendor child window was silently omitted from input protection");
  [window removeChildWindow:other];
  {Tracker::MacPluginInputBarrier barrier(window);}
  [window close];[other close];
  check(closeCount==1,"Release did not restore normal close notification");window.delegate=nil;
}
int main(){@autoreleasepool {try{
  [NSApplication sharedApplication];
  // AU factories may return a translated/scaled view. Bounds describe the
  // plugin's drawing coordinates; only frame size describes its host footprint.
  NSView *plugin=[[NSView alloc] initWithFrame:NSMakeRect(19,83,640,360)];
  plugin.bounds=NSMakeRect(7,11,1280,720);
  plugin.autoresizingMask=NSViewWidthSizable|NSViewHeightSizable;
  const auto bounds=plugin.bounds;
  NSWindow *window=RSCreatePluginEditorWindow(plugin.frame.size,@"Editor geometry regression");
  RSPluginEditorContainer *container=[[RSPluginEditorContainer alloc] initWithPluginView:plugin];
  window.contentView=container;[container synchronizePluginFrame];
  check(window.contentView!=plugin&&plugin.superview==container,"Vendor view must not be the window content view");
  check(NSEqualRects(plugin.frame,NSMakeRect(0,0,640,360))&&NSEqualRects(plugin.bounds,bounds),"Embedding must normalize only frame origin, preserving vendor drawing bounds");
  check(NSEqualSizes(container.frame.size,NSMakeSize(640,360)),"Window must use point frame size, not scaled bounds");
  const auto top=NSMaxY(window.frame);
  [plugin setFrame:NSMakeRect(35,61,480,240)];
  check(NSEqualRects(plugin.frame,NSMakeRect(0,0,480,240)),"AU resize must reset origin without doubling autoresize");
  check(NSEqualSizes(container.frame.size,NSMakeSize(480,240))&&NSMaxY(window.frame)==top,"AU resize must fit the content and preserve the window top");
  [plugin setFrameSize:NSMakeSize(700,410)];
  check(NSEqualSizes(window.contentView.frame.size,plugin.frame.size),"Repeated plugin resize must leave no clipped or blank content band");
  check(!RSValidPluginEditorSize(NSMakeSize(NAN,200))&&!RSValidPluginEditorSize(NSMakeSize(100,INFINITY))&&!RSValidPluginEditorSize(NSMakeSize(0,200))&&!RSValidPluginEditorSize(NSMakeSize(8193,200)),"Invalid plugin sizes must be rejected");
  [window close];
  inputProtection();
  std::cout<<"PASS offscreen plugin editor geometry: owned container, scaled/offset AU view, dynamic resize and top-edge preservation\n";
  std::cout<<"PASS owned input protection: enabled-state restoration, independent windows, raw text/focus and draft/child refusal\n";
  return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}}
