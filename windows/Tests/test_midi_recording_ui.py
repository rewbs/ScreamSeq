"""Retained MIDI settings and recovered take review through actual private HWNDs."""
import ctypes
from ctypes import wintypes
import plistlib
import time
import unittest

import private_desktop
import test_parameter_compact_ui as compact
import test_recording as recording


class MidiRecordingUITests(unittest.TestCase):
    setUp = recording.RecordingTests.setUp
    launch = recording.RecordingTests.launch
    doc = recording.RecordingTests.doc
    read = recording.RecordingTests.read
    write = recording.RecordingTests.write
    settings = recording.RecordingTests.settings
    configure = recording.RecordingTests.configure
    command = recording.RecordingTests.command
    window = recording.RecordingTests.tool
    resize_client = compact.ParameterCompactUITests.resize_client
    assert_visible_geometry = compact.ParameterCompactUITests.assert_visible_geometry
    modified_key = compact.ParameterCompactUITests.modified_key

    def control(self, identifier):
        result = private_desktop.user.GetDlgItem(self.window(), identifier)
        self.assertTrue(result, identifier)
        return result

    def state(self): return self.read('workspace.get')['midiWindow']

    def idle(self):
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            state = self.read('workspace.get')
            if not state['documentBusy'] and not state['midiWindow'].get('pending'):
                return
            time.sleep(.03)
        self.fail(str(state))

    def start(self):
        self.command(549)
        self.idle()
        self.assertTrue(self.state()['visible'])

    def press(self, identifier):
        self.idle()
        self.assertTrue(private_desktop.user.IsWindowEnabled(self.control(identifier)), identifier)
        self.desktop.send(self.control(identifier), 0xF5)
        self.idle()

    def field(self, identifier, value):
        text = ctypes.create_unicode_buffer(str(value))
        self.desktop.send(self.control(identifier), 0xC, 0, ctypes.addressof(text))

    def text(self, identifier):
        text = ctypes.create_unicode_buffer(4096)
        self.desktop.send(self.control(identifier), 0xD, len(text), ctypes.addressof(text))
        return text.value

    def focus_control(self, identifier):
        hwnd = self.control(identifier)
        user = private_desktop.user
        user.AttachThreadInput.argtypes = [wintypes.DWORD, wintypes.DWORD, wintypes.BOOL]
        user.SetFocus.argtypes = [wintypes.HWND]
        user.SetFocus.restype = wintypes.HWND
        thread = private_desktop.kernel.GetCurrentThreadId()
        target = user.GetWindowThreadProcessId(hwnd, None)
        private_desktop.check(user.AttachThreadInput(thread, target, True))
        try:
            user.SetFocus(hwnd)
        finally:
            private_desktop.check(user.AttachThreadInput(thread, target, False))
        self.assertEqual(self.desktop.focus(self.window()), hwnd)

    def test_minimum_controls_draft_polling_stale_apply_and_saved_settings(self):
        self.start(); self.resize_client(720, 570)
        self.assertTrue(set(range(7201, 7217)).issubset(self.assert_visible_geometry()))
        before = self.doc(); original = self.settings(); native = self.window()
        self.field(7208, '-'); self.field(7206, '3'); self.focus_control(7208)
        self.desktop.send(self.control(7208), 0xB1, 1, 1)  # Retained caret.
        self.desktop.send(self.control(7208), 0x100, 0x74); self.idle(); time.sleep(.25)  # Local F5 rescan and telemetry polls.
        self.assertEqual(self.state()['draft']['latencyMSText'], '-')
        self.assertEqual(self.desktop.focus(native), self.control(7208))
        selection = self.desktop.send(self.control(7208), 0xB0)
        self.assertEqual((selection & 65535, selection >> 16), (1, 1))
        self.press(7209); self.assertTrue(self.state()['dirty']); self.assertEqual(self.settings()['revision'], original['revision'])
        self.configure(latencyMS=25)
        time.sleep(.2)
        self.assertEqual(self.state()['draft']['baseRevision'], original['revision'])
        self.assertEqual(self.state()['draft']['latencyMSText'], '-')
        self.assertIn('changed elsewhere', self.text(7309))
        self.field(7208, '7'); self.press(7209)
        self.assertIn('changed', self.state()['status'])
        self.assertEqual(self.settings()['latencyMS'], 25)
        self.press(7210); self.assertFalse(self.state()['dirty']); self.assertEqual(float(self.text(7208)), 25)
        self.assertEqual(before, self.doc())
        self.field(7208, '-'); self.press(7216); self.assertFalse(self.state()['visible'])
        self.start(); self.assertEqual(native, self.window()); self.assertEqual(self.text(7208), '-')

    def test_keyboard_settings_keep_document_and_owned_focus(self):
        self.start(); before = self.doc(); self.focus_control(7206)
        self.desktop.send(self.control(7206), 0x100, 9)
        self.assertEqual(self.desktop.focus(self.window()), self.control(7207))
        self.field(7208, '-4.5'); self.focus_control(7208)
        self.modified_key(self.control(7208), 13, ctrl=True); self.idle()
        self.assertFalse(self.state()['dirty']); self.assertEqual(self.settings()['latencyMS'], -4.5)
        self.assertEqual(self.desktop.focus(self.window()), self.control(7208))
        self.focus_control(7203); self.desktop.send(self.control(7203), 0x100, 32)
        self.assertTrue(self.state()['dirty']); self.assertFalse(self.read('transport.get')['audioActive'])
        self.modified_key(self.control(7203), 13, ctrl=True); self.idle(); self.assertTrue(self.settings()['armed'])
        self.desktop.send(self.control(7203), 0x100, 0x74); self.idle()  # F5 rescan.
        self.assertEqual(before, self.doc())
        self.desktop.send(self.control(7203), 0x100, 27); self.assertFalse(self.state()['visible'])

    def test_recovered_review_selection_navigation_and_loss_reason(self):
        path = self.folder / 'Owned review fixture.screamseq'
        self.write('document.save', path=str(path))
        tree = plistlib.loads(path.read_bytes()); native = tree['native']
        pattern = native['patterns'][0][1]['id']; track = native['tracks'][1][1]['id']
        tree['recoveryTake'] = dict(compatible=True, missingTime=2, exhaustedVoices=1, overflow=0,
            inputError='MIDI input disconnected during the recovered performance', events=[
                dict(pattern=pattern, track=track, position=3 * 65536 + 16384, instrument=1, note=61, velocity=100),
                dict(pattern=pattern, track=track, position=7 * 65536 + 32768, instrument=1, note=65, velocity=90)])
        path.write_bytes(plistlib.dumps(tree, fmt=plistlib.FMT_BINARY))
        self.write('document.open', path=str(path), discard=True)
        self.start(); self.press(7214)
        self.assertEqual(self.state()['reviewedCount'], 2)
        self.assertIn('disconnected', self.state()['status'])
        self.assertIn('Missing time 2 / No free column 1', self.text(7310))
        notes = self.control(7211)
        self.focus_control(7211); self.desktop.send(notes, 0x100, 0x23)  # Native End selects last event.
        self.assertEqual(self.state()['selectedEvent']['position'], 7 * 65536 + 32768)
        selected = self.state()['selectedEvent']; before = self.doc()
        self.press(7214); self.assertEqual(self.state()['selectedEvent'], selected); self.focus_control(7211)
        self.desktop.send(notes, 0x100, 13); self.idle()
        context = self.read('context.get'); editor = self.read('workspace.get')['noteEditor']
        self.assertEqual((context['row'], context['channel']), (7, 1)); self.assertTrue(editor['visible'])
        self.assertEqual(before, self.doc())
        self.write('document.patch', title='Changed while reviewing')
        time.sleep(.2); self.assertFalse(private_desktop.user.IsWindowEnabled(self.control(7212)))
        self.assertTrue(private_desktop.user.IsWindowEnabled(self.control(7213)))
        self.assertEqual(self.state()['selectedEvent'], selected)
        self.press(7213); self.assertEqual(self.state()['reviewedCount'], 0)
        self.assertFalse(private_desktop.user.IsWindowEnabled(self.control(7215)))
