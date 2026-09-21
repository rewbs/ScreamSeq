#import <Foundation/Foundation.h>
NS_ASSUME_NONNULL_BEGIN
@interface TrackerSession : NSObject
@property(nonatomic, readonly) BOOL playing;
@property(nonatomic, readonly) BOOL canUndo;
@property(nonatomic, readonly) BOOL canRedo;
@property(nonatomic, readonly) double sampleRate;
@property(nonatomic, readonly) NSUInteger bufferSize;
// Called on the document queue, never concurrently with other session operations.
@property(nonatomic, readonly) NSString *automationRevision;
- (nullable NSDictionary *)automationMethod:(NSString *)method
                                     params:(NSDictionary *)params
                                      error:(NSError **)error __attribute__((swift_error(none)));
- (NSDictionary *)snapshot:(NSInteger)pattern;
- (NSDictionary *)telemetry;
- (NSArray<NSDictionary *> *)mixerMeters;
- (NSArray<NSDictionary *> *)devices;
- (BOOL)configureDevice:(NSUInteger)device buffer:(NSUInteger)buffer error:(NSError **)error;
- (BOOL)openPath:(NSString *)path error:(NSError **)error;
- (void)newSong:(BOOL)demo;
- (BOOL)savePath:(NSString *)path error:(NSError **)error;
// A recovery snapshot also preserves an unfinished take, without stopping it.
- (BOOL)saveRecoveryPath:(NSString *)path error:(NSError **)error;
- (nullable NSData *)recoveryData:(NSError **)error;
- (BOOL)playOrder:(NSInteger)order error:(NSError **)error;
- (BOOL)playRegion:(NSDictionary *)region error:(NSError **)error;
- (void)setPlaybackLoop:(BOOL)enabled;
- (BOOL)sampleNote:(NSInteger)note sample:(NSInteger)sample velocity:(NSInteger)velocity on:(BOOL)on;
- (void)stop;
- (BOOL)editPattern:(NSInteger)pattern
                row:(NSInteger)row
            channel:(NSInteger)channel
             values:(NSArray<NSNumber *> *)values
              error:(NSError **)error;
- (void)undo;
- (void)redo;
- (void)muteChannel:(NSInteger)channel muted:(BOOL)muted;
- (BOOL)editCells:(NSArray<NSDictionary *> *)edits error:(NSError **)error;
- (NSInteger)addPattern:(NSInteger)rows
              duplicate:(BOOL)duplicate
                 source:(NSInteger)source
                  error:(NSError **)error __attribute__((swift_error(nonnull_error)));
- (BOOL)removeOrder:(NSInteger)order error:(NSError **)error;
- (BOOL)editOrder:(NSInteger)order pattern:(NSInteger)pattern operation:(NSString *)operation error:(NSError **)error;
- (BOOL)selectSequence:(NSInteger)sequence error:(NSError **)error;
- (BOOL)songTitle:(NSString *)title
            tempo:(NSInteger)tempo
            speed:(NSInteger)speed
         channels:(NSInteger)channels
            error:(NSError **)error;
- (NSDictionary *)sampleInfo:(NSInteger)sample;
- (NSInteger)importSample:(NSString *)path
                     slot:(NSInteger)slot
                    error:(NSError **)error __attribute__((swift_error(nonnull_error)));
- (BOOL)processSample:(NSInteger)sample
            operation:(NSString *)operation
                start:(NSUInteger)start
                  end:(NSUInteger)end
                error:(NSError **)error;
- (BOOL)sampleSettings:(NSInteger)sample values:(NSDictionary *)values error:(NSError **)error;
- (NSInteger)addInstrument:(NSInteger)sample error:(NSError **)error __attribute__((swift_error(nonnull_error)));
- (NSInteger)importInstrument:(NSString *)path
                         slot:(NSInteger)slot
                        error:(NSError **)error __attribute__((swift_error(nonnull_error)));
- (NSDictionary *)instrumentInfo:(NSInteger)instrument;
- (BOOL)instrumentSettings:(NSInteger)instrument values:(NSDictionary *)values error:(NSError **)error;
- (BOOL)previewSample:(NSInteger)sample note:(NSInteger)note error:(NSError **)error;
- (NSArray<NSDictionary *> *)availablePlugins:(NSError **)error __attribute__((swift_error(nonnull_error)));
- (NSArray<NSDictionary *> *)builtInPlugins;
- (NSArray<NSDictionary *> *)availablePluginsRescan:(BOOL)rescan
                                              error:(NSError **)error __attribute__((swift_error(nonnull_error)));
- (BOOL)addPlugin:(NSDictionary *)descriptor error:(NSError **)error;
- (BOOL)assignPlugin:(NSInteger)slot instrument:(NSInteger)instrument error:(NSError **)error;
- (BOOL)showPluginEditor:(NSInteger)slot error:(NSError **)error;
- (NSInteger)collectPluginEdits:(BOOL)record error:(NSError **)error;
- (BOOL)removePlugin:(NSInteger)slot error:(NSError **)error;
- (BOOL)movePlugin:(NSInteger)slot direction:(NSInteger)direction error:(NSError **)error;
- (BOOL)bypassPlugin:(NSInteger)slot bypass:(BOOL)bypass error:(NSError **)error;
- (NSArray<NSDictionary *> *)pluginParameters:(NSInteger)slot;
- (NSDictionary *)pluginMeters:(NSInteger)slot;
- (BOOL)pluginParameter:(NSInteger)slot
             identifier:(NSInteger)identifier
                  value:(double)value
                 record:(BOOL)record
                  error:(NSError **)error;
- (BOOL)clearAutomation:(NSError **)error;
- (BOOL)undoEffectChange:(NSError **)error;
- (BOOL)redoEffectChange:(NSError **)error;
- (BOOL)deviceChanged;
- (BOOL)refreshDevice:(NSError **)error;
- (NSArray<NSDictionary *> *)midiSources;
- (BOOL)connectMIDI:(NSUInteger)source error:(NSError **)error;
- (NSArray<NSDictionary *> *)midiEvents;
@property(nonatomic, readonly) BOOL recordingActive;
@property(nonatomic, readonly, nullable) NSString *recordingTakeID;
- (BOOL)prepareAudition:(NSError **)error;
- (BOOL)note:(NSInteger)note instrument:(NSInteger)instrument velocity:(NSInteger)velocity on:(BOOL)on;
- (void)panic;
- (NSData *)serializedData;
+ (BOOL)exportData:(NSData *)data path:(NSString *)path error:(NSError **)error;
+ (NSDictionary *)formulaReference;
// Independent decoder for library audition: no song, history or audio-device mutation.
+ (nullable NSDictionary *)inspectSampleFile:(NSString *)path error:(NSError **)error;
@end
NS_ASSUME_NONNULL_END
