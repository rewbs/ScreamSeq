"""Retained native editor docking through the owned app, controls and API."""
import ctypes
from ctypes import wintypes as w
import time
import unittest

import private_desktop
import test_parameter_automation_ui as support
import test_workspace
from client import ApiError


user = private_desktop.user
user.EnumChildWindows.argtypes = [w.HWND, private_desktop.callback, w.LPARAM]
user.GetParent.argtypes = [w.HWND]
user.GetParent.restype = w.HWND
user.IsChild.argtypes = [w.HWND, w.HWND]
user.IsWindowVisible.argtypes = [w.HWND]
user.PostMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]


class WorkspaceDockingTests(unittest.TestCase):
    setUp = support.ParameterAutomationUITests.setUp
    doc = support.ParameterAutomationUITests.doc
    read = support.ParameterAutomationUITests.read
    write = support.ParameterAutomationUITests.write
    add_gain = support.ParameterAutomationUITests.add_gain
    navigate = test_workspace.WorkspaceTests.navigate
    resize_client = test_workspace.WorkspaceTests.resize_client

    def state(self):
        return self.read('workspace.get')

    def ready(self):
        deadline = time.monotonic() + 10
        quiet = None
        while time.monotonic() < deadline:
            state = self.state()
            pending = state['documentBusy'] or state['pendingViewCommands']
            pending |= any(state[key].get('pending', False)
                           for key in ('parameterAutomation', 'instrumentEnvelope',
                                       'graphEditor', 'mixerEditor', 'noteEditor'))
            if pending:
                quiet = None
            elif quiet is None:
                quiet = time.monotonic()
            # The curve's retained preview coalesces edits on a 120 ms timer.
            elif time.monotonic() - quiet >= .2:
                return state
            time.sleep(.01)
        self.fail('Docked editor did not settle')

    def native_window(self, kind='ScreamSeqWindowsDevelopment'):
        # A docked tool is a descendant; a floating tool is a top-level owned
        # window. Inspect both surfaces without depending on its placement.
        matches = set()
        @private_desktop.callback
        def collect(hwnd, _):
            owner = w.DWORD()
            user.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
            name = ctypes.create_unicode_buffer(100)
            user.GetClassNameW(hwnd, name, len(name))
            if owner.value == self.pid and name.value == kind:
                matches.add(hwnd)
            return True
        @private_desktop.callback
        def top(hwnd, unused):
            collect(hwnd, unused)
            user.EnumChildWindows(hwnd, collect, 0)
            return True
        private_desktop.check(user.EnumDesktopWindows(self.desktop.desktop, top, 0))
        self.assertEqual(len(matches), 1, (kind, matches))
        return matches.pop()

    def control(self, window, identifier):
        result = user.GetDlgItem(window, identifier)
        self.assertTrue(result, f'Missing editor control {identifier}')
        return result

    def command(self, identifier):
        self.ready()
        self.desktop.send(self.native_window(), 0x111, identifier)
        return self.ready()

    def press(self, window, identifier):
        self.desktop.send(window, 0x111, identifier, self.control(window, identifier))
        return self.ready()

    def field(self, window, identifier, value):
        control = self.control(window, identifier)
        text = ctypes.create_unicode_buffer(value)
        self.desktop.send(control, 0xC, 0, ctypes.addressof(text))
        return control

    def text(self, control):
        # GetWindowText only returns captions for another process; WM_GETTEXT
        # marshals the actual retained EDIT contents across the process boundary.
        text = ctypes.create_unicode_buffer(self.desktop.send(control, 0xE) + 1)
        self.desktop.send(control, 0xD, len(text), ctypes.addressof(text))
        return text.value

    def panel(self, identifier, **fields):
        self.client.call('workspace.panel', dict(panel=identifier, **fields))
        return self.ready()

    def focus_control(self, control):
        self.desktop.send(control, 0x201, 1, 4 | (4 << 16))
        self.desktop.send(control, 0x202, 0, 4 | (4 << 16))
        self.assertEqual(self.desktop.focus(control), control)

    def queued_key(self, window, key):
        # Exercise the real main/worker message pump, which must let a retained
        # tool handle its own keys before applying global workspace bindings.
        private_desktop.check(user.PostMessageW(window, 0x100, key, 0))
        private_desktop.check(user.PostMessageW(window, 0x101, key, 0))
        return self.ready()

    def automation(self):
        self.add_gain()
        self.command(502)
        tool = self.native_window('ScreamSeq.ParameterAutomation')
        parameters = self.control(tool, 4204)
        self.desktop.send(parameters, 0x186, 1)
        self.desktop.send(tool, 0x111, 4204 | (1 << 16), parameters)
        self.press(tool, 4212)  # Stage an ascending curve without applying it.
        return tool

    def assert_retained(self, kind, window, control, value, song):
        self.assertEqual(self.native_window(kind), window)
        self.assertTrue(user.IsChild(window, control))
        self.assertEqual(self.text(control), value)
        self.assertEqual(self.doc(), song)

    def test_automation_dock_float_hide_keep_hwnd_fields_and_captured_draft(self):
        window = self.automation()
        song = self.doc()
        field = self.field(window, 4207, '3.')
        captured = self.ready()['parameterAutomation']
        self.assertTrue(captured['fieldDraft'] and captured['dirty'])
        for placement in ('right', 'bottom', 'secondary', 'float', 'hide', 'right', 'float'):
            with self.subTest(placement=placement):
                state = self.panel('automation', placement=placement, focus=placement != 'hide')
                self.assert_retained('ScreamSeq.ParameterAutomation', window, field, '3.', song)
                current = state['parameterAutomation']
                for key in ('document', 'expectedRevision', 'patternID', 'plugin', 'parameter',
                            'points', 'dirty', 'fieldDraft', 'selectedPoint'):
                    self.assertEqual(current[key], captured[key], key)
                self.assertEqual(bool(user.IsWindowVisible(window)), placement != 'hide')
                if placement != 'hide':
                    self.assertEqual(bool(user.IsChild(self.native_window(), window)),
                                     placement in ('right', 'bottom', 'secondary'))

    def test_instrument_dock_float_retains_property_text_and_sound_target(self):
        self.write('instrument.create', sample=1)
        self.command(503)
        window = self.native_window('ScreamSeq.InstrumentEnvelope')
        field = self.field(window, 4439, 'Retained instrument draft')
        captured = self.ready()['instrumentEnvelope']
        self.assertTrue(captured['dirty'])
        self.assertTrue(captured['instrument'])
        song = self.doc()
        for width, placement in ((1600, 'right'), (1000, 'right'), (1600, 'float'), (1000, 'right')):
            with self.subTest(width=width, placement=placement):
                self.resize_client(width, 900)
                state = self.panel('instruments', placement=placement)
                self.assert_retained('ScreamSeq.InstrumentEnvelope', window, field,
                                     'Retained instrument draft', song)
                for key in ('document', 'expectedRevision', 'instrument', 'index', 'kind',
                            'dirty', 'envelope', 'mapping', 'selectedPoint'):
                    self.assertEqual(state['instrumentEnvelope'][key], captured[key], key)

    def test_regions_show_graph_and_responsive_tabs_keep_both_editors(self):
        window = self.automation()
        self.write('graph.create', name='Docked routing')
        self.command(430)
        song = self.doc()
        self.resize_client(1600, 900)
        state = self.panel('automation', placement='right')
        self.assertEqual(state['editorDock']['mode'], 'regions')
        self.assertTrue(state['editorDock']['trackerVisible'])
        self.assertTrue(state['graphEditor']['visible'] and state['parameterAutomation']['visible'])
        self.assertTrue(user.IsWindowVisible(self.control(self.native_window(), 431)))
        self.assertEqual(self.desktop.focus(window), self.native_window())
        self.assertEqual(state['focus'], 'graph')
        self.resize_client(1000, 760)
        state = self.ready()
        self.assertEqual(state['editorDock']['mode'], 'tabs')
        self.assertFalse(state['editorDock']['trackerVisible'])
        self.assertEqual(state['editorDock']['active'], 'automation')
        self.assertEqual(state['editorDock']['compactSelection'], 'graph')
        self.assertTrue(state['graphEditor']['visible'])
        self.assertFalse(state['parameterAutomation']['visible'])
        self.assertEqual(self.native_window('ScreamSeq.ParameterAutomation'), window)
        self.assertEqual(self.desktop.focus(window), self.native_window())
        self.assertEqual(state['focus'], 'graph')
        # An explicit tab selection, rather than resize, changes input owner.
        state = self.panel('automation', focus=True)
        self.assertTrue(state['parameterAutomation']['visible'])
        self.assertEqual(self.desktop.focus(window), window)
        self.assertEqual(state['focus'], 'automation')
        self.resize_client(1600, 900)
        state = self.ready()
        self.assertEqual(state['editorDock']['mode'], 'regions')
        self.assertTrue(state['graphEditor']['visible'])
        self.assertTrue(state['parameterAutomation']['visible'])
        self.assertEqual(self.doc(), song)

    def test_docked_local_keyboard_owns_f6_escape_and_pending_fields(self):
        window = self.automation()
        self.resize_client(1600, 900)
        self.panel('automation', placement='right')
        self.press(window, 4242)  # Compact Curve page; retained native controls.
        field = self.field(window, 4207, '3.')
        self.focus_control(field)
        song = self.doc()
        self.queued_key(field, 0x75)  # F6 toggles the local list/canvas focus.
        self.assertEqual(self.desktop.focus(window), window)
        self.assertTrue(self.state()['parameterAutomation']['fieldDraft'])
        state = self.queued_key(window, 0x1B)
        self.assertFalse(state['parameterAutomation']['fieldDraft'])
        self.assertTrue(state['parameterAutomation']['visible'])
        self.assertEqual(self.doc(), song)

    def test_placement_only_preserves_focus_and_hide_returns_to_tracker(self):
        self.resize_client(1600, 900)
        main = self.native_window()
        # Main-window mouse input places keyboard focus in the actual tracker.
        state = self.state()
        grid, scale = state['geometry']['pattern'], state['dpi'] / 96
        point = round((grid['x'] + 44) * scale) | (round((grid['y'] + grid['headerHeight'] + 6) * scale) << 16)
        self.desktop.send(main, 0x201, 1, point)
        self.desktop.send(main, 0x202, 0, point)
        before = self.desktop.focus(main)
        self.assertEqual(before, main)
        song = self.doc()
        context = self.read('context.get')
        for identifier in ('automation', 'instruments'):
            for placement in ('float', 'right'):
                with self.subTest(panel=identifier, placement=placement):
                    self.panel(identifier, placement=placement, focus=False)
                    self.assertEqual(self.desktop.focus(main), before)
                    self.assertEqual(self.read('context.get'), context)
        self.panel('automation', focus=True)
        window = self.native_window('ScreamSeq.ParameterAutomation')
        self.assertEqual(self.desktop.focus(window), window)
        state = self.panel('automation', placement='hide')
        self.assertEqual(self.desktop.focus(main), main)
        self.assertEqual(state['focus'], 'pattern')
        self.resize_client(1000, 760)
        self.command(532)
        state = self.panel('automation', placement='right', focus=False)
        self.assertTrue(state['editorDock']['trackerVisible'])
        self.assertFalse(user.IsWindowVisible(window))
        self.assertEqual(self.desktop.focus(main), main)
        self.panel('automation', focus=True)
        self.client.call('workspace.layout', {'name': 'Save custom', 'savedName': 'Active editor'})
        self.command(532)
        self.client.call('workspace.layout', {'name': 'Restore custom', 'savedName': 'Active editor'})
        state = self.ready()
        self.assertFalse(state['editorDock']['trackerVisible'])
        self.assertEqual(state['focus'], 'automation')
        self.assertTrue(user.IsWindowVisible(window))
        self.assertEqual(self.desktop.focus(window), window)
        self.press(window, 4241)
        search = self.control(window, 4203)
        self.focus_control(search)
        self.client.call('workspace.layout', {'name': 'Restore custom', 'savedName': 'Active editor'})
        self.ready()
        self.assertEqual(self.desktop.focus(window), search)
        self.command(104)  # Persistent Compose toolbar command normally focuses main.
        self.assertEqual(self.desktop.focus(window), window)
        toolbar = self.control(main, 104)
        self.desktop.send(toolbar, 0x201, 1, 4 | (4 << 16))
        try:
            self.assertEqual(self.desktop.focus(toolbar), toolbar)
            self.queued_key(toolbar, 0x2E)  # Delete belongs to no covered canvas.
            self.assertEqual(self.doc(), song)
        finally:
            self.desktop.send(toolbar, 0x202, 0, 4 | (4 << 16))
        covered = self.state()['geometry']['pattern']
        covered_point = round((covered['x'] + 44) * scale) | (round((covered['y'] + covered['headerHeight'] + 6) * scale) << 16)
        self.desktop.send(main, 0x201, 1, covered_point)
        self.desktop.send(main, 0x202, 0, covered_point)
        self.assertEqual(self.read('context.get'), context)
        self.assertEqual(self.doc(), song)

    def test_native_tabs_and_layout_restore_keep_both_editor_drafts(self):
        self.write('instrument.create', sample=1)
        automation = self.automation()
        # New editors follow by default. This persistence case deliberately
        # captures explicit pins, independently of saved placement preferences.
        self.panel('automation', pinned=True)
        curve_field = self.field(automation, 4207, '3.')
        self.command(503)
        instrument = self.native_window('ScreamSeq.InstrumentEnvelope')
        self.panel('instruments', pinned=True)
        name_field = self.field(instrument, 4439, 'Draft through dock tabs')
        song = self.doc()
        self.resize_client(1000, 760)
        for command, identifier in ((530, 'automation'), (531, 'instruments'),
                                    (533, 'automation'), (534, 'instruments')):
            with self.subTest(command=command):
                state = self.command(command)
                self.assertEqual(state['editorDock']['mode'], 'tabs')
                self.assertEqual(state['editorDock']['active'], identifier)
                self.assertFalse(state['editorDock']['trackerVisible'])
                self.assert_retained('ScreamSeq.ParameterAutomation', automation,
                                     curve_field, '3.', song)
                self.assert_retained('ScreamSeq.InstrumentEnvelope', instrument,
                                     name_field, 'Draft through dock tabs', song)
        self.client.call('workspace.layout', {'name': 'Save custom', 'savedName': 'Both editors'})
        state = self.panel('automation', pinned=False)
        self.assertFalse(state['pins']['automation'])
        self.assertTrue(state['pins']['instruments'])
        self.assertEqual(state['editorDock']['active'], 'instruments')
        origins = state['returnPoints']
        self.command(535)  # Float the active instrument using its dock header.
        self.panel('automation', placement='hide')
        self.client.call('workspace.layout', {'name': 'Restore custom', 'savedName': 'Both editors'})
        state = self.ready()
        self.assertEqual(state['locations']['automation'], 'right')
        self.assertEqual(state['locations']['instruments'], 'right')
        self.assertEqual(state['editorDock']['active'], 'instruments')
        self.assertFalse(state['pins']['automation'])
        self.assertTrue(state['pins']['instruments'])
        self.assertEqual(state['returnPoints'], origins)
        state = self.command(532)  # Tracker tab hides both retained editor HWNDs.
        self.assertTrue(state['editorDock']['trackerVisible'])
        self.assertFalse(user.IsWindowVisible(automation) or user.IsWindowVisible(instrument))
        self.command(533)
        self.assert_retained('ScreamSeq.ParameterAutomation', automation, curve_field, '3.', song)
        self.assert_retained('ScreamSeq.InstrumentEnvelope', instrument,
                             name_field, 'Draft through dock tabs', song)

    def test_pin_follow_and_return_keep_dirty_target_and_opening_location(self):
        self.add_gain()
        other = self.write('pattern.create', rows=64)['pattern']
        self.navigate(pattern=0, row=5, channel=1, following=False)
        self.resize_client(1600, 900)
        self.command(502)
        window = self.native_window('ScreamSeq.ParameterAutomation')
        state = self.panel('automation', placement='right', pinned=True)
        origin = state['returnPoints']['automation']
        self.assertTrue(state['pins']['automation'])
        # Returning from compact tabs at the unchanged opening cursor must
        # still update native visibility; navigation itself has nothing to do.
        self.resize_client(1000, 760)
        context = self.read('context.get')
        state = self.panel('automation', **{'return': True})
        self.assertTrue(state['editorDock']['trackerVisible'])
        self.assertFalse(user.IsWindowVisible(window))
        self.assertEqual(state['focus'], 'pattern')
        self.assertEqual(self.desktop.focus(window), self.native_window())
        self.assertEqual(self.read('context.get'), context)
        self.command(533)
        self.resize_client(1600, 900)
        self.client.call('workspace.layout', {'name': 'Save custom', 'savedName': 'Opening pattern'})
        self.navigate(pattern=other, row=18)
        self.assertEqual(self.state()['parameterAutomation']['pattern'], 0)
        state = self.panel('automation', pinned=False)
        self.assertEqual(state['parameterAutomation']['pattern'], other)
        self.assertFalse(state['pins']['automation'])
        self.assertEqual(state['returnPoints']['automation'], origin)
        self.press(window, 4212)
        field = self.field(window, 4207, '3.')
        draft = self.state()['parameterAutomation']
        self.navigate(pattern=0, row=29)
        state = self.panel('automation', follow=True)
        self.assertEqual(state['parameterAutomation']['pattern'], other)
        self.assertEqual(state['parameterAutomation']['points'], draft['points'])
        self.assertEqual(self.text(field), '3.')
        song = self.doc()
        self.client.call('workspace.layout', {'name': 'Restore custom', 'savedName': 'Opening pattern'})
        state = self.ready()
        self.assertFalse(state['pins']['automation'])
        self.assertEqual(state['parameterAutomation']['pattern'], other)
        self.assertEqual(state['parameterAutomation']['points'], draft['points'])
        self.assertEqual(self.text(field), '3.')
        state = self.panel('automation', **{'return': True})
        cursor = self.read('context.get')
        self.assertEqual((cursor['pattern'], cursor['row'], cursor['channel']), (0, 5, 1))
        self.assertEqual(state['returnPoints']['automation'], origin)
        self.assertEqual(self.doc(), song)

    def test_return_rejects_removed_opening_pattern_before_placement_changes(self):
        self.add_gain()
        opening_pattern = self.write('pattern.create', rows=64)['pattern']
        self.navigate(pattern=opening_pattern, row=12)
        self.panel('automation', placement='right', pinned=True)
        # Undo removes the newly created pattern while leaving the document
        # identity active; a reused index must never become a return target.
        self.write('history.undo', domain='document')
        for reused_index in (False, True):
            with self.subTest(reused_index=reused_index):
                if reused_index:
                    self.assertEqual(self.write('pattern.create', rows=64)['pattern'], opening_pattern)
                before = self.ready()
                song = self.doc()
                context = self.read('context.get')
                with self.assertRaises(ApiError) as error:
                    self.client.call('workspace.panel', {
                        'panel': 'automation', 'placement': 'float', 'return': True})
                self.assertEqual(error.exception.code, -32602)
                self.assertIn('opening pattern', str(error.exception))
                after = self.ready()
                self.assertEqual(after['locations'], before['locations'])
                self.assertEqual(after['returnPoints'], before['returnPoints'])
                self.assertEqual(self.read('context.get'), context)
                self.assertEqual(self.doc(), song)

    def test_invalid_dock_requests_preserve_existing_placement_and_song(self):
        self.panel('automation', placement='right')
        song = self.doc()
        for fields in (dict(placement='left'), dict(placement=True), dict(pinned=1),
                       dict(focus='yes'), dict(unexpected=True)):
            with self.subTest(fields=fields):
                before = self.ready()
                with self.assertRaises(ApiError) as error:
                    self.client.call('workspace.panel', dict(panel='automation', **fields))
                self.assertEqual(error.exception.code, -32602)
                after = self.ready()
                self.assertEqual(after['editorDock'], before['editorDock'])
                self.assertEqual(after['locations'], before['locations'])
                self.assertEqual(self.doc(), song)


if __name__ == '__main__':
    unittest.main()
