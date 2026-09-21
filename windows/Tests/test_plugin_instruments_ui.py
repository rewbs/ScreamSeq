"""Native shared-plugin assignments and exact Mac-compatible API contracts."""
import ctypes
from ctypes import wintypes
import os
import plistlib
import unittest
from unittest.mock import patch
import private_desktop
import test_plugins_app as support
from client import ApiError


class PluginInstrumentsTests(unittest.TestCase):
    def setUp(self):
        if self._testMethodName == 'test_installed_surge_aliases_preserve_sound_and_plugin_lifecycle':
            with patch.dict(os.environ, SCREAMSEQ_TEST_PLUGIN_CACHE=os.environ['SCREAMSEQ_TEST_INSTRUMENT_CACHE']):
                support.PluginAppTests.setUp(self)
        else:
            support.PluginAppTests.setUp(self)
    write = support.PluginAppTests.write
    doc = support.PluginAppTests.doc
    rack = support.PluginAppTests.rack
    add = support.PluginAppTests.add
    tick = support.PluginAppTests.tick
    select = support.PluginAppTests.select
    command = support.PluginAppTests.command

    def fixture(self, aliases=False):
        self.write('instrument.create', sample=1)
        self.assertGreaterEqual(len(self.doc()['data']['instruments']), 4)
        self.add()
        self.add(1)
        path = self.folder / 'assignments.screamseq'
        self.write('document.save', path=str(path))
        tree = plistlib.loads(path.read_bytes())
        for index, plugin in enumerate(tree['plugins']):
            assignments = [dict(instrument=1, channel=3)] if index == 0 else [dict(instrument=3, channel=7)]
            if aliases and index == 0:
                assignments.append(dict(instrument=2, channel=9))
            plugin.update(format='AU', type=int.from_bytes(b'aumu', 'big'), classID='',
                          isInstrument=True, state=b'opaque-assignments-'+bytes([index]),
                          instrument=assignments[0]['instrument'], instrumentAssignments=assignments,
                          futureAssignmentField=dict(retain=index))
        path.write_bytes(plistlib.dumps(tree, fmt=plistlib.FMT_BINARY))
        self.write('document.open', path=str(path))
        self.tick()
        return [p['instanceID'] for p in self.rack()]

    def assignments(self, plugin):
        return self.client.call('plugin.instruments.get', dict(plugin=plugin))['data']

    def local(self):
        return self.client.call('workspace.get')['data']['pluginInstruments']

    def window(self):
        found = []
        @private_desktop.callback
        def visit(hwnd, _):
            pid = wintypes.DWORD()
            private_desktop.user.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
            name = ctypes.create_unicode_buffer(128)
            private_desktop.user.GetClassNameW(hwnd, name, 128)
            if pid.value == self.pid and name.value == 'ScreamSeq.PluginInstruments':
                found.append(hwnd)
            return True
        private_desktop.check(private_desktop.user.EnumDesktopWindows(self.desktop.desktop, visit, 0))
        self.assertEqual(len(found), 1)
        return found[0]

    def control(self, identifier):
        result = private_desktop.user.GetDlgItem(self.window(), identifier)
        self.assertTrue(result)
        return result

    def press(self, identifier):
        self.desktop.send(self.window(), 0x111, identifier, self.control(identifier))

    def choose(self, identifier, index):
        self.desktop.send(self.control(identifier), 0x14E, index)
        self.desktop.send(self.window(), 0x111, identifier | (1 << 16), self.control(identifier))

    def choices(self):
        control = self.control(3002)
        return [self.desktop.send(control, 0x150, i) for i in range(self.desktop.send(control, 0x146))]

    def open(self):
        self.select(318, 3)
        self.tick()
        self.command(323)
        self.assertTrue(self.local()['visible'])

    def test_api_response_preview_noop_primary_promotion_and_atomic_owner_move(self):
        first, second = self.fixture(aliases=True)
        current = self.assignments(first)
        routes = [dict(instrument=a['instrument'], channel=a['channel']) for a in current['assignments']]
        before = self.doc()
        result = self.write('plugin.instruments.set', plugin=first, assignments=routes, dryRun=True)['data']
        self.assertEqual(result, dict(wouldChange=False, dryRun=True, routing=current))
        self.assertEqual(self.doc(), before)
        self.assertFalse(self.write('instrument.plugin.set', instrument=1, plugin=first, channel=3)['data']['wouldChange'])
        self.assertEqual(self.doc(), before)
        routes.append(dict(instrument=4, channel=2))
        result = self.write('plugin.instruments.set', plugin=first, assignments=routes, dryRun=True)['data']
        self.assertTrue(result['wouldChange'])
        self.assertEqual(result['routing']['assignments'][-1], dict(instrument=4, channel=2, available=True))
        self.assertEqual(self.doc(), before)
        for invalid in [routes+[routes[0]], routes+[dict(instrument=3, channel=1)],
                        [dict(instrument=1, channel=17)], [dict(instrument=True, channel=1)]]:
            for dry in (True, False):
                with self.assertRaises(ApiError) as rejected:
                    self.write('plugin.instruments.set', plugin=first, assignments=invalid, dryRun=dry)
                self.assertEqual(rejected.exception.code, -32602)
                self.assertEqual(self.doc(), before)
        self.write('plugin.instruments.set', plugin=first, assignments=routes)
        before = self.doc()
        self.assertFalse(self.write('plugin.instruments.set', plugin=first, assignments=routes)['data']['wouldChange'])
        self.assertEqual(self.doc(), before)
        self.write('plugin.assign', slot=0, instrument=4)
        self.assertEqual(self.assignments(first)['assignments'], [dict(instrument=4, channel=2, available=True), dict(instrument=2, channel=9, available=True)])
        before_move = self.rack()
        self.write('plugin.assign', slot=0, instrument=3)
        self.assertEqual(self.assignments(first)['assignments'], [dict(instrument=3, channel=1, available=True), dict(instrument=2, channel=9, available=True)])
        self.assertEqual(self.assignments(second)['assignments'], [])
        self.write('history.undo', domain='plugins')
        self.assertEqual(self.rack(), before_move)
        self.write('history.redo', domain='plugins')
        self.write('plugin.assign', slot=0, instrument=0)
        self.assertEqual(self.assignments(first)['assignments'], [])

    def test_native_routes_preview_history_opaque_state_and_reopen(self):
        first, second = self.fixture()
        self.open()
        before = self.doc()
        original = self.assignments(first)
        self.press(3004)
        self.assertEqual(self.local()['assignments'], [dict(instrument=1, channel=3), dict(instrument=2, channel=1)])
        self.assertNotIn(3, self.choices())  # Another plugin owns this instrument.
        self.choose(3002, self.choices().index(4))
        self.choose(3003, 15)
        expected = [dict(instrument=1, channel=3), dict(instrument=4, channel=16)]
        self.assertEqual(self.local()['assignments'], expected)
        self.press(3006)
        self.assertEqual(self.doc(), before)
        self.assertTrue(self.local()['dirty'])
        self.press(3007)
        self.assertFalse(self.local()['dirty'])
        self.assertEqual(self.assignments(first)['assignments'], [dict(a, available=True) for a in expected])
        self.assertEqual(self.assignments(second)['assignments'], [dict(instrument=3, channel=7, available=True)])
        self.write('history.undo', domain='plugins')
        self.assertEqual(self.assignments(first), original)
        self.write('history.redo', domain='plugins')
        saved_rack = self.rack()
        path = self.folder / 'native-aliases.screamseq'
        self.write('document.save', path=str(path))
        tree = plistlib.loads(path.read_bytes())
        for index, plugin in enumerate(tree['plugins']):
            self.assertEqual(plugin['state'], b'opaque-assignments-'+bytes([index]))
            self.assertEqual(plugin['futureAssignmentField'], dict(retain=index))
        self.write('document.open', path=str(path))
        self.assertEqual(self.rack(), saved_rack)
        self.assertEqual(self.assignments(first)['assignments'], [dict(a, available=True) for a in expected])

    def test_stale_retained_window_and_document_reload_are_explicit(self):
        first, second = self.fixture()
        self.open()
        hwnd = self.window()
        self.choose(3003, 15)
        draft = self.local()['assignments']
        self.write('document.patch', title='Changed outside alias editor')
        before = self.doc()
        self.press(3007)
        self.assertEqual(self.doc(), before)
        self.assertTrue(self.local()['stale'])
        self.assertEqual(self.local()['assignments'], draft)
        self.press(3009)
        rack = private_desktop.user.GetDlgItem(self.hwnd, 300)
        self.desktop.send(rack, 0x186, 1)
        self.desktop.send(self.hwnd, 0x111, 300 | (1 << 16), rack)
        self.tick()
        self.command(323)
        self.assertEqual(self.window(), hwnd)
        self.assertEqual(self.local()['plugin'], first)
        self.assertEqual(self.local()['assignments'], draft)
        path = self.folder / 'different-document.screamseq'
        self.write('document.save', path=str(path))
        self.write('document.open', path=str(path))
        before = self.doc()
        self.press(3007)
        self.assertEqual(self.doc(), before)
        self.assertEqual(self.local()['assignments'], draft)
        self.press(3008)
        self.assertFalse(self.local()['dirty'])
        self.assertFalse(self.local()['stale'])
        self.assertEqual(self.local()['assignments'], [dict(instrument=1, channel=3)])
        self.choose(3003, 15)
        self.write('plugin.remove', slot=0)
        self.press(3008)  # Reload cannot replace a missing plugin with another.
        self.assertTrue(self.local()['dirty'])
        self.assertEqual(self.local()['plugin'], first)
        self.press(3010)  # Explicit discard remains available for a removed source.
        self.assertFalse(self.local()['dirty'])
        self.assertFalse(self.local()['visible'])
        self.tick()
        self.command(323)
        self.assertEqual(self.local()['plugin'], second)

    def test_keyboard_minimum_bounds_remove_and_effect_rejection(self):
        first, _ = self.fixture()
        self.open()
        self.desktop.send(self.control(3001), 0x100, 0x75)
        self.assertEqual(self.desktop.focus(self.window()), self.control(3002))
        self.desktop.send(self.control(3002), 0x100, 0x75)
        self.assertEqual(self.desktop.focus(self.window()), self.control(3001))
        self.desktop.send(self.control(3001), 0x100, 0x2E)
        self.assertEqual(self.local()['assignments'], [])
        self.press(3007)
        self.assertEqual(self.assignments(first)['assignments'], [])
        user = private_desktop.user
        user.SetWindowPos.argtypes = [wintypes.HWND, wintypes.HWND, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, wintypes.UINT]
        user.GetWindowRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
        scale = self.client.call('workspace.get')['data']['dpi']/96
        self.assertTrue(user.SetWindowPos(self.window(), None, 0, 0, int(620*scale), int(420*scale), 0x16))
        frame = wintypes.RECT()
        user.GetWindowRect(self.window(), ctypes.byref(frame))
        buttons = []
        for identifier in list(range(3001, 3011))+list(range(3100, 3103)):
            rect = wintypes.RECT()
            user.GetWindowRect(self.control(identifier), ctypes.byref(rect))
            self.assertGreater(rect.right, rect.left)
            self.assertGreaterEqual(rect.left, frame.left)
            self.assertLessEqual(rect.right, frame.right)
            self.assertLessEqual(rect.bottom, frame.bottom)
            if identifier in (3004, 3006, 3007, 3009, 3010):
                buttons.append((rect.left, rect.right))
        buttons.sort()
        for left, right in zip(buttons, buttons[1:]):
            self.assertLessEqual(left[1], right[0])
        self.add()
        effect = self.rack()[-1]['instanceID']
        before = self.doc()
        with self.assertRaises(ApiError):
            self.write('plugin.instruments.set', plugin=effect, assignments=[], dryRun=True)
        self.assertEqual(self.doc(), before)

    @unittest.skipUnless(os.environ.get('SCREAMSEQ_TEST_INSTRUMENT_CACHE'), 'opt-in installed VST3 instrument')
    def test_installed_surge_aliases_preserve_sound_and_plugin_lifecycle(self):
        descriptors = self.client.call('plugin.discover', {'format': 'VST3'})['data']
        surge = next(p for p in descriptors if p['name'] == 'Surge XT')
        self.write('plugin.add', descriptor=surge)
        plugin = self.rack()[0]['instanceID']
        instruments = [self.write('instrument.create', empty=True, name=name)['data']['instrument']
                       for name in ('Surge lead', 'Surge second part')]
        state = self.client.call('plugin.state.get', {'slot': 0})['data']['data']
        self.tick()
        self.open()
        for index, number in enumerate(instruments):
            self.press(3004)
            self.choose(3002, self.choices().index(number))
            self.choose(3003, index*8)
        routes = [dict(instrument=number, channel=index*8+1, available=True)
                  for index, number in enumerate(instruments)]
        self.press(3006)
        self.assertEqual(self.assignments(plugin)['assignments'], [])
        self.press(3007)
        self.assertEqual(self.assignments(plugin)['assignments'], routes)
        self.assertEqual(self.client.call('plugin.state.get', {'slot': 0})['data']['data'], state)
        path = self.folder / 'surge-shared-instruments.screamseq'
        self.write('document.save', path=str(path))
        self.write('document.open', path=str(path))
        self.assertEqual(self.assignments(plugin)['assignments'], routes)
        self.write('plugin.remove', slot=0)
        self.write('history.undo', domain='plugins')
        self.assertEqual(self.assignments(plugin)['assignments'], routes)
        self.assertEqual(self.client.call('plugin.state.get', {'slot': 0})['data']['data'], state)


if __name__ == '__main__':
    unittest.main()
