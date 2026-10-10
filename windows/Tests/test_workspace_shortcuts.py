"""Configurable native keys and API, with owned desktops and private preferences."""
import ctypes
from ctypes import wintypes as w
import os
import time
import unittest

import graph_curve_native_support as curve_native
import private_desktop
import test_formula_workbench as formula
import test_workspace_docking as docking
from client import ApiError, Client, TransportError


user = private_desktop.user
user.AttachThreadInput.argtypes = [w.DWORD, w.DWORD, w.BOOL]
user.GetKeyboardState.argtypes = [ctypes.POINTER(ctypes.c_ubyte)]
user.SetKeyboardState.argtypes = [ctypes.POINTER(ctypes.c_ubyte)]
user.PostMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
user.GetClientRect.argtypes = [w.HWND, ctypes.POINTER(w.RECT)]
user.GetWindowRect.argtypes = [w.HWND, ctypes.POINTER(w.RECT)]
user.SetWindowPos.argtypes = [w.HWND, w.HWND, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, w.UINT]


class WorkspaceShortcutTests(unittest.TestCase):
    setUp = docking.WorkspaceDockingTests.setUp
    doc = docking.WorkspaceDockingTests.doc
    read = docking.WorkspaceDockingTests.read
    write = docking.WorkspaceDockingTests.write
    add_gain = docking.WorkspaceDockingTests.add_gain
    navigate = docking.WorkspaceDockingTests.navigate
    state = docking.WorkspaceDockingTests.state
    ready = docking.WorkspaceDockingTests.ready
    native_window = docking.WorkspaceDockingTests.native_window
    control = docking.WorkspaceDockingTests.control
    command = docking.WorkspaceDockingTests.command
    press = docking.WorkspaceDockingTests.press
    field = docking.WorkspaceDockingTests.field
    text = docking.WorkspaceDockingTests.text
    focus_control = docking.WorkspaceDockingTests.focus_control
    automation = docking.WorkspaceDockingTests.automation
    workbench = formula.FormulaWorkbenchTests.workbench
    wait_preview = formula.FormulaWorkbenchTests.wait_preview

    def catalogue(self):
        return {item['id']: item for item in self.read('workspace.commands.get')['commands']}

    def binding(self, identifier):
        return self.catalogue()[f'windows.command.{identifier}']

    def bind(self, identifier, keys):
        before = self.doc()
        response = self.client.call('workspace.shortcut.set', dict(command=f'windows.command.{identifier}', keys=keys))
        self.assertFalse(response['changed'])
        self.assertFalse(response['playbackStopped'])
        self.assertEqual(response['revision'], before['revision'])
        self.assertEqual(self.doc(), before)
        return response['data']

    def key(self, key, *, hwnd=None, ctrl=False, alt=False, shift=False, altgr=False, repeat=False):
        """Queue to the real app pump, modifying only the owned attached queue."""
        hwnd = hwnd or self.desktop.focus(self.native_window()) or self.native_window()
        caller = private_desktop.kernel.GetCurrentThreadId()
        target = user.GetWindowThreadProcessId(hwnd, None)
        private_desktop.check(user.AttachThreadInput(caller, target, True))
        original = (ctypes.c_ubyte * 256)()
        try:
            private_desktop.check(user.GetKeyboardState(original))
            state = (ctypes.c_ubyte * 256).from_buffer_copy(original)
            for modifier in (0x10, 0x11, 0x12, 0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5):
                state[modifier] = 0
            if ctrl or altgr:
                state[0x11] = state[0xA2] = 0x80
            if alt or altgr:
                state[0x12] = state[0xA5 if altgr else 0xA4] = 0x80
            if shift:
                state[0x10] = state[0xA0] = 0x80
            private_desktop.check(user.SetKeyboardState(state))
            down, up = (0x104, 0x105) if alt or altgr else (0x100, 0x101)
            private_desktop.check(user.PostMessageW(hwnd, down, key, 1 | ((1 << 30) if repeat else 0)))
            private_desktop.check(user.PostMessageW(hwnd, up, key, 0xC0000001))
            # API work is posted to that same UI queue after the key messages;
            # waiting for idle also drains generated WM_CHAR notifications.
            return self.ready()
        finally:
            private_desktop.check(user.SetKeyboardState(original))
            private_desktop.check(user.AttachThreadInput(caller, target, False))

    def focus_pattern(self):
        state = self.ready()
        rect, scale = state['geometry']['pattern'], state['dpi'] / 96
        x = round((rect['x'] + 44) * scale)
        y = round((rect['y'] + rect['headerHeight'] + 6) * scale)
        self.desktop.send(self.native_window(), 0x201, 1, x | (y << 16))
        self.desktop.send(self.native_window(), 0x202, 0, x | (y << 16))
        self.assertEqual(self.desktop.focus(self.native_window()), self.native_window())

    def pending(self, value):
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            state = self.state()['shortcuts']
            if state['pending'] == value:
                return state
            time.sleep(.02)
        self.fail(f'Shortcut pending state did not become {value}: {state}')

    def test_pattern_select_all_keeps_cursor_view_and_native_text_ownership(self):
        self.focus_pattern()
        self.navigate(row=8, channel=1, column=0, following=False)
        before, context = self.doc(), self.read('context.get')
        viewport = self.state()['viewport']
        pattern = self.read('pattern.get', pattern=context['pattern'], rowCount=1, channelCount=1)
        self.key(ord('A'), ctrl=True)
        selected = self.read('context.get')
        self.assertEqual(selected['selection'], dict(startRow=0, endRow=pattern['rows'] - 1,
                                                     startChannel=0, endChannel=before['data']['channels'] - 1))
        for key in ('pattern', 'row', 'channel', 'column', 'following'):
            self.assertEqual(selected[key], context[key], key)
        self.assertEqual(self.doc(), before)
        self.assertEqual(self.state()['viewport'], viewport)
        self.key(ord('A'), ctrl=True)
        self.assertEqual(self.read('context.get')['contextRevision'], selected['contextRevision'])
        self.key(0x28)  # Down starts ordinary cursor navigation again.
        moved = self.read('context.get')
        self.assertEqual(moved['selection'], dict(startRow=9, endRow=9, startChannel=1, endChannel=1))
        self.command(584)
        tool = self.native_window('ScreamSeq.CreateNoteTrack')
        field = self.field(tool, 7901, 'Keep this native draft')
        self.focus_control(field)
        self.desktop.send(field, 0xB1, 4, 4)
        context = self.read('context.get')
        self.key(ord('A'), hwnd=field, ctrl=True)
        span = self.desktop.send(field, 0xB0)
        self.assertEqual((span & 0xFFFF, (span >> 16) & 0xFFFF), (0, len('Keep this native draft')))
        self.assertEqual(self.read('context.get'), context)
        self.assertEqual(self.text(field), 'Keep this native draft')
        self.assertEqual(self.doc(), before)

    def test_api_catalogue_validation_conflicts_and_atomic_presentation_only_edits(self):
        catalogue = self.catalogue()
        self.assertGreater(len(catalogue), 80)
        for identifier, keys in ((112, ['ctrl+k']), (513, ['ctrl+alt+w']), (507, ['ctrl+alt+l']),
                                 (526, ['ctrl+j']), (545, ['space']), (102, ['escape']),
                                 (540, ['ctrl+c']), (541, ['ctrl+v']), (543, ['delete']),
                                 (587, ['shift+space']), (588, ['ctrl+space']), (589, ['ctrl+shift+space'])):
            entry = catalogue[f'windows.command.{identifier}']
            self.assertEqual(entry['keys'], keys)
            self.assertEqual(entry['defaults'], keys)
            self.assertFalse(entry['customized'])
            self.assertIsInstance(entry['name'], str)
            self.assertIsInstance(entry['contextHint'], str)
        before = self.doc()
        invalid = [dict(command='missing', keys=['ctrl+q']), dict(command=526, keys=['ctrl+q']),
                   dict(command='windows.command.526', keys='ctrl+q'),
                   dict(command='windows.command.526', keys=[True]),
                   dict(command='windows.command.526', keys=['q']),
                   dict(command='windows.command.526', keys=['shift+q']),
                   dict(command='windows.command.526', keys=['cmd+q']),
                   dict(command='windows.command.526', keys=['ctrl+ctrl+q']),
                   dict(command='windows.command.526', keys=['ctrl+unknown']),
                   dict(command='windows.command.526', keys=['ctrl+q'] * 5),
                   dict(command='windows.command.526', keys=['ctrl+k']),
                   dict(command='windows.command.526', keys=['ctrl+k', 'x']),
                   dict(command='windows.command.526', keys=['ctrl+alt+q', 'escape']),
                   dict(command='windows.command.526', keys=['ctrl+alt+q', 'ctrl+esc']),
                   dict(command='windows.command.526', keys=[], extra=True)]
        for fields in invalid:
            with self.subTest(fields=fields):
                with self.assertRaises(ApiError):
                    self.client.call('workspace.shortcut.set', fields)
                self.assertEqual(self.catalogue(), catalogue)
                self.assertEqual(self.doc(), before)
        response = self.bind(526, ['ALT+CTRL+Q', 'SHIFT+Enter', 'PGUP', 'PGDN'])
        self.assertEqual(response['keys'], ['ctrl+alt+q', 'shift+return', 'pageup', 'pagedown'])
        self.assertTrue(self.binding(526)['customized'])
        changed = self.catalogue()
        for conflicting in (['ctrl+alt+q'], ['ctrl+alt+q', 'shift+return']):
            with self.assertRaises(ApiError):
                self.client.call('workspace.shortcut.set', dict(command='windows.command.507', keys=conflicting))
            self.assertEqual(self.catalogue(), changed)
        self.bind(526, [])
        self.assertEqual(self.binding(526)['keys'], [])
        self.assertTrue(self.binding(526)['customized'])
        self.bind(526, ['ctrl+j'])
        self.assertFalse(self.binding(526)['customized'])
        for identifier in (545, 102, 114, 103, 543):
            with self.subTest(default_roundtrip=identifier):
                default = self.binding(identifier)['defaults']
                self.bind(identifier, ['ctrl+alt+q'])
                self.assertTrue(self.binding(identifier)['customized'])
                self.bind(identifier, default)
                self.assertEqual(self.binding(identifier)['keys'], default)
                self.assertFalse(self.binding(identifier)['customized'])
        self.assertIn('reload saved shortcuts', self.binding(547)['name'].lower())
        self.bind(526, ['ctrl+alt+q'])
        retained = self.catalogue()
        self.command(547)
        self.assertEqual(self.catalogue(), retained)  # Inspection reload preserves its memory-only overrides.
        self.assertEqual(self.doc(), before)
        with self.assertRaises(ApiError):
            self.client.call('workspace.commands.get', dict(extra=True))

    def test_remap_clear_old_binding_and_key_repeat(self):
        self.focus_pattern()
        before = self.doc()
        self.bind(526, ['ctrl+alt+q'])
        visible = self.state()['lowerVisible']
        self.key(ord('J'), ctrl=True)
        self.assertEqual(self.state()['lowerVisible'], visible)
        self.key(ord('Q'), ctrl=True, alt=True)
        self.assertEqual(self.state()['lowerVisible'], not visible)
        self.key(ord('Q'), ctrl=True, alt=True, repeat=True)
        self.assertEqual(self.state()['lowerVisible'], not visible)
        self.key(ord('Q'), ctrl=True, alt=True)
        self.assertEqual(self.state()['lowerVisible'], visible)
        self.bind(526, [])
        self.key(ord('Q'), ctrl=True, alt=True)
        self.key(ord('J'), ctrl=True)
        self.assertEqual(self.state()['lowerVisible'], visible)
        self.assertEqual(self.doc(), before)

    def test_sequences_cancel_on_escape_timeout_context_document_and_focus(self):
        self.focus_pattern()
        self.bind(526, ['ctrl+alt+q', 'ctrl+alt+r'])
        before, visible = self.doc(), self.state()['lowerVisible']
        self.key(ord('Q'), ctrl=True, alt=True)
        self.assertIn('ctrl+alt+q', self.pending(True)['hint'])
        self.key(ord('R'), ctrl=True, alt=True)
        self.pending(False)
        self.assertEqual(self.state()['lowerVisible'], not visible)
        self.key(ord('Q'), ctrl=True, alt=True)
        self.key(ord('R'), ctrl=True, alt=True)
        self.assertEqual(self.state()['lowerVisible'], visible)
        self.assertEqual(self.doc(), before)
        for cause in ('escape', 'timeout', 'context', 'document', 'focus'):
            with self.subTest(cause=cause):
                self.focus_pattern()
                self.key(ord('Q'), ctrl=True, alt=True)
                self.pending(True)
                if cause == 'escape':
                    self.key(0x1B)
                elif cause == 'timeout':
                    time.sleep(1.65)
                elif cause == 'context':
                    self.navigate(row=7, following=False)
                elif cause == 'document':
                    self.write('document.patch', title='Shortcut context changed')
                else:
                    self.command(108)
                    self.focus_control(self.control(self.native_window(), 230))
                self.pending(False)
                after = self.doc()
                self.key(ord('R'), ctrl=True, alt=True)
                self.assertEqual(self.state()['lowerVisible'], visible)
                self.assertEqual(self.doc(), after)
        self.focus_pattern()
        self.bind(526, ['ctrl+alt+q', 'ctrl+alt+r', 'ctrl+alt+t', 'ctrl+alt+y'])
        for key in 'QRT':
            self.key(ord(key), ctrl=True, alt=True)
            self.pending(True)
            self.assertEqual(self.state()['lowerVisible'], visible)
        self.key(ord('Y'), ctrl=True, alt=True)
        self.assertEqual(self.state()['lowerVisible'], not visible)
        self.pending(False)

    def test_native_text_undo_copy_altgr_and_pending_sequence_ownership(self):
        self.command(108)
        edit = self.field(self.native_window(), 230, '2')
        self.focus_control(edit)
        before, visible = self.doc(), self.state()['lowerVisible']
        self.bind(540, [])
        self.bind(526, ['ctrl+c'])
        # A collapsed native selection exercises Copy ownership without touching
        # the user's clipboard. Inspection pattern copy is independently private.
        self.desktop.send(edit, 0xB1, 1, 1)
        sequence = user.GetClipboardSequenceNumber()
        self.key(ord('C'), hwnd=edit, ctrl=True)
        self.assertEqual(self.state()['lowerVisible'], visible)
        self.assertEqual(user.GetClipboardSequenceNumber(), sequence)
        self.bind(122, [])
        self.bind(526, ['ctrl+z'])
        text = ctypes.create_unicode_buffer('3')
        self.desktop.send(edit, 0xC2, 1, ctypes.addressof(text))  # EM_REPLACESEL with Undo.
        self.assertEqual(self.text(edit), '23')
        self.key(ord('Z'), hwnd=edit, ctrl=True)
        self.assertEqual(self.text(edit), '2')
        self.assertEqual(self.state()['lowerVisible'], visible)
        self.assertEqual(self.doc(), before)
        self.bind(526, ['ctrl+alt+q'])
        self.key(ord('Q'), hwnd=edit, altgr=True)
        self.assertEqual(self.state()['lowerVisible'], visible)
        self.assertEqual(self.doc(), before)
        self.bind(526, ['ctrl+alt+q', 'ctrl+alt+m'])
        self.key(ord('Q'), hwnd=edit, ctrl=True, alt=True)
        self.pending(True)
        self.key(ord('E'), hwnd=edit, altgr=True)
        self.pending(False)
        self.key(ord('M'), hwnd=edit, ctrl=True, alt=True)
        self.assertEqual(self.state()['lowerVisible'], visible)
        self.assertEqual(self.doc(), before)
        # Once a sequence has begun, its next stroke wins over a native Ctrl+C.
        self.bind(526, ['ctrl+alt+q', 'ctrl+c'])
        self.key(ord('Q'), hwnd=edit, ctrl=True, alt=True)
        self.pending(True)
        self.key(ord('C'), hwnd=edit, ctrl=True)
        self.assertEqual(self.state()['lowerVisible'], not visible)
        self.assertEqual(user.GetClipboardSequenceNumber(), sequence)
        self.assertEqual(self.doc(), before)

    def test_rich_edit_formula_owns_text_keys_and_configured_native_navigation(self):
        # The same retained graph-point/workbench fixture as the formula suite,
        # using queued keys through the shared shortcut dispatcher this time.
        graph = self.write('graph.create')['graph']
        self.write('graph.node.add', graph=graph, kind='automation')
        self.command(430)
        for identifier, index in ((442, 2), (443, 5)):
            control = self.control(self.native_window(), identifier)
            self.desktop.send(control, 0x14E, index)
            self.desktop.send(self.native_window(), 0x111, identifier | (1 << 16), control)
            self.ready()
        self.command(490)
        curve = self.native_window('ScreamSeq.GraphCurve')
        # Reuse bounded read/control readiness without changing this fixture's
        # Main-window helpers or inheriting another suite's test methods.
        def curve_ready(control=None):
            return curve_native.GraphCurveNativeMixin.curve_api_idle(self, control)
        page = self.control(curve, 9101)
        curve_ready(page)
        self.desktop.send(page, 0xF5)  # BM_CLICK: retained Formula page.
        self.assertEqual(self.state()['graphCurve']['page'], 'formula')
        shape = self.control(curve, 481)
        curve_ready(shape)
        self.desktop.send(shape, 0x14E, 8)
        self.desktop.send(curve, 0x111, 481 | (1 << 16), shape)
        curve_ready(self.control(curve, 485))
        self.field(curve, 485, 'mix(start,end,t)')
        expand = self.control(curve, 498)
        curve_ready(expand)
        self.desktop.send(expand, 0xF5)  # Open this curve's retained workbench once.
        self.assertTrue(self.wait_preview()['valid'])
        tool = self.native_window('ScreamSeq.FormulaWorkbench')
        code = self.field(tool, 2001, '1+2')
        name = ctypes.create_unicode_buffer(100)
        user.GetClassNameW(code, name, len(name))
        self.assertIn('richedit', name.value.lower())
        self.focus_control(code)
        self.desktop.send(code, 0xB1, 3, 3)
        document, context = self.doc(), self.read('context.get')
        transport = self.read('transport.get')

        def unchanged():
            self.assertEqual(self.doc(), document)
            self.assertEqual(self.read('context.get')['following'], context['following'])
            current = self.read('transport.get')
            self.assertEqual((current['playing'], current['audioActive']),
                             (transport['playing'], transport['audioActive']))
            self.assertEqual(self.desktop.focus(tool), code)

        self.key(0x20, hwnd=code)
        self.assertEqual(self.workbench()['source'], '1+2 ')
        unchanged()
        self.key(ord('F'), hwnd=code)
        self.assertEqual(self.workbench()['source'], '1+2 f')
        unchanged()
        self.desktop.send(code, 0x458)  # EM_STOPGROUPTYPING separates Delete from earlier typing.
        self.desktop.send(code, 0xB1, 0, 1)
        self.key(0x2E, hwnd=code)
        self.assertEqual(self.workbench()['source'], '+2 f')
        unchanged()
        self.key(ord('Z'), hwnd=code, ctrl=True)
        self.assertEqual(self.workbench()['source'], '1+2 f')
        unchanged()
        self.field(tool, 2001, '1+\r\n2+\r\n3')
        source = self.workbench()['source']
        visible = self.state()['lowerVisible']
        clipboard = user.GetClipboardSequenceNumber()
        for encoded, key, at in (('ctrl+insert', 0x2D, len(source)),
                                 ('ctrl+up', 0x26, len(source)), ('ctrl+down', 0x28, 0)):
            with self.subTest(native_key=encoded):
                self.bind(526, [encoded])
                self.desktop.send(code, 0xB1, at, at)  # Empty selection makes Copy a clipboard no-op.
                self.key(key, hwnd=code, ctrl=True)
                self.assertEqual(self.state()['lowerVisible'], visible)
                self.assertEqual(self.workbench()['source'], source)
                self.assertEqual(user.GetClipboardSequenceNumber(), clipboard)
                if key != 0x2D:
                    self.assertNotEqual(self.desktop.send(code, 0xB0) & 0xFFFF, at)
                unchanged()

    def test_retained_editor_global_bindings_preserve_focus_and_raw_draft(self):
        self.write('pattern.apply', cells=[dict(pattern=0, row=0, channel=0, note=61),
                                           dict(pattern=0, row=8, channel=0, note=0)])
        self.focus_pattern()
        self.command(124)  # Inspection's private pattern clipboard.
        self.navigate(row=8, following=False)
        tool = self.automation()
        edit = self.field(tool, 4207, '3.')
        self.focus_control(edit)
        before = self.doc()
        points = self.state()['parameterAutomation']['points']
        enabled = self.state()['liveKeyboard']
        self.key(ord('L'), hwnd=edit, ctrl=True, alt=True)
        self.assertEqual(self.state()['liveKeyboard'], not enabled)
        self.assertEqual(self.desktop.focus(tool), edit)
        self.assertEqual(self.text(edit), '3.')
        self.bind(507, ['ctrl+alt+q'])
        self.key(ord('L'), hwnd=edit, ctrl=True, alt=True)
        self.assertEqual(self.state()['liveKeyboard'], not enabled)
        self.key(ord('Q'), hwnd=edit, ctrl=True, alt=True)
        self.assertEqual(self.state()['liveKeyboard'], enabled)
        self.assertEqual(self.desktop.focus(tool), edit)
        self.assertEqual(self.text(edit), '3.')
        self.assertEqual(self.state()['parameterAutomation']['points'], points)
        self.assertTrue(self.state()['parameterAutomation']['fieldDraft'])
        self.assertEqual(self.doc(), before)
        # TranslateMessage also queues ':' for Shift+semicolon. Completing the
        # command must consume that character before the retained EDIT sees it.
        self.bind(507, ['ctrl+alt+q', 'shift+;'])
        self.key(ord('Q'), hwnd=edit, ctrl=True, alt=True)
        self.pending(True)
        self.key(0xBA, hwnd=edit, shift=True)
        self.assertEqual(self.state()['liveKeyboard'], not enabled)
        self.assertEqual(self.text(edit), '3.')
        self.assertEqual(self.desktop.focus(tool), edit)
        self.assertEqual(self.doc(), before)
        # A floating editor's own canvas must not use stale main-workspace
        # focus to paste or delete cells in the covered tracker.
        client, frame = w.RECT(), w.RECT()
        user.GetClientRect(tool, ctypes.byref(client))
        user.GetWindowRect(tool, ctypes.byref(frame))
        scale = self.state()['dpi'] / 96
        private_desktop.check(user.SetWindowPos(tool, None, 0, 0,
            round(440 * scale) + frame.right - frame.left - client.right,
            round(500 * scale) + frame.bottom - frame.top - client.bottom, 0x16))
        self.press(tool, 4242)
        self.assertEqual(self.desktop.focus(tool), tool)
        self.key(ord('V'), hwnd=tool, ctrl=True)
        self.assertIn('Focus the tracker or sample', self.state()['status'])
        self.assertEqual(self.doc(), before)
        self.assertEqual(self.desktop.focus(tool), tool)
        self.bind(543, ['ctrl+alt+u'])
        self.key(ord('U'), hwnd=tool, ctrl=True, alt=True)
        self.assertIn('Focus the tracker or sample', self.state()['status'])
        self.assertEqual(self.doc(), before)
        self.assertEqual(self.state()['parameterAutomation']['points'], points)
        self.assertEqual(self.text(edit), '3.')
        # Running the same global action through the palette must restore its
        # original editor field just as the configurable shortcut does.
        self.focus_control(edit)
        enabled = self.state()['liveKeyboard']
        self.key(ord('K'), hwnd=edit, ctrl=True)
        palette = self.native_window('ScreamSeqCommands')
        self.field(palette, 101, 'Toggle live musical typing')
        self.assertEqual(self.desktop.send(self.control(palette, 102), 0x18B), 1)
        self.desktop.send(palette, 0x111, 103, self.control(palette, 103))
        self.ready()
        self.assertEqual(self.state()['liveKeyboard'], not enabled)
        self.assertEqual(self.desktop.focus(tool), edit)
        self.assertEqual(self.text(edit), '3.')
        self.assertEqual(self.doc(), before)

    @unittest.skipUnless(os.environ.get('SCREAMSEQ_TEST_LIVE_AUDIO') == '1', 'requires the owned silent audio fixture')
    def test_shortcut_changes_and_remapped_transport_preserve_playback_song_and_editor(self):
        self.add_gain()
        path = self.folder / 'shortcut-playback.screamseq'
        self.write('document.save', path=str(path))
        # audioTest keeps shortcut preferences in memory, just as inspection
        # does. This owned process never changes the musician's preference file.
        self.pid = self.desktop.launch([os.environ['SCREAMSEQ_TEST_EXE'], '--audio-test-silent',
                                        '--audio-test-allow-stop', '--automation', '--seconds', '120',
                                        '--project', str(path), '--vst3-test-cache', str(self.cache)])
        self.client = Client(r'\\.\pipe\ScreamSeq.Api.' + str(self.pid), timeout=20)
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            try:
                transport = self.read('transport.get')
                if transport['playing'] and transport['audioActive'] and not self.state()['documentBusy']:
                    break
            except TransportError:
                pass
            process = self.desktop.processes[-1]
            code = w.DWORD()
            private_desktop.check(private_desktop.kernel.GetExitCodeProcess(process.hProcess, ctypes.byref(code)))
            self.assertEqual(code.value, 259, f'Silent shortcut app exited: {code.value:#x}')
            time.sleep(.1)
        else:
            self.fail('Owned silent shortcut fixture did not start playing')
        self.command(502)
        tool = self.native_window('ScreamSeq.ParameterAutomation')
        parameters = self.control(tool, 4204)
        self.desktop.send(parameters, 0x186, 1)
        self.desktop.send(tool, 0x111, 4204 | (1 << 16), parameters)
        self.desktop.send(tool, 0x111, 4212, self.control(tool, 4212))
        self.ready()
        edit = self.field(tool, 4207, '3.')
        self.focus_control(edit)
        before = self.doc()
        self.bind(545, ['ctrl+alt+p'])
        self.assertTrue(self.read('transport.get')['playing'])
        self.assertTrue(self.read('transport.get')['audioActive'])
        self.bind(526, ['ctrl+alt+g', 'ctrl+alt+h'])
        self.key(ord('G'), hwnd=edit, ctrl=True, alt=True)
        self.pending(True)
        self.key(0x1B, hwnd=edit)
        self.pending(False)
        self.assertTrue(self.read('transport.get')['playing'])
        self.assertEqual(self.text(edit), '3.')
        for playing in (False, True, False):
            self.key(ord('P'), hwnd=edit, ctrl=True, alt=True)
            transport = self.read('transport.get')
            self.assertEqual(transport['playing'], playing)
            if playing:
                self.assertTrue(transport['audioActive'])
            self.assertEqual(self.desktop.focus(tool), edit)
            self.assertEqual(self.text(edit), '3.')
            self.assertEqual(self.doc(), before)


if __name__ == '__main__':
    unittest.main()
