#import "TrackerSession.h"
#include "../Audio/AudioDevice.hpp"
#include "../Audio/AudioExport.hpp"
#include "editor/CurveFormulaReference.hpp"
#include "../Audio/MidiInput.hpp"
#include "../Plugins/PluginInventory.hpp"
#include "../Plugins/PluginLibrary.hpp"
#include "../Plugins/PluginPreset.hpp"
#include "AutomationValidation.hpp"
#include "editor/PatternTools.hpp"
#include "editor/AutomationTools.hpp"
#include "editor/InstrumentEnvelopeTools.hpp"
#include "editor/PatternCommands.hpp"
#include "editor/TrackLayout.hpp"
#include "editor/SampleArchive.hpp"
#include "editor/SongTiming.hpp"
#include "editor/NoteRecording.hpp"
#include <mach/mach_time.h>
#include "editor/ArrangementTools.hpp"
#include "soundlib/ModInstrument.h"
#include "soundlib/mod_specifications.h"
#include "soundlib/NativeNoteEffects.h"
#include <CommonCrypto/CommonDigest.h>
#include <chrono>
#include <cmath>
#include <fcntl.h>
#include <sys/file.h>
#include <limits>
#include <poll.h>
#include <signal.h>
#include <unistd.h>
using namespace Tracker;
namespace {
#include "NativeSongMetadata.inc"
#include "PatternCommands.inc"
#include "TrackLayout.inc"
#include "SongTiming.inc"
#include "InstrumentEnvelopeTools.inc"
#include "EnvelopeCatalogue.inc"
#include "PluginPrograms.inc"
void failure(NSError **e, const std::exception &ex) {
  if (e)
    *e = [NSError errorWithDomain:@"Tracker" code:1 userInfo:@{NSLocalizedDescriptionKey : @(ex.what())}];
}
} // namespace
namespace {
NSData *runScanner(NSArray<NSString *> *arguments) {
  NSString *path = [NSBundle.mainBundle.executablePath.stringByDeletingLastPathComponent
      stringByAppendingPathComponent:@"plugin-scanner"];
  NSTask *task = [NSTask new];
  task.executableURL = [NSURL fileURLWithPath:path];
  task.arguments = arguments;
  NSPipe *output = [NSPipe pipe], *errors = [NSPipe pipe];
  task.standardOutput = output;
  task.standardError = errors;
  NSError *error = nil;
  if (![task launchAndReturnError:&error])
    throw std::runtime_error(error.localizedDescription.UTF8String);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  const int descriptors[] = {output.fileHandleForReading.fileDescriptor, errors.fileHandleForReading.fileDescriptor};
  for (auto fd : descriptors)
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
  NSMutableData *data = [NSMutableData data], *details = [NSMutableData data];
  auto drain = [&](int fd, NSMutableData *destination, size_t maximum) {
    std::array<char, 16384> buffer;
    for (int count = 0; count < 16; ++count) {
      const auto received = ::read(fd, buffer.data(), buffer.size());
      if (received <= 0)
        break;
      const auto keep = std::min(size_t(received), maximum - std::min(size_t(destination.length), maximum));
      if (keep)
        [destination appendBytes:buffer.data() length:keep];
    }
  };
  while (true) {
    drain(descriptors[0], data, 8 * 1024 * 1024);
    drain(descriptors[1], details, 64 * 1024);
    if (!task.running) {
      drain(descriptors[0], data, 8 * 1024 * 1024);
      drain(descriptors[1], details, 64 * 1024);
      break;
    }
    if (std::chrono::steady_clock::now() >= deadline || data.length >= 8 * 1024 * 1024) {
      kill(task.processIdentifier, SIGKILL);
      [task waitUntilExit];
      throw std::runtime_error(
          "Plugin scan exceeded its time or output limit. The scanner was isolated from the application.");
    }
    pollfd waiting[] = {{descriptors[0], POLLIN, 0}, {descriptors[1], POLLIN, 0}};
    poll(waiting, 2, 20);
  }
  if (task.terminationStatus != 0) {
    NSString *text = [[NSString alloc] initWithData:details encoding:NSUTF8StringEncoding];
    throw std::runtime_error(text.length ? text.UTF8String : "The plugin crashed during validation.");
  }
  return data;
}
uint64_t unsignedInteger(id value, uint64_t maximum, const char *message) {
  if (![value isKindOfClass:NSNumber.class])
    throw std::runtime_error(message);
  const double number = [value doubleValue];
  if (!std::isfinite(number) || number < 0 || number > double(maximum) || std::floor(number) != number)
    throw std::runtime_error(message);
  return [value unsignedLongLongValue];
}
PluginDescriptor descriptor(NSDictionary *item) {
  if (![item isKindOfClass:NSDictionary.class])
    throw std::runtime_error("Invalid Audio Unit description");
  PluginDescriptor d;
  d.format = [item[@"format"] isKindOfClass:NSString.class] ? [item[@"format"] UTF8String] : "AU";
  if (d.format != "AU" && d.format != "VST3" && d.format != "Built-in")
    throw std::runtime_error("Unsupported native plugin format");
  d.type = uint32_t(unsignedInteger(item[@"type"], UINT32_MAX, "Invalid plugin type"));
  d.subtype = uint32_t(unsignedInteger(item[@"subtype"], UINT32_MAX, "Invalid plugin subtype"));
  d.manufacturer = uint32_t(unsignedInteger(item[@"manufacturer"], UINT32_MAX, "Invalid plugin manufacturer"));
  d.name = [item[@"name"] isKindOfClass:NSString.class] ? [item[@"name"] UTF8String] : "Plugin";
  if (d.format == "VST3") {
    if (![item[@"path"] isKindOfClass:NSString.class] || ![item[@"classID"] isKindOfClass:NSString.class] ||
        [item[@"classID"] length] != 32)
      throw std::runtime_error("Invalid VST3 bundle or class identifier");
    d.path = [item[@"path"] UTF8String];
    d.classID = [item[@"classID"] UTF8String];
  }
  if (item[@"isInstrument"] && ![item[@"isInstrument"] isKindOfClass:NSNumber.class])
    throw std::runtime_error("Invalid plugin instrument flag");
  d.instrument = d.type == kAudioUnitType_MusicDevice || [item[@"isInstrument"] boolValue];
  if (d.format == "Built-in") {
    d.classID = Automation::string(item[@"classID"], 128).UTF8String;
    if (d.type || d.subtype || d.manufacturer || d.instrument || (item[@"path"] && [Automation::string(item[@"path"]) length]))
      throw std::invalid_argument("Invalid built-in effect descriptor");
    d.name = nativeEffect(d.classID).name;
  }
  return d;
}
NSDictionary *descriptorDictionary(const PluginDescriptor &d) {
  return @{
    @"type" : @(d.type),
    @"subtype" : @(d.subtype),
    @"manufacturer" : @(d.manufacturer),
    @"name" : @(d.name.c_str()),
    @"format" : @(d.format.c_str()),
    @"path" : @(d.path.c_str()),
    @"classID" : @(d.classID.c_str()),
    @"isInstrument" : @(d.instrument || d.type == kAudioUnitType_MusicDevice)
  };
}
NSArray *pluginBusDictionaries(const std::vector<PluginAudioBus> &buses) {
  NSMutableArray *result = [NSMutableArray array];
  for (const auto &bus : buses) [result addObject:@{@"index": @(bus.index), @"channels": @(bus.channels),
    @"name": @(bus.name.c_str()), @"direction": bus.input ? @"input" : @"output", @"active": @(bus.active), @"supported": @(bus.supported)}];
  return result;
}
NSString *songString(const CSoundFile &song, const std::string &value);
#include "PluginAssignments.inc"
NSDictionary *decodeProject(NSData *data) {
  if (!data || data.length > 600 * 1024 * 1024)
    throw std::runtime_error("Project is unreadable or exceeds 600 MB");
  NSError *error = nil;
  id root = [NSPropertyListSerialization propertyListWithData:data
                                                      options:NSPropertyListImmutable
                                                       format:nil
                                                        error:&error];
  if (![root isKindOfClass:NSDictionary.class] ||
      ![root[@"module"] isKindOfClass:NSData.class] || ![root[@"plugins"] isKindOfClass:NSArray.class])
    throw std::runtime_error("Invalid or unsupported ScreamSeq project.");
  if ([root[@"module"] length] == 0 || [root[@"module"] length] > 512 * 1024 * 1024)
    throw std::runtime_error("Invalid embedded module size");
  const auto version = Automation::integer(root[@"version"], 1, 5);
  NSData *module = root[@"module"];
  const bool snapshot = isSongSnapshot({static_cast<const std::byte *>(module.bytes), module.length});
  if (snapshot != (version >= 4))
    throw std::runtime_error("The embedded song does not match the project version");
  return root;
}
std::vector<PluginState> decodePlugins(NSDictionary *root) {
  NSArray *plugins = root[@"plugins"];
  if (plugins.count > maximumNativePlugins)
    throw std::runtime_error("Project exceeds 64 native devices");
  std::vector<PluginState> states;
  std::set<std::string> instanceIDs;
  for (id item in plugins) {
    if (![item isKindOfClass:NSDictionary.class] || ![item[@"state"] isKindOfClass:NSData.class] ||
        ![item[@"type"] isKindOfClass:NSNumber.class] || ![item[@"subtype"] isKindOfClass:NSNumber.class] ||
        ![item[@"manufacturer"] isKindOfClass:NSNumber.class])
      throw std::runtime_error("Invalid Audio Unit state entry");
    NSData *data = item[@"state"];
    if (data.length > 16 * 1024 * 1024)
      throw std::runtime_error("Audio Unit state exceeds 16 MB");
    if (item[@"bypass"] && ![item[@"bypass"] isKindOfClass:NSNumber.class])
      throw std::runtime_error("Invalid Audio Unit bypass value");
    PluginState state{descriptor(item), {}, [item[@"bypass"] boolValue]};
    state.instanceID = item[@"instanceID"] ? Automation::string(item[@"instanceID"], 128).UTF8String : NSUUID.UUID.UUIDString.UTF8String;
    if (state.instanceID.empty() || !instanceIDs.insert(state.instanceID).second)
      throw std::runtime_error("Invalid or duplicate plugin instance identity");
    auto busIndices = [](id raw) {
      std::vector<uint32_t> result;
      if (!raw) return result;
      for (id value : Automation::array(raw, 63)) {
        const auto index = uint32_t(Automation::integer(value, 1, 63));
        if (std::find(result.begin(), result.end(), index) != result.end()) throw std::runtime_error("Duplicate plugin bus index");
        result.push_back(index);
      }
      std::sort(result.begin(), result.end()); return result;
    };
    state.auxiliaryInputs = busIndices(item[@"auxiliaryInputs"]);
    state.auxiliaryOutputs = busIndices(item[@"auxiliaryOutputs"]);
    if (item[@"instrument"])
      state.instrument = uint32_t(unsignedInteger(item[@"instrument"], 255, "Invalid tracker instrument assignment"));
    if ([root[@"version"] unsignedIntegerValue] >= 5) {
      auto assignments = decodePluginAssignments(item[@"instrumentAssignments"]);
      if ((assignments.empty() ? 0 : assignments.front().instrument) != state.instrument)
        throw std::runtime_error("Primary instrument does not match plugin assignments");
      setPluginAssignments(state, assignments);
    } else Automation::require(!item[@"instrumentAssignments"], "Legacy projects cannot contain instrument aliases");
    if (data.length) {
      auto begin = static_cast<const std::byte *>(data.bytes);
      state.state.assign(begin, begin + data.length);
    }
    states.push_back(std::move(state));
  }
  validatePluginAssignments(states);
  return states;
}
std::vector<ParameterChange> decodeAutomation(NSDictionary *root) {
  std::vector<ParameterChange> result;
  id values = root[@"automation"];
  if (!values)
    return result;
  if (![values isKindOfClass:NSArray.class] || [values count] > 100000)
    throw std::runtime_error("Invalid automation data");
  for (id item in values) {
    if (![item isKindOfClass:NSArray.class] || [item count] != 4)
      throw std::runtime_error("Invalid automation point");
    for (id value in item)
      if (![value isKindOfClass:NSNumber.class])
        throw std::runtime_error("Invalid automation value");
    const auto slot = unsignedInteger(item[0], maximumNativePlugins - 1, "Invalid automation effect slot");
    const auto identifier = unsignedInteger(item[1], UINT32_MAX, "Invalid automation parameter");
    // Canonical 48 kHz timeline; one week is well beyond the supported export duration.
    const auto frame = unsignedInteger(item[3], uint64_t(48000) * 604800, "Invalid automation timestamp");
    const float value = [item[2] floatValue];
    if (slot >= [root[@"plugins"] count] || !std::isfinite(value))
      throw std::runtime_error("Invalid automation effect or value");
    result.push_back({uint32_t(slot), uint32_t(identifier), value, frame});
  }
  return result;
}
std::vector<std::byte> byteVector(NSData *data) {
  if (!data.length)
    return {};
  auto begin = static_cast<const std::byte *>(data.bytes);
  return {begin, begin + data.length};
}
NSString *songString(const CSoundFile &song, const std::string &value) {
  auto utf8 = ::OpenMPT::mpt::ToCharset(::OpenMPT::mpt::Charset::UTF8, song.GetCharsetInternal(), value);
  return [[NSString alloc] initWithBytes:utf8.data() length:utf8.size() encoding:NSUTF8StringEncoding] ?: @"";
}
struct EffectSnapshot {
  std::vector<PluginState> plugins;
  std::vector<ParameterChange> automation;
  size_t bytes() const {
    size_t result = automation.size() * sizeof(ParameterChange);
    for (const auto &plugin : plugins)
      result += plugin.state.size() + plugin.descriptor.name.size() + sizeof(PluginState) + plugin.aliases.size() * sizeof(PluginInstrumentAlias);
    return result;
  }
};
void trimEffectHistory(std::vector<EffectSnapshot> &history) {
  size_t bytes = 0;
  for (const auto &entry : history)
    bytes += entry.bytes();
  while (history.size() > 1 && (bytes > 128 * 1024 * 1024 || history.size() > 100)) {
    bytes -= history.front().bytes();
    history.erase(history.begin());
  }
}
} // namespace
@implementation TrackerSession {
  BOOL _playbackLoop, _isolatedSamplePreview;
  NSDictionary *_playbackRegion;
  std::unique_ptr<Document> _document;
  std::unique_ptr<AudioDevice> _audio;
  std::unique_ptr<MidiInput> _midi;
  std::unique_ptr<PluginInventory> _pluginInventory;
  std::unique_ptr<PluginLibrary> _pluginLibrary;
  std::vector<PluginState> _plugins;
  std::vector<ParameterChange> _automation;
  std::vector<ParameterChange> _manualParameters;
  std::vector<EffectSnapshot> _effectUndo, _effectRedo;
  std::string _pluginError;
  std::unique_ptr<NativePlugin> _graphEditorPlugin;
  GraphPluginRecipe _graphEditorRecipe;
  uint64_t _graphEditorGraph, _graphEditorNode;
  NSString *_graphEditorID,*_graphEditorDocument;
  NSString *_automationDocumentID;
  uint64_t _pluginRevision;
  std::string _lastTouchedPlugin;
  uint32_t _lastTouchedParameter;
  uint64_t _touchSequence;
  NSString *_lastTouchedSource;
  std::optional<SampleClipboard> _sampleClipboard;
  NSString *_sampleClipboardID;
  std::unique_ptr<NoteRecording> _recording;
  NSString *_recordingRevision, *_recordingID;
  double _recordingLatencyMS;
  uint64_t _recordingLastTimestamp, _recordingStartTimestamp;
}
- (instancetype)init {
  if ((self = [super init])) {
    _document = Document::demo();
    _automationDocumentID = NSUUID.UUID.UUIDString;
    _audio = std::make_unique<AudioDevice>();
    _midi = std::make_unique<MidiInput>();
    _pluginInventory = std::make_unique<PluginInventory>();
    _pluginLibrary = std::make_unique<PluginLibrary>();
  }
  return self;
}
- (BOOL)playing {
  return _audio->playing();
}
- (BOOL)canUndo {
  try { validatePluginCapacity(_plugins, _document->historyNative(false).mixer.buses.size()); } catch (...) { return NO; }
  return _document->canUndo();
}
- (BOOL)canRedo {
  try { validatePluginCapacity(_plugins, _document->historyNative(true).mixer.buses.size()); } catch (...) { return NO; }
  return _document->canRedo();
}
- (double)sampleRate {
  return _audio->sampleRate();
}
- (NSUInteger)bufferSize {
  return _audio->bufferSize();
}
- (void)newSong:(BOOL)demo {
  _audio->setPlugins({});
  _plugins.clear();
  _automation.clear();
  _manualParameters.clear();
  _effectUndo.clear();
  _effectRedo.clear();
  _pluginError.clear();
  _document = demo ? Document::demo() : std::make_unique<Document>();
  _automationDocumentID = NSUUID.UUID.UUIDString;
  _graphEditorPlugin.reset();_graphEditorID=nil;
  _lastTouchedPlugin.clear(); _touchSequence = 0; _lastTouchedSource = nil;
}
- (BOOL)openPath:(NSString *)path error:(NSError **)error {
  try {
    if(_recording)throw std::runtime_error("Finish or discard the recording take before opening another song.");
    std::unique_ptr<Document> next;
    std::vector<PluginState> plugins;
    std::vector<ParameterChange> automation;
    std::unique_ptr<NoteRecording> recoveredTake;
    bool recoveredTakeCompatible = false;
    if ([@[@"screamseq", @"resonance"] containsObject:path.pathExtension.lowercaseString]) {
      auto attributes = [[NSFileManager defaultManager] attributesOfItemAtPath:path error:nil];
      if ([attributes fileSize] > 600 * 1024 * 1024)
        throw std::runtime_error("Project exceeds 600 MB");
      NSDictionary *root = decodeProject([NSData dataWithContentsOfFile:path]);
      next = std::make_unique<Document>(byteVector(root[@"module"]));
      if (root[@"sequence"]) {
        const auto sequence = unsignedInteger(root[@"sequence"], UINT8_MAX, "Invalid sequence selection");
        if (sequence >= next->song().Order.GetNumSequences())
          throw std::runtime_error("The selected sequence is missing from this project.");
        next->song().Order.SetSequence(SEQUENCEINDEX(sequence));
      }
      if ([root[@"version"] unsignedIntegerValue] >= 3) next->restoreNative(decodeNativeSong(root[@"native"]));
      plugins = decodePlugins(root);
      automation = decodeAutomation(root);
      if (root[@"recoveryTake"]) {
        using namespace Automation;
        const auto data = object(root[@"recoveryTake"]);
        keys(data, @[@"compatible", @"events", @"missingTime", @"exhaustedVoices", @"overflow"]);
        recoveredTakeCompatible = boolean(data[@"compatible"]);
        recoveredTake = std::make_unique<NoteRecording>(next->native(), next->song(), std::vector<uint16_t>{0}, 1, 0);
        recoveredTake->capturing = false;
        for (id raw : array(data[@"events"], maximumPreciseNotes)) {
          const auto e = object(raw);
          keys(e, @[@"pattern", @"track", @"position", @"instrument", @"note", @"velocity"]);
          const auto note = uint8_t(integer(e[@"note"], 1, 255));
          require(note <= 120 || note == 254 || note == 255, "Invalid recovered note");
          recoveredTake->events.push_back({decodeNativeID(e[@"pattern"]), decodeNativeID(e[@"track"]),
            uint32_t(integer(e[@"position"], 0, UINT32_MAX)), uint16_t(integer(e[@"instrument"], 0, 255)),
            note, uint8_t(integer(e[@"velocity"], 1, 127))});
        }
        recoveredTake->missingTime = uint32_t(integer(data[@"missingTime"], 0, UINT32_MAX));
        recoveredTake->exhaustedVoices = uint32_t(integer(data[@"exhaustedVoices"], 0, UINT32_MAX));
        recoveredTake->overflow = uint32_t(integer(data[@"overflow"], 0, UINT32_MAX));
      }
    } else
      next = Document::open(path.UTF8String);
    validatePluginCapacity(plugins, next->native().mixer.buses.size());
    _audio->stop();
    _pluginError.clear();
    try {
      _audio->setPlugins(plugins, automation);
    } catch (const std::exception &e) {
      _audio->setPlugins({});
      _pluginError = e.what();
    }
    _plugins = std::move(plugins);
    _automation = std::move(automation);
    _manualParameters.clear();
    _effectUndo.clear();
    _effectRedo.clear();
    _graphEditorPlugin.reset();_graphEditorID=nil;
    _document = std::move(next);
    _automationDocumentID = NSUUID.UUID.UUIDString;
    _recording = std::move(recoveredTake);
    _recordingID = _recording ? NSUUID.UUID.UUIDString : nil;
    _recordingRevision = _recording ? (recoveredTakeCompatible ? self.automationRevision : @"unmatched-recovered-take") : nil;
    _lastTouchedPlugin.clear(); _touchSequence = 0; _lastTouchedSource = nil;
    return YES;
  } catch (const std::exception &e) {
    failure(error, e);
    return NO;
  }
}
// Fold manual parameter edits into the saved baseline without baking the current
// playback automation values into it. Runs on the control worker while UI writes
// are suspended; the live parameter queue remains bounded and cheap.
- (void)commitManualParameters {
  if (_manualParameters.empty())
    return;
  auto states = _plugins;
  for (size_t slot = 0; slot < states.size(); ++slot) {
    if (std::none_of(_manualParameters.begin(), _manualParameters.end(), [&](const auto &p) { return p.slot == slot; }))
      continue;
    NativePlugin plugin(states[slot], _audio->sampleRate());
    for (const auto &point : _manualParameters)
      if (point.slot == slot && !plugin.parameter(point.id, point.value))
        throw std::runtime_error("Plugin rejected a saved parameter value");
    auto state = plugin.state();
    state.bypass = states[slot].bypass;
    state.instrument = states[slot].instrument;
    states[slot] = std::move(state);
  }
  _plugins = std::move(states);
  _manualParameters.clear();
}
- (NSData *)projectData {
  return [self projectDataForRecovery:NO];
}
- (NSData *)projectDataForRecovery:(BOOL)recovery {
  [self commitManualParameters];
  _document->validateSamples();
  auto bytes = _document->snapshotData();
  // A recovery/save must not stop transport to query mutable AU state. During
  // playback use the cached baseline plus all parameter edits made by our controls.
  if (!_audio->active() && _pluginError.empty() && (_automation.empty() && (_document->native().automation.empty() && _document->native().performance.commands.empty() && !_audio->hasAutomatedState())))
    _plugins = _audio->pluginStates();
  auto plugins = [NSMutableArray array];
  const int projectVersion = nativeProjectVersion(_plugins);
  for (auto &plugin : _plugins) {
    NSMutableDictionary *item = [descriptorDictionary(plugin.descriptor) mutableCopy];
    item[@"bypass"] = @(plugin.bypass);
    item[@"instrument"] = @(plugin.instrument);
    if (projectVersion >= 5) item[@"instrumentAssignments"] = encodePluginAssignments(plugin);
    item[@"instanceID"] = @(plugin.instanceID.c_str());
    item[@"state"] = [NSData dataWithBytes:plugin.state.data() length:plugin.state.size()];
    NSMutableArray *inputs = [NSMutableArray array], *outputs = [NSMutableArray array];
    for (auto bus : plugin.auxiliaryInputs) [inputs addObject:@(bus)];
    for (auto bus : plugin.auxiliaryOutputs) [outputs addObject:@(bus)];
    item[@"auxiliaryInputs"] = inputs; item[@"auxiliaryOutputs"] = outputs;
    [plugins addObject:item];
  }
  auto automation = [NSMutableArray array];
  for (auto &point : _automation)
    [automation addObject:@[ @(point.slot), @(point.id), @(point.value), @(point.frame) ]];
  NSMutableDictionary *root = [@{
    @"version" : @(projectVersion),
    @"native" : encodeNativeSong(_document->native()),
    @"sequence" : @(_document->song().Order.GetCurrentSequenceIndex()),
    @"module" : [NSData dataWithBytes:bytes.data() length:bytes.size()],
    @"plugins" : plugins,
    @"automation" : automation
  } mutableCopy];
  if (recovery && _recording) {
    // Close held notes in a copy. The live take and audio clock keep running.
    auto take = *_recording;
    auto position = _audio->renderer() ? _audio->renderer()->recordingClock().locate(mach_absolute_time()) : std::nullopt;
    take.stop(position);
    NSMutableArray *events = [NSMutableArray array];
    for (const auto &n : take.events) [events addObject:@{@"pattern":nativeID(n.pattern), @"track":nativeID(n.track),
      @"position":@(n.position), @"instrument":@(n.instrument), @"note":@(n.note), @"velocity":@(n.velocity)}];
    root[@"recoveryTake"] = @{@"compatible":@([_recordingRevision isEqual:self.automationRevision]),
      @"events":events, @"missingTime":@(take.missingTime), @"exhaustedVoices":@(take.exhaustedVoices), @"overflow":@(take.overflow)};
  }
  NSError *error = nil;
  NSData *data = [NSPropertyListSerialization dataWithPropertyList:root
                                                            format:NSPropertyListBinaryFormat_v1_0
                                                           options:0
                                                             error:&error];
  if (!data)
    throw std::runtime_error(error.localizedDescription.UTF8String);
  if (data.length > 600 * 1024 * 1024)
    throw std::runtime_error("Project exceeds the 600 MB save/reopen limit");
  return data;
}
- (BOOL)saveRecoveryPath:(NSString *)path error:(NSError **)error {
  try {
    NSData *data = [self projectDataForRecovery:YES];
    NSError *writeError = nil;
    if (![data writeToFile:path options:NSDataWritingAtomic error:&writeError])
      throw std::runtime_error(writeError.localizedDescription.UTF8String);
    return YES;
  } catch (const std::exception &e) { failure(error, e); return NO; }
}
- (NSData *)recoveryData:(NSError **)error {
  try { return [self projectDataForRecovery:YES]; }
  catch (const std::exception &e) { failure(error, e); return nil; }
}
- (BOOL)savePath:(NSString *)path error:(NSError **)error {
  try {
    if(_recording)throw std::runtime_error("Finish or discard the recording take before saving.");
    if ([@[@"screamseq", @"resonance"] containsObject:path.pathExtension.lowercaseString]) {
      NSData *data = [self projectData];
      NSError *writeError = nil;
      if (![data writeToFile:path options:NSDataWritingAtomic error:&writeError])
        throw std::runtime_error(writeError.localizedDescription.UTF8String);
    } else {
      if (!_plugins.empty() || _document->native().hasAnnotations())
        throw std::runtime_error(
            "Use Save As and the .screamseq project format to retain native metadata, effects and automation.");
      _document->save(path.UTF8String, true);
    }
    return YES;
  } catch (const std::exception &e) {
    failure(error, e);
    return NO;
  }
}
- (BOOL)playOrder:(NSInteger)order error:(NSError **)error {
  return [self playRegion:@{@"order": @(order)} error:error];
}
- (void)setPlaybackLoop:(BOOL)enabled { _playbackLoop=enabled; _audio->loop(enabled); }
- (BOOL)playRegion:(NSDictionary *)settings error:(NSError **)error {
  try {
    using namespace Automation;
    keys(settings, @[@"order", @"pattern", @"startRow", @"endRow", @"cursorRow", @"loop"]);
    auto &song = _document->song();
    const auto order = uint32_t(integer(settings[@"order"] ?: @0, 0, UINT16_MAX));
    require(order < song.Order().size() && song.Order().IsValidPat(order), "Select a playable order");
    PlaybackRegion region;
    region.loop = settings[@"loop"] ? boolean(settings[@"loop"]) : bool(_playbackLoop);
    region.cursorRow = uint32_t(integer(settings[@"cursorRow"] ?: @0, 0, UINT16_MAX));
    if(settings[@"pattern"]) {
      region.pattern = uint32_t(integer(settings[@"pattern"], 0, UINT16_MAX));
      require(song.Patterns.IsValidPat(PATTERNINDEX(region.pattern)), "Pattern does not exist");
      region.startRow = uint32_t(integer(settings[@"startRow"] ?: @0, 0, UINT16_MAX));
      region.endRow = uint32_t(integer(settings[@"endRow"] ?: @(song.Patterns[region.pattern].GetNumRows()), 1, UINT16_MAX));
      require(region.startRow < region.endRow && region.endRow <= song.Patterns[region.pattern].GetNumRows() && region.cursorRow >= region.startRow && region.cursorRow < region.endRow, "Cursor must lie inside the playback range");
    } else {
      require(!settings[@"startRow"] && !settings[@"endRow"], "Row bounds require a pattern");
      require(region.cursorRow < song.Patterns[song.Order()[order]].GetNumRows(), "Cursor is outside the pattern");
    }
    if(_recording&&_recording->capturing&&!_recording->events.empty())throw std::runtime_error("Stop and commit or discard the current recording before restarting playback.");
    if (!_pluginError.empty())
      throw std::runtime_error(_pluginError);
    [self commitManualParameters];
    if ((_automation.empty() && (_document->native().automation.empty() && _document->native().performance.commands.empty() && !_audio->hasAutomatedState())))
      _plugins = _audio->pluginStates();
    _audio->setPlugins(_plugins, _automation, true);
    _audio->play(_document->playbackData(), order, false, _document->sourcePath(),
                 _document->song().Order.GetCurrentSequenceIndex(), &_document->native(), region);
    _playbackLoop=region.loop; _playbackRegion=[settings copy]; _isolatedSamplePreview=NO;
    return YES;
  } catch (const std::exception &e) {
    failure(error, e);
    return NO;
  }
}
- (void)stop {
  [self midiEvents];
  [self stopRecordingCapture];
  _audio->stop();
}
- (BOOL)configureDevice:(NSUInteger)device buffer:(NSUInteger)buffer error:(NSError **)error {
  try {
    _audio->configure(uint32_t(device), uint32_t(buffer));
    return YES;
  } catch (const std::exception &e) {
    failure(error, e);
    return NO;
  }
}
- (NSArray<NSDictionary *> *)devices {
  auto result = [NSMutableArray array];
  try {
    for (auto &d : AudioDevice::devices())
      [result addObject:@{@"id" : @(d.id), @"name" : @(d.name.c_str())}];
  } catch (...) {
  }
  return result;
}
- (NSDictionary *)telemetry {
  auto t = _audio->telemetry();
  return @{
    @"playing": @(self.playing), @"loop": @(_playbackLoop), @"region": _playbackRegion ?: @{},
    @"order" : @(t.order),
    @"pattern" : @(t.pattern),
    @"row" : @(t.row),
    @"voices" : @(t.voices),
    @"left" : @(t.left),
    @"right" : @(t.right),
    @"frames" : @(t.frames),
    @"callbacks" : @(t.callbacks),
    @"overruns" : @(t.overruns),
    @"maxMicros" : @(t.maxMicros),
    @"p999Micros" : @(t.p999Micros),
    @"pluginFailure" : @(_audio->pluginFailed()),
    @"pluginLatency" : @(_audio->pluginLatency()),
    @"graphActivity" : encodeSignalActivity(_audio->graphActivity()),
    @"fault" : @(_audio->renderer() && _audio->renderer()->faulted())
  };
}
- (NSDictionary *)snapshot:(NSInteger)pattern {
  auto &s = _document->song();
  auto samples = [NSMutableArray array];
  auto instruments = [NSMutableArray array];
  for (int i = 1; i <= s.GetNumSamples(); ++i) {
    auto &sample = s.GetSample(i);
    [samples addObject:@{
      @"index" : @(i),
      @"name" : songString(s, s.GetSampleName(i)),
      @"id" : nativeID(_document->native().samples.at(i).id),
      @"frames" : @(sample.nLength),
      @"rate" : @(sample.nC5Speed),
      @"loopStart" : @(sample.nLoopStart),
      @"loopEnd" : @(sample.nLoopEnd),
      @"loop" : @(sample.uFlags[CHN_LOOP])
    }];
  }
  for (int i = 1; i <= s.GetNumInstruments(); ++i)
    [instruments addObject:@{@"index" : @(i), @"name" : songString(s, s.GetInstrumentName(i)), @"id": nativeID(_document->native().instruments.at(i).id)}];
  auto orders = [NSMutableArray array];
  for (auto pat : s.Order())
    [orders addObject:@(pat)];
  auto sequences = [NSMutableArray array];
  for (SEQUENCEINDEX index = 0; index < s.Order.GetNumSequences(); ++index) {
    const auto name = ::OpenMPT::mpt::ToCharset(::OpenMPT::mpt::Charset::UTF8, s.Order(index).GetName());
    [sequences addObject:@{@"index" : @(index), @"name" : _document->native().sequences[index].info.name.empty() ? @(name.c_str()) : @(_document->native().sequences[index].info.name.c_str()), @"id": nativeID(_document->native().sequences[index].info.id)}];
  }
  const auto &native = _document->native();
  auto orderMetadata = [NSMutableArray array];
  for (const auto &entry : native.sequences[s.Order.GetCurrentSequenceIndex()].orders)
    [orderMetadata addObject:nativeEntity(entry)];
  auto tracks = [NSMutableArray array];
  for (const auto &[index, entry] : native.tracks) {
    NSMutableDictionary *item = [nativeEntity(entry) mutableCopy]; item[@"index"] = @(index); item[@"mute"] = @(effectiveColumnMute(native, s, index)); [tracks addObject:item];
  }
  auto patterns = [NSMutableArray array];
  for (int i = 0; i < s.Patterns.Size(); ++i)
    if (s.Patterns.IsValidPat(i))
    {
      NSMutableDictionary *item = [nativeEntity(native.patterns.at(uint16_t(i))) mutableCopy];
      item[@"index"] = @(i); item[@"rows"] = @(s.Patterns[i].GetNumRows());
      [patterns addObject:item];
    }
  if (pattern < 0 || !s.Patterns.IsValidPat(pattern))
    pattern = patterns.count ? [patterns[0][@"index"] integerValue] : 0;
  NSInteger rows = s.Patterns.IsValidPat(pattern) ? s.Patterns[pattern].GetNumRows() : 0;
  auto data = [NSMutableData dataWithLength:rows * s.GetNumChannels() * 6];
  auto out = static_cast<uint8_t *>(data.mutableBytes);
  for (int r = 0; r < rows; ++r)
    for (int c = 0; c < s.GetNumChannels(); ++c) {
      auto cell = _document->cell(int(pattern), r, c);
      std::memcpy(out, &cell, 6);
      out += 6;
    }
  NSMutableArray *extraColumns=[NSMutableArray array], *performanceCommands=[NSMutableArray array];
  for(const auto &[channel,track]:native.tracks) {
    const auto count=native.performance.columns.find(track.id);
    [extraColumns addObject:@(count==native.performance.columns.end()?0:count->second)];
  }
  if(s.Patterns.IsValidPat(PATTERNINDEX(pattern)))for(const auto &command:native.performance.commands)if(command.pattern==native.patterns.at(uint16_t(pattern)).id) {
    const auto track=std::find_if(native.tracks.begin(),native.tracks.end(),[&](const auto &v){return v.second.id==command.track;});
    [performanceCommands addObject:@{@"channel":@(track->first),@"position":@(command.position),@"duration":@(command.duration),
      @"column":@(command.column),@"kind":patternCommandName(command.kind),@"binding":@(command.binding),@"value":@(command.value),@"pitchRange":@(command.pitchRange)}];
  }
  NSMutableArray *graphLanes=[NSMutableArray array],*graphCommands=[NSMutableArray array];
  for(const auto &[id,count]:native.signal.lanes){auto bus=std::find_if(native.mixer.buses.begin(),native.mixer.buses.end(),[&](const auto &b){return b.id==id;});[graphLanes addObject:@{@"target":nativeID(id),@"count":@(count),@"name":bus==native.mixer.buses.end()?@"Bus":@(bus->name.c_str())}];}
  if(s.Patterns.IsValidPat(PATTERNINDEX(pattern)))for(const auto &c:native.signal.commands)if(c.pattern==native.patterns.at(uint16_t(pattern)).id){NSMutableDictionary *item=[encodeSignalCommand(c) mutableCopy];auto graph=std::find_if(native.signal.library.begin(),native.signal.library.end(),[&](const auto &d){return d.id==c.graph;});item[@"number"]=@(graph==native.signal.library.end()?0:graph->number);[graphCommands addObject:item];}
  NSMutableArray *preciseNotes=[NSMutableArray array];
  if(s.Patterns.IsValidPat(PATTERNINDEX(pattern)))for(const auto &note:native.preciseNotes)if(note.pattern==native.patterns.at(uint16_t(pattern)).id) {
    const auto track=std::find_if(native.tracks.begin(),native.tracks.end(),[&](const auto &v){return v.second.id==note.track;});
    NSMutableDictionary *item=[encodePreciseNote(note) mutableCopy];item[@"channel"]=@(track->first);[preciseNotes addObject:item];
  }
  auto nativePlugins = [NSMutableArray array];
  for (auto &plugin : _plugins) {
    NSMutableDictionary *item = [descriptorDictionary(plugin.descriptor) mutableCopy];
    item[@"bypass"] = @(plugin.bypass);
    item[@"instrument"] = @(plugin.instrument);
    item[@"instrumentAssignments"] = encodePluginAssignments(plugin);
    item[@"instanceID"] = @(plugin.instanceID.c_str());
    [nativePlugins addObject:item];
  }
  auto effects = [NSMutableArray array], volumes = [NSMutableArray array];
  for (int i = 0; i < MAX_EFFECTS; ++i)
    [effects addObject:[NSString stringWithFormat:@"%c", s.GetModSpecifications().GetEffectLetter(EffectCommand(i))]];
  for (int i = 0; i < MAX_VOLCMDS; ++i)
    [volumes
        addObject:[NSString stringWithFormat:@"%c", s.GetModSpecifications().GetVolEffectLetter(VolumeCommand(i))]];
  auto issues = [NSMutableArray array];
  if (!_document->editable())
    [issues addObject:@"This format is available for preview. Editing and saving support MOD, XM, S3M, IT, and MPTM."];
  for (size_t i = 0; i < s.m_MixPlugins.size(); ++i)
    if (s.m_MixPlugins[i].IsValidPlugin())
      [issues addObject:[NSString
                            stringWithFormat:
                                @"Embedded tracker plug-in slot %zu is preserved but does not run in this native host.",
                                i + 1]];
  for (SAMPLEINDEX i = 1; i <= s.GetNumSamples(); ++i)
    if (s.SampleHasPath(i) && !s.GetSample(i).HasSampleData())
      [issues
          addObject:[NSString stringWithFormat:
                                  @"External sample %u could not be loaded. Replace it before saving or playback.", i]];
  if (!_pluginError.empty())
    [issues addObject:@(_pluginError.c_str())];
  const char *format = s.GetType() == MOD_TYPE_MPT   ? "MPTM"
                       : s.GetType() == MOD_TYPE_IT  ? "IT"
                       : s.GetType() == MOD_TYPE_XM  ? "XM"
                       : s.GetType() == MOD_TYPE_MOD ? "MOD"
                       : s.GetType() == MOD_TYPE_S3M ? "S3M"
                                                     : "Legacy";
  return @{
    @"issues" : issues,
    @"editable" : @(_document->editable()),
    @"nativePlugins" : nativePlugins,
    @"pluginError" : @(_pluginError.c_str()),
    @"automationPoints" : @(_automation.size()),
    @"canUndoEffect" : @(!_effectUndo.empty()),
    @"canRedoEffect" : @(!_effectRedo.empty()),
    @"effectLetters" : effects,
    @"commandCatalog" : patternCommandCatalog(s.GetType()),
    @"preciseNoteEffects": preciseNoteEffects(s.GetType()),
    @"graphLanes":graphLanes,@"graphCommands":graphCommands, @"preciseNotes":preciseNotes, @"extraEffectColumns": extraColumns, @"performanceCommands": performanceCommands,
    @"volumeLetters" : volumes,
    @"title" : songString(s, s.m_songName),
    @"format" : @(format),
    @"noteMin" : @(s.GetModSpecifications().noteMin),
    @"noteMax" : @(s.GetModSpecifications().noteMax),
    @"channels" : @(s.GetNumChannels()),
    @"pattern" : @(pattern),
    @"rows" : @(rows),
    @"cells" : data,
    @"samples" : samples,
    @"instruments" : instruments,
    @"patterns" : patterns,
    @"orders" : orders,
    @"orderMetadata" : orderMetadata,
    @"tracks" : tracks,
    @"trackLayout": trackLayout(native, s),
    @"hasNativeMetadata" : @(native.hasAnnotations()),
    @"revisionToken" : self.automationRevision,
    @"sequences" : sequences,
    @"sequence" : @(s.Order.GetCurrentSequenceIndex()),
    @"tempo" : @(s.Order().GetDefaultTempo().ToDouble()),
    @"timing": timingInfo(songTiming(s), s),
    @"displayRowsPerBeat": @(s.Patterns.IsValidPat(PATTERNINDEX(pattern)) && s.Patterns[PATTERNINDEX(pattern)].GetOverrideSignature() ? s.Patterns[PATTERNINDEX(pattern)].GetRowsPerBeat() : s.m_nDefaultRowsPerBeat),
    @"displayRowsPerMeasure": @(s.Patterns.IsValidPat(PATTERNINDEX(pattern)) && s.Patterns[PATTERNINDEX(pattern)].GetOverrideSignature() ? s.Patterns[PATTERNINDEX(pattern)].GetRowsPerMeasure() : s.m_nDefaultRowsPerMeasure),
    @"speed" : @(s.Order().GetDefaultSpeed()),
    @"revision" : @(_document->revision)
  };
}
- (BOOL)editPattern:(NSInteger)p
                row:(NSInteger)r
            channel:(NSInteger)c
             values:(NSArray<NSNumber *> *)values
              error:(NSError **)error {
  try {
    if (values.count != 6)
      throw std::runtime_error("A pattern cell has six fields.");
    Cell after{values[0].unsignedCharValue, values[1].unsignedCharValue, values[2].unsignedCharValue,
               values[3].unsignedCharValue, values[4].unsignedCharValue, values[5].unsignedCharValue};
    auto edits = _document->edit({{uint16_t(p), uint16_t(r), uint16_t(c), {}, after}});
    if (_audio->playing() && !_audio->renderer()->enqueue(edits)) {
      _audio->stop();
      throw std::runtime_error("Playback stopped because its edit queue is full. Your edit is saved in the document.");
    }
    return YES;
  } catch (const std::exception &e) {
    failure(error, e);
    return NO;
  }
}
- (void)undo {
  if (!self.canUndo) return;
  if (_document->undoChangesStructure() || _document->undoChangesAutomation() || _document->undoChangesMixer())
    _audio->stop();
  auto e = _document->undo();
  if (_audio->renderer()) _audio->renderer()->applyColumnMutes(_document->native(), _document->song());
  if (_audio->playing() && !_audio->renderer()->enqueue(e))
    _audio->stop();
}
- (void)redo {
  if (!self.canRedo) return;
  if (_document->redoChangesStructure() || _document->redoChangesAutomation() || _document->redoChangesMixer())
    _audio->stop();
  auto e = _document->redo();
  if (_audio->renderer()) _audio->renderer()->applyColumnMutes(_document->native(), _document->song());
  if (_audio->playing() && !_audio->renderer()->enqueue(e))
    _audio->stop();
}
- (NSArray<NSDictionary *> *)mixerMeters {
  NSMutableArray *result = [NSMutableArray array];
  const auto levels = _audio->mixerMeters(); const auto &buses = _document->native().mixer.buses;
  for (size_t i = 0; i < levels.size() && i < buses.size(); ++i)
    [result addObject:@{@"bus": nativeID(buses[i].id), @"left": @(levels[i].left), @"right": @(levels[i].right)}];
  return result;
}
- (void)muteChannel:(NSInteger)c muted:(BOOL)m {
  if (_audio->renderer())
    _audio->renderer()->mute(uint32_t(c), m);
}
- (BOOL)editCells:(NSArray<NSDictionary *> *)input error:(NSError **)error {
  try {
    std::vector<Edit> edits;
    for (NSDictionary *item in input) {
      NSArray<NSNumber *> *v = item[@"values"];
      if (v.count != 6)
        throw std::runtime_error("Invalid clipboard cell.");
      edits.push_back({[item[@"pattern"] unsignedShortValue],
                       [item[@"row"] unsignedShortValue],
                       [item[@"channel"] unsignedShortValue],
                       {},
                       {
                         v[0].unsignedCharValue, v[1].unsignedCharValue, v[2].unsignedCharValue, v[3].unsignedCharValue,
                             v[4].unsignedCharValue, v[5].unsignedCharValue
                       }});
    }
    auto changed = _document->edit(edits);
    if (_audio->playing() && !_audio->renderer()->enqueue(changed))
      _audio->stop();
    return YES;
  } catch (const std::exception &e) {
    failure(error, e);
    return NO;
  }
}
- (BOOL)editOrder:(NSInteger)order pattern:(NSInteger)pattern operation:(NSString *)operation error:(NSError **)error {
  try {
    _audio->stop();
    _document->editOrder(int(order), int(pattern), operation.UTF8String);
    return YES;
  } catch (const std::exception &e) {
    failure(error, e);
    return NO;
  }
}
- (BOOL)selectSequence:(NSInteger)sequence error:(NSError **)error {
  try {
    if (sequence < 0 || sequence >= _document->song().Order.GetNumSequences())
      throw std::runtime_error("Select a valid sequence.");
    _audio->stop();
    _document->song().Order.SetSequence(SEQUENCEINDEX(sequence));
    return YES;
  } catch (const std::exception &e) {
    failure(error, e);
    return NO;
  }
}
- (NSInteger)addPattern:(NSInteger)rows duplicate:(BOOL)duplicate source:(NSInteger)source error:(NSError **)error {
  try {
    _audio->stop();
    return _document->addPattern(int(rows), duplicate, int(source));
  } catch (const std::exception &e) {
    failure(error, e);
    return -1;
  }
}
- (BOOL)removeOrder:(NSInteger)order error:(NSError **)error {
  try {
    _audio->stop();
    _document->removeOrder(int(order));
    return YES;
  } catch (const std::exception &e) {
    failure(error, e);
    return NO;
  }
}
- (BOOL)songTitle:(NSString *)title
            tempo:(NSInteger)tempo
            speed:(NSInteger)speed
         channels:(NSInteger)channels
            error:(NSError **)error {
  try {
    if (_document->native().mixer.active() && channels >= 0) {
      const auto buses = _document->native().mixer.buses.size() - _document->song().GetNumChannels() + size_t(channels);
      validatePluginCapacity(_plugins, buses);
    }
    _audio->stop();
    _document->transaction([&](CSoundFile &s) {
      Document::resizeChannels(s, int(channels));
      s.SetTitle(::OpenMPT::mpt::ToCharset(s.GetCharsetInternal(), ::OpenMPT::mpt::Charset::UTF8,
                                           std::string(title.UTF8String)));
      s.Order().SetDefaultTempoInt(uint32_t(std::clamp(tempo, NSInteger(32), NSInteger(512))));
      s.Order().SetDefaultSpeed(uint32_t(std::clamp(speed, NSInteger(1), NSInteger(31))));
    });
    return YES;
  } catch (const std::exception &e) {
    failure(error, e);
    return NO;
  }
}
- (NSDictionary *)sampleInfo:(NSInteger)index {
  return [self sampleInfo:index includeWaveform:YES];
}
- (NSDictionary *)sampleInfo:(NSInteger)index includeWaveform:(BOOL)includeWaveform {
  auto &s = _document->song();
  if (index < 1 || index > s.GetNumSamples())
    return @{};
  auto &sample = s.GetSample(SAMPLEINDEX(index));
  NSMutableDictionary *info = [@{
    @"index" : @(index),
    @"name" : songString(s, s.GetSampleName(SAMPLEINDEX(index))),
    @"frames" : @(sample.nLength),
    @"rate" : @(sample.nC5Speed),
    @"volume" : @(sample.nVolume / 4),
    @"pan" : @(sample.nPan),
    @"loopStart" : @(sample.nLoopStart),
    @"loopEnd" : @(sample.nLoopEnd),
    @"loop" : @(sample.uFlags[CHN_LOOP]),
    @"pingpong" : @(sample.uFlags[CHN_PINGPONGLOOP]),
    @"sustainStart" : @(sample.nSustainStart),
    @"sustainEnd" : @(sample.nSustainEnd),
    @"sustainLoop" : @(sample.uFlags[CHN_SUSTAINLOOP]),
    @"sustainPingpong" : @(sample.uFlags[CHN_PINGPONGSUSTAIN]),
    @"reverseLoop" : @((sample.nativeReverseLoops & 1) != 0),
    @"sustainReverse" : @((sample.nativeReverseLoops & 2) != 0),
    @"bits" : @(sample.uFlags[CHN_16BIT] ? 16 : 8),
    @"channels" : @(sample.GetNumChannels())
  } mutableCopy];
  if (includeWaveform) {
    auto wave = _document->waveform(int(index), 2048);
    info[@"waveform"] = [NSData dataWithBytes:wave.data() length:wave.size() * sizeof(float)];
  }
  return info;
}
- (NSInteger)importSample:(NSString *)path slot:(NSInteger)slot error:(NSError **)error {
  try {
    _audio->stop();
    return _document->importSample(path.UTF8String, int(slot));
  } catch (const std::exception &e) {
    failure(error, e);
    return -1;
  }
}
- (BOOL)processSample:(NSInteger)sample
            operation:(NSString *)operation
                start:(NSUInteger)start
                  end:(NSUInteger)end
                error:(NSError **)error {
  try {
    if ([operation isEqual:@"trim"]) {
      _audio->stop();
      _document->processSample(int(sample), operation.UTF8String, uint32_t(start), uint32_t(end));
    } else {
      if (sample < 1 || sample > _document->song().GetNumSamples() || start > UINT32_MAX || end > UINT32_MAX)
        throw std::invalid_argument("Invalid sample range");
      SampleProcessOptions options; options.operation = operation.UTF8String; options.first = uint32_t(start);
      options.last = end ? uint32_t(end) : _document->song().GetSample(SAMPLEINDEX(sample)).nLength;
      auto prepared = _document->prepareSampleProcess(int(sample), options);
      if (prepared.hasChanges()) { _audio->stop(); _document->applySampleProcess(std::move(prepared)); }
    }
    return YES;
  } catch (const std::exception &e) {
    failure(error, e);
    return NO;
  }
}
- (BOOL)sampleSettings:(NSInteger)sample values:(NSDictionary *)v error:(NSError **)error {
  try {
    NSMutableDictionary *merged = [[self sampleInfo:sample includeWaveform:NO] mutableCopy];
    [merged addEntriesFromDictionary:v];
    v = merged;
    _audio->stop();
    _document->sampleSettings(
        int(sample), [v[@"rate"] intValue], [v[@"volume"] intValue], [v[@"pan"] intValue],
        [v[@"loopStart"] unsignedIntValue], [v[@"loopEnd"] unsignedIntValue], [v[@"loop"] boolValue],
        [v[@"pingpong"] boolValue],
        [v[@"name"] isKindOfClass:NSString.class] ? std::optional<std::string>([v[@"name"] UTF8String]) : std::nullopt);
    return YES;
  } catch (const std::exception &e) {
    failure(error, e);
    return NO;
  }
}
- (NSInteger)addInstrument:(NSInteger)sample error:(NSError **)error {
  try {
    _audio->stop();
    auto &song = _document->song();
    int result = 0;
    _document->transaction([&](CSoundFile &s) {
      if (s.GetType() != MOD_TYPE_IT && s.GetType() != MOD_TYPE_XM && s.GetType() != MOD_TYPE_MPT)
        throw std::runtime_error("This format does not support instruments.");
      if (!s.GetNumInstruments()) {
        if (s.GetNumSamples() > s.GetModSpecifications().instrumentsMax)
          throw std::runtime_error("Creating matching instruments exceeds this format’s instrument limit.");
        for (int i = 1; i <= std::max(1, int(s.GetNumSamples())); ++i) {
          s.Instruments[i] = new ModInstrument(SAMPLEINDEX(i));
          s.Instruments[i]->name = s.GetSampleName(SAMPLEINDEX(std::min(i, int(s.GetNumSamples()))));
        }
        s.m_nInstruments = std::max(SAMPLEINDEX(1), s.GetNumSamples());
        result = std::clamp(int(sample), 1, int(s.m_nInstruments));
      } else {
        result = s.GetNumInstruments() + 1;
        if (result >= MAX_INSTRUMENTS || result > s.GetModSpecifications().instrumentsMax)
          throw std::runtime_error("Instrument slots are full.");
        s.Instruments[result] =
            new ModInstrument(SAMPLEINDEX(std::clamp(sample, NSInteger(0), NSInteger(s.GetNumSamples()))));
        s.Instruments[result]->name = "New instrument";
        s.m_nInstruments = INSTRUMENTINDEX(result);
      }
    });
    return result;
  } catch (const std::exception &e) {
    failure(error, e);
    return -1;
  }
}
- (NSInteger)importInstrument:(NSString *)path slot:(NSInteger)slot error:(NSError **)error {
  try {
    _audio->stop();
    return _document->importInstrument(path.UTF8String, int(slot));
  } catch (const std::exception &e) {
    failure(error, e);
    return -1;
  }
}
- (NSDictionary *)instrumentInfo:(NSInteger)index {
  auto &s = _document->song();
  if (index < 1 || index > s.GetNumInstruments() || !s.Instruments[index])
    return @{};
  auto &ins = *s.Instruments[index];
  auto envelopeInfo = [&](const InstrumentEnvelope &envelope) {
    return instrumentEnvelopeInfo(envelope, s.GetModSpecifications().envelopePointsMax);
  };
  auto mapping = [NSMutableArray array];
  for (auto sample : ins.Keyboard)
    [mapping addObject:@(sample)];
  auto noteMapping = [NSMutableArray array];
  for (auto note : ins.NoteMap) [noteMapping addObject:@(note)];
  return @{
    @"name" : songString(s, ins.GetName()),
    @"volume" : @(ins.nGlobalVol),
    @"pan" : @(ins.nPan),
    @"fadeout" : @(ins.nFadeOut),
    @"nna" : @(uint8_t(ins.nNNA)),
    @"dct" : @(uint8_t(ins.nDCT)),
    @"dna" : @(uint8_t(ins.nDNA)),
    @"enabled" : @(ins.VolEnv.dwFlags[ENV_ENABLED]),
    @"sustain" : @(ins.VolEnv.dwFlags[ENV_SUSTAIN]),
    @"sustainPoint" : @(ins.VolEnv.nSustainStart),
    @"envelopes" : @[ envelopeInfo(ins.VolEnv), envelopeInfo(ins.PanEnv), envelopeInfo(ins.PitchEnv) ],
    @"mapping" : mapping, @"noteMapping" : noteMapping
  };
}
- (BOOL)instrumentSettings:(NSInteger)index values:(NSDictionary *)v error:(NSError **)error {
  try {
    _audio->stop();
    _document->transaction([&](CSoundFile &s) {
      if (index < 1 || index > s.GetNumInstruments() || !s.Instruments[index])
        throw std::runtime_error("Select an instrument.");
      auto &ins = *s.Instruments[index];
      const int envelopeKind = [v[@"envelope"] intValue];
      if (envelopeKind < 0 || envelopeKind > 2)
        throw std::runtime_error("Invalid envelope type");
      auto &envelope = envelopeKind == 0 ? ins.VolEnv : envelopeKind == 1 ? ins.PanEnv : ins.PitchEnv;
      if (envelopeKind == 2 && s.GetType() == MOD_TYPE_XM)
        throw std::runtime_error("XM does not store pitch envelopes");
      if (v[@"name"])
        ins.name = ::OpenMPT::mpt::ToCharset(s.GetCharsetInternal(), ::OpenMPT::mpt::Charset::UTF8,
                                             std::string([v[@"name"] UTF8String]));
      if (v[@"volume"])
        ins.nGlobalVol = std::clamp([v[@"volume"] intValue], 0, 64);
      if (v[@"pan"]) {
        ins.nPan = std::clamp([v[@"pan"] intValue], 0, 256);
        ins.dwFlags.set(INS_SETPANNING);
      }
      if (v[@"fadeout"])
        ins.nFadeOut = std::clamp([v[@"fadeout"] intValue], 0, 32768);
      if (v[@"nna"])
        ins.nNNA = NewNoteAction(std::clamp([v[@"nna"] intValue], 0, 3));
      if (v[@"dct"])
        ins.nDCT = DuplicateCheckType(std::clamp([v[@"dct"] intValue], 0, 4));
      if (v[@"dna"])
        ins.nDNA = DuplicateNoteAction(std::clamp([v[@"dna"] intValue], 0, 2));
      if (v[@"mapping"]) {
        NSArray<NSNumber *> *mapping = v[@"mapping"];
        if (mapping.count != 128)
          throw std::runtime_error("A keymap needs 128 entries.");
        for (int n = 0; n < 128; ++n)
          ins.Keyboard[n] = SAMPLEINDEX(std::clamp(mapping[n].intValue, 0, int(s.GetNumSamples())));
      }
      if (v[@"points"]) {
        NSArray<NSArray<NSNumber *> *> *points = v[@"points"];
        if (points.count > s.GetModSpecifications().envelopePointsMax)
          throw std::runtime_error("Envelope exceeds this format’s point limit.");
        envelope.clear();
        for (NSArray<NSNumber *> *point in points) {
          if (point.count != 2)
            throw std::runtime_error("Invalid envelope point.");
          envelope.push_back(uint16_t(std::clamp(point[0].intValue, 0, 65535)),
                             uint8_t(std::clamp(point[1].intValue, 0, 64)));
        }
        envelope.Sanitize();
      }
      if (v[@"enabled"])
        envelope.dwFlags.set(ENV_ENABLED, [v[@"enabled"] boolValue]);
      if (v[@"sustain"])
        envelope.dwFlags.set(ENV_SUSTAIN, [v[@"sustain"] boolValue]);
      if (v[@"sustainPoint"])
        envelope.nSustainStart =
            uint8_t(std::clamp([v[@"sustainPoint"] intValue], 0, std::max(0, int(envelope.size()) - 1)));
      const int last = std::max(0, int(envelope.size()) - 1);
      if (v[@"sustainEnd"])
        envelope.nSustainEnd = uint8_t(std::clamp([v[@"sustainEnd"] intValue], int(envelope.nSustainStart), last));
      else if (v[@"sustainPoint"])
        envelope.nSustainEnd = envelope.nSustainStart;
      if (v[@"loop"])
        envelope.dwFlags.set(ENV_LOOP, [v[@"loop"] boolValue]);
      if (v[@"loopStart"])
        envelope.nLoopStart = uint8_t(std::clamp([v[@"loopStart"] intValue], 0, last));
      if (v[@"loopEnd"])
        envelope.nLoopEnd = uint8_t(std::clamp([v[@"loopEnd"] intValue], int(envelope.nLoopStart), last));
      if (v[@"filter"] && envelopeKind == 2)
        envelope.dwFlags.set(ENV_FILTER, [v[@"filter"] boolValue]);
      envelope.Sanitize();
    });
    return YES;
  } catch (const std::exception &e) {
    failure(error, e);
    return NO;
  }
}
- (BOOL)previewSample:(NSInteger)sample note:(NSInteger)note error:(NSError **)error {
  try {
    auto &source = _document->song();
    if (sample < 1 || sample > source.GetNumSamples())
      throw std::runtime_error("Select a sample to preview.");
    Document preview;
    auto &s = preview.song(); s.m_nSamples = 1;
    s.ReadSampleFromSong(1, source, SAMPLEINDEX(sample));
    auto &m = *s.Patterns[0].GetpModCommand(0, 0);
    m.note = uint8_t(std::clamp(note, NSInteger(1), NSInteger(120))); m.instr = 1;
    s.Patterns[0].GetpModCommand(16, 0)->note = NOTE_NOTECUT;
    _audio->play(preview.snapshotData()); _isolatedSamplePreview=YES;
    return YES;
  } catch (const std::exception &e) {
    failure(error, e);
    return NO;
  }
}
- (NSArray<NSDictionary *> *)availablePlugins:(NSError **)error {
  return [self availablePluginsRescan:NO error:error];
}
- (NSArray<NSDictionary *> *)builtInPlugins {
  NSMutableArray *result = [NSMutableArray array];
  for (const auto &descriptor : NativePlugin::builtins()) [result addObject:descriptorDictionary(descriptor)];
  return result;
}
- (NSArray<NSDictionary *> *)availablePluginsRescan:(BOOL)rescan error:(NSError **)error {
  try {
    NSArray *external = _pluginInventory->load(rescan, [&]() -> NSArray<NSDictionary *> * {
      NSData *data = runScanner(@[ @"--list" ]);
      id result = [NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
      if (![result isKindOfClass:NSArray.class])
        throw std::runtime_error("Invalid scanner response");
      NSMutableArray *plugins = [result mutableCopy];
      for (NSString *directory in
           @[ @"/Library/Audio/Plug-Ins/VST3", [@"~/Library/Audio/Plug-Ins/VST3" stringByExpandingTildeInPath] ]) {
        NSDirectoryEnumerator *enumerator = [[NSFileManager defaultManager] enumeratorAtPath:directory];
        for (NSString *relative in enumerator) {
          if ([relative.pathExtension.lowercaseString isEqual:@"vst3"]) {
            [enumerator skipDescendants];
            try {
              NSData *found = runScanner(@[ @"--list-vst3", [directory stringByAppendingPathComponent:relative] ]);
              id entries = [NSJSONSerialization JSONObjectWithData:found options:0 error:nil];
              if ([entries isKindOfClass:NSArray.class])
                [plugins addObjectsFromArray:entries];
            } catch (...) { /* An incompatible candidate cannot prevent discovering other plugins. */
            }
          }
          if (plugins.count > 4096)
            throw std::runtime_error("Too many installed plugins");
        }
      }
      [plugins sortUsingComparator:^NSComparisonResult(NSDictionary *a, NSDictionary *b) {
        return [a[@"name"] localizedCaseInsensitiveCompare:b[@"name"]];
      }];
      return plugins;
    });
    return [[self builtInPlugins] arrayByAddingObjectsFromArray:external];
  } catch (const std::exception &e) {
    failure(error, e);
    return nil;
  }
}
// Editing an unresolved project must still allow removing missing effects one by one.
// Keep its graph inactive and all remaining opaque states intact until it can instantiate.
- (void)restorePluginGraph:(const std::vector<PluginState> &)states
                automation:(const std::vector<ParameterChange> &)automation {
  validatePluginCapacity(states, _document->native().mixer.buses.size());
  auto nextPlugins = states;
  auto nextAutomation = automation;
  std::string issue;
  try {
    _audio->setPlugins(states, automation);
  } catch (const std::exception &e) {
    if (_pluginError.empty())
      throw;
    issue = e.what();
    _audio->setPlugins({});
  }
  _plugins = std::move(nextPlugins);
  _automation = std::move(nextAutomation);
  _pluginError = std::move(issue);
  ++_pluginRevision;
}
- (void)applyPluginGraph:(const std::vector<PluginState> &)states
              automation:(const std::vector<ParameterChange> &)automation {
  EffectSnapshot before{_plugins, _automation};
  _effectUndo.reserve(_effectUndo.size() + 1);
  [self restorePluginGraph:states automation:automation];
  _effectUndo.push_back(std::move(before));
  _effectRedo.clear();
  trimEffectHistory(_effectUndo);
}
- (BOOL)restoreEffectHistory:(BOOL)redo error:(NSError **)error {
  try {
    auto &source = redo ? _effectRedo : _effectUndo;
    auto &destination = redo ? _effectUndo : _effectRedo;
    if (source.empty())
      return YES;
    [self commitManualParameters];
    EffectSnapshot current{_plugins, _automation};
    destination.reserve(destination.size() + 1);
    // Missing effects remain opaque and repairable when restoring history too.
    const auto previousError = _pluginError;
    _pluginError = "Restoring effect history";
    try {
      [self restorePluginGraph:source.back().plugins automation:source.back().automation];
    } catch (...) {
      _pluginError = previousError;
      throw;
    }
    destination.push_back(std::move(current));
    source.pop_back();
    trimEffectHistory(destination);
    return YES;
  } catch (const std::exception &e) {
    failure(error, e);
    return NO;
  }
}
- (BOOL)undoEffectChange:(NSError **)error {
  return [self restoreEffectHistory:NO error:error];
}
- (BOOL)redoEffectChange:(NSError **)error {
  return [self restoreEffectHistory:YES error:error];
}
- (BOOL)addPlugin:(NSDictionary *)item error:(NSError **)error {
  try {
    [self commitManualParameters];
    auto d = descriptor(item);
    if (d.format == "VST3")
      runScanner(@[ @"--validate-vst3", @(d.path.c_str()), @(d.classID.c_str()), d.instrument ? @"1" : @"0" ]);
    else if (d.format == "AU")
      runScanner(
          @[ @"--validate", [@(d.type) stringValue], [@(d.subtype) stringValue], [@(d.manufacturer) stringValue] ]);
    auto states = (_pluginError.empty() && (_automation.empty() && (_document->native().automation.empty() && _document->native().performance.commands.empty() && !_audio->hasAutomatedState()))) ? _audio->pluginStates() : _plugins;
    states.push_back({d, {}, false, 0, NSUUID.UUID.UUIDString.UTF8String});
    [self applyPluginGraph:states automation:_automation];
    return YES;
  } catch (const std::exception &e) {
    failure(error, e);
    return NO;
  }
}
- (BOOL)assignPlugin:(NSInteger)slot instrument:(NSInteger)instrument error:(NSError **)error {
  try {
    if (slot < 0 || slot >= _plugins.size() ||
        !(_plugins[slot].descriptor.instrument || _plugins[slot].descriptor.type == kAudioUnitType_MusicDevice))
      throw std::runtime_error("Select an instrument plugin");
    if (instrument < 0 || instrument > _document->song().GetNumInstruments())
      throw std::runtime_error("Create or select an existing tracker instrument");
    [self commitManualParameters];
    auto states = (_pluginError.empty() && (_automation.empty() && (_document->native().automation.empty() && _document->native().performance.commands.empty() && !_audio->hasAutomatedState()))) ? _audio->pluginStates() : _plugins;
    // Legacy single-assignment control replaces the primary entry, retaining
    // other aliases. Explicit unassignment clears the complete list.
    if (instrument) {
      auto assignments = pluginAssignments(states[slot]);
      const auto found = std::find_if(assignments.begin(), assignments.end(), [&](const auto &a) { return a.instrument == instrument; });
      const uint32_t channel = found == assignments.end() ? 1 : found->channel;
      std::vector<PluginInstrumentAlias> next{{uint32_t(instrument), channel}};
      for (const auto &alias : states[slot].aliases) if (alias.instrument != instrument) next.push_back(alias);
      for (size_t i = 0; i < states.size(); ++i) if (i != size_t(slot)) removePluginAssignment(states[i], uint32_t(instrument));
      setPluginAssignments(states[slot], next);
    } else setPluginAssignments(states[slot], {});
    [self applyPluginGraph:states automation:_automation];
    return YES;
  } catch (const std::exception &e) {
    failure(error, e);
    return NO;
  }
}
- (BOOL)showPluginEditor:(NSInteger)slot error:(NSError **)error {
  try {
    _audio->showPluginEditor(size_t(slot));
    return YES;
  } catch (const std::exception &e) {
    failure(error, e);
    return NO;
  }
}
- (NSInteger)collectPluginEdits:(BOOL)record error:(NSError **)error {
  NSInteger count = 0;
  for (size_t slot = 0; slot < _plugins.size(); ++slot) {
    uint32_t id;
    float value;
    for (int n = 0; n < 128 && _audio->popPluginEdit(slot, id, value); ++n) {
      if (![self acceptPluginParameter:slot identifier:id value:value record:record alreadyApplied:YES error:error])
        return -1;
      ++count;
    }
  }
  return count;
}
- (BOOL)removePlugin:(NSInteger)slot error:(NSError **)error {
  try {
    [self commitManualParameters];
    if (slot < 0 || slot >= _plugins.size())
      throw std::runtime_error("Select an effect");
    auto states = (_pluginError.empty() && (_automation.empty() && (_document->native().automation.empty() && _document->native().performance.commands.empty() && !_audio->hasAutomatedState()))) ? _audio->pluginStates() : _plugins;
    states.erase(states.begin() + slot);
    auto automation = _automation;
    automation.erase(
        std::remove_if(automation.begin(), automation.end(), [&](const auto &point) { return point.slot == slot; }),
        automation.end());
    for (auto &point : automation)
      if (point.slot > slot)
        --point.slot;
    [self applyPluginGraph:states automation:automation];
    return YES;
  } catch (const std::exception &e) {
    failure(error, e);
    return NO;
  }
}
- (BOOL)movePlugin:(NSInteger)slot direction:(NSInteger)direction error:(NSError **)error {
  try {
    [self commitManualParameters];
    auto target = slot + direction;
    if (slot < 0 || target < 0 || slot >= _plugins.size() || target >= _plugins.size())
      return YES;
    auto states = (_pluginError.empty() && (_automation.empty() && (_document->native().automation.empty() && _document->native().performance.commands.empty() && !_audio->hasAutomatedState()))) ? _audio->pluginStates() : _plugins;
    std::swap(states[slot], states[target]);
    auto automation = _automation;
    for (auto &point : automation) {
      if (point.slot == slot)
        point.slot = uint32_t(target);
      else if (point.slot == target)
        point.slot = uint32_t(slot);
    }
    [self applyPluginGraph:states automation:automation];
    return YES;
  } catch (const std::exception &e) {
    failure(error, e);
    return NO;
  }
}
- (BOOL)bypassPlugin:(NSInteger)slot bypass:(BOOL)bypass error:(NSError **)error {
  try {
    [self commitManualParameters];
    if (slot < 0 || slot >= _plugins.size())
      throw std::runtime_error("Select an effect");
    auto states = (_pluginError.empty() && (_automation.empty() && (_document->native().automation.empty() && _document->native().performance.commands.empty() && !_audio->hasAutomatedState()))) ? _audio->pluginStates() : _plugins;
    states[slot].bypass = bypass;
    [self applyPluginGraph:states automation:_automation];
    return YES;
  } catch (const std::exception &e) {
    failure(error, e);
    return NO;
  }
}
- (NSDictionary *)pluginMeters:(NSInteger)slot {
  if (slot < 0 || size_t(slot) >= _plugins.size()) return @{@"supported": @NO, @"active": @NO};
  auto meter = _audio->pluginMeters(size_t(slot));
  if (!meter) return @{@"supported": @NO, @"active": @NO};
  const bool active = _audio->active() && !_plugins[slot].bypass;
  if (!active) *meter = EffectMeters{};
  return @{@"supported": @YES, @"active": @(active), @"plugin": @(_plugins[slot].instanceID.c_str()),
    @"reductionDB": @[@(meter->reductionDB[0]), @(meter->reductionDB[1])],
    @"detectorDB": @[@(meter->detectorDB[0]), @(meter->detectorDB[1])]};
}
- (NSArray<NSDictionary *> *)pluginParameters:(NSInteger)slot {
  auto result = [NSMutableArray array];
  try {
    for (auto &p : _audio->pluginParameters(slot)) {
      NSMutableArray *choices = [NSMutableArray array];
      for (const auto &choice : p.choices) [choices addObject:@(choice.c_str())];
      [result addObject:@{
        @"id" : @(p.id),
        @"name" : @(p.name.c_str()),
        @"min" : @(p.min),
        @"max" : @(p.max),
        @"value" : @(p.value),
        @"unit" : @(p.unit), @"unitLabel": @(p.unitLabel.c_str()), @"choices": choices,
        @"displayScale": p.logarithmic ? @"logarithmic" : @"linear", @"step": @(p.step), @"canSlide": @(p.continuous), @"writable": @(p.writable)
      }];
    }
  } catch (...) {
  }
  return result;
}
- (BOOL)pluginParameter:(NSInteger)slot
             identifier:(NSInteger)identifier
                  value:(double)value
                 record:(BOOL)record
                  error:(NSError **)error {
  return [self acceptPluginParameter:slot
                          identifier:identifier
                               value:value
                              record:record
                      alreadyApplied:NO
                               error:error];
}
- (void)rememberParameterTouch:(size_t)slot identifier:(uint32_t)identifier source:(NSString *)source {
  // Control-thread bookkeeping only. Automation playback and state/history
  // restoration never call this; rack positions are resolved when queried.
  _lastTouchedPlugin = _plugins.at(slot).instanceID;
  _lastTouchedParameter = identifier;
  _lastTouchedSource = source;
  ++_touchSequence;
}
- (NSDictionary *)lastTouchedParameter {
  NSString *token = [NSString stringWithFormat:@"%@:%llu", _automationDocumentID, (unsigned long long)_touchSequence];
  if (_lastTouchedPlugin.empty()) return @{@"token": token, @"target": NSNull.null};
  NSMutableDictionary *target = [@{@"plugin": @(_lastTouchedPlugin.c_str()), @"parameter": @(_lastTouchedParameter),
    @"source": _lastTouchedSource, @"available": @NO, @"slot": NSNull.null, @"reason": @"Plugin was removed"} mutableCopy];
  for (size_t slot = 0; slot < _plugins.size(); ++slot) if (_plugins[slot].instanceID == _lastTouchedPlugin) {
    target[@"slot"] = @(slot); target[@"pluginName"] = @(_plugins[slot].descriptor.name.c_str());
    target[@"reason"] = @"Plugin or parameter is unavailable";
    for (NSDictionary *parameter in [self pluginParameters:slot]) if ([parameter[@"id"] unsignedIntValue] == _lastTouchedParameter) {
      target[@"available"] = @YES;
      [target removeObjectForKey:@"reason"];
      target[@"name"] = parameter[@"name"];
      target[@"minimum"] = parameter[@"min"]; target[@"maximum"] = parameter[@"max"];
      target[@"value"] = parameter[@"value"];
      const double low = [parameter[@"min"] doubleValue], high = [parameter[@"max"] doubleValue];
      target[@"normalizedValue"] = @(high > low ? std::clamp(([parameter[@"value"] doubleValue] - low) / (high - low), 0.0, 1.0) : 0.0);
      break;
    }
    break;
  }
  return @{@"token": token, @"target": target};
}
- (BOOL)acceptPluginParameter:(NSInteger)slot
                   identifier:(NSInteger)identifier
                        value:(double)value
                       record:(BOOL)record
               alreadyApplied:(BOOL)alreadyApplied
                        error:(NSError **)error {
  try {
    if (slot < 0 || size_t(slot) >= _plugins.size() || identifier < 0 || uint64_t(identifier) > UINT32_MAX || !std::isfinite(value))
      throw std::runtime_error("Invalid plugin parameter edit");
    const bool recording = record && _audio->playing();
    if (recording && slot >= 0 && size_t(slot) < _plugins.size() &&
        std::any_of(_document->native().automation.begin(), _document->native().automation.end(), [&](const auto &lane) {
          return lane.enabled && lane.plugin == _plugins[slot].instanceID && lane.parameter == uint32_t(identifier);
        }))
      throw std::runtime_error("Disable pattern automation for this parameter before recording absolute automation.");
    if (recording && _document->native().performance.controls(_plugins[slot].instanceID,uint32_t(identifier)))
      throw std::runtime_error("Remove pattern commands for this parameter before recording absolute automation.");
    if (recording && _automation.size() >= 100000)
      throw std::runtime_error("Automation reached 100,000 points.");
    if (!alreadyApplied && !_audio->pluginParameter(uint32_t(slot), uint32_t(identifier), float(value)))
      throw std::runtime_error("The plugin parameter queue is full or unavailable.");
    if (recording) {
      auto frames = _audio->telemetry().frames;
      _automation.push_back({uint32_t(slot), uint32_t(identifier), float(value),
                             uint64_t(double(frames) * 48000 / _audio->sampleRate())});
    }
    if (!recording) {
      auto found = std::find_if(_manualParameters.begin(), _manualParameters.end(),
                                [&](const auto &p) { return p.slot == slot && p.id == identifier; });
      if (found == _manualParameters.end())
        _manualParameters.push_back({uint32_t(slot), uint32_t(identifier), float(value), 0});
      else
        found->value = float(value);
    }
    ++_pluginRevision;
    [self rememberParameterTouch:size_t(slot) identifier:uint32_t(identifier) source:alreadyApplied ? @"plugin-editor" : @"native-control"];
    return YES;
  } catch (const std::exception &e) {
    failure(error, e);
    return NO;
  }
}
- (BOOL)clearAutomation:(NSError **)error {
  try {
    if (!_pluginError.empty())
      throw std::runtime_error(_pluginError);
    auto states = _audio->pluginStates();
    [self commitManualParameters];
    [self applyPluginGraph:states automation:{}];
    _manualParameters.clear();
    return YES;
  } catch (const std::exception &e) {
    failure(error, e);
    return NO;
  }
}
- (BOOL)deviceChanged {
  return _audio->deviceChanged();
}
- (BOOL)refreshDevice:(NSError **)error {
  try {
    _audio->refreshDevice();
    return YES;
  } catch (const std::exception &e) {
    failure(error, e);
    return NO;
  }
}
- (NSArray<NSDictionary *> *)midiSources {
  auto result = [NSMutableArray array];
  for (auto &s : MidiInput::sources())
    [result addObject:@{@"id" : @(s.id), @"name" : @(s.name.c_str())}];
  return result;
}
- (BOOL)connectMIDI:(NSUInteger)source error:(NSError **)error {
  try {
    _midi->connect(uint32_t(source));
    return YES;
  } catch (const std::exception &e) {
    failure(error, e);
    return NO;
  }
}
- (NSString *)recordingTakeID {return _recordingID;}
- (BOOL)recordingActive { return _recording && _recording->capturing; }
- (void)captureRecordingEvent:(uint64_t)timestamp status:(uint8_t)status note:(uint8_t)note velocity:(uint8_t)velocity {
  if(!_recording||!_recording->capturing)return;
  if(![_recordingRevision isEqual:self.automationRevision]||!_audio->renderer()||timestamp<_recordingLastTimestamp||timestamp<_recordingStartTimestamp){++_recording->missingTime;return;}
  _recordingLastTimestamp=timestamp;
  mach_timebase_info_data_t timebase;mach_timebase_info(&timebase);
  const double adjusted=double(timestamp)-_recordingLatencyMS*1e6*timebase.denom/timebase.numer;
  const auto position=adjusted>=1&&adjusted<double(UINT64_MAX)?_audio->renderer()->recordingClock().locate(uint64_t(adjusted)):std::nullopt;
  if(position)_recording->capture(*position,status,note,velocity);else ++_recording->missingTime;
}
- (void)stopRecordingCapture {
  if(!_recording)return;
  auto position=_audio->renderer()?_audio->renderer()->recordingClock().locate(mach_absolute_time()):std::nullopt;
  _recording->stop(position);
}
- (NSArray<NSDictionary *> *)midiEvents {
  auto result = [NSMutableArray array];
  auto events=_midi->drainSafe();
  std::stable_sort(events.begin(),events.end(),[](const auto &a,const auto &b){return a.timestamp<b.timestamp;});
  for (auto &e : events) {
    if((e.status&0xf0)==0xb0)_audio->graphController(e.note,e.velocity);
    [self captureRecordingEvent:e.timestamp status:e.status note:e.note velocity:e.velocity];
    [result addObject:@{
      @"timestamp" : @(e.timestamp),
      @"status" : @(e.status),
      @"note" : @(e.note),
      @"velocity" : @(e.velocity)
    }];
  }
  return result;
}
- (BOOL)prepareAudition:(NSError **)error {
  try {
    if(_isolatedSamplePreview) { _audio->stop(); _isolatedSamplePreview=NO; }
    if (!_audio->active())
      _audio->play(_document->playbackData(), 0, true, _document->sourcePath(),
                   _document->song().Order.GetCurrentSequenceIndex());
    return YES;
  } catch (const std::exception &e) {
    failure(error, e);
    return NO;
  }
}
- (BOOL)note:(NSInteger)note instrument:(NSInteger)instrument velocity:(NSInteger)velocity on:(BOOL)on {
  if (!_audio->active() || _isolatedSamplePreview)
    return NO;
  return _audio->renderer()->preview({uint8_t(std::clamp(note, NSInteger(1), NSInteger(120))),
                                      uint16_t(std::clamp(instrument, NSInteger(0), NSInteger(UINT16_MAX))),
                                      uint8_t(std::clamp(velocity, NSInteger(0), NSInteger(127))), bool(on)});
}
- (BOOL)sampleNote:(NSInteger)note sample:(NSInteger)sample velocity:(NSInteger)velocity on:(BOOL)on {
  if (!_audio->active() || _isolatedSamplePreview || sample < 1 || sample > _document->song().GetNumSamples()) return NO;
  return _audio->renderer()->preview({uint8_t(std::clamp(note, NSInteger(1), NSInteger(120))), 0,
    uint8_t(std::clamp(velocity, NSInteger(0), NSInteger(127))), bool(on), uint16_t(sample)});
}
- (void)panic {
  if (_audio->renderer())
    _audio->renderer()->panic();
}
- (NSData *)serializedData {
  try {
    if (_document->editable())
      return [self projectData];
    auto bytes = _document->playbackData();
    if (_document->song().Order.GetCurrentSequenceIndex() != 0) {
      return [NSPropertyListSerialization dataWithPropertyList:@{
        @"version" : @1,
        @"module" : [NSData dataWithBytes:bytes.data() length:bytes.size()],
        @"plugins" : @[],
        @"automation" : @[],
        @"sequence" : @(_document->song().Order.GetCurrentSequenceIndex())
      }
                                                        format:NSPropertyListBinaryFormat_v1_0
                                                       options:0
                                                         error:nil];
    }
    return [NSData dataWithBytes:bytes.data() length:bytes.size()];
  } catch (...) {
    return [NSData data];
  }
}
+ (BOOL)exportData:(NSData *)data path:(NSString *)path error:(NSError **)error {
  try {
    std::vector<PluginState> states;
    std::vector<ParameterChange> automation;
    NSData *module = data;
    std::optional<NativeSong> native;
    uint32_t sequence = UINT32_MAX;
    if (data.length >= 6 && std::memcmp(data.bytes, "bplist", 6) == 0) {
      NSDictionary *root = decodeProject(data);
      module = root[@"module"];
      states = decodePlugins(root);
      automation = decodeAutomation(root);
      if ([root[@"version"] unsignedIntegerValue] >= 3) {
        native = decodeNativeSong(root[@"native"]);
        Document validation(byteVector(module));
        native->validate(validation.song());
      }
      if (root[@"sequence"])
        sequence = uint32_t(unsignedInteger(root[@"sequence"], UINT8_MAX, "Invalid sequence selection"));
    }
    exportProjectAudio(byteVector(module), states, automation, path.UTF8String, sequence, native ? &*native : nullptr);
    return YES;
  } catch (const std::exception &e) {
    failure(error, e);
    return NO;
  }
}
+ (nullable NSDictionary *)inspectSampleFile:(NSString *)path error:(NSError **)error {
  try {
    Document decoded;decoded.importSample(path.UTF8String,1);
    const auto &sample=decoded.song().GetSample(1);
    const SamplePCMView pcm{{sample.sampleb(),size_t(sample.nLength)*sample.GetBytesPerSample()},sample.nLength,
      uint8_t(sample.GetElementarySampleSize()*8),sample.GetNumChannels()};
    if(!pcm.frames || !sample.HasSampleData() || pcm.channels>2 || !sample.nC5Speed)
      throw std::runtime_error("This file contains no previewable sample audio");
    const auto rate=sample.GetSampleRate(decoded.song().GetType());
    if(rate<100 || rate>768000)throw std::runtime_error("Unsupported preview sample rate");
    const uint32_t frames=uint32_t(std::min<uint64_t>({pcm.frames,uint64_t(rate)*30,2097152}));
    NSMutableData *data=[NSMutableData dataWithLength:size_t(frames)*pcm.channels*sizeof(float)];auto out=static_cast<float *>(data.mutableBytes);
    for(size_t i=0;i<size_t(frames)*pcm.channels;++i) {
      if(pcm.bits==16){int16_t value;std::memcpy(&value,pcm.data.data()+i*2,2);out[i]=float(value)/32768.f;}
      else out[i]=float(static_cast<int8_t>(pcm.data[i]))/128.f;
    }
    NSMutableArray *peaks=[NSMutableArray array];for(float v:decoded.waveform(1,0,frames,256,SampleChannels::Both))[peaks addObject:@(v)];
    return @{@"path":path,@"frames":@(pcm.frames),@"previewFrames":@(frames),@"rate":@(rate),@"channels":@(pcm.channels),
      @"seconds":@(double(pcm.frames)/rate),@"previewSeconds":@(double(frames)/rate),@"peaks":peaks,@"pcm":data};
  } catch(const std::exception &e){failure(error,e);return nil;}
}
#include "TrackerSessionAPI.inc"
@end
