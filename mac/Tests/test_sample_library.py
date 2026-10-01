#!/usr/bin/env python3
"""Run the real app's sample library and batch importer without windows/audio devices."""
import math
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import time
import wave

ROOT = Path(__file__).resolve().parents[2]
BUILD = Path(os.environ.get('RESONANCE_BUILD_DIR', ROOT / 'bin/mac-background')).resolve()
sys.path.insert(0, str(ROOT / 'mac/Tools'))
from resonance_api import Client, APIError, endpoints


def reject(code, action):
    try:
        action()
    except APIError as error:
        assert error.code == code, (error.code, str(error))
    else:
        raise AssertionError(f'Expected error {code}')


def wav(path, channels=1, rate=24000):
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), 'wb') as output:
        output.setparams((channels, 2, rate, 2400, 'NONE', ''))
        output.writeframes(b''.join(struct.pack('<h', int(math.sin(frame * .08 + channel) * 16000))
                                   for frame in range(2400) for channel in range(channels)))


with tempfile.TemporaryDirectory(prefix='resonance-library-') as tmp:
    root = Path(tmp).resolve()
    pack = root / 'Packs'
    kick = pack / '808 From Mars/WAV/Bass Drum/BD Smooth.wav'
    snare = pack / '808 From Mars/WAV/Snares/SD Snappy.wav'
    chord = pack / 'Junos From Mars/WAV/Chords/Chord C4.wav'
    for path in [kick, chord, pack / '__MACOSX/._hidden.wav', pack / '.hidden/hidden.wav']:
        wav(path)
    wav(snare, 2, 48000)
    broken = root / 'broken.wav'
    broken.write_text('not sample audio')
    log = open(root / 'app.log', 'w+')
    process = None

    def stop():
        global process
        if process is not None and process.poll() is None:
            process.terminate()
            process.wait(timeout=10)
        process = None

    def launch(file=None):
        global process
        process = subprocess.Popen([str(BUILD / 'ScreamSeq.app/Contents/MacOS/ScreamSeq'), '--automation-test'] + ([str(file)] if file else []),
            stdout=log, stderr=log, env={**os.environ, 'RESONANCE_AUTOMATION_TEST_DIRECTORY': tmp})
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            assert process.poll() is None, 'Application exited before readiness'
            found = [entry for entry in endpoints(root) if entry['pid'] == process.pid]
            if found:
                return Client(found[0]['socket'], timeout=20)
            time.sleep(.05)
        raise AssertionError('No private API endpoint')

    def ready():
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            status = client.call('sample.library.get')['data']
            if status['ready'] and not status['indexing']:
                assert status['error'] is None, status
                return status
            time.sleep(.025)
        raise AssertionError('Index did not finish')

    def search(**params):
        return client.call('sample.library.search', params)['data']

    def library_write(method, **params):
        return client.call(method, {'expectedLibraryRevision': ready()['libraryRevision'], **params})

    def write(method, **params):
        return client.call(method, {'expectedRevision': client.call('document.get')['revision'], **params})

    try:
        client = launch()
        initial = client.call('document.get')
        assert ready()['count'] == 0, 'Isolated tests must not scan the user library'
        library_write('sample.library.roots.set', roots=[str(pack)])
        status = ready()
        assert status['count'] == 3
        assert search()['items'][0]['folders'] == ['Packs', '808 From Mars', 'WAV', 'Bass Drum'], 'Canonical aliases must not corrupt folder tags'
        assert search(query='808 "bass drum"')['items'][0]['path'] == str(kick)
        assert search(tags=['808 From Mars', 'WAV'])['total'] == 2
        assert search(query='-snares -chords')['total'] == 1
        assert search(root=str(pack), query='Junos chords')['total'] == 1
        page = search(limit=1, offset=1, expectedLibraryRevision=status['libraryRevision'])
        assert len(page['items']) == 1 and page['total'] == 3 and page['offset'] == 1
        reject(-32001, lambda: client.call('sample.library.search', {'expectedLibraryRevision': 'stale'}))
        for invalid in [{'limit': 0}, {'offset': True}, {'limit': 1e100}, {'tags': 'WAV'}, {'root': '/missing'}, {'typo': 1}]:
            reject(-32602, lambda: client.call('sample.library.search', invalid))
        reject(-32001, lambda: client.call('sample.library.roots.set', {'roots': [], 'expectedLibraryRevision': 'stale'}))
        reject(-32602, lambda: library_write('sample.library.roots.set', roots=[str(root / 'missing')]))
        detail = client.call('sample.library.inspect', {'path': str(snare)})['data']
        assert detail['channels'] == 2 and detail['rate'] == 48000 and detail['frames'] == 2400
        assert len(detail['peaks']) == 512 and any(abs(value) > .1 for value in detail['peaks']) and 'pcm' not in detail
        audition = client.call('sample.library.preview', {'path': str(kick), 'gainDB': -12})['data']
        assert audition['audible'] is False and audition['frames'] == 2400
        assert client.call('sample.library.preview.stop')['data']['playing'] is False
        reject(-32003, lambda: client.call('sample.library.inspect', {'path': str(broken)}))
        reject(-32602, lambda: client.call('sample.library.preview', {'path': str(kick), 'gainDB': True}))
        assert client.call('document.get') == initial, 'Browsing, inspection and audition must preserve song and history'

        params = {'paths': [str(kick), str(snare)], 'createInstruments': True, 'expectedRevision': initial['revision']}
        preview = client.call('sample.importMany', {**params, 'dryRun': True})
        assert not preview['changed'] and not preview['playbackStopped'] and preview['data']['count'] == 2
        assert client.call('document.get') == initial
        reject(-32003, lambda: write('sample.importMany', paths=[str(kick), str(broken)]))
        reject(-32602, lambda: write('sample.importMany', paths=[str(kick), str(kick)]))
        for invalid in [{'paths': []}, {'paths': ['relative.wav']}, {'paths': [str(kick)] * 129},
                        {'paths': [str(kick)], 'createInstruments': 1}, {'paths': [str(kick)], 'unknown': True}]:
            reject(-32602, lambda: write('sample.importMany', **invalid))
        assert client.call('document.get') == initial, 'Invalid batch cannot partially append'
        imported = client.call('sample.importMany', params, 'bulk-retry')
        assert imported['changed'] and imported['data']['count'] == 2
        assert client.call('sample.importMany', params, 'bulk-retry') == imported
        reject(-32001, lambda: client.call('sample.importMany', params))
        after = client.call('document.get')['data']
        assert all(item['sample'] > 0 and item['instrument'] > 0 for item in imported['data']['samples'])
        write('history.undo', domain='document')
        undone = client.call('document.get')['data']
        assert undone['canRedo']
        for key, value in initial['data'].items():
            if key not in ('canUndo', 'canRedo', 'revisionToken'):
                assert undone[key] == value, (key, value, undone[key])
        write('history.redo', domain='document')
        redone = client.call('document.get')['data']
        assert {key: value for key, value in redone.items() if key != 'revisionToken'} == {
            key: value for key, value in after.items() if key != 'revisionToken'}
        write('document.save', path=str(root / 'batch.resonance'))

        indexed_at = status['indexedAt']
        added = pack / '808 From Mars/WAV/Claps/Hand.wav'
        wav(added)
        stop()
        client = launch()
        assert ready()['indexedAt'] == indexed_at and ready()['count'] == 3, 'Restart must use the existing disk index'
        library_write('sample.library.rescan')
        assert ready()['count'] == 4 and search(query='claps')['total'] == 1
        snare.unlink()
        library_write('sample.library.rescan')
        assert ready()['count'] == 3 and search(query='snares')['total'] == 0
        library_write('sample.library.roots.set', roots=[])
        assert ready()['count'] == 0 and kick.exists() and added.exists(), 'Removing folders must never remove files'
        multi = root / 'Multisamples'
        files = [multi / name for name in ['072 Clav C4.wav', '074 Clav D4.wav', '076 Clav E4.wav']]
        for file in files: wav(file)
        library_write('sample.library.roots.set', roots=[str(multi)])
        library_revision = ready()['libraryRevision']
        assert search(query='C4')['total'] == 1
        group = client.call('sample.library.multisample.get', {'path': str(files[0]), 'expectedLibraryRevision': library_revision})['data']['group']
        assert group['count'] == 3 and group['suggestedOctaveShift'] == 2, 'Detection uses the entire family, not a filtered page'
        assert client.call('sample.library.multisample.get', {'path': str(kick)})['data']['group'] is None
        reject(-32001, lambda: client.call('sample.library.multisample.get', {'path': str(files[0]), 'expectedLibraryRevision': 'stale'}))
        sources = [{'path': item['path'], 'rootNote': item['semitone'] + 12 * group['suggestedOctaveShift'] + 1} for item in group['samples']]
        before = client.call('document.get')
        params = {'samples': sources, 'name': 'Clav family', 'expectedRevision': before['revision']}
        checked = client.call('instrument.importMultisample', {**params, 'dryRun': True})
        assert not checked['changed'] and not checked['playbackStopped'] and client.call('document.get') == before
        for bad in [[{**sources[0], 'rootNote': True}, sources[1]], [{**sources[0], 'rootNote': 121}, sources[1]],
                    [sources[0], {**sources[1], 'rootNote': sources[0]['rootNote']}]]:
            reject(-32602, lambda: client.call('instrument.importMultisample', {**params, 'samples': bad}))
        reject(-32003, lambda: client.call('instrument.importMultisample', {**params, 'samples': [sources[0], {**sources[1], 'path': str(broken)}]}))
        assert client.call('document.get') == before
        imported = client.call('instrument.importMultisample', params, 'multisample-retry')
        assert client.call('instrument.importMultisample', params, 'multisample-retry') == imported
        reject(-32001, lambda: client.call('instrument.importMultisample', params))
        data = imported['data']; instrument = data['instrument']
        assert data['count'] == 3 and data['zones'] == checked['data']['zones']
        mapping = client.call('instrument.get', {'instrument': instrument})['data']
        assert mapping['noteMapping'][72:77] == [61, 62, 61, 62, 61]
        assert mapping['mapping'][71] == 0 and mapping['mapping'][77] == 0
        assert mapping['mapping'][72] == mapping['mapping'][73] == data['zones'][0]['sample']
        write('history.undo', domain='document')
        undone = client.call('document.get')['data']
        for key, value in before['data'].items():
            if key not in ('canUndo', 'canRedo', 'revisionToken'): assert undone[key] == value, key
        write('history.redo', domain='document')
        assert client.call('instrument.get', {'instrument': instrument})['data'] == mapping
        project = root / 'multisample.resonance'
        write('document.save', path=str(project))
        stop(); client = launch(project)
        assert client.call('instrument.get', {'instrument': instrument})['data'] == mapping
        print('PASS actual app multisample: full-family detection, octave suggestion, explicit roots, dry run/retry/stale/types, atomic failures, nearest zones, note mapping, Undo/Redo and native relaunch')
        print('PASS actual app sample library: cached restart, folder search/tags/pagination, hidden files, rescan/removal, strict API revisions/types, waveform and silent preview, atomic import/dry run/retry/Undo/Redo and save')
    except Exception:
        log.flush(); log.seek(0); print(log.read(), file=sys.stderr)
        raise
    finally:
        stop(); log.close()
