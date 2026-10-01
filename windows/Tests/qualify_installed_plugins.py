"""Opt-in installed VST3 qualification through the actual application.

Consumes a passing ExternalLifecycle report and its private scanner cache. All
projects are generated in a new output directory. No user registry is changed.
Hardware qualification, when requested, renders normally then silences output.
"""
import argparse
import hashlib
import json
from pathlib import Path
import plistlib
import subprocess
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'Api'))
from client import Client, TransportError


def ready(process):
    client = Client(r'\\.\pipe\ScreamSeq.Api.' + str(process.pid), timeout=1)
    for _ in range(100):
        if process.poll() is not None:
            raise RuntimeError('App exited before API publication')
        try:
            client.call('document.get')
            return client
        except TransportError:
            time.sleep(.1)
    raise RuntimeError('App API did not become ready')


def run(args):
    for path in (args.exe, args.fixture_tool, args.lifecycle, args.cache, args.output):
        if not path.is_absolute():
            raise ValueError('All qualification paths must be absolute')
    hashes = {name: hashlib.sha256(path.read_bytes()).hexdigest() for name, path in
              (('executable', args.exe), ('fixtureTool', args.fixture_tool), ('lifecycle', args.lifecycle), ('cache', args.cache))}
    evidence = json.loads(args.lifecycle.read_text())
    if not evidence['passed'] or not evidence['plugins']:
        raise ValueError('A passing external lifecycle report is required')
    args.output.mkdir(parents=True, exist_ok=False)
    fixtures = args.output / 'fixtures'
    fixtures.mkdir()
    subprocess.run([str(args.fixture_tool), '--fixtures', str(fixtures)], check=True, timeout=30)
    baseline = plistlib.loads((fixtures / 'unicode.screamseq').read_bytes())
    results = []
    common = [str(args.exe), '--vst3-test-cache', str(args.cache)]
    for index, record in enumerate(evidence['plugins']):
        if record['isInstrument']:
            raise ValueError('This app rack qualification requires an effect plugin')
        record = dict(record, state=bytes(record['state']))
        tree = dict(baseline, plugins=[record])
        source = args.output / f'plugin-{index}.screamseq'
        saved = args.output / f'plugin-{index}-saved.screamseq'
        source.write_bytes(plistlib.dumps(tree, fmt=plistlib.FMT_BINARY))
        original = source.read_bytes()
        process = subprocess.Popen(common + ['--inspection', '--automation', '--seconds', '4', '--project', str(source)])
        try:
            client = ready(process)
            client.call('document.save', {'expectedRevision': client.call('document.get')['revision'], 'path': str(saved)})
            client.call('document.open', {'expectedRevision': client.call('document.get')['revision'], 'path': str(saved)})
            if client.call('transport.get')['data']['voicePositions']:
                raise AssertionError('Stopped transport retained active markers')
            if process.wait(timeout=15) != 0:
                raise RuntimeError('App inspection shutdown failed')
        finally:
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=10)
        if plistlib.loads(saved.read_bytes())['plugins'] != [record] or source.read_bytes() != original:
            raise AssertionError('Save/reopen changed plugin identity/state or source')
        offline = args.output / f'plugin-{index}-offline.json'
        rendered = subprocess.run(common + ['--offline-hosted-test', '--project', str(saved), '--report', str(offline)], timeout=90)
        result = {'name': record['name'], 'identityAndStatePreserved': True, 'offlinePassed': rendered.returncode == 0,
                  'offline': json.loads(offline.read_text()) if offline.exists() else {'error': 'No completed render report'}}
        if args.silent_seconds:
            report = args.output / f'plugin-{index}-wasapi.json'
            process = subprocess.Popen(common + ['--audio-test-silent', '--automation', '--seconds', str(args.silent_seconds), '--project', str(saved), '--report', str(report)])
            samples = []
            try:
                client = ready(process)
                # The pipe is published before worker preparation finishes;
                # await() intentionally keeps the UI/API responsive meanwhile.
                start_deadline = time.monotonic() + 10
                while True:
                    transport = client.call('transport.get')['data']
                    if transport['fault']:
                        raise AssertionError('Live preparation faulted')
                    if transport['audioActive']:
                        break
                    if time.monotonic() >= start_deadline or process.poll() is not None:
                        raise RuntimeError('Silent playback did not start')
                    time.sleep(.05)
                end = time.monotonic() + max(0, args.silent_seconds - 1)
                while time.monotonic() < end and process.poll() is None:
                    transport = client.call('transport.get')['data']
                    if not transport['audioActive'] or transport['fault']:
                        raise AssertionError('Live playback inactive or faulted')
                    samples.extend(transport['voicePositions'])
                    time.sleep(.15)
                if process.wait(timeout=15) != 0:
                    raise RuntimeError('Silent WASAPI qualification failed')
            finally:
                if process.poll() is None:
                    process.terminate()
                    process.wait(timeout=10)
            if not samples or not any(v['sampleFrame'] for v in samples):
                raise AssertionError('Live sample positions did not advance')
            result['wasapi'] = json.loads(report.read_text())
            result['voiceSnapshots'] = len(samples)
            audio = result['wasapi']['audio']
            result['wasapiPassed'] = (audio['silentOutput'] and audio['preparedPlayback'] and not audio['processorFault']
                and audio['callbackCount'] > 0 and not any(audio[k] for k in ('deadlineOverruns', 'starvationIndicators', 'deviceErrors', 'lastError', 'mmcssError')))
        results.append(result)
    passed = all(r['offlinePassed'] and r.get('wasapiPassed', True) for r in results)
    if hashlib.sha256(args.exe.read_bytes()).hexdigest() != hashes['executable']:
        raise RuntimeError('Executable changed during qualification')
    (args.output / 'result.json').write_text(json.dumps({'passed': passed, 'sha256': hashes, 'plugins': results}, indent=2))
    print(json.dumps({'passed': passed, 'plugins': [r['name'] for r in results], 'evidence': str(args.output)}))
    return 0 if passed else 1


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('exe', 'fixture-tool', 'lifecycle', 'cache', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--silent-seconds', type=int, choices=range(3, 61), default=0)
    sys.exit(run(parser.parse_args()))
