"""Actual worker/pipe plus native strip controls on an owned private desktop.

These are integration checks, not foreground visual or physical-device evidence.
"""
import ctypes
from ctypes import wintypes
import time
import unittest
import uuid

import private_desktop
import test_graph_mixer_app as support


class MixerStripsUITests(unittest.TestCase):
    # Reuse process/API helpers without inheriting another test inventory.
    setUp = support.GraphMixerAppTests.setUp
    doc = support.GraphMixerAppTests.doc
    write = support.GraphMixerAppTests.write

    def read(self, method, **fields):
        # Timer-driven publication can start between settle() and a read.
        # Retry only the explicit busy response, under one bounded deadline;
        # writes and uncertain transport outcomes keep their single-send path.
        self.assertIn(method, ('mixer.get',))
        deadline = time.monotonic() + 5
        request_id = uuid.uuid4().hex
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                self.fail('Mixer read stayed busy for five seconds')
            observer = self.client.__class__(self.client.pipe, timeout=min(.5, remaining))
            try:
                return observer.call(method, fields, request_id=request_id)['data']
            except support.ApiError as error:
                if error.code != -32002 or 'Document worker is busy' not in str(error):
                    raise
                time.sleep(min(.01, max(0, deadline - time.monotonic())))

    def main_command(self, identifier):
        self.desktop.send(self.desktop.hwnd(self.pid), 0x111, identifier, 0)
        return self.settle()

    def settle(self):
        deadline = time.monotonic() + 5
        state = None
        while time.monotonic() < deadline:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                break
            observer = self.client.__class__(self.client.pipe, timeout=min(.5, remaining))
            state = observer.call('workspace.get')['data']
            if not state['documentBusy'] and not state['pendingViewCommands'] and not state['mixerStrips'].get('pending', False):
                return state
            time.sleep(.01)
        self.fail('Mixer did not settle: ' + str(state))

    def refresh(self):
        self.desktop.send(self.desktop.hwnd(self.pid), 0x113, 3)
        return self.settle()

    def window(self):
        found = set()

        @private_desktop.callback
        def child(hwnd, _):
            pid = wintypes.DWORD()
            private_desktop.user.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
            name = ctypes.create_unicode_buffer(128)
            private_desktop.user.GetClassNameW(hwnd, name, len(name))
            if pid.value == self.pid and name.value == 'ScreamSeqMixerStrips':
                found.add(int(hwnd))
            return True

        @private_desktop.callback
        def root(hwnd, unused):
            child(hwnd, unused)
            pid = wintypes.DWORD()
            private_desktop.user.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
            if pid.value == self.pid:
                private_desktop.user.EnumChildWindows(hwnd, child, 0)
            return True

        private_desktop.check(private_desktop.user.EnumDesktopWindows(self.desktop.desktop, root, 0))
        self.assertEqual(len(found), 1, found)
        return next(iter(found))

    def strip(self, bus):
        return next(item for item in self.settle()['mixerStrips']['strips'] if item['bus'] == bus)

    def control(self, identifier):
        handle = private_desktop.user.GetDlgItem(self.window(), identifier)
        self.assertTrue(handle, identifier)
        return handle

    def field(self, identifier, value):
        text = ctypes.create_unicode_buffer(value)
        self.desktop.send(self.control(identifier), 0xC, 0, ctypes.addressof(text))

    def text(self, identifier):
        text = ctypes.create_unicode_buffer(128)
        self.desktop.send(self.control(identifier), 0xD, len(text), ctypes.addressof(text))
        return text.value

    def press(self, identifier):
        control = self.control(identifier)
        self.assertTrue(private_desktop.user.IsWindowEnabled(control), identifier)
        self.desktop.send(self.window(), 0x111, identifier, control)
        return self.settle()

    def key(self, identifier, key, release=False):
        self.desktop.send(self.control(identifier), 0x101 if release else 0x100, key, 0)
        return self.settle()

    def slide(self, identifier, position, notification=5, vertical=True):
        control = self.control(identifier)
        self.assertTrue(private_desktop.user.IsWindowEnabled(control), identifier)
        self.desktop.send(control, 0x405, 1, position)  # TBM_SETPOS
        self.desktop.send(self.window(), 0x115 if vertical else 0x114,
                          notification | (position << 16), control)
        return self.settle()

    def bus(self, identity):
        return next(bus for bus in self.read('mixer.get')['buses'] if bus['id'] == identity)

    def open_strips(self):
        self.write('mixer.enable')
        state = self.main_command(400)
        self.assertTrue(state['mixerStrips']['visible'], 'The default Mixer command must open strips')
        self.refresh()
        bus = self.read('mixer.get')['buses'][0]['id']
        return bus, self.strip(bus)['controlBase']

    def test_preview_single_commit_history_and_native_save_reopen(self):
        bus, base = self.open_strips()
        original = self.read('mixer.get')
        before = self.doc()
        self.slide(base + 1, 330)
        self.refresh()
        self.slide(base + 1, 360)
        self.refresh()
        self.assertEqual(self.doc(), before, 'Preview must not persist or allocate history')
        self.assertEqual(self.read('mixer.get'), original)
        self.slide(base + 1, 360, notification=8)  # TB_ENDTRACK
        self.assertEqual(self.bus(bus)['gainDB'], -12)
        self.assertFalse(self.settle()['mixerStrips']['gesture'])
        self.write('history.undo', domain='document')
        self.assertEqual(self.read('mixer.get'), original, 'One Undo must undo the complete drag')
        self.write('history.redo', domain='document')
        self.assertEqual(self.bus(bus)['gainDB'], -12)
        self.refresh()
        saved = self.read('mixer.get')
        path = self.folder / 'strip-history.screamseq'
        self.write('document.save', path=str(path), overwrite=False)
        self.write('mixer.bus.set', bus=bus, gainDB=-3)
        self.write('document.open', path=str(path), discard=True)
        self.main_command(418)
        self.assertEqual(self.read('mixer.get'), saved)
        self.assertEqual(self.strip(bus)['bus'], bus)

    def test_keyboard_control_cancel_and_details_preserve_target(self):
        bus, base = self.open_strips()
        self.field(base + 7, '-9.123456789')
        self.key(base + 7, 0x0D)
        self.assertEqual(self.bus(bus)['preGainDB'], -9.123456789)
        self.assertEqual(self.bus(bus)['gainDB'], 0)
        self.slide(base + 9, 150, notification=1, vertical=False)
        self.key(base + 9, 0x27, release=True)
        self.assertEqual(self.bus(bus)['width'], 1.5)
        before = self.doc()
        self.slide(base + 1, 400)
        self.refresh()
        self.key(base + 1, 0x1B)
        self.assertEqual(self.doc(), before)
        self.assertFalse(self.settle()['mixerStrips']['gesture'])
        state = self.press(base + 6)
        self.assertFalse(state['mixerStrips']['visible'])
        self.assertEqual(state['mixerEditor']['bus'], bus)
        self.assertFalse(state['mixerEditor']['draft'])
        self.main_command(418)
        self.assertEqual(self.strip(bus)['bus'], bus)

    def test_hidden_invalid_draft_blocks_replacement_and_retains_identity(self):
        bus, base = self.open_strips()
        path = self.folder / 'protected-strip.screamseq'
        self.write('document.save', path=str(path), overwrite=False)
        self.field(base + 2, '--')
        generation = self.settle()['mixerStrips']['generation']
        state = self.main_command(419)
        self.assertFalse(state['mixerStrips']['visible'])
        before = self.doc()
        with self.assertRaises(support.ApiError):
            self.write('document.open', path=str(path))
        self.assertEqual(self.doc(), before)
        self.write('mixer.bus.add', kind='return', name='Later return')
        self.main_command(418)
        self.assertEqual(self.strip(bus)['controlBase'], base)
        self.assertEqual(self.text(base + 2), '--')
        self.assertEqual(self.settle()['mixerStrips']['generation'], generation)
        before = self.doc()
        self.key(base + 2, 0x0D)
        self.assertEqual(self.doc(), before, 'A stale raw draft must not be rebased silently')
        self.press(12)
        self.assertFalse(self.settle()['mixerStrips']['gesture'])
        self.write('document.open', path=str(path), discard=True)
        self.main_command(418)
        self.assertFalse(self.settle()['mixerStrips']['gesture'])
        self.assertEqual(self.strip(bus)['bus'], bus)


if __name__ == '__main__':
    unittest.main()
