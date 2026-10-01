"""Record real standalone hosted build/test artifacts; never synthesize results."""
import hashlib
import json
import sys
import xml.etree.ElementTree as ET
import zipfile
from pathlib import Path

root = Path(__file__).resolve().parents[3]
build = Path(sys.argv[1]).resolve()
editor = Path(sys.argv[2]).resolve()

def digest(path):
    with path.open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()

junit = ET.parse(build / 'hosted-tests.xml').getroot()
tests = [{'name': t.attrib['name'], 'status': t.attrib.get('status'),
          'failed': t.find('failure') is not None, 'seconds': t.attrib.get('time')}
         for t in junit.iter('testcase')]
assert len(tests) == 7 and all(not t['failed'] and t['status'] == 'run' for t in tests), tests
build_log = (build / 'final-build.log').read_text(encoding='utf-8')
assert 'error C' not in build_log and 'warning C' not in build_log
sources = list((root / 'editor/hosted').glob('*')) + list((root / 'windows/Tests/Hosted').glob('*'))
sources += [root / 'mac/CMakeLists.txt', root / 'LICENSE']
for name in ['AudioUnitHost.hpp', 'AudioUnitHost.mm', 'NativeInstrument.cpp', 'NativeSignalGraph.cpp',
             'NativeSignalGraph.hpp', 'PluginAssignments.cpp', 'PatternCommandRuntime.cpp',
             'PatternCommandRuntime.hpp', 'PatternPitchRuntime.cpp', 'PatternPitchRuntime.hpp', 'VST3Host.mm']:
    sources.append(root / 'mac/Audio' / name)
for name in ['SignalRoutingTests.mm', 'MusicalAutomationTests.mm', 'PreciseNoteTests.mm',
             'NativeSignalGraphTests.mm', 'FixtureVST3.mm']:
    sources.append(root / 'mac/Tests' / name)
sources = sorted(set(p for p in sources if p.is_file()))
source_hashes = {str(p.relative_to(root)).replace('\\', '/'): digest(p) for p in sources}
artifacts = list((build / 'Release').glob('hosted-*.exe')) + [build / 'hosted/Release/TrackerHosted.lib']
artifacts += [editor / 'Release' / (name + '.lib') for name in ['TrackerEditor', 'OpenMPTCore', 'TrackerFLAC']]
artifact_hashes = {str(p.relative_to(root)).replace('\\', '/'): digest(p) for p in artifacts}
with zipfile.ZipFile(build / 'hosted-source-evidence.zip', 'w', zipfile.ZIP_DEFLATED) as archive:
    for p in sources:
        archive.write(p, str(p.relative_to(root)))
    for p in (build / 'generated').glob('*'):
        if p.is_file():
            archive.write(p, 'generated/' + p.name)
result = {'target': 'TrackerHosted', 'configuration': 'Release ARM64',
          'tests': tests, 'passed': len(tests), 'sources': source_hashes, 'artifacts': artifact_hashes,
          'importedEngineLibrariesRebuilt': False,
          'qualification': {'offlinePCM': True, 'realVST3': False, 'AU': False,
                            'hardware': False, 'realtimeAudit': False, 'sanitizers': False, 'MacBuild': False},
          'logs': ['final-build.log', 'final-tests.log', 'hosted-tests.xml'],
          'sourceArchive': {'path': 'hosted-source-evidence.zip', 'sha256': digest(build / 'hosted-source-evidence.zip')}}
(build / 'HostedBuildInfo.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
print(json.dumps({'passed': len(tests), 'sources': len(source_hashes), 'artifacts': len(artifact_hashes),
                  'manifest': str(build / 'HostedBuildInfo.json')}, indent=2))
