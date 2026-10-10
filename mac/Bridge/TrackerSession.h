#import <Foundation/Foundation.h>
#include "../../editor/PatternDisplay.h"
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
- (NSDictionary *)signalTelemetry;
// Root graph UI only; same reading semantics, excluding individual-copy ports.
// Call under the same session ownership guard as signalTelemetry.
- (NSDictionary *)songSignalTelemetry;
- (NSDictionary *)routingTelemetry;
- (NSDictionary *)listenTelemetry;
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
// Terminal operation: call on the main thread after draining document work.
// Idempotent; no further session operations are allowed after shutdown.
- (void)shutdown;
- (BOOL)editPattern:(NSInteger)pattern
                row:(NSInteger)row
            channel:(NSInteger)channel
             values:(NSArray<NSNumber *> *)values
              error:(NSError **)error;
- (void)undo;
- (void)redo;
- (BOOL)historyUndo:(BOOL)redo error:(NSError **)error;
- (void)parameterGesture:(BOOL)active;
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
/// Adds a plugin chosen by the user or an agent. A VST3 descriptor names its
/// bundle directly; after validation that bundle is trusted for this process
/// and recorded in the app's private per-user trust store for later launches.
- (BOOL)addPlugin:(NSDictionary *)descriptor error:(NSError **)error;
/// Trusts one .vst3 bundle, or every bundle inside a folder, until the process
/// exits. Nothing is written to the persistent trust store. Call only for a location the user chose explicitly (or a test fixture).
/// VST3 paths stored in projects, recovery files and graph recipes are hints:
/// they load only from a standard VST3 folder, the scanned inventory or a
/// location trusted here; otherwise the plugin is kept as missing with its state.
/// Returns NO when the location does not exist.
+ (BOOL)trustPluginLocation:(NSString *)path;
/// Missing VST3 plugins of the open document whose stored bundle exists on this
/// Mac outside the trusted locations: name, classID, storedPath, canonicalPath
/// and kind ("rack" or "graph"). Nothing is loaded. Ask the user before trusting.
@property(nonatomic, readonly) NSArray<NSDictionary *> *unresolvedPluginLocations;
/// Trusts canonical paths the user approved from unresolvedPluginLocations,
/// remembers them for later launches and resolves the waiting rack and graph
/// plugins in place, keeping their saved state. Never call without consent.
- (BOOL)trustPluginLocations:(NSArray<NSString *> *)canonicalPaths error:(NSError **)error;
- (BOOL)addPlugin:(NSDictionary *)descriptor target:(nullable NSString *)target error:(NSError **)error;
- (BOOL)addPlugin:(NSDictionary *)descriptor target:(nullable NSString *)target before:(nullable NSString *)before position:(nullable NSDictionary *)position error:(NSError **)error;
- (BOOL)addPlugin:(NSDictionary *)descriptor target:(nullable NSString *)target before:(nullable NSString *)before position:(nullable NSDictionary *)position parent:(nullable NSString *)parent error:(NSError **)error;
- (BOOL)addPlugin:(NSDictionary *)descriptor target:(nullable NSString *)target before:(nullable NSString *)before position:(nullable NSDictionary *)position parent:(nullable NSString *)parent detached:(BOOL)detached error:(NSError **)error;
- (BOOL)assignPlugin:(NSInteger)slot instrument:(NSInteger)instrument error:(NSError **)error;
- (BOOL)showPluginEditor:(NSInteger)slot error:(NSError **)error;
- (NSInteger)collectPluginEdits:(BOOL)record error:(NSError **)error;
- (BOOL)removePlugin:(NSInteger)slot error:(NSError **)error;
- (BOOL)removePlugins:(NSArray<NSString *> *)identifiers error:(NSError **)error;
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
- (BOOL)pluginLatencyChanged;
- (BOOL)refreshPluginLatencies:(NSError **)error;
- (BOOL)refreshDevice:(NSError **)error;
- (NSArray<NSDictionary *> *)midiSources;
- (BOOL)connectMIDI:(NSUInteger)source error:(NSError **)error;
- (NSArray<NSDictionary *> *)midiEvents;
@property(nonatomic, readonly) BOOL recordingActive;
@property(nonatomic, readonly, nullable) NSString *recordingTakeID;
@property(nonatomic, readonly, nullable) NSString *sampleRecordingTakeID;
- (BOOL)prepareAudition:(NSError **)error;
- (BOOL)note:(NSInteger)note instrument:(NSInteger)instrument velocity:(NSInteger)velocity on:(BOOL)on;
- (BOOL)note:(NSInteger)note instrument:(NSInteger)instrument velocity:(NSInteger)velocity on:(BOOL)on channel:(NSInteger)channel;
- (BOOL)sampleNote:(NSInteger)note sample:(NSInteger)sample velocity:(NSInteger)velocity on:(BOOL)on channel:(NSInteger)channel;
- (void)panic;
- (NSData *)serializedData;
+ (BOOL)exportData:(NSData *)data path:(NSString *)path error:(NSError **)error;
+ (NSDictionary *)formulaReference;
// Independent decoder for library audition: no song, history or audio-device mutation.
+ (nullable NSDictionary *)inspectSampleFile:(NSString *)path error:(NSError **)error;
@end
NS_ASSUME_NONNULL_END
