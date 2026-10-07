#import "TrackerSession.h"
#include "../Audio/AudioDevice.hpp"
#include "../Audio/AudioExport.hpp"
#include "../Audio/SampleRecorder.hpp"
#include "editor/hosted/SelectionRender.hpp"
#include "editor/Sampling.hpp"
#include "editor/CurveFormulaReference.hpp"
#include "../Audio/MidiInput.hpp"
#include "../Plugins/PluginInventory.hpp"
#include "../Plugins/PluginLibrary.hpp"
#include "../Plugins/PluginPreset.hpp"
#include "AutomationValidation.hpp"
#include "SignalTelemetry.hpp"
#include "editor/PatternTools.hpp"
#include "editor/ProjectLoadRecovery.hpp"
#include <sys/stat.h>
#include "editor/ParameterProvenance.hpp"
#include "editor/ParameterBaseline.hpp"
#include "editor/GraphEditing.hpp"
#include "editor/GraphTrims.hpp"
#include "editor/GraphClipboard.hpp"
#include "editor/hosted/PluginAudioLayout.hpp"
#include "editor/PluginNoteSources.hpp"
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
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <fcntl.h>
#include <sys/file.h>
#include <limits>
#include <poll.h>
#include <signal.h>
#include <unistd.h>
using namespace Tracker;
namespace {
#include "NativeSongMetadata.inc"
#include "ProjectLoadRecovery.inc"
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
    @"physicalBus": @(bus.physicalChannels ? bus.physicalBus : bus.index), @"firstChannel": @(bus.firstChannel), @"physicalChannels": @(bus.physicalChannels ? bus.physicalChannels : bus.channels),
    @"name": @(bus.name.c_str()), @"direction": bus.input ? @"input" : @"output", @"active": @(bus.active), @"supported": @(bus.supported)}];
  return result;
}
NSString *songString(const CSoundFile &song, const std::string &value);
#include "PluginAssignments.inc"
NSDictionary *decodeProject(NSData *data, ProjectLoadRecovery *recovery = nullptr) {
  if (!data || data.length > 600 * 1024 * 1024)
    throw std::runtime_error("Project is unreadable or exceeds 600 MB");
  NSError *error = nil;
  id root = [NSPropertyListSerialization propertyListWithData:data
                                                      options:NSPropertyListImmutable
                                                       format:nil
                                                        error:&error];
  if (![root isKindOfClass:NSDictionary.class] ||
      ![root[@"module"] isKindOfClass:NSData.class] || (!recovery && ![root[@"plugins"] isKindOfClass:NSArray.class]))
    throw std::runtime_error("Invalid or unsupported ScreamSeq project.");
  if ([root[@"module"] length] == 0 || [root[@"module"] length] > 512 * 1024 * 1024)
    throw std::runtime_error("Invalid embedded module size");
  if(recovery) {
    if(![root[@"version"] isEqual:@(nativeProjectVersion)])recovery->warn("Read known fields from an incompatible project container version (current: " + std::to_string(nativeProjectVersion) + ").",false,true);
    for(NSString *key in root)if(![key hasPrefix:@"screamseqLoadRecovery"] && ![@[@"version",@"module",@"native",@"sequence",@"plugins",@"automation",@"recoveryTake"] containsObject:key])recovery->warn("Ignored unsupported project field: "+std::string(key.UTF8String),true);
  } else Automation::integer(root[@"version"], nativeProjectVersion, nativeProjectVersion);
  NSData *module = root[@"module"];
  const bool snapshot = isSongSnapshot({static_cast<const std::byte *>(module.bytes), module.length});
  if (!snapshot && !recovery)throw std::runtime_error("The embedded song does not match the project version");
  return root;
}
std::vector<PluginState> decodePlugins(NSDictionary *root) {
  NSArray *plugins = root[@"plugins"];
  if (![plugins isKindOfClass:NSArray.class])
    throw std::runtime_error("Invalid plugin rack; expected an array");
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
    PluginState state{descriptor(item), {}, bool([item[@"bypass"] boolValue])};
    state.instanceID = Automation::string(item[@"instanceID"], 128).UTF8String;
    state.audioLayout = Automation::string(item[@"audioLayout"] ?: @"",8192).UTF8String;
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
    {
      auto assignments = decodePluginAssignments(item[@"instrumentAssignments"]);
      if ((assignments.empty() ? 0 : assignments.front().instrument) != state.instrument)
        throw std::runtime_error("Primary instrument does not match plugin assignments");
      setPluginAssignments(state, assignments);
    }
    if (data.length) {
      auto begin = static_cast<const std::byte *>(data.bytes);
      state.state.assign(begin, begin + data.length);
    }
    states.push_back(std::move(state));
  }
  validatePluginAssignments(states);
  return states;
}
// A bundle location stored in a project, recovery file or graph recipe is only
// a hint. In memory a VST3 names the canonical location that was checked, or
// nothing when it could not be resolved, so no host code can load the stored
// string. The stored hint returns unchanged when the project is saved.
struct PluginPathHint {
  std::string classID, path; // As stored in the project.
  std::string loaded;        // In-memory location; empty while the plugin is missing.
  bool applies(const std::string &format, const std::string &currentPath, const std::string &currentClass) const {
    return format == "VST3" && currentClass == classID && (currentPath.empty() || currentPath == loaded);
  }
};
struct PluginPathHints {
  std::map<std::string, PluginPathHint> rack;                        // By plugin instance ID.
  std::map<std::pair<uint64_t, uint64_t>, PluginPathHint> graph;     // By subgraph and node ID.
  void clear() { rack.clear(); graph.clear(); }
};
std::string missingPlugin(const std::string &name, const std::string &path) {
  return "Plugin \"" + name + "\" is unavailable: its VST3 bundle " +
         (path.empty() ? std::string("has no known location")
                       : "\"" + path + "\" is missing or outside the trusted plugin locations") +
         ". Its saved state is preserved. Install it in a VST3 folder or add it from the plugin browser, then reopen the project.";
}
NSString *pathString(const std::string &value) {
  return [NSString stringWithUTF8String:value.c_str()];
}
// Returns an empty string when every VST3 resolved; otherwise one line per missing plugin.
std::string resolvePluginLocations(std::vector<PluginState> &plugins, NativeSong *native, NSArray<NSDictionary *> *inventory,
                                   PluginPathHints &hints) {
  std::string issue;
  auto resolve = [&](std::string &path, const std::string &classID, const std::string &name) {
    PluginPathHint hint{classID, path, {}};
    if (NSString *found = PluginTrust::resolve(pathString(path), pathString(classID), inventory))
      hint.loaded = found.UTF8String;
    else
      issue += (issue.empty() ? "" : "\n") + missingPlugin(name, path);
    path = hint.loaded;
    return hint;
  };
  for (auto &plugin : plugins) {
    auto &d = plugin.descriptor;
    if (d.format == "VST3")
      hints.rack[plugin.instanceID] = resolve(d.path, d.classID, d.name);
  }
  if (native)
    for (auto &definition : native->signal.library)
      for (auto &node : definition.nodes) {
        auto &r = node.plugin;
        if (node.kind == SignalNodeKind::Plugin && r.format == "VST3")
          hints.graph[{definition.id, node.id}] = resolve(r.path, r.classID, r.name.empty() ? node.name : r.name);
      }
  return issue;
}
// Load-site check for recipes that reached the document by any route.
std::string pluginLocationIssue(const PluginDescriptor &d, const PluginPathHint *hint, NSArray<NSDictionary *> *inventory) {
  if (d.format != "VST3" || (!d.path.empty() && PluginTrust::trusted(pathString(d.path), inventory)))
    return {};
  return missingPlugin(d.name, d.path.empty() && hint && hint->applies(d.format, d.path, d.classID) ? hint->path : d.path);
}
std::string graphLocationIssue(const NativeSong &native, const PluginPathHints &hints, NSArray<NSDictionary *> *inventory,
                               bool unresolvedOnly = false) {
  std::string issue;
  for (const auto &definition : native.signal.library)
    for (const auto &node : definition.nodes) {
      const auto &r = node.plugin;
      if (node.kind != SignalNodeKind::Plugin || r.format != "VST3" || (unresolvedOnly && !r.path.empty()))
        continue;
      const auto hint = hints.graph.find({definition.id, node.id});
      const auto text = pluginLocationIssue({r.type, r.subtype, r.manufacturer, r.name.empty() ? node.name : r.name, r.format, r.path, r.classID, false},
                                            hint == hints.graph.end() ? nullptr : &hint->second, inventory);
      if (!text.empty())
        issue += (issue.empty() ? "" : "\n") + text;
    }
  return issue;
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
  std::vector<ParameterChange> automation, manual;
  uint64_t sequence = 0;
  std::string bypassTarget; // A delta, so save/state capture cannot turn Undo into a rack rebuild.
  std::vector<ParameterChange> parameterValues; // Historical manual scalars, never effective modulation output.
  size_t bytes() const {
    size_t result = (automation.size() + manual.size() + parameterValues.size()) * sizeof(ParameterChange) + bypassTarget.size();
    for (const auto &plugin : plugins)
      result += plugin.state.size() + plugin.audioLayout.size() + plugin.descriptor.name.size() + sizeof(PluginState) + plugin.aliases.size() * sizeof(PluginInstrumentAlias);
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
  NSInteger _isolatedPreviewSample;
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
  std::vector<std::pair<uint64_t,uint64_t>> _historyGroups;
  uint64_t _knownHistorySequence, _parameterGestureSequence;
  BOOL _parameterGesture;
  NSInteger _parameterGestureSlot, _parameterGestureID;
  std::chrono::steady_clock::time_point _lastParameterEdit;
  std::string _pluginError;
  std::string _pluginWarning; // Non-blocking; reported with the document issues.
  ProjectLoadRecovery _loadRecovery;
  NSString *_loadSourcePath;
  NSDictionary *_loadRecoveryArchives; // Inactive opaque source fragments from portable recovery.
  PluginPathHints _pluginPathHints;
  std::unique_ptr<NativePlugin> _graphEditorPlugin;
  GraphPluginRecipe _graphEditorRecipe;
  uint64_t _graphEditorGraph, _graphEditorNode;
  NSString *_graphEditorID,*_graphEditorDocument;
  NSString *_graphParameterGesture,*_graphParameterGestureDocument;
  uint64_t _graphParameterGestureSequence,_graphParameterGestureGraph,_graphParameterGestureNode;
  uint32_t _graphParameterGestureID;
  NSString *_automationDocumentID;
  uint64_t _pluginRevision;
  std::string _lastTouchedPlugin;
  uint32_t _lastTouchedParameter;
  uint64_t _touchSequence;
  NSString *_lastTouchedSource;
  std::optional<SampleClipboard> _sampleClipboard;
  NSString *_sampleClipboardID;
  std::unique_ptr<NoteRecording> _recording;
  std::unique_ptr<SampleRecorder> _sampleRecorder;
  NSString *_sampleRecordingID, *_sampleRecordingDocument, *_sampleRecordingRevision;
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
- (NSString *)sampleRecordingTakeID { return _sampleRecordingID; }
- (void)synchronizeHistory {
  if (_knownHistorySequence == _document->historySequence()) return;
  // A document edit forks the same redo branch as a plugin edit.
  _effectRedo.clear();
  _knownHistorySequence = _document->historySequence();
  _parameterGestureSequence = 0;
}
- (BOOL)nextHistoryIsPlugin:(BOOL)redo {
  const auto &history = redo ? _effectRedo : _effectUndo;
  const auto document = _document->historyHead(redo);
  return !history.empty() && (!document || (redo ? history.back().sequence < document : history.back().sequence > document));
}
- (BOOL)canUndo {
  [self synchronizeHistory];
  if ([self nextHistoryIsPlugin:NO]) return YES;
  try { validatePluginCapacity(_plugins, _document->historyNative(false).mixer.buses.size()); } catch (...) { return NO; }
  return _document->canUndo();
}
- (BOOL)canRedo {
  [self synchronizeHistory];
  if ([self nextHistoryIsPlugin:YES]) return YES;
  try { validatePluginCapacity(_plugins, _document->historyNative(true).mixer.buses.size()); } catch (...) { return NO; }
  return _document->canRedo();
}
- (void)parameterGesture:(BOOL)active {
  _parameterGesture = active;
  _parameterGestureSequence = 0;
}
- (double)sampleRate {
  return _audio->sampleRate();
}
- (NSUInteger)bufferSize {
  return _audio->bufferSize();
}
+ (BOOL)trustPluginLocation:(NSString *)path {
  return PluginTrust::trust(path);
}
- (void)newSong:(BOOL)demo {
  _sampleRecorder.reset();
  _sampleRecordingID = nil; _sampleRecordingDocument = nil; _sampleRecordingRevision = nil;
  // The bridge method cannot refuse, so a replaced document discards its take
  // instead of leaving it attached to a song it was not recorded against.
  _recording.reset();
  _recordingID = nil;
  _recordingRevision = nil;
  _audio->setPlugins({});
  _plugins.clear();
  _automation.clear();
  _manualParameters.clear();
  _effectUndo.clear();
  _effectRedo.clear();
  _historyGroups.clear();
  _knownHistorySequence = _parameterGestureSequence = 0; _parameterGesture = NO;
  _pluginError.clear();
  _pluginWarning.clear();
  _pluginPathHints.clear();
  _document = demo ? Document::demo() : std::make_unique<Document>();
  _automationDocumentID = NSUUID.UUID.UUIDString;
  _loadRecovery={};_loadSourcePath=nil;_loadRecoveryArchives=nil;
  _graphEditorPlugin.reset();_graphEditorID=nil;
  _lastTouchedPlugin.clear(); _touchSequence = 0; _lastTouchedSource = nil;
}
- (BOOL)openPath:(NSString *)path error:(NSError **)error {
  try {
    if(_recording)throw std::runtime_error("Finish or discard the recording take before opening another song.");
    if(_sampleRecorder)throw std::runtime_error("Add or discard the recorded sample before opening another song.");
    std::unique_ptr<Document> next;
    std::vector<PluginState> plugins;
    std::vector<ParameterChange> automation;
    std::unique_ptr<NoteRecording> recoveredTake;
    bool recoveredTakeCompatible = false;
    PluginPathHints hints;
    std::string missing;
    ProjectLoadRecovery recovery;
    NSMutableDictionary *recoveryArchives=[NSMutableDictionary dictionary];
    if ([@[@"screamseq", @"resonance"] containsObject:path.pathExtension.lowercaseString]) {
      auto attributes = [[NSFileManager defaultManager] attributesOfItemAtPath:path error:nil];
      if ([attributes fileSize] > 600 * 1024 * 1024)
        throw std::runtime_error("Project exceeds 600 MB");
      NSDictionary *root = decodeProject([NSData dataWithContentsOfFile:path], &recovery);
      auto snapshot=byteVector(root[@"module"]);
      recoverLegacySongSnapshot(snapshot,recovery);
      if(!isSongSnapshot(snapshot))recovery.warn("Opening the legacy embedded tracker module; native sample/timing extensions may be unavailable.",false,true);
      next = std::make_unique<Document>(snapshot);
      for(NSString *key in root)if([key hasPrefix:@"screamseqLoadRecovery"])recoveryArchives[key]=root[key];
      if (root[@"sequence"]) try {
        const auto sequence = unsignedInteger(root[@"sequence"], UINT8_MAX, "Invalid sequence selection");
        if (sequence >= next->song().Order.GetNumSequences())
          throw std::runtime_error("The selected sequence is missing from this project.");
        next->song().Order.SetSequence(SEQUENCEINDEX(sequence));
      } catch(const std::exception &e) {recovery.warn("Reset unavailable sequence selection: "+std::string(e.what()),true);}
      auto native = recoverNativeSong(root[@"native"], *next, recovery);
      std::map<uint32_t,uint32_t> recoveredSlots;
      try {auto decoded=decodePlugins(root);validatePluginCapacity(decoded,native.mixer.buses.size());plugins=std::move(decoded);for(uint32_t i=0;i<plugins.size();++i)recoveredSlots[i]=i;}
      catch(const std::exception &e) {
        id entries=root[@"plugins"];
        if(![entries isKindOfClass:NSArray.class] || [entries count]>maximumNativePlugins)recovery.warn("Skipped incompatible plugin rack: "+std::string(e.what()),true);
        else for(NSUInteger i=0;i<[entries count];++i)try {
          auto one=decodePlugins(@{@"plugins":@[entries[i]]});
          if(std::any_of(plugins.begin(),plugins.end(),[&](const auto &p){return p.instanceID==one[0].instanceID;}))throw std::invalid_argument("Duplicate plugin instance identity");
          auto candidate=plugins;candidate.push_back(std::move(one[0]));
          validatePluginCapacity(candidate,native.mixer.buses.size());
          recoveredSlots[uint32_t(i)]=uint32_t(plugins.size());plugins=std::move(candidate);
        }catch(const std::exception &error){recovery.warn("Skipped plugin slot "+std::to_string(i+1)+": "+error.what(),true);}
      }
      const auto removedCables=std::erase_if(native.signal.stageConnections,[&](const auto &r){
        for(const auto &e:{r.source,r.target})if(!e.plugin.empty()&&std::none_of(plugins.begin(),plugins.end(),[&](const auto &p){return p.instanceID==e.plugin;})) {
          recovery.warn("Disconnected a graph cable referring to an unavailable rack plugin: "+e.plugin,true);return true;
        }return false;
      });
      if(removedCables)native.reconcile(next->song());
      missing = resolvePluginLocations(plugins, &native, _pluginInventory->cached(), hints);
      next->restoreNative(std::move(native));
      id recorded=root[@"automation"];
      if(recorded) {
        if(![recorded isKindOfClass:NSArray.class] || [recorded count]>100000)recovery.warn("Skipped invalid or oversized recorded automation.",true);
        else for(NSUInteger i=0;i<[recorded count];++i)try {
          NSArray *savedPlugins=[root[@"plugins"] isKindOfClass:NSArray.class]?root[@"plugins"]:@[];
          auto points=decodeAutomation(@{@"plugins":savedPlugins,@"automation":@[recorded[i]]});
          auto &point=points[0];const auto slot=recoveredSlots.find(point.slot);
          if(slot==recoveredSlots.end())throw std::invalid_argument("Target plugin was not recovered");
          point.slot=slot->second;automation.push_back(point);
        }catch(const std::exception &e){recovery.warn("Skipped recorded automation["+std::to_string(i)+"]: "+e.what(),true);}
      }
      if (root[@"recoveryTake"]) try {
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
      }catch(const std::exception &e){recoveredTake.reset();recovery.warn("Skipped incompatible recording take: "+std::string(e.what()),true);}
    } else
      next = Document::open(path.UTF8String);
    validatePluginCapacity(plugins, next->native().mixer.buses.size());
    _audio->stop();
    _pluginError.clear();
    _pluginWarning.clear();
    _pluginPathHints = std::move(hints);
    try {
      // Missing or untrusted rack plugins are never instantiated.
      if (const auto issue = [self rackLocationIssue:plugins]; !issue.empty())
        throw std::runtime_error(issue);
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
    _historyGroups.clear();
    _knownHistorySequence = _parameterGestureSequence = 0; _parameterGesture = NO;
    _graphEditorPlugin.reset();_graphEditorID=nil;
    _document = std::move(next);
    _loadRecovery=std::move(recovery);_loadSourcePath=[path copy];_loadRecoveryArchives=[recoveryArchives copy];
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
- (std::string)rackLocationIssue:(const std::vector<PluginState> &)states {
  std::string issue;
  NSArray *inventory = nil;
  bool loaded = false;
  for (const auto &state : states) {
    if (state.descriptor.format != "VST3")
      continue;
    if (!loaded) { inventory = _pluginInventory->cached(); loaded = true; }
    const auto hint = _pluginPathHints.rack.find(state.instanceID);
    const auto text = pluginLocationIssue(state.descriptor, hint == _pluginPathHints.rack.end() ? nullptr : &hint->second, inventory);
    if (!text.empty())
      issue += (issue.empty() ? "" : "\n") + text;
  }
  return issue;
}
- (void)requireGraphPluginLocations:(const NativeSong &)native {
  if (const auto issue = graphLocationIssue(native, _pluginPathHints, _pluginInventory->cached()); !issue.empty())
    throw std::runtime_error(issue);
}
// Missing plugins whose stored bundle exists on this Mac but is not trusted.
// Nothing is loaded or trusted here; the caller must ask the user.
- (NSArray<NSDictionary *> *)unresolvedPluginLocations {
  NSMutableArray *result = [NSMutableArray array];
  auto add = [&](const PluginPathHint &hint, const std::string &format, const std::string &path, const std::string &classID,
                 const std::string &name, NSString *kind, bool instrument = false) {
    if (!path.empty() || !hint.applies(format, path, classID))
      return;
    NSString *real = PluginTrust::canonical(pathString(hint.path));
    BOOL directory = NO;
    if (!real || ![real.pathExtension.lowercaseString isEqual:@"vst3"] ||
        ![NSFileManager.defaultManager fileExistsAtPath:real isDirectory:&directory] || !directory)
      return;
    [result addObject:@{@"name" : pathString(name) ?: @"Plugin", @"classID" : pathString(classID) ?: @"",
                        @"storedPath" : pathString(hint.path) ?: @"", @"canonicalPath" : real, @"kind" : kind,
                        @"isInstrument" : @(instrument)}];
  };
  for (const auto &plugin : _plugins)
    if (const auto hint = _pluginPathHints.rack.find(plugin.instanceID); hint != _pluginPathHints.rack.end())
      add(hint->second, plugin.descriptor.format, plugin.descriptor.path, plugin.descriptor.classID, plugin.descriptor.name, @"rack",
          plugin.descriptor.instrument || plugin.descriptor.type == kAudioUnitType_MusicDevice);
  for (const auto &definition : _document->native().signal.library)
    for (const auto &node : definition.nodes)
      if (const auto hint = _pluginPathHints.graph.find({definition.id, node.id});
          node.kind == SignalNodeKind::Plugin && hint != _pluginPathHints.graph.end())
        add(hint->second, node.plugin.format, node.plugin.path, node.plugin.classID,
            node.plugin.name.empty() ? node.name : node.plugin.name, @"graph");
  return result;
}
// The user approved these exact canonical locations. Each must be one this
// document is waiting for; it is validated in the isolated scanner, trusted,
// remembered, and the waiting plugins resolve in place with their saved state.
- (BOOL)trustPluginLocations:(NSArray<NSString *> *)paths error:(NSError **)error {
  try {
    if (![paths isKindOfClass:NSArray.class] || !paths.count || paths.count > maximumNativePlugins + 64)
      throw std::invalid_argument("Name the canonical plugin locations to trust");
    NSArray *waiting = [self unresolvedPluginLocations];
    NSMutableSet *approved = [NSMutableSet set];
    for (id path in paths) {
      if (![path isKindOfClass:NSString.class] || ![PluginTrust::canonical(path) isEqual:path])
        throw std::invalid_argument("Plugin locations must be canonical paths of existing bundles");
      bool expected = false;
      for (NSDictionary *entry in waiting)
        if ([entry[@"canonicalPath"] isEqual:path]) {
          expected = true;
          if (![approved containsObject:@[path, entry[@"classID"]]]) {
            const bool known = PluginTrust::trusted(path, nil);
            PluginTrust::trust(path); // The scanner result is only meaningful for a loadable bundle.
            try {
              runScanner(@[ @"--validate-vst3", path, entry[@"classID"], [entry[@"isInstrument"] boolValue] ? @"1" : @"0" ]);
            } catch (...) {
              if (!known) PluginTrust::revoke(path);
              throw;
            }
            [approved addObject:@[path, entry[@"classID"]]];
          }
        }
      if (!expected)
        throw std::invalid_argument("This document is not waiting for that plugin location");
    }
    _audio->stop();
    auto plugins = _plugins;
    auto hints = _pluginPathHints;
    auto native = _document->native();
    auto accept = [&](PluginPathHint &hint, const std::string &format, std::string &path, const std::string &classID) {
      if (!path.empty() || !hint.applies(format, path, classID))
        return;
      NSString *real = PluginTrust::canonical(pathString(hint.path));
      if (real && [approved containsObject:@[real, pathString(classID) ?: @""]])
        path = hint.loaded = real.UTF8String;
    };
    for (auto &plugin : plugins)
      if (auto hint = hints.rack.find(plugin.instanceID); hint != hints.rack.end())
        accept(hint->second, plugin.descriptor.format, plugin.descriptor.path, plugin.descriptor.classID);
    bool graphChanged = false;
    for (auto &definition : native.signal.library)
      for (auto &node : definition.nodes)
        if (auto hint = hints.graph.find({definition.id, node.id}); node.kind == SignalNodeKind::Plugin && hint != hints.graph.end()) {
          const auto before = node.plugin.path;
          accept(hint->second, node.plugin.format, node.plugin.path, node.plugin.classID);
          graphChanged = graphChanged || before != node.plugin.path;
        }
    if (graphChanged)
      _document->restoreNative(std::move(native)); // Resolution is not an edit: no Undo step, no revision.
    _pluginPathHints = std::move(hints);
    for (NSArray *entry in approved)
      PluginTrust::persist(entry[0]);
    std::string issue;
    try {
      if (const auto location = [self rackLocationIssue:plugins]; !location.empty())
        throw std::runtime_error(location);
      _audio->setPlugins(plugins, _automation);
    } catch (const std::exception &e) {
      issue = e.what();
      _audio->setPlugins({});
    }
    _plugins = std::move(plugins);
    _pluginError = std::move(issue);
    _graphEditorPlugin.reset();_graphEditorID=nil;
    ++_pluginRevision;
    return YES;
  } catch (const std::exception &e) {
    failure(error, e);
    return NO;
  }
}
// File- or API-supplied graph recipes name a bundle only as a hint.
- (void)resolveGraphRecipe:(GraphPluginRecipe &)recipe {
  if (recipe.format != "VST3" || recipe.path.empty())
    return; // An empty path is a preserved missing plugin; it cannot be loaded.
  NSString *found = PluginTrust::resolve(pathString(recipe.path), pathString(recipe.classID), _pluginInventory->cached());
  if (!found)
    throw Automation::Error(-32602, ("Graph plugin \"" + recipe.name + "\" names a VST3 bundle outside the trusted plugin locations. "
                                     "Add it with plugin.add or install it in a VST3 folder first.").c_str());
  recipe.path = found.UTF8String;
}
// Pin new recipes to the actual physical layout without modifying their saved sound.
- (void)pinGraphRecipeLayout:(GraphPluginRecipe &)recipe {
  PluginState state;state.descriptor={recipe.type,recipe.subtype,recipe.manufacturer,recipe.name,recipe.format,recipe.path,recipe.classID,false};state.state=recipe.state;state.audioLayout=recipe.audioLayout;state.auxiliaryInputs=recipe.inputs;state.auxiliaryOutputs=recipe.outputs;
  NativePlugin probe(state,_audio->sampleRate(),false);Automation::require(!probe.isInstrument(),"Graph nodes require effect plugins");recipe.audioLayout=pluginAudioLayoutSignature(probe.buses());
}
// Fold manual parameter edits into the saved baseline without baking the current
// playback automation values into it. Runs on the control worker while UI writes
// are suspended; the live parameter queue remains bounded and cheap.
// This runs inside Save, recovery and Play, so it never throws and never keeps
// an entry: values a plugin refuses are dropped and reported as a warning.
- (void)commitManualParameters {
  if (_manualParameters.empty())
    return;
  const auto pending = std::move(_manualParameters);
  _manualParameters.clear();
  auto states = _plugins;
  size_t dropped = 0;
  std::string detail;
  for (size_t slot = 0; slot < states.size(); ++slot) {
    const auto count = size_t(std::count_if(pending.begin(), pending.end(), [&](const auto &p) { return p.slot == slot; }));
    if (!count)
      continue;
    size_t rejected = 0;
    try {
      if (const auto issue = [self rackLocationIssue:std::vector<PluginState>{states[slot]}]; !issue.empty())
        throw std::runtime_error(issue);
      NativePlugin plugin(states[slot], _audio->sampleRate());
      for (const auto &point : pending) {
        if (point.slot != slot || plugin.parameter(point.id, point.value))
          continue;
        // A full host queue is drained by capturing state; then retry once.
        (void)plugin.state();
        if (!plugin.parameter(point.id, point.value))
          ++rejected;
      }
      auto state = plugin.state();
      state.bypass = states[slot].bypass;
      state.instrument = states[slot].instrument;
      states[slot] = std::move(state);
    } catch (const std::exception &e) {
      // Keep the previously saved state of this slot.
      rejected = count;
      detail = e.what();
    }
    if (rejected) {
      dropped += rejected;
      detail = "\"" + states[slot].descriptor.name + "\"" + (detail.empty() ? "" : " (" + detail + ")");
    }
  }
  for (const auto &point : pending)
    if (point.slot >= states.size())
      ++dropped;
  _plugins = std::move(states);
  if (dropped) {
    _pluginWarning = std::to_string(dropped) + " manual plugin parameter edit" + (dropped == 1 ? " was" : "s were") +
                     " not stored in the saved plugin state" + (detail.empty() ? "" : ": " + detail) + ".";
    NSLog(@"ScreamSeq: %s", _pluginWarning.c_str());
  } else
    _pluginWarning.clear();
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
  const int projectVersion = nativeProjectVersion;
  for (auto &plugin : _plugins) {
    NSMutableDictionary *item = [descriptorDictionary(plugin.descriptor) mutableCopy];
    if (const auto hint = _pluginPathHints.rack.find(plugin.instanceID); hint != _pluginPathHints.rack.end() &&
        hint->second.applies(plugin.descriptor.format, plugin.descriptor.path, plugin.descriptor.classID))
      item[@"path"] = pathString(hint->second.path) ?: @""; // Projects keep the location they stored.
    item[@"bypass"] = @(plugin.bypass);
    item[@"instrument"] = @(plugin.instrument);
    item[@"instrumentAssignments"] = encodePluginAssignments(plugin);
    item[@"instanceID"] = @(plugin.instanceID.c_str());
    item[@"state"] = [NSData dataWithBytes:plugin.state.data() length:plugin.state.size()];
    item[@"audioLayout"] = @(plugin.audioLayout.c_str());
    NSMutableArray *inputs = [NSMutableArray array], *outputs = [NSMutableArray array];
    for (auto bus : plugin.auxiliaryInputs) [inputs addObject:@(bus)];
    for (auto bus : plugin.auxiliaryOutputs) [outputs addObject:@(bus)];
    item[@"auxiliaryInputs"] = inputs; item[@"auxiliaryOutputs"] = outputs;
    [plugins addObject:item];
  }
  auto automation = [NSMutableArray array];
  for (auto &point : _automation)
    [automation addObject:@[ @(point.slot), @(point.id), @(point.value), @(point.frame) ]];
  NSDictionary *nativeMetadata = nil;
  if (_pluginPathHints.graph.empty())
    nativeMetadata = encodeNativeSong(_document->native());
  else {
    auto native = _document->native();
    for (auto &definition : native.signal.library)
      for (auto &node : definition.nodes) {
        const auto hint = _pluginPathHints.graph.find({definition.id, node.id});
        if (hint != _pluginPathHints.graph.end() && node.kind == SignalNodeKind::Plugin &&
            hint->second.applies(node.plugin.format, node.plugin.path, node.plugin.classID))
          node.plugin.path = hint->second.path;
      }
    nativeMetadata = encodeNativeSong(native);
  }
  NSMutableDictionary *root = [@{
    @"version" : @(projectVersion),
    @"native" : nativeMetadata,
    @"sequence" : @(_document->song().Order.GetCurrentSequenceIndex()),
    @"module" : [NSData dataWithBytes:bytes.data() length:bytes.size()],
    @"plugins" : plugins,
    @"automation" : automation
  } mutableCopy];
  if(_loadRecoveryArchives)[root addEntriesFromDictionary:_loadRecoveryArchives];
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
    [self validateRecoveredSavePath:path];
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
- (void)validateRecoveredSavePath:(NSString *)path {
  if(!_loadRecovery.protectSource || !_loadSourcePath.length)return;
  const auto canonical=[](NSString *p){return p.stringByStandardizingPath.stringByResolvingSymlinksInPath;};
  struct stat original{},destination{};
  const bool sameInode=::stat(_loadSourcePath.fileSystemRepresentation,&original)==0 && ::stat(path.fileSystemRepresentation,&destination)==0 && original.st_dev==destination.st_dev && original.st_ino==destination.st_ino;
  if(sameInode || [canonical(path) isEqual:canonical(_loadSourcePath)])throw std::runtime_error("This project was recovered with compatibility warnings. Save a new .screamseq copy to preserve the original file.");
}
- (BOOL)savePath:(NSString *)path error:(NSError **)error {
  try {
    [self validateRecoveredSavePath:path];
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
    _loadRecovery.protectSource=false;
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
    [self requireGraphPluginLocations:_document->native()];
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
- (void)shutdown {
  _sampleRecorder.reset();
  _sampleRecordingID = nil; _sampleRecordingDocument = nil; _sampleRecordingRevision = nil;
  // NSApplication terminates via exit(), so the app controller's retained
  // session need not deallocate before vendor static destructors run. Retire
  // every live processor/editor while AppKit and the main thread still exist.
  // AudioDevice's destructor joins output callbacks before releasing its graph.
  _midi.reset();
  _audio.reset();
  _graphEditorPlugin.reset();
  _graphEditorID = nil;
  _graphEditorDocument = nil;
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
- (NSDictionary *)listenTelemetry {
  NSDictionary *listen=@{@"port":NSNull.null,@"gainDB":@0,@"available":@YES,@"pending":@NO,@"transitionFrames":@0};
  if(const auto *observation=_audio->signalObservation()) {
    const auto target=observation->listen.requested();
    listen=@{@"port":target.token&&target.token<=observation->ports.size()?@(observation->ports[target.token-1].key.c_str()):(id)NSNull.null,
      @"gainDB":@(20*std::log10(target.gain)),@"available":@(!target.token||observation->available(target.token)),@"pending":@(observation->listen.pending()),@"transitionFrames":@(observation->listen.transitionFrames())};
  }
  return listen;
}
- (NSDictionary *)signalTelemetryWithSongPortsOnly:(BOOL)songOnly {
  auto ports=encodeSignalTelemetryPorts(_audio->signalObservation(),songOnly);
  return @{@"ports":ports,@"listen":[self listenTelemetry],@"routing":[self routingTelemetry],@"active":@(_audio->active()),@"playing":@(self.playing),@"sampleRate":@(_audio->sampleRate()),
    @"freshnessFrames":@4096,@"peakDecaySeconds":@0.2,@"silenceThreshold":@1e-7,@"scope":@"Host ports, exact reusable graph copies, cable contributions and control values; control ranges describe quantum endpoints"};
}
- (NSDictionary *)signalTelemetry {
  return [self signalTelemetryWithSongPortsOnly:NO];
}
- (NSDictionary *)songSignalTelemetry {
  return [self signalTelemetryWithSongPortsOnly:YES];
}
- (NSDictionary *)routingTelemetry {
  const auto reading=_audio->mixerRoutingReading();
  const bool active=_audio->active();
  return @{@"available":@(reading.requested!=0),@"active":@(active),
    @"requestedPlan":@(reading.requested),@"renderedPlan":@(reading.rendered),@"failedPlan":@(reading.failed),
    @"state":!active?@"stopped":reading.rejected()?@"failed":reading.preparing()?@"preparing":@"stable",
    @"latencyPending":@(active && _audio->pluginLatencyChanged())};
}
- (NSDictionary *)telemetry {
  auto t = _audio->telemetry();
  auto positions=[NSMutableArray array];
  if(_audio->active() && _audio->renderer()) for(const auto &v:_audio->renderer()->voicePositions())
    [positions addObject:@{@"channel":@(v.channel),@"sample":@(v.sample),@"instrument":@(v.instrument),@"sampleFrame":@(v.sampleFrame),@"generation":@(v.generation),@"envelopeTicks":@[@(v.envelopeTicks[0]),@(v.envelopeTicks[1]),@(v.envelopeTicks[2])]}];
  return @{
    @"voicePositions":positions, @"audioActive":@(_audio->active()), @"audition":@(_audio->active()&&!self.playing),
    @"playing": @(self.playing), @"loop": @(_playbackLoop), @"region": _playbackRegion ?: @{},
    @"order" : @(t.order),
    @"pattern" : @(t.pattern),
    @"row" : @(t.row), @"patternPosition":@(t.patternPosition),
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
    @"routing": [self routingTelemetry],
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
    [extraColumns addObject:@(count==native.performance.columns.end()?1:count->second)];
  }
  if(s.Patterns.IsValidPat(PATTERNINDEX(pattern))) [performanceCommands addObjectsFromArray:patternEffectObjects(native,s,uint16_t(pattern))];
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
  if (const auto issue = graphLocationIssue(native, _pluginPathHints, nil, true); !issue.empty())
    [issues addObject:@(issue.c_str())];
  if (!_pluginWarning.empty())
    [issues addObject:@(_pluginWarning.c_str())];
  const char *format = s.GetType() == MOD_TYPE_MPT   ? "MPTM"
                       : s.GetType() == MOD_TYPE_IT  ? "IT"
                       : s.GetType() == MOD_TYPE_XM  ? "XM"
                       : s.GetType() == MOD_TYPE_MOD ? "MOD"
                       : s.GetType() == MOD_TYPE_S3M ? "S3M"
                                                     : "Legacy";
  NSMutableArray *loadWarnings=[NSMutableArray array];
  for(const auto &warning:_loadRecovery.warnings)[loadWarnings addObject:@(warning.c_str())];
  [issues addObjectsFromArray:loadWarnings];
  return @{
    @"loadWarnings":loadWarnings,@"requiresSaveAs":@(_loadRecovery.protectSource),@"loadSourcePath":_loadSourcePath?:@"",
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
    @"graphLanes":graphLanes,@"graphCommands":graphCommands, @"preciseNotes":preciseNotes, @"effectColumns": extraColumns, @"performanceCommands": performanceCommands, @"effectBindings":encodePatternPerformance(native.performance)[@"bindings"], @"scratchGestures":encodeScratchGestures(native),
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
    const auto beforeRevision=_document->revision;
    auto edits = _document->edit({{uint16_t(p), uint16_t(r), uint16_t(c), {}, after}});
    if(_document->revision!=beforeRevision && _document->undoChangesAutomation()) _audio->stop();
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
- (void)undoDocument {
  if (!_document->canUndo()) return;
  const auto &next = _document->historyNative(false);
  std::unique_ptr<MixerTransition::Plan> routing;
  std::unique_ptr<GraphControlPlan> parameters;
  if (_document->undoChangesStructure() ||
      next.performance != _document->native().performance || next.preciseNotes != _document->native().preciseNotes)
    _audio->stop();
  else if (_document->undoChangesMixer() && next.automation!=_document->native().automation) {
    // A linked bank edit can affect both the song graph and pattern lanes.
    // Carry their snapshots in one routing publication at one audio boundary.
    parameters=_audio->prepareGraphControls(next);
    if(!parameters)routing=_audio->prepareMixerRouting(next);
    if(!parameters && !routing && _audio->active())throw std::runtime_error("This combined graph/automation Undo requires a stopped transport; playback was preserved");
  }
  else if (_document->undoChangesMixer() && next.mixer==_document->native().mixer) {
    parameters=_audio->prepareGraphControls(next);if(!parameters){routing=_audio->prepareMixerRouting(next);if(_audio->active()&&!routing)throw std::runtime_error("This routing Undo needs a stopped transport; playback was preserved");}
  }
  else if (_document->undoChangesMixer()) {
    if(!_audio->mixerRoutingReady())throw std::runtime_error("Routing transition is still preparing; retry Undo");
    routing=_audio->prepareMixerRouting(next);if(!routing && _audio->active())throw std::runtime_error("This routing Undo needs a stopped transport; playback was preserved");
  }
  else if (_document->undoChangesAutomation()) {
    try { _audio->updateMusicalAutomation(next); } catch (...) { _audio->stop(); }
  }
  auto scratch=next.scratchGestures!=_document->native().scratchGestures?_audio->prepareScratchUpdate(next):nullptr;
  if(scratch&&(routing||parameters))throw std::runtime_error("This combined scratch/routing history edit requires stopped playback");
  auto publish=[&]{if(scratch&&!_audio->publishScratchUpdate(scratch))throw std::runtime_error("Scratch publication is busy; retry history edit");if(parameters && !_audio->publishGraphControls(std::move(parameters)))throw std::runtime_error("Graph parameter publication is busy; retry Undo");if(routing && !_audio->publishMixerRouting(routing))throw std::runtime_error("Routing publication is busy; retry Undo");};
  auto e = routing||parameters||scratch?_document->undo(publish):_document->undo();
  if (_audio->renderer()) _audio->renderer()->applyColumnMutes(_document->native(), _document->song());
  if (_audio->playing() && !_audio->renderer()->enqueue(e))
    _audio->stop();
}
- (void)redoDocument {
  if (!_document->canRedo()) return;
  const auto &next = _document->historyNative(true);
  std::unique_ptr<MixerTransition::Plan> routing;
  std::unique_ptr<GraphControlPlan> parameters;
  if (_document->redoChangesStructure() ||
      next.performance != _document->native().performance || next.preciseNotes != _document->native().preciseNotes)
    _audio->stop();
  else if (_document->redoChangesMixer() && next.automation!=_document->native().automation) {
    // A linked bank edit can affect both the song graph and pattern lanes.
    // Carry their snapshots in one routing publication at one audio boundary.
    parameters=_audio->prepareGraphControls(next);
    if(!parameters)routing=_audio->prepareMixerRouting(next);
    if(!parameters && !routing && _audio->active())throw std::runtime_error("This combined graph/automation Redo requires a stopped transport; playback was preserved");
  }
  else if (_document->redoChangesMixer() && next.mixer==_document->native().mixer) {
    parameters=_audio->prepareGraphControls(next);if(!parameters){routing=_audio->prepareMixerRouting(next);if(_audio->active()&&!routing)throw std::runtime_error("This routing Redo needs a stopped transport; playback was preserved");}
  }
  else if (_document->redoChangesMixer()) {
    if(!_audio->mixerRoutingReady())throw std::runtime_error("Routing transition is still preparing; retry Redo");
    routing=_audio->prepareMixerRouting(next);if(!routing && _audio->active())throw std::runtime_error("This routing Redo needs a stopped transport; playback was preserved");
  }
  else if (_document->redoChangesAutomation()) {
    try { _audio->updateMusicalAutomation(next); } catch (...) { _audio->stop(); }
  }
  auto scratch=next.scratchGestures!=_document->native().scratchGestures?_audio->prepareScratchUpdate(next):nullptr;
  if(scratch&&(routing||parameters))throw std::runtime_error("This combined scratch/routing history edit requires stopped playback");
  auto publish=[&]{if(scratch&&!_audio->publishScratchUpdate(scratch))throw std::runtime_error("Scratch publication is busy; retry history edit");if(parameters && !_audio->publishGraphControls(std::move(parameters)))throw std::runtime_error("Graph parameter publication is busy; retry Redo");if(routing && !_audio->publishMixerRouting(routing))throw std::runtime_error("Routing publication is busy; retry Redo");};
  auto e = routing||parameters||scratch?_document->redo(publish):_document->redo();
  if (_audio->renderer()) _audio->renderer()->applyColumnMutes(_document->native(), _document->song());
  if (_audio->playing() && !_audio->renderer()->enqueue(e))
    _audio->stop();
}
- (BOOL)historyUndo:(BOOL)redo error:(NSError **)error {
  try {
    [self synchronizeHistory];
    _parameterGestureSequence = 0;
    auto head = [&]() -> uint64_t { const auto &history=redo?_effectRedo:_effectUndo; return [self nextHistoryIsPlugin:redo] ? history.back().sequence : _document->historyHead(redo); };
    const auto first=head();if(!first)return YES;
    auto range=std::pair{first,first};
    for(const auto &group:_historyGroups)if(first>=group.first&&first<=group.second){range=group;break;}
    auto &effectSource=redo?_effectRedo:_effectUndo;
    if(_audio->active() && range.first!=range.second && !effectSource.empty() &&
       effectSource.back().sequence==range.first && _document->historyHead(redo)==range.second) {
      if((redo?_document->redoChangesStructure():_document->undoChangesStructure()))
        throw std::runtime_error("This grouped edit changes source adapters; stop playback before restoring it");
      const auto &saved=effectSource.back();const auto &native=_document->historyNative(redo);
      if(native.preciseNotes!=_document->native().preciseNotes)
        throw std::runtime_error("This grouped edit changes note timing; stop playback before restoring it");
      auto prepared=[self prepareLivePlugins:saved.plugins automation:saved.automation native:native];
      auto nextPlugins=saved.plugins;auto nextAutomation=saved.automation;auto nextManual=saved.manual;
      auto &destination=redo?_effectUndo:_effectRedo;
      EffectSnapshot current{_plugins,_automation,_manualParameters,saved.sequence,saved.bypassTarget};
      destination.reserve(destination.size()+1);
      // Document::undo/redo stages every allocation before modifying history.
      // This group is native-only, so there are no engine cell edits to enqueue.
      auto publish=[&]{if(!_audio->publishLiveRack(prepared))throw std::runtime_error("Prepared rack publication was rejected; history and playback were preserved");};
      if(redo)_document->redo(publish);else _document->undo(publish);
      _plugins.swap(nextPlugins);_automation.swap(nextAutomation);_manualParameters.swap(nextManual);++_pluginRevision;
      destination.push_back(std::move(current));effectSource.pop_back();trimEffectHistory(destination);
      return YES;
    }
    do {
      if ([self nextHistoryIsPlugin:redo]) { if(![self restoreEffectHistory:redo error:error]) return NO; }
      else {
        validatePluginCapacity(_plugins, _document->historyNative(redo).mixer.buses.size());
        if (redo) [self redoDocument]; else [self undoDocument];
      }
    } while(head()>=range.first && head()<=range.second);
    return YES;
  } catch (const std::exception &e) { failure(error, e); return NO; }
}
- (void)undo { [self historyUndo:NO error:nil]; }
- (void)redo { [self historyUndo:YES error:nil]; }
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
    const auto beforeRevision=_document->revision;
    auto changed = _document->edit(edits);
    if(_document->revision!=beforeRevision && _document->undoChangesAutomation()) _audio->stop();
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
    const Tracker::SampleSettingsFields fields{v[@"rate"] != nil, v[@"volume"] != nil, v[@"pan"] != nil,
        v[@"loopStart"] != nil || v[@"loopEnd"] != nil || v[@"loop"] != nil || v[@"pingpong"] != nil};
    const auto name = [v[@"name"] isKindOfClass:NSString.class] ? std::optional<std::string>([v[@"name"] UTF8String]) : std::nullopt;
    NSMutableDictionary *merged = [[self sampleInfo:sample includeWaveform:NO] mutableCopy];
    [merged addEntriesFromDictionary:v];
    v = merged;
    _audio->stop();
    _document->sampleSettings(
        int(sample), [v[@"rate"] intValue], [v[@"volume"] intValue], [v[@"pan"] intValue],
        [v[@"loopStart"] unsignedIntValue], [v[@"loopEnd"] unsignedIntValue], [v[@"loop"] boolValue],
        [v[@"pingpong"] boolValue],
        name, fields);
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
      if (v[@"carry"])
        envelope.dwFlags.set(ENV_CARRY, [v[@"carry"] boolValue]);
      if (v[@"releaseNode"]) {
        const int release = [v[@"releaseNode"] intValue];
        if (release != 255 && (release < 0 || size_t(release) >= envelope.size()))
          throw std::runtime_error("Release node must exist, or use 255 for none");
        envelope.nReleaseNode = uint8_t(release);
      }
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
    _audio->play(preview.snapshotData(),0,false,{},0,nullptr,{},true); _isolatedSamplePreview=YES; _isolatedPreviewSample=sample;
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
// Live topology edits preserve authoritative vendor instances. Parameter/preset
// replacement is a different operation and must not silently discard editor state.
- (std::unique_ptr<AudioDevice::LiveRackPlan>)prepareLivePlugins:(const std::vector<PluginState> &)states
              automation:(const std::vector<ParameterChange> &)automation native:(const NativeSong &)native {
  std::vector<std::string> presets;
  for(const auto &state:states)if(const auto old=std::find_if(_plugins.begin(),_plugins.end(),[&](const auto &p){return p.instanceID==state.instanceID;});old!=_plugins.end()&&old->state!=state.state)presets.push_back(state.instanceID);
  // Recorded automation is attached to stable processors. A topology edit may
  // remap rack indexes or remove targets, but cannot replace a retained timeline.
  for(const auto &state:states) {
    const auto old=std::find_if(_plugins.begin(),_plugins.end(),[&](const auto &p){return p.instanceID==state.instanceID;});
    if(old==_plugins.end())continue;
    const auto oldSlot=size_t(old-_plugins.begin()),newSlot=size_t(&state-states.data());
    std::vector<ParameterChange> a,b;
    for(auto p:_automation)if(p.slot==oldSlot){p.slot=0;a.push_back(p);}
    for(auto p:automation)if(p.slot==newSlot){p.slot=0;b.push_back(p);}
    if(a!=b)throw std::runtime_error("Replacing recorded automation requires a stopped transport; playback was preserved");
  }
  return _audio->prepareLiveRack(states,automation,native,presets);
}
// Editing an unresolved project must still allow removing missing effects one by one.
// Keep its graph inactive and all remaining opaque states intact until it can instantiate.
- (void)restorePluginGraph:(const std::vector<PluginState> &)states
                automation:(const std::vector<ParameterChange> &)automation {
  [self restorePluginGraph:states automation:automation native:_document->native()];
}
- (void)restorePluginGraph:(const std::vector<PluginState> &)states
                automation:(const std::vector<ParameterChange> &)automation native:(const NativeSong &)native {
  validatePluginCapacity(states, _document->native().mixer.buses.size());
  auto nextPlugins = states;
  auto nextAutomation = automation;
  std::string issue;
  try {
    if (const auto location = [self rackLocationIssue:states]; !location.empty())
      throw std::runtime_error(location);
    if(_audio->active()) {
      if(states==_plugins&&native==_document->native()) {
        auto prepared=_audio->prepareRecordedAutomation(automation);
        if(!_audio->publishRecordedAutomation(std::move(prepared)))throw std::runtime_error("Recorded automation publication is busy; playback was preserved");
      } else {
        auto prepared=[self prepareLivePlugins:states automation:automation native:native];
        if(!_audio->publishLiveRack(prepared))throw std::runtime_error("Prepared rack publication was rejected; playback was preserved");
      }
    } else _audio->setPlugins(states, automation);
  } catch (const std::exception &e) {
    if (_pluginError.empty() || _audio->active())
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
  [self applyPluginGraph:states automation:automation unassignedNoteSource:0];
}
- (void)applyPluginGraph:(const std::vector<PluginState> &)states
              automation:(const std::vector<ParameterChange> &)automation unassignedNoteSource:(uint64_t)instrument {
  auto next=_document->native();reconcilePluginNoteSources(next,_plugins,states,instrument);
  if(next==_document->native()){[self applyPluginGraph:states automation:automation native:next];return;}
  next.validate(_document->song());_historyGroups.reserve(_historyGroups.size()+1);const auto first=_document->historySequence()+1;
  _document->annotate([&](NativeSong &native){native=next;},[&]{[self applyPluginGraph:states automation:automation native:next];});
  _historyGroups.emplace_back(first,_document->historySequence());if(_historyGroups.size()>512)_historyGroups.erase(_historyGroups.begin());_knownHistorySequence=_document->historySequence();
}
- (void)applyPluginGraph:(const std::vector<PluginState> &)states
              automation:(const std::vector<ParameterChange> &)automation native:(const NativeSong &)native {
  [self synchronizeHistory];
  EffectSnapshot before{_plugins, _automation, _manualParameters};
  _effectUndo.reserve(_effectUndo.size() + 1);
  [self restorePluginGraph:states automation:automation native:native];
  before.sequence = _document->externalHistoryEdit();
  _knownHistorySequence = before.sequence; _parameterGestureSequence = 0;
  _effectUndo.push_back(std::move(before));
  _effectRedo.clear();
  trimEffectHistory(_effectUndo);
}
// A custom interface edit has already changed the host catalogue when harvested.
// Its previous manual value is the queued override or the saved opaque baseline.
- (float)manualValueForHistory:(uint32_t)slot identifier:(uint32_t)identifier alreadyApplied:(BOOL)alreadyApplied {
  for(const auto &point:_manualParameters)if(point.slot==slot&&point.id==identifier)return point.value;
  if(!alreadyApplied)for(const auto &parameter:_audio->pluginParameters(slot))
    if(parameter.id==identifier&&parameter.manualValue)return float(*parameter.manualValue);
  if(slot>=_plugins.size())throw std::runtime_error("Historical parameter processor is unavailable");
  NativePlugin baseline(_plugins[slot],_audio->sampleRate());
  for(const auto &parameter:baseline.parameters())if(parameter.id==identifier)return parameter.value;
  throw std::runtime_error("Historical manual parameter value is unavailable");
}
- (BOOL)restoreEffectHistory:(BOOL)redo error:(NSError **)error {
  try {
    auto &source = redo ? _effectRedo : _effectUndo;
    auto &destination = redo ? _effectUndo : _effectRedo;
    if (source.empty())
      return YES;
    EffectSnapshot current{_plugins, _automation, _manualParameters, source.back().sequence, source.back().bypassTarget};
    destination.reserve(destination.size() + 1);
    const auto &saved=source.back();
    if(_pluginError.empty()&&saved.plugins==_plugins&&saved.manual==_manualParameters&&saved.automation!=_automation) {
      [self restorePluginGraph:saved.plugins automation:saved.automation];
      destination.push_back(std::move(current));source.pop_back();trimEffectHistory(destination);return YES;
    }
    if(_pluginError.empty()&&!saved.bypassTarget.empty()) {
      const auto old=std::find_if(saved.plugins.begin(),saved.plugins.end(),[&](const auto &p){return p.instanceID==saved.bypassTarget;});
      const auto target=std::find_if(_plugins.begin(),_plugins.end(),[&](const auto &p){return p.instanceID==saved.bypassTarget;});
      if(old==saved.plugins.end()||target==_plugins.end()||!_audio->pluginBypass(size_t(target-_plugins.begin()),old->bypass))
        throw std::runtime_error("Prepared plugin bypass target is unavailable");
      target->bypass=old->bypass;++_pluginRevision;
      destination.push_back(std::move(current));source.pop_back();trimEffectHistory(destination);return YES;
    }
    if(_pluginError.empty()&&!saved.parameterValues.empty()) {
      // Numeric history must not replace a playing processor or lose its LFO,
      // tail, recorded timeline, custom interface, or autosave-folded baseline.
      if(saved.automation!=_automation)throw std::runtime_error("Recorded automation changed; retry parameter history after restoring its timeline");
      std::vector<ParameterChange> changes;auto nextManual=_manualParameters;
      changes.reserve(saved.parameterValues.size());current.parameterValues.reserve(saved.parameterValues.size());
      nextManual.reserve(nextManual.size()+saved.parameterValues.size());
      for(const auto &point:saved.parameterValues) {
        if(point.slot>=saved.plugins.size())throw std::runtime_error("Historical parameter processor is unavailable");
        const auto &old=saved.plugins[point.slot];const auto target=std::find_if(_plugins.begin(),_plugins.end(),[&](const auto &p){return p.instanceID==old.instanceID;});
        if(target==_plugins.end()||target->descriptor!=old.descriptor||target->audioLayout!=old.audioLayout||target->bypass!=old.bypass||target->instrument!=old.instrument||target->midiChannel!=old.midiChannel||target->aliases!=old.aliases||target->auxiliaryInputs!=old.auxiliaryInputs||target->auxiliaryOutputs!=old.auxiliaryOutputs)
          throw std::runtime_error("Historical parameter processor changed; restore its routing before its values");
        const auto slot=uint32_t(target-_plugins.begin());
        const auto value=[self manualValueForHistory:slot identifier:point.id alreadyApplied:NO];
        current.parameterValues.push_back({slot,point.id,value,0});changes.push_back({slot,point.id,point.value,0});
        const auto previous=std::find_if(nextManual.begin(),nextManual.end(),[&](const auto &p){return p.slot==slot&&p.id==point.id;});
        if(previous==nextManual.end())nextManual.push_back(changes.back());else previous->value=point.value;
      }
      // All allocation and history staging precedes the all-or-nothing queue.
      if(!_audio->pluginParameterBatch(changes))throw std::runtime_error("Plugin parameter queue is busy; retry history (song and playback preserved)");
      _manualParameters.swap(nextManual);++_pluginRevision;
      destination.push_back(std::move(current));source.pop_back();trimEffectHistory(destination);return YES;
    }
    // Missing effects remain opaque and repairable when restoring history too.
    const auto previousError = _pluginError;
    _pluginError = "Restoring effect history";
    try {
      auto states = source.back().plugins;
      for (size_t slot = 0; slot < states.size(); ++slot) {
        if (std::none_of(source.back().manual.begin(), source.back().manual.end(), [&](const auto &p) { return p.slot == slot; })) continue;
        NativePlugin plugin(states[slot], _audio->sampleRate());
        for (const auto &point : source.back().manual) if (point.slot == slot && !plugin.parameter(point.id, point.value))
          throw std::runtime_error("Plugin rejected a restored parameter value");
        auto restored = plugin.state();
        restored.bypass = states[slot].bypass;
        restored.instrument = states[slot].instrument;
        states[slot] = std::move(restored);
      }
      [self restorePluginGraph:states automation:source.back().automation];
      _manualParameters.clear();
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
  return [self historyUndo:NO error:error];
}
- (BOOL)redoEffectChange:(NSError **)error {
  return [self historyUndo:YES error:error];
}
- (BOOL)addPlugin:(NSDictionary *)item error:(NSError **)error {
  return [self addPlugin:item target:nil error:error];
}
- (BOOL)addPlugin:(NSDictionary *)item target:(NSString *)target error:(NSError **)error {
  return [self addPlugin:item target:target before:nil position:nil error:error];
}
- (BOOL)addPlugin:(NSDictionary *)item target:(NSString *)target before:(NSString *)before position:(NSDictionary *)position error:(NSError **)error {
  return [self addPlugin:item target:target before:before position:position parent:nil error:error];
}
- (BOOL)addPlugin:(NSDictionary *)item target:(NSString *)target before:(NSString *)before position:(NSDictionary *)position parent:(NSString *)parent error:(NSError **)error {
  return [self addPlugin:item target:target before:before position:position parent:parent detached:NO error:error];
}
- (BOOL)addPlugin:(NSDictionary *)item target:(NSString *)target before:(NSString *)before position:(NSDictionary *)position parent:(NSString *)parent detached:(BOOL)detached error:(NSError **)error {
  NSString *chosen = nil;
  bool newlyTrusted = false;
  try {
    if(detached&&(target||before||parent))throw std::invalid_argument("An unconnected effect cannot have an insertion destination");
    if((before || parent || (position&&!detached)) && !target) throw std::invalid_argument("Insertion needs an effect destination");
    [self commitManualParameters];
    auto d = descriptor(item);
    if (d.format == "VST3")
    {
      // The user or an agent chose this bundle explicitly; the isolated scanner
      // has validated it, so this process may load it.
      runScanner(@[ @"--validate-vst3", @(d.path.c_str()), @(d.classID.c_str()), d.instrument ? @"1" : @"0" ]);
      chosen = PluginTrust::canonical(pathString(d.path));
      newlyTrusted = chosen && !PluginTrust::trusted(chosen, nil);
      if (!chosen || !PluginTrust::trust(chosen))
        throw std::runtime_error("The VST3 bundle no longer exists");
    }
    else if (d.format == "AU")
      runScanner(
          @[ @"--validate", [@(d.type) stringValue], [@(d.subtype) stringValue], [@(d.manufacturer) stringValue] ]);
    auto states = (!_audio->active() && _pluginError.empty() && (_automation.empty() && (_document->native().automation.empty() && _document->native().performance.commands.empty() && !_audio->hasAutomatedState()))) ? _audio->pluginStates() : _plugins;
    states.push_back({d, {}, false, 0, NSUUID.UUID.UUIDString.UTF8String});
    {NativePlugin probe(states.back(),_audio->sampleRate(),false);states.back().audioLayout=pluginAudioLayoutSignature(probe.buses());}
    const bool effect=!d.instrument&&d.type!=kAudioUnitType_MusicDevice;
    if (target || detached || (effect&&!_document->native().signal.groups.empty())) {
      if(!effect) throw std::invalid_argument("Instrument plugins use instrument assignments");
      auto next=_document->native();
      if(detached)next.mixer.detached.push_back(states.back().instanceID);
      else if(target) {
      next.ensureMixer();
      const auto id=decodeNativeID(target);
      auto bus=std::find_if(next.mixer.buses.begin(),next.mixer.buses.end(),[&](const auto &b){return b.id==id;});
      if(bus==next.mixer.buses.end())throw std::invalid_argument("Effect destination no longer exists");
      auto point=before ? std::find(bus->inserts.begin(),bus->inserts.end(),std::string(before.UTF8String)) : bus->inserts.end();
      if(before && point==bus->inserts.end()) throw std::invalid_argument("Insertion point is not on the destination bus");
      bus->inserts.insert(point,states.back().instanceID);
      }
      if(parent) {
        const auto groupID=decodeNativeID(parent);
        auto group=std::find_if(next.signal.groups.begin(),next.signal.groups.end(),[&](const auto &g){return g.id==groupID;});
        if(group==next.signal.groups.end())throw std::invalid_argument("Song processing group no longer exists");
        group->nodes.push_back("plugin:"+states.back().instanceID);
      }
      if(position) {
        const double x=[position[@"x"] doubleValue],y=[position[@"y"] doubleValue];
        if(!std::isfinite(x)||!std::isfinite(y)||x<0||y<0||x>100000||y>100000)throw std::invalid_argument("Invalid graph position");
        next.signal.layout["plugin:"+states.back().instanceID]={x,y};
      }
      if(!next.signal.groups.empty()) {
        auto previous=_document->native(),after=next;previous.ensureMixer();after.ensureMixer();
        std::vector<std::string> priorRack,nextRack;
        for(size_t i=0;i<states.size();++i)if(!states[i].descriptor.instrument&&states[i].descriptor.type!=kAudioUnitType_MusicDevice){nextRack.push_back(states[i].instanceID);if(i+1<states.size())priorRack.push_back(states[i].instanceID);}
        preserveSongGroupInsertion(next.signal,previous.signal,previous.mixer,after.mixer,priorRack,nextRack,states.back().instanceID);
      }
      next.validate(_document->song());validatePluginCapacity(states,next.mixer.buses.size());
      if(next==_document->native()) { [self applyPluginGraph:states automation:_automation native:next]; }
      else {
      _historyGroups.reserve(_historyGroups.size()+1);
      const auto first=_document->historySequence()+1;
      // Document::annotate rolls back its prepared snapshot if hosting fails.
      _document->annotate([&](NativeSong &native){native=next;},[&]{[self applyPluginGraph:states automation:_automation native:next];});
      _historyGroups.emplace_back(first,_document->historySequence());
      if(_historyGroups.size()>512)_historyGroups.erase(_historyGroups.begin());
      _knownHistorySequence=_document->historySequence();
      }
    } else [self applyPluginGraph:states automation:_automation];
    // Only a plugin that was really added is remembered for later launches.
    if (chosen)
      PluginTrust::persist(chosen);
    return YES;
  } catch (const std::exception &e) {
    if (newlyTrusted)
      PluginTrust::revoke(chosen);
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
    auto states = (!_audio->active() && _pluginError.empty() && (_automation.empty() && (_document->native().automation.empty() && _document->native().performance.commands.empty() && !_audio->hasAutomatedState()))) ? _audio->pluginStates() : _plugins;
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
  if(slot<0 || slot>=_plugins.size()) {failure(error,std::runtime_error("Select an effect"));return NO;}
  return [self removePlugins:@[@(_plugins[slot].instanceID.c_str())] error:error];
}
- (BOOL)removePlugins:(NSArray<NSString *> *)identifiers error:(NSError **)error {
  return [self removePlugins:identifiers sources:std::vector<uint64_t>{} dryRun:NO error:error];
}
- (BOOL)removePlugins:(NSArray<NSString *> *)identifiers sources:(const std::vector<uint64_t> &)sources dryRun:(BOOL)dryRun error:(NSError **)error {
  try {
    std::set<std::string> removed;
    for(NSString *raw in identifiers) {
      const auto id=std::string(raw.UTF8String);
      if(!removed.insert(id).second || std::none_of(_plugins.begin(),_plugins.end(),[&](const auto &p){return p.instanceID==id;}))
        throw std::invalid_argument("Select distinct existing plugins");
    }
    if(removed.empty())throw std::invalid_argument("Select at least one plugin");
    auto next=_document->native();next.removeSongSources(sources);
    for(const auto &id:removed)next.removePluginRoutes(id);
    next.validate(_document->song());
    if(dryRun)return YES;
    [self commitManualParameters];
    auto states = (!_audio->active() && _pluginError.empty() && (_automation.empty() && (_document->native().automation.empty() && _document->native().performance.commands.empty() && !_audio->hasAutomatedState()))) ? _audio->pluginStates() : _plugins;
    std::vector<size_t> slots(states.size(),SIZE_MAX);size_t index=0;
    for(size_t i=0;i<states.size();++i)if(!removed.contains(states[i].instanceID))slots[i]=index++;
    std::erase_if(states,[&](const auto &p){return removed.contains(p.instanceID);});
    reconcilePluginNoteSources(next,_plugins,states);
    auto automation = _automation;
    std::erase_if(automation,[&](const auto &point){return point.slot>=slots.size() || slots[point.slot]==SIZE_MAX;});
    for(auto &point:automation)point.slot=uint32_t(slots[point.slot]);
    if(next!=_document->native()) {
      next.validate(_document->song());
      _historyGroups.reserve(_historyGroups.size()+1);
      const auto first=_document->historySequence()+1;
      _document->annotate([&](NativeSong &native){native=next;},[&]{[self applyPluginGraph:states automation:automation native:next];});
      _historyGroups.emplace_back(first,_document->historySequence());
      if(_historyGroups.size()>512)_historyGroups.erase(_historyGroups.begin());
      _knownHistorySequence=_document->historySequence();
    } else [self applyPluginGraph:states automation:automation];
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
    if (target == slot) return YES;
    auto states = (!_audio->active() && _pluginError.empty() && (_automation.empty() && (_document->native().automation.empty() && _document->native().performance.commands.empty() && !_audio->hasAutomatedState()))) ? _audio->pluginStates() : _plugins;
    auto moved = std::move(states[slot]);
    states.erase(states.begin() + slot);
    states.insert(states.begin() + target, std::move(moved));
    auto automation = _automation;
    for (auto &point : automation) {
      if (point.slot == slot)
        point.slot = uint32_t(target);
      else if (target > slot && point.slot > slot && point.slot <= target) --point.slot;
      else if (target < slot && point.slot >= target && point.slot < slot) ++point.slot;
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
    if (slot < 0 || slot >= _plugins.size())
      throw std::runtime_error("Select an effect");
    if(_plugins[slot].bypass==bool(bypass))return YES;
    if(!_pluginError.empty()) {auto states=_plugins;states[slot].bypass=bypass;[self applyPluginGraph:states automation:_automation];return YES;}
    [self synchronizeHistory];EffectSnapshot before{_plugins,_automation,_manualParameters};before.bypassTarget=_plugins[slot].instanceID;_effectUndo.reserve(_effectUndo.size()+1);
    if(!_audio->pluginBypass(size_t(slot),bypass))throw std::runtime_error("Prepared plugin bypass target is unavailable");
    _plugins[slot].bypass=bypass;++_pluginRevision;
    before.sequence=_document->externalHistoryEdit();_knownHistorySequence=before.sequence;_parameterGestureSequence=0;
    _effectUndo.push_back(std::move(before));_effectRedo.clear();trimEffectHistory(_effectUndo);
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
        @"manualValue" : @(p.manualValue.value_or(p.value)),
        @"effectiveValue" : @(p.value), @"valueRole": @"effective",
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
    [self synchronizeHistory];
    const auto now = std::chrono::steady_clock::now();
    const bool continued = _parameterGestureSequence == _document->historySequence() && _parameterGestureSequence &&
      _parameterGestureSlot == slot && _parameterGestureID == identifier &&
      (_parameterGesture || (alreadyApplied && now - _lastParameterEdit < std::chrono::milliseconds(400)));
    std::optional<EffectSnapshot> before;
    if (!continued) {
      before.emplace(EffectSnapshot{_plugins, _automation, _manualParameters});
      if(!recording)before->parameterValues.push_back({uint32_t(slot),uint32_t(identifier),[self manualValueForHistory:uint32_t(slot) identifier:uint32_t(identifier) alreadyApplied:alreadyApplied],0});
      _effectUndo.reserve(_effectUndo.size() + 1);
    }
    if (recording) _automation.reserve(_automation.size() + 1);
    else _manualParameters.reserve(_manualParameters.size() + 1);
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
    if (before) {
      before->sequence = _document->externalHistoryEdit();
      _knownHistorySequence = _parameterGestureSequence = before->sequence;
      _effectUndo.push_back(std::move(*before)); _effectRedo.clear(); trimEffectHistory(_effectUndo);
    }
    _parameterGestureSlot = slot; _parameterGestureID = identifier; _lastParameterEdit = now;
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
- (BOOL)pluginLatencyChanged { return _audio->pluginLatencyChanged(); }
- (BOOL)refreshPluginLatencies:(NSError **)error {
  try { _audio->refreshPluginLatencies(_document->native()); return YES; }
  catch (const std::exception &e) { failure(error, e); return NO; }
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
                   _document->song().Order.GetCurrentSequenceIndex(), &_document->native());
    return YES;
  } catch (const std::exception &e) {
    failure(error, e);
    return NO;
  }
}
- (BOOL)note:(NSInteger)note instrument:(NSInteger)instrument velocity:(NSInteger)velocity on:(BOOL)on {
  return [self note:note instrument:instrument velocity:velocity on:on channel:-1];
}
- (BOOL)note:(NSInteger)note instrument:(NSInteger)instrument velocity:(NSInteger)velocity on:(BOOL)on channel:(NSInteger)channel {
  if(channel < -1 || channel >= _document->song().GetNumChannels()) return NO;
  if (!_audio->active() || _isolatedSamplePreview)
    return NO;
  return _audio->renderer()->preview({uint8_t(std::clamp(note, NSInteger(1), NSInteger(120))),
                                      uint16_t(std::clamp(instrument, NSInteger(0), NSInteger(UINT16_MAX))),
                                      uint8_t(std::clamp(velocity, NSInteger(0), NSInteger(127))), bool(on), 0, uint16_t(channel)});
}
- (BOOL)sampleNote:(NSInteger)note sample:(NSInteger)sample velocity:(NSInteger)velocity on:(BOOL)on {
  return [self sampleNote:note sample:sample velocity:velocity on:on channel:-1];
}
- (BOOL)sampleNote:(NSInteger)note sample:(NSInteger)sample velocity:(NSInteger)velocity on:(BOOL)on channel:(NSInteger)channel {
  if(channel < -1 || channel >= _document->song().GetNumChannels()) return NO;
  if (!_audio->active() || _isolatedSamplePreview || sample < 1 || sample > _document->song().GetNumSamples()) return NO;
  return _audio->renderer()->preview({uint8_t(std::clamp(note, NSInteger(1), NSInteger(120))), 0,
    uint8_t(std::clamp(velocity, NSInteger(0), NSInteger(127))), bool(on), uint16_t(sample), uint16_t(channel)});
}
- (void)panic {
  if (_audio->renderer())
    _audio->renderer()->panic();
}
- (NSData *)serializedData {
  try {
    return [self projectData];
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
      {
        native = decodeNativeSong(root[@"native"]);
        for(const auto &r:native->signal.stageConnections)for(const auto &e:{r.source,r.target})if(!e.plugin.empty()&&std::none_of(states.begin(),states.end(),[&](const auto &p){return p.instanceID==e.plugin;}))throw std::invalid_argument("Graph stage cable refers to a missing rack plugin");
        Document validation(byteVector(module));
        native->validate(validation.song());
      }
      PluginPathHints hints;
      if (const auto missing = resolvePluginLocations(states, &*native, PluginInventory().cached(), hints); !missing.empty())
        throw std::runtime_error(missing);
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
