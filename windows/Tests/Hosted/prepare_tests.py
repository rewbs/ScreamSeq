"""Generate private functional adaptations, keeping Mac assertions source-faithful.

Only fixture binding, Apple test harness/API/export sections, Darwin audit calls
and unsafe local destruction order are changed. No production algorithms copied.
Generated sources and a provenance manifest stay in the build directory.
"""
import hashlib
import json
import re
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[3]
out = Path(sys.argv[1]).resolve()
out.mkdir(parents=True, exist_ok=True)
manifest = {}


def source(name):
    path = root / "mac/Tests" / name
    text = path.read_text(encoding="utf-8")
    manifest[name] = {"sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
    return text


def between(text, begin, end):
    return text[text.index(begin):text.index(end)]


def functional(text):
    text = text.replace('#include "../Audio/AudioUnitHost.hpp"', '#include "editor/hosted/HostedAudio.hpp"')
    text = text.replace('#include "../Audio/NativeSignalGraph.hpp"', '#include "mac/Audio/NativeSignalGraph.hpp"')
    text = text.replace('#include "GraphRealtimeAudit.hpp"', '')
    text = text.replace('@autoreleasepool{', '{').replace('@autoreleasepool {', '{')
    # Delete interposer scaffolding rather than fabricating zero audit results.
    text = re.sub(r'#ifdef TRACKER_SANITIZER.*?#endif\n', '', text, flags=re.S)
    text = re.sub(r'uint64_t a\s*,\s*f\s*,\s*l\s*;', '', text)
    text = re.sub(r'tracker_audit_begin\(\);', '', text)
    text = re.sub(r'tracker_audit_end\(&a,\s*&f,\s*&l\);', '', text)
    text = re.sub(r'check\(a\s*\+\s*f\s*\+\s*l\s*==\s*0,\s*"[^"]*"\);', '', text)
    text = re.sub(r'&&\s*a\s*\+\s*f\s*\+\s*l\s*==\s*0', '', text)
    text = re.sub(r'a\s*\+\s*f\s*\+\s*l\s*==\s*0\s*&&', '', text)
    text = text.replace('VST3', 'hosted fixture').replace('VST ramps', 'queued ramps')
    text = re.sub(r'realtime safety|zero realtime allocations/frees/locks|realtime audit|realtime safe|realtime-safe',
                  'functional-only (no realtime audit)', text)
    assert 'tracker_audit_' not in text
    return '// Generated from mac/Tests; see provenance.json. NO realtime audit.\n' + text


def save(name, text):
    path = out / name
    path.write_text(text, encoding="utf-8")
    manifest[name] = {"sha256": hashlib.sha256(path.read_bytes()).hexdigest()}


routing = source('SignalRoutingTests.mm')
# Original fixture declared renderer before chain. In the shared host its
# adapters retain references to chain: fix only lifetime ordering in adaptation.
routing = routing.replace('Renderer renderer(doc.serialize(),rate);PluginChain chain({},rate,true);',
                          'PluginChain chain({},rate,true);Renderer renderer(doc.serialize(),rate);')
save('SignalRouting.cpp', functional(routing))

musical = source('MusicalAutomationTests.mm')
headers = between(musical, '#include "../Audio/AudioUnitHost.hpp"', 'static NSDictionary *descriptor')
body = between(musical, '      Document doc;', '      TrackerSession *session')
text = headers + '''int main() {{ try {
  const auto discovered=NativePlugin::discover();
  PluginState gain{discovered.at(0)}, synth{discovered.at(1)};
  gain.instanceID="gain"; synth.instanceID="synth"; synth.instrument=1;
''' + body + '''
  std::cout << "PASS shared musical automation: exact knots, queued ramps, repeats, seek, groove and stable identities; functional-only\\n";
  return 0;
} catch(const std::exception &e) {std::cerr << e.what() << '\\n'; return 1;} }}
'''
save('MusicalAutomation.cpp', functional(text))

precise = source('PreciseNoteTests.mm')
headers = between(precise, '#include "editor/TrackerDocument.hpp"', 'std::vector<PluginDescriptor> registerFixtureAUs();')
helpers = between(precise, 'static void check(', 'static void apiTest()')
helpers = helpers[:helpers.index('  NSString *wav=')] + helpers[helpers.index('  doc->annotate([](NativeSong &n){auto off='):]
body = between(precise, '  for(const auto &descriptor:', '  voiceIsolationTest();recordingTest();apiTest();')
body = body.replace('for(const auto &descriptor:{vst[1],au[1]})', 'for(const auto &descriptor:{fixtures[1]})')
body = body.replace('Renderer renderer(doc.snapshotData(),rate);PluginChain chain({plugin},rate,true);',
                    'PluginChain chain({plugin},rate,true);Renderer renderer(doc.snapshotData(),rate);')
text = '#include "editor/hosted/HostedAudio.hpp"\n' + headers + helpers + '''int main() {{ try {
  const auto fixtures=NativePlugin::discover();
''' + body + '''
  voiceIsolationTest();recordingTest();
  std::cout << "PASS shared precise samples and fixture notes: exact timing, voice isolation, repeats, groove, partitions and recording clock; functional-only\\n";
  return 0;
} catch(const std::exception &e) {std::cerr << e.what() << '\\n';return 1;} }}
'''
save('PreciseNotes.cpp', functional(text))

graph = source('NativeSignalGraphTests.mm')
graph = graph.replace('#include <dlfcn.h>', '#include "FixtureBackend.hpp"')
graph = graph.replace('check(argc==2,"Pass VST3 fixture");auto plugins=NativePlugin::discoverVST3(argv[1]);',
                      'auto plugins=NativePlugin::discover();')
a = graph.index('  void *bundle=dlopen(')
b = graph.index('  NativeSong timed;', a)
graph = graph[:a] + '''  auto delayed=fixtureEffectDelay; auto observe=fixtureObserve;
  auto observed=fixtureObservedFrames; auto clockErrors=fixtureClockErrors; delayed(true);
''' + graph[b:]
graph = graph.replace('  dlclose(bundle);', '')
save('NativeSignalGraph.cpp', functional(graph))
manifest['scope'] = {'audit': False, 'realVST3': False, 'AU': False, 'hardware': False,
                     'notes': 'Source-faithful functional assertions; fixture binding replaces vendor loader; Objective-C session/API/export omitted; renderer destroys before chain.'}
(out / 'provenance.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
print('Generated four source-faithful hosted functional regressions.')
