"""Read-only pack qualification through a private, silent instance of the built app."""
import json
import math
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[3]
BUILD = ROOT / 'bin/mac-background'
sys.path.insert(0, str(ROOT / 'mac/Tools'))
from resonance_api import Client, endpoints

with tempfile.TemporaryDirectory(prefix='resonance-real-library-') as directory:
    root = Path(directory)
    with (root / 'app.log').open('w+') as log:
        process = subprocess.Popen([str(BUILD / 'Resonance.app/Contents/MacOS/Resonance'), '--automation-test'],
            stdout=log, stderr=log, env={**os.environ, 'RESONANCE_AUTOMATION_TEST_DIRECTORY': directory})
        try:
            deadline = time.monotonic() + 20
            while time.monotonic() < deadline:
                assert process.poll() is None
                found = [item for item in endpoints(root) if item['pid'] == process.pid]
                if found:
                    client = Client(found[0]['socket'])
                    break
                time.sleep(.05)
            else:
                raise AssertionError('No private endpoint')

            def ready():
                deadline = time.monotonic() + 30
                while time.monotonic() < deadline:
                    state = client.call('sample.library.get')['data']
                    if state['ready'] and not state['indexing']:
                        assert state['error'] is None, state
                        return state
                    time.sleep(.05)
                raise AssertionError('Library not ready')

            initial = client.call('document.get')
            started = time.monotonic()
            client.call('sample.library.roots.set', {'roots': [str(Path.home() / 'samples')],
                'expectedLibraryRevision': ready()['libraryRevision']})
            state = ready()
            evidence = {'count': state['count'], 'scanAndCacheSeconds': time.monotonic() - started,
                        'warnings': state['warnings'], 'searches': [], 'previews': []}
            for query in ['808 "bass drum"', 'Junos chords', 'snare -maschine', '"From Mars" WAV']:
                started = time.monotonic()
                result = client.call('sample.library.search', {'query': query, 'limit': 5,
                    'expectedLibraryRevision': state['libraryRevision']})['data']
                evidence['searches'].append({'query': query, 'matches': result['total'],
                    'roundTripMilliseconds': (time.monotonic() - started) * 1000})
                assert result['items'], query
                for sample in result['items'][:2]:
                    started = time.monotonic()
                    preview = client.call('sample.library.preview', {'path': sample['path']})['data']
                    assert preview['audible'] is False and preview['frames'] > 0
                    assert len(preview['peaks']) == 512 and all(math.isfinite(value) for value in preview['peaks'])
                    evidence['previews'].append({key: preview[key] for key in
                        ['path', 'rate', 'channels', 'seconds', 'previewSeconds']})
                    evidence['previews'][-1]['decodeRoundTripMilliseconds'] = (time.monotonic() - started) * 1000
                    client.call('sample.library.preview.stop')
            assert client.call('document.get') == initial
            evidence['songUnchanged'] = True
            evidence['physicalAudio'] = False
            print(json.dumps(evidence, indent=2))
        finally:
            process.terminate()
            process.wait(timeout=10)
