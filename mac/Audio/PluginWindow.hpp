#pragma once
#import <AppKit/AppKit.h>
@interface RSPluginWindowDelegate : NSObject <NSWindowDelegate>
@property(nonatomic, copy) void (^onClose)(void);
@end
