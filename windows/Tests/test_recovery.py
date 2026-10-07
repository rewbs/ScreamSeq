"""Application recovery against private storage and never-switched owned desktops."""
import concurrent.futures
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import sys
import subprocess
import tempfile
import time
import unittest

import private_desktop
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'Api'))
from client import Client, ApiError, TransportError
private_desktop.user.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]


class RecoveryTests(unittest.TestCase):
    def setUp(self):
        self.folder = Path(self.enterContext(tempfile.TemporaryDirectory(prefix='recovery-app-', dir=os.environ['TMPDIR'])))
        self.directory = self.folder / 'Recovery'
        self.desktop = self.enterContext(private_desktop.PrivateDesktop())
        self.pid, self.client = self.launch()

    def launch(self, *, directory=True, delay=0, audio=False):
        args = [os.environ['SCREAMSEQ_TEST_EXE'], '--audio-test-silent' if audio else '--inspection', '--automation', '--seconds', '120']
        if audio: args += ['--audio-test-allow-stop']
        if directory: args += ['--recovery-test-directory', str(self.directory)]
        if delay: args += ['--recovery-test-write-delay-ms', str(delay)]
        pid = self.desktop.launch(args)
        client = Client(r'\\.\pipe\ScreamSeq.Api.' + str(pid), timeout=20)
        for _ in range(120):
            try:
                client.call('document.get')
                if not audio or client.call('transport.get')['data']['playing']:
                    return pid, client
            except TransportError: time.sleep(.05)
        self.fail('recovery fixture did not start')

    def doc(self): return self.client.call('document.get')
    def read(self, method, **params): return self.client.call(method, params)['data']
    def write(self, method, **params): return self.client.call(method, dict(expectedRevision=self.doc()['revision'], **params))
    def copies(self): return self.read('recovery.list')['copies']
    def reject(self, code, method, **params):
        with self.assertRaises(ApiError) as raised: self.client.call(method, params)
        self.assertEqual(raised.exception.code, code, str(raised.exception))

    def window(self, kind='ScreamSeqWindowsDevelopment'):
        found = []
        @private_desktop.callback
        def visit(hwnd, _):
            pid = wintypes.DWORD()
            private_desktop.user.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
            name = ctypes.create_unicode_buffer(100)
            private_desktop.user.GetClassNameW(hwnd, name, 100)
            if pid.value == self.pid and name.value == kind: found.append(hwnd)
            return True
        private_desktop.check(private_desktop.user.EnumDesktopWindows(self.desktop.desktop, visit, 0))
        self.assertEqual(len(found), 1, found)
        return found[0]

    def command(self, identifier, kind='ScreamSeqWindowsDevelopment'):
        self.desktop.send(self.window(kind), 0x111, identifier, 0)

    def wait(self, condition, seconds=8):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            result = condition()
            if result: return result
            time.sleep(.05)
        self.fail('recovery state did not settle')

    def test_contract_force_no_mutation_validation_and_request_replay(self):
        before = self.doc()
        context = self.read('context.get')
        desc = self.read('api.describe')
        self.assertEqual(desc['recovery']['generations'], 10)
        for method in ('recovery.status', 'recovery.list'): self.assertIn(method, desc['reads'])
        for method in ('recovery.save', 'recovery.restore'):
            self.assertIn(method, desc['writes'])
            self.assertEqual(desc['revisionGuards'][method], ['expectedRevision'])
        status = self.read('recovery.status')
        self.assertTrue(status['enabled']); self.assertFalse(status['saving'])
        self.assertEqual(status, context['autosave']); self.assertEqual(status['intervalSeconds'], 10)
        self.assertEqual(self.copies(), [])
        for method, params in [('recovery.status', {'unexpected': 1}), ('recovery.list', {'expectedRevision': before['revision']}),
                               ('recovery.save', {}), ('recovery.save', {'expectedRevision': True}),
                               ('recovery.restore', {'expectedRevision': before['revision'], 'id': '../escape.screamseq'}),
                               ('recovery.restore', {'expectedRevision': before['revision'], 'id': True})]:
            self.reject(-32602, method, **params)
        self.reject(-32001, 'recovery.save', expectedRevision='old')
        params = dict(expectedRevision=before['revision'])
        saved = self.client.call('recovery.save', params, request_id='same-recovery-snapshot')
        self.assertFalse(saved['changed']); self.assertFalse(saved['playbackStopped'])
        self.assertEqual(saved['revision'], before['revision']); self.assertEqual(before, self.doc())
        self.assertEqual(context['dirty'], self.read('context.get')['dirty'])
        self.assertEqual(context['file'], self.read('context.get')['file'])
        self.assertEqual(saved, self.client.call('recovery.save', params, request_id='same-recovery-snapshot'))
        copies = self.copies(); self.assertEqual(len(copies), 1)
        self.assertEqual(copies[0]['id'], saved['data']['lastCopy'])
        self.assertEqual(copies[0]['title'], before['data']['title'])
        self.assertIsNone(copies[0]['source']); self.assertFalse(copies[0]['hasRecording'])

    def test_restore_protects_current_song_opens_unsaved_and_save_clears_only_current_session(self):
        original = self.folder / 'Original.screamseq'
        self.write('document.save', path=str(original), overwrite=False)
        original_hash = hashlib.sha256(original.read_bytes()).hexdigest()
        self.write('document.patch', title='Copy to recover')
        chosen = self.write('recovery.save')['data']['lastCopy']
        chosen_bytes = (self.directory / chosen).read_bytes()
        self.write('document.patch', title='Current unsaved song')
        before = self.doc()
        restored = self.write('recovery.restore', id=chosen)
        self.assertTrue(restored['changed']); self.assertFalse(restored['playbackStopped'])
        self.assertNotEqual(before['revision'], restored['revision'])
        self.assertNotEqual(before['documentId'], restored['documentId'])
        self.assertEqual(self.doc()['data']['title'], 'Copy to recover')
        context = self.read('context.get'); self.assertTrue(context['dirty']); self.assertIsNone(context['file'])
        caption = ctypes.create_unicode_buffer(1024)
        self.desktop.send(self.window(), 0xD, len(caption), ctypes.addressof(caption))
        self.assertTrue(caption.value.startswith('* Copy to recover - '), caption.value)
        copies = self.copies()
        protection = [copy for copy in copies if copy['title'] == 'Current unsaved song']
        self.assertEqual(len(protection), 1)
        self.assertNotEqual(protection[0]['document'], next(c['document'] for c in copies if c['id'] == chosen))
        self.assertEqual(chosen_bytes, (self.directory / chosen).read_bytes())
        own = self.write('recovery.save')['data']['lastCopy']
        destination = self.folder / 'Recovered.screamseq'
        self.write('document.save', path=str(destination), overwrite=False)
        self.wait(lambda: not (self.directory / own).exists())
        self.assertFalse(self.read('context.get')['dirty'])
        self.assertEqual(self.read('context.get')['file'], str(destination))
        self.assertEqual({c['id'] for c in self.copies()}, {chosen, protection[0]['id']})
        self.assertEqual(hashlib.sha256(original.read_bytes()).hexdigest(), original_hash)

    def test_corrupt_restore_preserves_current_state(self):
        chosen = self.write('recovery.save')['data']['lastCopy']
        good = (self.directory / chosen).read_bytes()
        self.write('document.patch', title='Must survive bad recovery')
        before = self.doc(); context = self.read('context.get')
        (self.directory / chosen).write_bytes(b'damaged project')
        self.reject(-32003, 'recovery.restore', expectedRevision=before['revision'], id=chosen)
        self.assertEqual(before, self.doc())
        after = self.read('context.get')
        for key in ('documentId', 'file', 'dirty', 'pattern', 'row', 'channel'): self.assertEqual(context[key], after[key])
        (self.directory / chosen).write_bytes(good)
        self.write('recovery.restore', id=chosen)
        self.assertEqual(self.doc()['data']['title'], 'Midnight Circuit')

    def test_storage_error_is_visible_and_retry_preserves_document(self):
        self.directory.write_bytes(b'not a directory')
        self.write('document.patch', title='Needs protection')
        before = self.doc()
        self.reject(-32003, 'recovery.save', expectedRevision=before['revision'])
        status = self.read('recovery.status'); self.assertIsNotNone(status['error']); self.assertFalse(status['saving'])
        self.assertEqual(status, self.read('context.get')['autosave']); self.assertEqual(before, self.doc())
        self.directory.unlink(); self.directory.mkdir()
        self.write('recovery.save')
        self.assertIsNone(self.read('recovery.status')['error']); self.assertEqual(before, self.doc())

    def test_actual_timer_deduplicates_and_recovers_after_owned_process_termination(self):
        self.write('document.patch', title='Survives an owned fixture crash')
        before = self.doc()
        self.wait(lambda: self.read('recovery.status')['lastCopy'], seconds=14)
        chosen = self.read('recovery.status')['lastCopy']
        self.assertEqual(before, self.doc())
        # Observe the next actual ten-second timer period: no unchanged churn.
        deadline = time.monotonic() + 10.5
        while time.monotonic() < deadline:
            self.assertEqual(self.read('recovery.status')['lastCopy'], chosen)
            time.sleep(.25)
        self.assertEqual(len(self.copies()), 1)
        process = next(p for p in self.desktop.processes if p.dwProcessId == self.pid)
        private_desktop.check(private_desktop.kernel.TerminateProcess(process.hProcess, 0))
        self.assertEqual(private_desktop.kernel.WaitForSingleObject(process.hProcess, 10000), 0)
        self.pid, self.client = self.launch()
        self.assertEqual(self.copies()[0]['id'], chosen)
        self.write('recovery.restore', id=chosen)
        self.assertEqual(self.doc()['data']['title'], 'Survives an owned fixture crash')
        self.assertTrue(self.read('context.get')['dirty']); self.assertIsNone(self.read('context.get')['file'])

    def test_inspection_without_private_directory_never_enables_storage(self):
        _, client = self.launch(directory=False)
        status = client.call('recovery.status')['data']
        self.assertFalse(status['enabled']); self.assertEqual(client.call('recovery.list')['data']['copies'], [])
        with self.assertRaises(ApiError) as raised:
            client.call('recovery.save', dict(expectedRevision=client.call('document.get')['revision']))
        self.assertEqual(raised.exception.code, -32003)
        self.assertFalse(self.directory.exists())

    def test_pending_write_allows_edits_and_save_cleanup_cannot_resurrect_old_copies(self):
        self.pid, self.client = self.launch(delay=1200)
        self.write('document.patch', title='Captured before concurrent edit')
        path = self.folder / 'Saved during recovery.screamseq'
        self.write('document.save', path=str(path), overwrite=False)
        before = self.doc()
        main = self.window()
        indicator = private_desktop.user.GetDlgItem(main, 548)
        def saving():
            text = ctypes.create_unicode_buffer(128)
            self.desktop.send(indicator, 0xD, len(text), ctypes.addressof(text))
            return text.value.startswith('Saving recovery')
        with concurrent.futures.ThreadPoolExecutor() as executor:
            pending = executor.submit(self.client.call, 'recovery.save', dict(expectedRevision=before['revision']))
            # The pipe serves one connection at a time. Real native commands
            # remain responsive through the app's pumped immutable disk wait.
            self.wait(saving)
            self.command(146)  # Transpose the captured tracker selection.
            self.command(120)  # Save to the existing path while old bytes wait.
            saved = pending.result(timeout=10)
        self.assertFalse(saved['changed']); self.assertFalse(saved['playbackStopped'])
        self.assertEqual(saved['revision'], before['revision'])
        self.assertNotEqual(self.doc()['revision'], before['revision'])
        self.wait(lambda: not list(self.directory.glob('*.screamseq')))
        self.assertEqual(self.copies(), []); self.assertIsNone(self.read('recovery.status')['lastCopy'])
        self.assertFalse(self.read('context.get')['dirty'])

    def test_native_browser_save_reload_restore_and_file_save_cleanup(self):
        path = self.folder / 'Native UI save.screamseq'
        self.write('document.save', path=str(path), overwrite=False)
        self.write('document.patch', title='Browser copy')
        self.command(548)
        self.assertTrue(self.read('workspace.get')['recovery']['visible'])
        self.command(7003, 'ScreamSeq.Recovery')
        self.wait(lambda: len(self.copies()) == 1)
        chosen = self.copies()[0]['id']
        self.command(7002, 'ScreamSeq.Recovery')
        state = self.read('workspace.get')['recovery']
        self.assertEqual(state['selected'], chosen); self.assertTrue(state['restoreEnabled'])
        self.command(120)  # Existing path avoids a file dialog.
        self.wait(lambda: not (self.directory / chosen).exists())
        self.command(7002, 'ScreamSeq.Recovery')
        self.assertFalse(self.read('workspace.get')['recovery']['restoreEnabled'])
        self.command(7003, 'ScreamSeq.Recovery')
        self.wait(lambda: len(self.copies()) == 1)
        # A removed selection must not silently target a newly created copy.
        list_window = private_desktop.user.GetDlgItem(self.window('ScreamSeq.Recovery'), 7001)
        self.desktop.send(list_window, 0x100, 0x24, 0)  # Native Home selects row 0.
        self.assertEqual(self.read('workspace.get')['recovery']['selected'], self.copies()[0]['id'])
        self.write('document.patch', title='Protected while restoring from browser')
        self.command(7004, 'ScreamSeq.Recovery')
        self.wait(lambda: not self.read('workspace.get')['recovery']['visible'])
        self.assertEqual(self.doc()['data']['title'], 'Browser copy')
        self.assertTrue(self.read('context.get')['dirty']); self.assertIsNone(self.read('context.get')['file'])

    def test_document_switch_during_native_save_cannot_relabel_old_snapshot(self):
        self.pid, self.client = self.launch(delay=1200)
        path = self.folder / 'Other document.screamseq'
        self.write('document.save', path=str(path), overwrite=False)
        self.write('document.patch', title='Old document recovery')
        self.command(548)
        browser = self.window('ScreamSeq.Recovery')
        private_desktop.check(private_desktop.user.PostMessageW(browser, 0x111, 7003, 0))
        # saving becomes true before immutable snapshot capture finishes. Switch
        # during the delayed disk write, after the document worker is available.
        self.wait(lambda: self.read('recovery.status')['saving'] and not self.read('workspace.get')['documentBusy'])
        self.write('document.open', path=str(path), discard=True)
        self.wait(lambda: not self.read('recovery.status')['saving'])
        status = self.read('recovery.status')
        self.assertIsNone(status['lastSavedAt']); self.assertIsNone(status['lastCopy'])
        self.assertEqual(self.doc()['data']['title'], 'Midnight Circuit')
        self.assertEqual(self.copies()[0]['title'], 'Old document recovery')

    def test_failed_protective_write_keeps_current_song_and_selected_copy(self):
        self.pid, self.client = self.launch(delay=1200)
        chosen = self.write('recovery.save')['data']['lastCopy']
        chosen_bytes = (self.directory / chosen).read_bytes()
        self.write('document.patch', title='Keep this song when protection fails')
        before = self.doc()
        self.command(548)
        browser = self.window('ScreamSeq.Recovery')
        private_desktop.check(private_desktop.user.PostMessageW(browser, 0x111, 7004, 0))
        def writing_protection():
            state = self.read('workspace.get')
            return state['recovery'].get('operation') == 'restore' and not state['documentBusy']
        self.wait(writing_protection)
        # Both paths are direct children of this unique owned fixture directory.
        # Swap storage only during the explicit delayed-write test seam.
        moved = self.folder / 'Recovery held for protection failure'
        self.assertTrue(self.directory.resolve().is_relative_to(self.folder.resolve()))
        self.assertTrue(moved.resolve().is_relative_to(self.folder.resolve()))
        self.directory.rename(moved)
        self.directory.write_bytes(b'cannot publish protective copy here')
        try:
            self.wait(lambda: not self.read('workspace.get')['recovery']['pending'])
            self.assertEqual(self.doc(), before)
            state = self.read('workspace.get')['recovery']
            self.assertTrue(state['visible']); self.assertTrue(state['error'])
            self.assertEqual(state['selected'], chosen)
            self.assertEqual((moved / chosen).read_bytes(), chosen_bytes)
        finally:
            self.directory.unlink()
            moved.rename(self.directory)

    def test_close_drains_pending_native_recovery_write(self):
        self.pid, self.client = self.launch(delay=1200)
        self.command(548)
        browser = self.window('ScreamSeq.Recovery')
        private_desktop.check(private_desktop.user.PostMessageW(browser, 0x111, 7003, 0))
        self.wait(lambda: self.read('recovery.status')['saving'])
        self.command(7005, 'ScreamSeq.Recovery')  # Closing the browser retains work.
        private_desktop.check(private_desktop.user.PostMessageW(self.window(), 0x10, 0, 0))
        process = next(p for p in self.desktop.processes if p.dwProcessId == self.pid)
        self.assertEqual(private_desktop.kernel.WaitForSingleObject(process.hProcess, 10000), 0)
        code = wintypes.DWORD(); private_desktop.check(private_desktop.kernel.GetExitCodeProcess(process.hProcess, ctypes.byref(code)))
        self.assertEqual(code.value, 0)
        self.assertEqual(len(list(self.directory.glob('*.screamseq'))), 1)

    @unittest.skipUnless(os.environ.get('SCREAMSEQ_TEST_LIVE_AUDIO') == '1', 'Explicit silent audio qualification required')
    def test_silent_playback_survives_capture_and_bad_restore_stops_only_successful_restore(self):
        self.pid, self.client = self.launch(audio=True)
        before = self.doc(); transport = self.read('transport.get')
        self.assertTrue(transport['playing'])
        saved = self.write('recovery.save')
        after = self.read('transport.get')
        self.assertFalse(saved['playbackStopped']); self.assertTrue(after['playing'])
        self.assertEqual(after['playbackEpoch'], transport['playbackEpoch'])
        self.assertGreater(after['frames'], transport['frames']); self.assertEqual(before, self.doc())
        chosen = saved['data']['lastCopy']; path = self.directory / chosen; good = path.read_bytes()
        path.write_bytes(b'damaged project')
        self.reject(-32003, 'recovery.restore', expectedRevision=before['revision'], id=chosen)
        self.assertEqual(before, self.doc()); self.assertTrue(self.read('transport.get')['playing'])
        path.write_bytes(good)
        self.assertTrue(self.write('recovery.restore', id=chosen)['playbackStopped'])
        self.assertFalse(self.read('transport.get')['playing'])


class RecoveryControllerTests(unittest.TestCase):
    def run_controller(self, args):
        executable = Path(os.environ['SCREAMSEQ_TEST_EXE']).with_name('document-controller-tests.exe')
        with tempfile.TemporaryDirectory(prefix='recovery-controller-', dir=os.environ['TMPDIR']) as directory:
            result = subprocess.run([str(executable), *args, directory], capture_output=True, text=True, timeout=90)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('PASS recovery', result.stdout)

    def test_immutable_capture_and_transactional_restore(self):
        self.run_controller(['--recovery'])

    def test_manual_vendor_editor_overlay_and_late_protection_guard(self):
        # run_isolated.py puts this worker on the owned private desktop;
        # the native child and its provider STA inherit that same desktop.
        executable = Path(os.environ['SCREAMSEQ_TEST_EXE'])
        self.run_controller(['--recovery-manual', str(executable.with_name('ScreamSeqVST3Scanner.exe')), os.environ['SCREAMSEQ_TEST_PROVIDER_CACHE']])


if __name__ == '__main__': unittest.main()
