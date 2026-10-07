#import "mac/Audio/PluginWindow.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

static void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
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
  std::cout<<"PASS offscreen plugin editor geometry: owned container, scaled/offset AU view, dynamic resize and top-edge preservation\n";
  return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}}
