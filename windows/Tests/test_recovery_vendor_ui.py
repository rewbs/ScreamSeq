"""Explicit recovery restores retain native vendor-window enable ownership."""
import ctypes
import json
from ctypes import wintypes
import os
import shutil
import time
import unittest

import private_desktop
import test_recovery as recovery

private_desktop.user.IsWindowEnabled.argtypes = [wintypes.HWND]
private_desktop.user.EnableWindow.argtypes = [wintypes.HWND, wintypes.BOOL]
private_desktop.user.IsWindow.argtypes = [wintypes.HWND]


@unittest.skipUnless(os.environ.get('SCREAMSEQ_TEST_PROVIDER_CACHE'), 'private VST3 provider fixture required')
class RecoveryVendorInputTests(unittest.TestCase):
    # Reuse only fixture helpers; inheriting RecoveryTests would duplicate its tests.
    setUp = recovery.RecoveryTests.setUp
    doc = recovery.RecoveryTests.doc
    read = recovery.RecoveryTests.read
    write = recovery.RecoveryTests.write
    window = recovery.RecoveryTests.window
    command = recovery.RecoveryTests.command
    wait = recovery.RecoveryTests.wait

    def launch(self):
        cache = self.folder / 'private-vst3-cache.json'
        shutil.copyfile(os.environ['SCREAMSEQ_TEST_PROVIDER_CACHE'], cache)
        self.pid = self.desktop.launch([
            os.environ['SCREAMSEQ_TEST_EXE'], '--inspection', '--automation', '--seconds', '120',
            '--vst3-test-cache', str(cache), '--recovery-test-directory', str(self.directory),
            '--recovery-test-write-delay-ms', '1200'])
        client = recovery.Client(r'\\.\pipe\ScreamSeq.Api.' + str(self.pid), timeout=20)
        for _ in range(120):
            try:
                client.call('document.get')
                return self.pid, client
            except recovery.TransportError:
                time.sleep(.05)
        self.fail('recovery vendor fixture did not start')

    def editors(self):
        found = set()
        @private_desktop.callback
        def visit(hwnd, _):
            pid = wintypes.DWORD()
            private_desktop.user.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
            kind = ctypes.create_unicode_buffer(100)
            private_desktop.user.GetClassNameW(hwnd, kind, 100)
            if pid.value == self.pid and kind.value == 'ScreamSeq.VST3.PrivateEditor':
                found.add(hwnd)
            return True
        private_desktop.check(private_desktop.user.EnumDesktopWindows(self.desktop.desktop, visit, 0))
        return found

    def recovery_diagnostic(self, browser, *, enabled=None, disabled=None, chosen=None):
        state = self.read('workspace.get')
        restore = private_desktop.user.GetDlgItem(browser, 7004)
        return dict(monotonic=time.monotonic(), chosen=chosen,
                    documentBusy=state['documentBusy'], recovery=state['recovery'],
                    restoreNativeEnabled=bool(private_desktop.user.IsWindowEnabled(restore)),
                    vendorEnabled=None if enabled is None else bool(private_desktop.user.IsWindowEnabled(enabled)),
                    vendorInitiallyDisabled=None if disabled is None else bool(private_desktop.user.IsWindowEnabled(disabled)),
                    focus=self.desktop.focus(browser))

    def select_ready_copy(self, browser, chosen):
        restore = private_desktop.user.GetDlgItem(browser, 7004)
        listing = private_desktop.user.GetDlgItem(browser, 7001)
        self.assertTrue(restore and listing)

        def ready():
            state = self.read('workspace.get')
            recovery_state = state['recovery']
            return (state if recovery_state['visible'] and not state['documentBusy']
                    and not recovery_state['pending'] and recovery_state['restoreEnabled']
                    and private_desktop.user.IsWindowEnabled(restore)
                    and any(copy['id'] == chosen for copy in recovery_state['copies']) else None)

        try:
            state = self.wait(ready)
            index = next(i for i, copy in enumerate(state['recovery']['copies']) if copy['id'] == chosen)
            # Use the real native list's selection path, never cross-process LVITEM pointers.
            self.desktop.send(listing, 0x100, 0x24, 0)  # Home.
            for _ in range(index):
                self.desktop.send(listing, 0x100, 0x28, 0)  # Down.
            selected = self.read('workspace.get')['recovery']['selected']
            self.assertEqual(selected, chosen)
            state = self.wait(ready)
            self.assertEqual(state['recovery']['selected'], chosen)
            self.assertTrue(state['recovery']['restoreEnabled'])
            self.assertTrue(private_desktop.user.IsWindowEnabled(restore))
            return state
        except BaseException:
            try:
                print('RECOVERY_VENDOR_READINESS ' + json.dumps(self.recovery_diagnostic(browser, chosen=chosen), sort_keys=True), flush=True)
            except Exception as error:
                print('RECOVERY_VENDOR_READINESS diagnostic error: ' + repr(error), flush=True)
            raise

    def test_failed_protection_reenables_only_previously_enabled_vendor_editor(self):
        chosen = self.write('recovery.save')['data']['lastCopy']
        chosen_bytes = (self.directory / chosen).read_bytes()
        descriptor = next(p for p in self.read('plugin.discover', format='VST3')
                          if p['classID'] == '5245534F4E414E434546464543540001')
        self.write('plugin.add', descriptor=descriptor)
        self.write('plugin.add', descriptor=descriptor)
        self.write('plugin.editor.open', slot=0)
        self.read('plugin.state.get', slot=0)  # Settle the fixture's real attachment gesture.
        first = self.editors()
        self.assertEqual(len(first), 1)
        enabled = next(iter(first))
        self.write('plugin.editor.open', slot=1)
        self.read('plugin.state.get', slot=1)
        second = self.editors() - first
        self.assertEqual(len(second), 1)
        disabled = next(iter(second))
        self.assertTrue(private_desktop.user.IsWindowEnabled(enabled))
        private_desktop.user.EnableWindow(disabled, False)
        self.assertFalse(private_desktop.user.IsWindowEnabled(disabled))
        before = self.doc()
        main = self.window()
        self.command(548)
        browser = self.window('ScreamSeq.Recovery')
        self.select_ready_copy(browser, chosen)
        private_desktop.check(private_desktop.user.PostMessageW(browser, 0x111, 7004, 0))

        def writing_protection():
            state = self.read('workspace.get')
            return state['recovery'].get('operation') == 'restore' and not state['documentBusy'] and not private_desktop.user.IsWindowEnabled(enabled)

        moved = self.folder / 'Recovery held for vendor protection failure'
        swapped = False
        try:
            self.wait(writing_protection)
            self.assertFalse(private_desktop.user.IsWindowEnabled(disabled))
            self.assertTrue(private_desktop.user.IsWindowEnabled(main))
            self.assertTrue(self.directory.resolve().is_relative_to(self.folder.resolve()))
            self.assertTrue(moved.resolve().is_relative_to(self.folder.resolve()))
            self.directory.rename(moved)
            swapped = True
            self.directory.write_bytes(b'protective write must fail')
            self.wait(lambda: not self.read('workspace.get')['recovery']['pending'])
            self.assertEqual(self.doc(), before)
            self.assertEqual(self.editors(), first | second)
            self.assertTrue(private_desktop.user.IsWindowEnabled(enabled))
            self.assertFalse(private_desktop.user.IsWindowEnabled(disabled))
            self.assertTrue(private_desktop.user.IsWindowEnabled(main))
            state = self.read('workspace.get')['recovery']
            self.assertTrue(state['visible'])
            self.assertTrue(state['error'])
            self.assertEqual(state['selected'], chosen)
            self.assertEqual((moved / chosen).read_bytes(), chosen_bytes)
        except BaseException:
            try:
                print('RECOVERY_VENDOR_FAILURE ' + json.dumps(self.recovery_diagnostic(
                    browser, enabled=enabled, disabled=disabled, chosen=chosen), sort_keys=True), flush=True)
            except Exception as error:
                print('RECOVERY_VENDOR_FAILURE diagnostic error: ' + repr(error), flush=True)
            raise
        finally:
            if swapped:
                if self.directory.is_file():
                    self.directory.unlink()
                moved.rename(self.directory)
            if private_desktop.user.IsWindow(disabled):
                private_desktop.user.EnableWindow(disabled, True)
