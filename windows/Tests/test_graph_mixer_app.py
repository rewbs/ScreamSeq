"""Real PID-pipe graph, mixer, rack and envelope integration on a private desktop."""
import json
import ctypes
from ctypes import wintypes
import os
from pathlib import Path
import sys
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'windows/Api'))
from client import Client, ApiError, TransportError
from private_desktop import PrivateDesktop
ctypes.windll.user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))


class GraphMixerAppTests(unittest.TestCase):
    def setUp(self):
        self.folder = Path(self.enterContext(tempfile.TemporaryDirectory(prefix='graph-mixer-', dir=os.environ['TMPDIR'])))
        self.catalogue = self.folder / 'catalogue' / 'envelope-catalogue-v1.json'
        self.desktop = self.enterContext(PrivateDesktop())
        args = [os.environ['SCREAMSEQ_TEST_EXE'], '--inspection', '--automation', '--seconds', '120',
                '--envelope-test-catalogue', str(self.catalogue)]
        if os.environ.get('SCREAMSEQ_TEST_PROVIDER_CACHE'):
            args += ['--vst3-test-cache', os.environ['SCREAMSEQ_TEST_PROVIDER_CACHE']]
        self.pid = self.desktop.launch(args)
        self.client = Client(r'\\.\pipe\ScreamSeq.Api.' + str(self.pid), timeout=20)
        for _ in range(100):
            try:
                self.doc()
                break
            except TransportError:
                time.sleep(.1)
        else:
            self.fail('graph test app did not publish its pipe')

    def doc(self):
        return self.client.call('document.get')

    def read(self, method, **fields):
        return self.client.call(method, fields)['data']

    def write(self, method, **fields):
        return self.client.call(method, dict(expectedRevision=self.doc()['revision'], **fields))['data']

    def rejected(self, method, **fields):
        before = self.doc()
        with self.assertRaises(ApiError):
            self.write(method, **fields)
        self.assertEqual(self.doc(), before)

    def add_gain(self):
        gain = next(p for p in self.read('plugin.discover', format='Built-in') if 'Gain' in p['name'])
        self.write('plugin.add', descriptor=gain)
        return self.doc()['data']['nativePlugins'][-1]['instanceID']

    def control(self, identifier):
        user = ctypes.WinDLL('user32')
        user.GetDlgItem.argtypes = [wintypes.HWND, ctypes.c_int]
        user.GetDlgItem.restype = wintypes.HWND
        handle = user.GetDlgItem(self.desktop.hwnd(self.pid), identifier)
        self.assertTrue(handle)
        return handle

    def command(self, identifier, notification=0):
        self.desktop.send(self.desktop.hwnd(self.pid), 0x111, identifier | (notification << 16), self.control(identifier))

    def field(self, identifier, value):
        buffer = ctypes.create_unicode_buffer(str(value))
        self.desktop.send(self.control(identifier), 0xC, 0, ctypes.addressof(buffer))

    def test_native_mixer_draft_stale_selection_undo_and_reload(self):
        self.command(400)
        self.assertTrue(self.read('workspace.get')['mixerEditor']['visible'])
        self.assertEqual(self.desktop.focus(self.desktop.hwnd(self.pid)), self.control(401))
        self.command(402)  # Enable.
        track = self.read('workspace.get')['mixerEditor']['bus']
        self.field(413, '-9')
        self.command(407)  # Mute draft.
        self.assertTrue(self.read('workspace.get')['mixerEditor']['draft'])
        # Selecting another bus cannot redirect a partially edited value.
        self.desktop.send(self.control(401), 0x186, 1)
        self.command(401, 1)
        self.assertEqual(self.read('workspace.get')['mixerEditor']['bus'], track)
        self.assertEqual(self.desktop.focus(self.desktop.hwnd(self.pid)), self.control(401))
        self.write('document.patch', title='Edited elsewhere')
        before = self.doc()
        self.command(406)  # Stale Apply.
        self.assertEqual(self.doc(), before)
        self.assertTrue(self.read('workspace.get')['mixerEditor']['draft'])
        self.command(405)  # Explicit Reload.
        self.field(413, '-9')
        self.command(407)
        self.command(406)
        bus = next(b for b in self.read('mixer.get')['buses'] if b['id'] == track)
        self.assertEqual((bus['gainDB'], bus['mute']), (-9, True))
        self.assertFalse(self.read('workspace.get')['mixerEditor']['draft'])
        self.write('history.undo', domain='document')
        bus = next(b for b in self.read('mixer.get')['buses'] if b['id'] == track)
        self.assertEqual((bus['gainDB'], bus['mute']), (0, False))
        self.desktop.send(self.desktop.hwnd(self.pid), 0x113, 3)
        self.command(403)  # Add group.
        group = self.read('workspace.get')['mixerEditor']['bus']
        self.assertNotEqual(group, track)
        self.desktop.send(self.control(409), 0x14E, 0)  # Disconnect.
        self.command(409, 1)
        self.command(406)
        self.assertEqual(next(b for b in self.read('mixer.get')['buses'] if b['id'] == group)['output'], '')
        self.command(404)  # Remove group.
        self.assertFalse(any(b['id'] == group for b in self.read('mixer.get')['buses']))
        self.command(107)  # Notes and mixer retain separate captured drafts.
        self.assertFalse(self.read('workspace.get')['mixerEditor']['visible'])
        self.command(400)
        self.assertTrue(self.read('workspace.get')['mixerEditor']['visible'])

    def test_native_mixer_controls_fit_small_window(self):
        user = ctypes.WinDLL('user32')
        user.SetWindowPos.argtypes = [wintypes.HWND, wintypes.HWND, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, wintypes.UINT]
        user.GetWindowRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
        user.IsWindowVisible.argtypes = [wintypes.HWND]
        dpi = self.read('workspace.get')['dpi'] / 96
        hwnd = self.desktop.hwnd(self.pid)
        self.assertTrue(user.SetWindowPos(hwnd, None, 0, 0, int(900*dpi), int(620*dpi), 0x16))
        self.command(400)
        self.command(402)
        frame = wintypes.RECT()
        user.GetWindowRect(hwnd, ctypes.byref(frame))
        for identifier in range(401, 417):
            with self.subTest(control=identifier):
                control = self.control(identifier)
                self.assertTrue(user.IsWindowVisible(control))
                rect = wintypes.RECT()
                self.assertTrue(user.GetWindowRect(control, ctypes.byref(rect)))
                self.assertGreater(rect.right, rect.left)
                self.assertGreaterEqual(rect.left, frame.left)
                self.assertLessEqual(rect.right, frame.right)
                self.assertLessEqual(rect.bottom, frame.bottom)

    def test_live_mixer_controls_keep_wasapi_running_and_routing_stops(self):
        self.write('mixer.enable')
        master = self.read('mixer.get')['buses'][-1]['id']
        path = self.folder / 'live-mixer.screamseq'
        self.write('document.save', path=str(path))
        pid = self.desktop.launch([os.environ['SCREAMSEQ_TEST_EXE'], '--audio-test-silent', '--automation',
                                   '--seconds', '30', '--project', str(path)])
        self.client = Client(r'\\.\pipe\ScreamSeq.Api.' + str(pid), timeout=20)
        for _ in range(100):
            try:
                if self.read('transport.get')['audioActive']:
                    break
            except TransportError:
                pass
            time.sleep(.1)
        else:
            self.fail('owned silent mixer test did not start audio')
        before = self.read('transport.get')
        for gain, pan in [(-12, -.5), (-24, .5), (0, 0)]:
            baseline = self.doc()
            self.write('mixer.bus.set', bus=master, gainDB=gain, pan=pan, preview=True)
            self.assertEqual(self.doc(), baseline)
            self.write('mixer.bus.set', bus=master, gainDB=gain, pan=pan)
            current = self.read('transport.get')
            self.assertTrue(current['audioActive'])
            self.assertFalse(current['fault'])
            self.assertGreaterEqual(current['frames'], before['frames'])
        meters = self.read('mixer.meters')
        self.assertTrue(meters['playing'])
        self.assertTrue(meters['meters'])
        self.write('mixer.bus.add', kind='return', name='New topology')
        self.assertFalse(self.read('transport.get')['audioActive'])

    def test_graph_clones_real_baseline_preserves_omitted_state_history_and_reopen(self):
        identity = self.add_gain()
        self.write('plugin.parameters.set', slot=0, values=[dict(id=1, value=-12)])
        baseline = self.read('plugin.state.get', slot=0)['data']
        graph = self.write('graph.create', name='Parallel colour')['graph']
        info = self.read('graph.get')
        self.assertEqual(info['plugins'][0]['id'], identity)
        node = self.write('graph.node.add', graph=graph, kind='plugin', slot=0,
                          insertAfter=info['library'][0]['nodes'][0]['id'])['node']
        definition = self.read('graph.get')['library'][0]
        self.assertEqual(next(n for n in definition['nodes'] if n['id'] == node)['plugin']['state'], baseline)
        self.write('plugin.parameters.set', slot=0, values=[dict(id=1, value=-24)])
        # Recipes are independent copies; editing the rack must not rewrite one.
        self.assertEqual(self.read('graph.get')['library'][0], definition)
        draft = self.read('graph.get', includeState=False)['library'][0]
        self.assertNotIn('state', next(n for n in draft['nodes'] if n['id'] == node)['plugin'])
        draft['name'] = 'Retained baseline'
        self.write('graph.update', definition=draft)
        updated = self.read('graph.get')['library'][0]
        self.assertEqual(next(n for n in updated['nodes'] if n['id'] == node)['plugin']['state'], baseline)
        self.write('history.undo', domain='document')
        self.assertEqual(self.read('graph.get')['library'][0], definition)
        self.write('history.redo', domain='document')
        path = self.folder / 'graph.screamseq'
        self.write('document.save', path=str(path))
        self.write('graph.remove', graph=graph)
        self.write('document.open', path=str(path), discard=True)
        self.assertEqual(self.read('graph.get')['library'][0], updated)

    def test_disconnected_routes_sends_validation_preview_and_persistence(self):
        self.write('mixer.enable')
        original = self.read('mixer.get')
        master = original['buses'][-1]['id']
        track = original['buses'][0]['id']
        group = self.write('mixer.bus.add', kind='group', name='Side bus')['bus']
        self.write('mixer.bus.set', bus=group, output=None)
        self.write('mixer.bus.set', bus=track, output=group)
        self.write('mixer.sends.set', bus=group, sends=[dict(target=master, gainDB=-6)])
        self.rejected('mixer.bus.set', bus=master, output=track)
        self.rejected('mixer.bus.set', bus=track, gainDB=True)
        before = self.doc()
        self.write('mixer.bus.set', bus=group, gainDB=-12, preview=True)
        self.assertEqual(self.doc(), before)
        self.write('mixer.bus.set', bus=group, gainDB=-12)
        saved = self.read('mixer.get')['buses']
        self.write('mixer.bus.remove', bus=group)
        self.assertEqual(next(b for b in self.read('mixer.get')['buses'] if b['id'] == track)['output'], '')
        self.write('history.undo', domain='document')
        self.assertEqual(self.read('mixer.get')['buses'], saved)
        path = self.folder / 'mixer.screamseq'
        self.write('document.save', path=str(path))
        self.write('mixer.enable', enabled=False)
        self.write('document.open', path=str(path), discard=True)
        self.assertEqual(self.read('mixer.get')['buses'], saved)

    def test_linked_graph_and_real_parameter_envelopes_catalogue_separate_revision(self):
        plugin = self.add_gain()
        graph = self.write('graph.create', name='Curve graph')['graph']
        node = self.write('graph.node.add', graph=graph, kind='automation')['node']
        shape = dict(span=16384, points=[dict(position=0, value=.25), dict(position=16383, value=.75)])
        template = self.write('envelope.bank.save', name='Rise', shape=shape)['id']
        target = dict(kind='graph', graph=graph, node=node, pattern=0)
        parameter = dict(kind='parameter', plugin=plugin, parameter=1, pattern=0)
        self.write('envelope.bank.apply', template=template, target=target, linked=True)
        self.write('envelope.bank.apply', template=template, target=parameter, linked=True)
        self.rejected('envelope.bank.apply', template=template,
                      target=dict(parameter, parameter=4294967295), linked=True)
        self.rejected('envelope.bank.apply', template=template,
                      target=dict(parameter, plugin='missing-plugin'), linked=True)
        first = self.read('graph.automation.get', graph=graph, node=node, pattern=0)
        shape['points'][0]['value'] = .9
        self.write('envelope.bank.save', id=template, name='Rise', shape=shape)
        curve = self.read('graph.automation.get', graph=graph, node=node, pattern=0)
        self.assertNotEqual(curve, first)
        self.assertEqual(curve['points'][0]['value'], .9)
        self.assertEqual(self.read('envelope.bank.list', target=parameter)['shape']['points'][0]['value'], .9)
        self.write('history.undo', domain='document')
        self.assertEqual(self.read('graph.automation.get', graph=graph, node=node, pattern=0), first)
        catalogue = self.read('envelope.catalogue.list')
        self.assertFalse(self.catalogue.parent.exists())
        before = self.doc()
        self.write('envelope.catalogue.publish', template=template, expectedCatalogueRevision=catalogue['revision'], dryRun=True)
        self.assertFalse(self.catalogue.exists())
        exported = self.write('envelope.catalogue.publish', template=template, expectedCatalogueRevision=catalogue['revision'])['id']
        self.assertEqual(self.doc(), before)
        current = self.read('envelope.catalogue.list')
        self.assertEqual(json.loads(self.catalogue.read_text())['entries'], current['entries'])
        self.rejected('envelope.catalogue.publish', template=template, expectedCatalogueRevision=catalogue['revision'])
        imported = self.write('envelope.catalogue.import', catalogueID=exported, expectedCatalogueRevision=current['revision'])['id']
        self.assertNotEqual(imported, template)
        self.write('history.undo', domain='document')
        self.assertEqual(len(self.read('envelope.bank.list')['entries']), 1)

    @unittest.skipUnless(os.environ.get('SCREAMSEQ_TEST_PROVIDER_CACHE'), 'private VST3 provider fixture')
    def test_real_plugin_ports_sidechains_and_explicit_disconnection(self):
        descriptors = self.read('plugin.discover', format='VST3')
        effect = next(p for p in descriptors if p['classID'] == '5245534F4E414E4350524F4752410001')
        instrument = next(p for p in descriptors if p['classID'] == '5245534F4E414E43494E535452550001')
        self.write('plugin.add', descriptor=effect)
        self.write('plugin.add', descriptor=instrument)
        rack = self.doc()['data']['nativePlugins']
        fx, synth = rack[0]['instanceID'], rack[1]['instanceID']
        self.write('mixer.enable')
        buses = self.read('mixer.get')['buses']
        track, master = buses[0]['id'], buses[-1]['id']
        self.write('mixer.bus.set', bus=master, inserts=[fx])
        self.rejected('mixer.sidechains.set', plugin=fx, input=1, sources=[dict(source=track)])
        self.write('plugin.buses.set', slot=0, inputs=[1])
        self.write('mixer.sidechains.set', plugin=fx, input=1, sources=[dict(source=track)])
        self.write('mixer.plugin.route', plugin=synth, output=0, target=None, disconnected=True)
        routes = self.read('mixer.get')['instruments']
        self.assertTrue(any(r['plugin'] == synth and r['target'] == '' for r in routes))
        self.write('mixer.plugin.route', plugin=synth, output=0, target=None)
        self.assertFalse(any(r['plugin'] == synth for r in self.read('mixer.get')['instruments']))
        self.write('history.undo', domain='document')
        self.assertEqual(self.read('mixer.get')['instruments'], routes)
        graph = self.write('graph.create')['graph']
        self.rejected('graph.node.add', graph=graph, kind='plugin', slot=1)


if __name__ == '__main__':
    unittest.main()
