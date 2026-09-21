"""Asset worker integration through a disposable application's exact PID pipe."""
import base64
import os
from pathlib import Path
import plistlib
import struct
import sys
import unittest
import wave

import test_editor_app as editor_helpers
from client import ApiError


class AssetAppTests(unittest.TestCase):
    setUp = editor_helpers.EditorAppTests.setUp
    close_apps = editor_helpers.EditorAppTests.close_apps
    launch = editor_helpers.EditorAppTests.launch
    write = editor_helpers.EditorAppTests.write

    def pcm(self, client, sample=1):
        return client.call('sample.pcm.get', {'sample': sample})['data']

    def install_pcm(self, client):
        raw = struct.pack('<8h', 1000, -2000, 3000, -4000, 5000, -6000, 7000, -8000)
        self.write(client, 'sample.pcm.set', sample=1, format='s16le', channels=1,
                   rate=48000, name='Café waveform', data=base64.b64encode(raw).decode())
        return raw

    def test_pcm_process_guards_history_and_persistence(self):
        client, _ = self.launch()
        methods = client.call('api.describe')['data']
        self.assertIn('sample.process', methods['writes'])
        self.assertIn('instrument.get', methods['reads'])
        raw = self.install_pcm(client)
        before = client.call('document.get')
        self.write(client, 'sample.process', sample=1, operation='reverse', dryRun=True)
        self.assertEqual(client.call('document.get'), before)
        self.write(client, 'sample.process', sample=1, operation='gain', gainDB=0)
        self.assertEqual(client.call('document.get'), before)
        with self.assertRaises(ApiError):
            self.write(client, 'sample.process', sample=1, operation='reverse', start=6, end=3)
        self.assertEqual(client.call('document.get'), before)
        self.write(client, 'sample.process', sample=1.0, operation='reverse')
        expected = struct.pack('<8h', *reversed(struct.unpack('<8h', raw)))
        self.assertEqual(base64.b64decode(self.pcm(client)['data']), expected)
        with self.assertRaises(ApiError) as error:
            client.call('sample.process', {'sample': 1, 'operation': 'reverse', 'expectedRevision': before['revision']})
        self.assertEqual(error.exception.code, -32001)
        self.write(client, 'history.undo', domain='document')
        self.assertEqual(base64.b64decode(self.pcm(client)['data']), raw)
        self.write(client, 'history.redo', domain='document')
        self.write(client, 'sample.loops.set', sample=1, normal={'start': 1, 'end': 7, 'enabled': True, 'pingpong': True})
        saved = self.directory / 'sample-edit.screamseq'
        self.write(client, 'document.save', path=str(saved))
        reopened, _ = self.launch(saved)
        self.assertEqual(base64.b64decode(self.pcm(reopened)['data']), expected)
        info = reopened.call('sample.get', {'sample': 1})['data']
        self.assertEqual((info['loopStart'], info['loopEnd'], info['loop'], info['pingpong']), (1, 7, True, True))

    def test_clipboard_lifetime_cut_paste_and_replacement(self):
        client, _ = self.launch()
        raw = self.install_pcm(client)
        self.write(client, 'sample.clipboard.copy', sample=1, start=0, end=4)
        clip = client.call('sample.clipboard.get')['data']
        self.assertTrue(clip['available'])
        self.write(client, 'sample.paste', sample=1, at=8, clipboardId=clip['clipboardId'], mode='insert')
        self.assertEqual(base64.b64decode(self.pcm(client)['data']), raw + raw[:8])
        self.write(client, 'history.undo', domain='document')
        self.write(client, 'sample.cut', sample=1, start=0, end=4)
        self.assertEqual(base64.b64decode(self.pcm(client)['data']), raw[8:])
        with self.assertRaises(ApiError) as error:
            self.write(client, 'sample.paste', sample=1, at=0, clipboardId=clip['clipboardId'])
        self.assertEqual(error.exception.code, -32001)
        path = self.directory / 'replacement.screamseq'
        self.write(client, 'document.save', path=str(path))
        self.write(client, 'document.open', path=str(path))
        self.assertFalse(client.call('sample.clipboard.get')['data']['available'])

    def test_large_pcm_request_and_replay(self):
        client, _ = self.launch()
        raw = bytes(range(256)) * 4096  # 1 MiB PCM -> >1 MiB framed request.
        params = dict(format='s8', channels=1, rate=48000, data=base64.b64encode(raw).decode(),
                      expectedRevision=client.call('document.get')['revision'])
        result = client.call('sample.pcm.set', params, request_id='large-pcm')
        self.assertEqual(client.call('sample.pcm.set', params, request_id='large-pcm'), result)
        sample = result['data']['sample']
        self.assertEqual(client.call('sample.get', {'sample': sample})['data']['frames'], len(raw))
        self.assertEqual(base64.b64decode(self.pcm(client, sample)['data']), raw[:65536])

    def test_import_preserves_plugin_aliases_and_rejects_reserved_slot(self):
        client, _ = self.launch()
        path = self.directory / 'plugin.screamseq'
        self.write(client, 'document.save', path=str(path))
        tree = plistlib.loads(path.read_bytes())
        tree['version'] = 6
        tree['plugins'] = [dict(format='AU', type=int.from_bytes(b'aumu', 'big'), subtype=0, manufacturer=0,
                                name='Preserved Mac instrument', state=b'opaque state', instanceID='fixture-plugin',
                                instrument=1, instrumentAssignments=[dict(instrument=1, channel=1)])]
        path.write_bytes(plistlib.dumps(tree, fmt=plistlib.FMT_BINARY))
        self.write(client, 'document.open', path=str(path))
        source = self.directory / '音.wav'
        with wave.open(str(source), 'wb') as f:
            f.setnchannels(1); f.setsampwidth(2); f.setframerate(48000); f.writeframes(struct.pack('<8h', *range(8)))
        before = client.call('document.get')
        for method, fields in [('sample.importMany', dict(paths=[str(source)], createInstruments=True, dryRun=True)),
                               ('sample.importMany', dict(paths=[str(source)], createInstruments=True)),
                               ('instrument.create', dict(sample=1)),
                               ('instrument.import', dict(path=str(source), slot=1)),
                               ('instrument.import', dict(path=str(source), slot=1.0, dryRun=True)),
                               ('instrument.import', dict(path=str(source), slot=1.0))]:
            with self.subTest(method=method, fields=fields):
                with self.assertRaises(ApiError): self.write(client, method, **fields)
                self.assertEqual(client.call('document.get'), before)
        self.write(client, 'sample.importMany', paths=[str(source)], createInstruments=False)
        destination = self.directory / 'preserved.screamseq'
        self.write(client, 'document.save', path=str(destination))
        self.assertEqual(plistlib.loads(destination.read_bytes())['plugins'], tree['plugins'])


if __name__ == '__main__':
    unittest.main()
