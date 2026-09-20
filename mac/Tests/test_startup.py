#!/usr/bin/env python3
"""Open a real module during native application startup without windows or audio."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
ROOT = Path(__file__).resolve().parents[2]
BUILD = Path(os.environ.get("RESONANCE_BUILD_DIR", str(ROOT / "bin/mac-native"))).resolve()
sys.path.insert(0, str(ROOT / 'mac/Tools'))
from resonance_api import Client, endpoints
with tempfile.TemporaryDirectory(prefix='resonance-startup-') as tmp:
    directory = Path(tmp)
    source = directory / 'startup.mptm'
    subprocess.run([str(BUILD / 'qualification-fixture'), str(source)], check=True, capture_output=True)
    with open(directory / 'app.log', 'w+') as log:
        process = subprocess.Popen([str(BUILD / 'ScreamSeq.app/Contents/MacOS/ScreamSeq'), str(source), '--automation-test'],
            stdout=log, stderr=log, env={**os.environ, 'RESONANCE_AUTOMATION_TEST_DIRECTORY': tmp})
        try:
            deadline = time.monotonic() + 20
            while time.monotonic() < deadline:
                if process.poll() is not None:
                    log.seek(0)
                    raise AssertionError(f'Application exited during launch: {process.returncode}\n{log.read()}')
                found = endpoints(directory)
                if found:
                    client = Client(found[0]['socket'])
                    document = client.call('document.get')['data']
                    assert document['channels'] == 127 and len(document['orders']) == 32, document
                    assert client.call('context.get')['data']['file'] == str(source)
                    print('PASS native startup file: populated 127-channel document, ready UI context and API; no windows or audio')
                    break
                time.sleep(.05)
            else:
                raise AssertionError('Startup API did not become ready')
        finally:
            process.terminate()
            process.wait(timeout=10)
