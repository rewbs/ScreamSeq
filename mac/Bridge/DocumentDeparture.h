#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN
// Main-thread presentation callbacks. Capture must inspect native state only;
// the document worker is waiting here and must never be read synchronously.
@protocol TrackerDocumentDepartureDelegate <NSObject>
- (NSArray<NSString *> *)retainedNativeEditorWork;
- (void)documentReplacementWillBegin;
// Failure releases input immediately. Success keeps it protected until the
// application has refreshed the newly adopted document.
- (void)documentReplacementDidFinish:(BOOL)adopted;
@end
NS_ASSUME_NONNULL_END
