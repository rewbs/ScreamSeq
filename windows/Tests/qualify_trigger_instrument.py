"""Opt-in Surge XT trigger qualification on a never-switched private desktop.

The input is the disposable trigger fixture from test_plugins_app. This explicit
Surge fixture enables oscillator retrigger for deterministic PCM comparisons;
it never changes the installed plugin defaults or an original project.
"""
import argparse
import ctypes
from ctypes import wintypes as w
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'Api'))
from client import Client, TransportError
from private_desktop import PrivateDesktop, kernel


def ready(pid, audio=False):
    client = Client(r'\\.\pipe\ScreamSeq.Api.' + str(pid), timeout=20)
    for _ in range(100):
        try:
            state = client.call('transport.get')['data']
            assert not state['fault'], state
            if not audio or state['audioActive']:
                return client
        except TransportError:
            pass
        time.sleep(.1)
    raise RuntimeError('Owned app did not become ready')


def write(client, method, **fields):
    return client.call(method, dict(expectedRevision=client.call('document.get')['revision'], **fields))


def quit_clean(desk, pid):
    desk.send(desk.hwnd(pid), 0x10)
    process = next(p for p in desk.processes if p.dwProcessId == pid)
    assert kernel.WaitForSingleObject(process.hProcess, 10000) == 0
    code = w.DWORD()
    assert kernel.GetExitCodeProcess(process.hProcess, ctypes.byref(code)) and code.value == 0, code.value


def run(args):
    for path in (args.exe, args.cache, args.project, args.output):
        assert path.is_absolute(), 'Qualification paths must be absolute'
    args.output.mkdir(parents=True, exist_ok=False)
    hashes = {key: hashlib.sha256(path.read_bytes()).hexdigest() for key, path in
              (('executable', args.exe), ('inputProject', args.project), ('cache', args.cache))}
    common = [str(args.exe), '--automation', '--vst3-test-cache', str(args.cache)]
    with PrivateDesktop() as desk:
        pid = desk.launch(common + ['--inspection', '--seconds', '120', '--project', str(args.project)])
        client = ready(pid)
        doc = client.call('document.get')['data']
        assert len(doc['nativePlugins']) == 1
        plugin = doc['nativePlugins'][0]
        assert plugin['classID'] == 'ABCDEF019182FAEB566D624153675854'
        instrument = plugin['instrument']
        assert instrument and set(client.call('instrument.get', dict(instrument=instrument))['data']['mapping']) == {0}
        catalog = client.call('plugin.parameters.get', dict(slot=0))['data']
        changes = [dict(id=p['id'], value=1) for p in catalog if p['name'].endswith(' Retrigger')]
        assert len(changes) == 6, changes
        write(client, 'plugin.parameters.set', slot=0, values=changes)
        # Surge's first editor open serializes its zoom into opaque processor
        # state without parameter notifications. Capture that known host stop
        # limitation while stopped; the live check below tests subsequent opens.
        initial_state = client.call('plugin.state.get', dict(slot=0))['data']['data']
        initial_parameters = client.call('plugin.parameters.get', dict(slot=0))['data']
        write(client, 'plugin.editor.open', slot=0)
        time.sleep(.6)
        editor_state = client.call('plugin.state.get', dict(slot=0))['data']['data']
        assert client.call('plugin.parameters.get', dict(slot=0))['data'] == initial_parameters
        write(client, 'plugin.editor.close', slot=0)
        # Every pattern contains only a trigger and key-off. Long playback must
        # not acquire a passing signal from the demo's original sample patterns.
        cells = []
        for pattern in doc['patterns']:
            assert pattern['rows'] > 4
            for row in range(pattern['rows']):
                for channel in range(doc['channels']):
                    cells.append(dict(pattern=pattern['index'], row=row, channel=channel,
                        note=61 if row == 0 and channel == 0 else 255 if row == 4 and channel == 0 else 0,
                        instrument=instrument if row == 0 and channel == 0 else 0,
                        volumeCommand=0, volume=0, effect=0, parameter=0))
        write(client, 'pattern.apply', cells=cells)
        release = args.output / 'retrigger-release.screamseq'
        held = args.output / 'retrigger-held.screamseq'
        write(client, 'document.save', path=str(release))
        write(client, 'pattern.apply', cells=[dict(pattern=p['index'], row=4, channel=0, note=0) for p in doc['patterns']])
        write(client, 'document.save', path=str(held))
        quit_clean(desk, pid)

        renders = {}
        for name, source in (('release', release), ('held', held)):
            report = args.output / (name + '-offline.json')
            subprocess.run([str(args.exe), '--offline-hosted-test', '--vst3-test-cache', str(args.cache),
                            '--project', str(source), '--report', str(report)], check=True, timeout=90)
            renders[name] = json.loads(report.read_text())
            assert renders[name]['finite'] and renders[name]['documentUnchanged']
            assert renders[name]['maxPartitionDelta'] < 1e-6 and renders[name]['energy'] > 1
        for off, on in zip(renders['release']['renders'], renders['held']['renders']):
            assert (off['rate'], off['block']) == (on['rate'], on['block'])
            assert off['firstAudibleFrame'] is not None and off['firstAudibleFrame'] < .02 * off['rate']
            assert off['quarterSecondEnergy'][0] == on['quarterSecondEnergy'][0]
            assert off['quarterSecondEnergy'][3] < on['quarterSecondEnergy'][3] * .2, (off, on)

        report = args.output / 'wasapi.json'
        pid = desk.launch(common + ['--audio-test-silent', '--seconds', str(args.seconds + 30),
                                   '--project', str(release), '--report', str(report)])
        client = ready(pid, audio=True)
        baseline = client.call('plugin.state.get', dict(slot=0))['data']['data']
        write(client, 'plugin.editor.open', slot=0)
        snapshots = []
        end = time.monotonic() + args.seconds
        while time.monotonic() < end:
            state = client.call('transport.get')['data']
            assert state['audioActive'] and not state['fault'] and state['overruns'] == 0, state
            if snapshots:
                assert state['frames'] > snapshots[-1]['frames']
            snapshots.append(state)
            time.sleep(.2)
        write(client, 'plugin.editor.close', slot=0)
        assert client.call('plugin.state.get', dict(slot=0))['data']['data'] == baseline
        assert client.call('transport.get')['data']['audioActive']
        quit_clean(desk, pid)
        audio = json.loads(report.read_text())
        a = audio['audio']
        assert a['silentOutput'] and a['preparedPlayback'] and not a['processorFault']
        assert a['callbackCount'] > 0 and not any(a[k] for k in ('deadlineOverruns', 'starvationIndicators', 'deviceErrors', 'lastError', 'mmcssError'))
    assert hashlib.sha256(args.exe.read_bytes()).hexdigest() == hashes['executable']
    assert hashlib.sha256(args.project.read_bytes()).hexdigest() == hashes['inputProject']
    evidence = dict(passed=True, sha256=hashes, plugin=plugin, deterministicParameters=changes,
                    offline=renders, wasapi=audio, snapshots=snapshots, silentOutput=True,
                    nativeEditorHeldOpen=True, savedBaselineUnchanged=True, originalProjectUnchanged=True,
                    editorInitializedWhileStopped=True, initialEditorStateChanged=initial_state != editor_state)
    (args.output / 'result.json').write_text(json.dumps(evidence, indent=2))
    print(json.dumps(dict(passed=True, evidence=str(args.output), audio=a)))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ('exe', 'cache', 'project', 'output'):
        parser.add_argument('--' + key, type=Path, required=True)
    parser.add_argument('--seconds', type=int, choices=range(3, 61), default=20)
    run(parser.parse_args())
