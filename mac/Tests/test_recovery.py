#!/usr/bin/env python3
"""Exercise the shipped application's autosave timer and crash recovery in isolation."""
import json
import os
from pathlib import Path
import plistlib
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
BUILD = Path(os.environ.get('RESONANCE_BUILD_DIR', ROOT / 'bin/mac-background')).resolve()
sys.path.insert(0, str(ROOT / 'mac/Tools'))
from resonance_api import Client, APIError, endpoints


def error(code, action):
    try:
        action()
    except APIError as exc:
        assert exc.code == code, (exc.code, str(exc))
    else:
        raise AssertionError(f'Expected error {code}')


with tempfile.TemporaryDirectory(prefix='resonance-recovery-') as tmp:
    root = Path(tmp)
    process = None
    log = open(root / 'app.log', 'w+')

    def launch(timer=False, file=None):
        global process
        command = [str(BUILD / 'ScreamSeq.app/Contents/MacOS/ScreamSeq'), '--automation-test']
        if timer:
            command += ['--recovery-test']
        if file:
            command += [str(file)]
        process = subprocess.Popen(command, stdout=log, stderr=log,
            env={**os.environ, 'RESONANCE_AUTOMATION_TEST_DIRECTORY': tmp})
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            assert process.poll() is None, 'Application exited before readiness'
            found = [entry for entry in endpoints(root) if entry['pid'] == process.pid]
            if found:
                client = Client(found[0]['socket'])
                client.call('context.get')
                return client
            time.sleep(.05)
        raise AssertionError('No private API endpoint')

    def stop(crash=False):
        global process
        if process is not None and process.poll() is None:
            process.kill() if crash else process.terminate()
            process.wait(timeout=10)
        process = None

    try:
        client = launch(timer=True)
        def write(method, **params):
            return client.call(method, {'expectedRevision': client.call('document.get')['revision'], **params})
        status = client.call('recovery.status')['data']
        assert status['enabled'] and status['intervalSeconds'] == 10 and status['generations'] == 10
        assert not client.call('recovery.list')['data']['copies']
        write('document.patch', title='Crash recovery test')
        write('pattern.apply', cells=[{'pattern': 0, 'row': 3, 'channel': 0, 'note': 61, 'instrument': 2, 'volumeCommand': 1, 'volume': 64}])
        pattern = client.call('pattern.get', {'pattern': 0})['data']
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            copies = client.call('recovery.list')['data']['copies']
            if copies:
                break
            time.sleep(.1)
        assert len(copies) == 1, 'The real ten-second timer must save without a manual call'
        copy = copies[0]
        assert copy['title'] == 'Crash recovery test' and copy['source'] is None
        assert client.call('context.get')['data']['dirty'], 'Recovery must not mark the song saved'
        revision = client.call('document.get')['revision']
        time.sleep(10.5)
        assert len(client.call('recovery.list')['data']['copies']) == 1, 'Unchanged songs do not churn the retained generations'
        assert client.call('document.get')['revision'] == revision
        stop(crash=True)
        client = launch()
        write('recovery.restore', id=copy['id'])
        assert client.call('pattern.get', {'pattern': 0})['data'] == pattern
        assert client.call('document.get')['data']['title'] == 'Crash recovery test'
        context = client.call('context.get')['data']
        assert context['dirty'] and context['file'] is None, 'Recovered song must require Save As'
        # Preserve the song being replaced, and retain older copies after a corrupt restore.
        write('document.patch', title='Current unsaved song')
        before = client.call('document.get')
        invalid = root / 'Recovery/broken.resonance'
        invalid.write_bytes(b'incomplete project')
        error(-32003, lambda: write('recovery.restore', id=invalid.name))
        assert client.call('document.get') == before
        assert any(c['title'] == 'Current unsaved song' for c in client.call('recovery.list')['data']['copies'])
        write('recovery.restore', id=copy['id'])
        assert client.call('pattern.get', {'pattern': 0})['data'] == pattern
        error(-32001, lambda: client.call('recovery.save', {'expectedRevision': 'stale'}))
        error(-32602, lambda: client.call('recovery.list', {'unknown': True}))
        error(-32003, lambda: write('recovery.restore', id='../outside.resonance'))
        revision = client.call('document.get')['revision']
        saved = client.call('recovery.save', {'expectedRevision': revision}, 'one-snapshot')
        count = len(client.call('recovery.list')['data']['copies'])
        assert client.call('recovery.save', {'expectedRevision': revision}, 'one-snapshot') == saved
        assert len(client.call('recovery.list')['data']['copies']) == count, 'Retry must not duplicate recovery files'
        # Force an actual filesystem failure; the app reports it without opening an alert or losing edits.
        recovery = root / 'Recovery'
        held = root / 'Recovery-held'
        recovery.rename(held)
        recovery.write_text('not a directory')
        error(-32003, lambda: write('recovery.save'))
        assert client.call('recovery.status')['data']['error']
        assert client.call('document.get')['revision'] == revision
        recovery.unlink(); held.rename(recovery)
        write('recovery.save')
        assert client.call('recovery.status')['data']['error'] is None
        # Unfinished takes survive a separate snapshot and restart as stopped takes.
        write('recording.start', channels=[0], instrument=1)
        take = client.call('recording.get')['data']
        write('recovery.save')
        assert client.call('recording.get')['data']['capturing'], 'Autosave must not stop recording'
        takecopy = client.call('recovery.status')['data']['lastCopy']
        stop(crash=True)
        client = launch()
        write('recovery.restore', id=takecopy)
        assert client.call('recording.get')['data']['take'] and not client.call('recording.get')['data']['capturing']
        write('recording.discard', take=client.call('recording.get')['data']['take'])
        write('recovery.save')
        newest = client.call('recovery.status')['data']['lastCopy']
        assert 'recoveryTake' not in plistlib.loads((recovery / newest).read_bytes())
        # A normal save cleans up only this recovered session's copies.
        older = {c['id'] for c in client.call('recovery.list')['data']['copies'] if c['id'] != newest}
        write('document.save', path=str(root / 'saved.resonance'))
        # Listing is queued after the app's asynchronous cleanup.
        client.call('recovery.list')
        assert not (recovery / newest).exists()
        assert any((recovery / id).exists() for id in older)
        assert not client.call('context.get')['data']['dirty']
        print('PASS real app recovery: timer, unchanged revisions, SIGKILL/relaunch, exact song recall, dirty/file identity, corrupt fallback, current-song protection, write failure/retry, API idempotency, recording take and document-scoped cleanup')
    except Exception:
        log.flush(); log.seek(0); print(log.read(), file=sys.stderr)
        raise
    finally:
        stop(); log.close()
