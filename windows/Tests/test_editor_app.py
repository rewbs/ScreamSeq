"""Real private-PID app editing/persistence; only disposable project copies."""
import ctypes
from ctypes import wintypes
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'windows/Api'))
from client import Client, ApiError, TransportError
ctypes.windll.user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))

class EditorAppTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='editor-app-', dir=os.environ['TMPDIR'])
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        self.processes = []
        self.addCleanup(self.close_apps)

    def close_apps(self):
        for process in self.processes:
            if process.poll() is None:
                process.terminate()
            process.wait(timeout=10)

    def launch(self, path=None):
        exe = os.environ.get('SCREAMSEQ_TEST_EXE', str(ROOT / 'bin/windows-editor/Release/ScreamSeq.exe'))
        args = [exe, '--inspection', '--automation', '--seconds', '120']
        if path:
            args += ['--project', str(path)]
        process = subprocess.Popen(args)
        self.processes.append(process)
        client = Client(r'\\.\pipe\ScreamSeq.Api.' + str(process.pid), timeout=10)
        for _ in range(100):
            self.assertIsNone(process.poll())
            try:
                client.call('document.get')
                return client, process
            except TransportError:
                time.sleep(.1)
        self.fail('app did not publish private pipe')

    def write(self, client, method, **fields):
        return client.call(method, {'expectedRevision': client.call('document.get')['revision'], **fields})

    def cell(self, client):
        return client.call('pattern.get', {'pattern': 0, 'rowCount': 1, 'channelCount': 1})['data']['cells'][0]

    def hwnd(self, process):
        user = ctypes.WinDLL('user32')
        callback = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
        user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
        user.GetClassNameW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
        found = []
        @callback
        def visit(hwnd, _):
            pid = wintypes.DWORD()
            user.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
            name = ctypes.create_unicode_buffer(128)
            user.GetClassNameW(hwnd, name, 128)
            if pid.value == process.pid and name.value == 'ScreamSeqWindowsDevelopment':
                found.append(hwnd)
            return True
        user.EnumWindows(visit, 0)
        self.assertEqual(len(found), 1)
        return found[0]

    def send(self, process, message, wp=0, lp=0):
        user = ctypes.WinDLL('user32')
        user.SendMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
        return user.SendMessageW(self.hwnd(process), message, wp, lp)

    def test_failed_candidate_publication_preserves_dirty_worker_and_cursor(self):
        import plistlib
        source = self.directory / 'old.mod'
        shutil.copyfile(ROOT / 'test/test.mod', source)
        client, _ = self.launch(source)
        self.write(client, 'document.patch', title='Dirty original')
        self.navigate(client, row=7, channel=1)
        before = client.call('document.get')
        context = client.call('context.get')
        sample = client.call('sample.get', {'sample': 1})
        candidate = self.directory / 'typed-au.screamseq'
        candidate.write_bytes(plistlib.dumps({'version': 1,
            'module': (ROOT / 'test/test.xm').read_bytes(), 'automation': [],
            'plugins': [{'type': 0, 'subtype': 0, 'manufacturer': 0, 'state': b'',
                'format': 'AU', 'name': 'Review AU', 'instanceID': 'review-au', 'classID': 17}]},
            fmt=plistlib.FMT_BINARY))
        # A supported opaque field may be presented or rejected, never half-opened.
        try:
            self.write(client, 'document.open', path=str(candidate), discard=True)
        except ApiError:
            self.assertEqual(client.call('document.get'), before)
            self.assertEqual(client.call('context.get'), context)
            self.assertEqual(client.call('sample.get', {'sample': 1}), sample)
        else:
            self.assertEqual(client.call('document.get')['data']['nativePlugins'][0]['classID'], 17)
        self.write(client, 'document.patch', title='Subsequent edit works')
        self.assertEqual(client.call('document.get')['data']['title'], 'Subsequent edit works')
        self.write(client, 'document.open', path=str(source), discard=True)
        self.assertEqual(Path(client.call('context.get')['data']['file']), source)

    def test_legacy_title_and_instrument_text_are_utf8_at_api_boundary(self):
        import struct
        base = bytearray((ROOT / 'test/test.xm').read_bytes())
        instrument = 60 + struct.unpack_from('<I', base, 60)[0]
        for _ in range(struct.unpack_from('<H', base, 70)[0]):
            instrument += struct.unpack_from('<I', base, instrument)[0] + struct.unpack_from('<H', base, instrument + 7)[0]
        for field in ('title', 'instrument'):
            with self.subTest(field=field):
                data = bytearray(base)
                start, length = (17, 20) if field == 'title' else (instrument + 4, 22)
                data[start:start+length] = b'Caf\xe9' + b' ' * (length-4)
                source = self.directory / (field + '.xm')
                source.write_bytes(data)
                client, _ = self.launch(source)
                document = client.call('document.get')['data']
                value = document['title'] if field == 'title' else document['instruments'][0]['name']
                self.assertEqual(value.rstrip(), 'Café')
                self.assertIsInstance(document['sequences'][0]['name'], str)
                self.assertEqual(source.read_bytes(), data)
                self.write(client, 'pattern.apply', cells=[{'pattern': 0, 'row': 0, 'channel': 0, 'note': 61}])
                saved = self.directory / (field + '.screamseq')
                self.write(client, 'document.save', path=str(saved))
                reopened, _ = self.launch(saved)
                self.assertEqual(reopened.call('document.get')['data']['title'], document['title'])
                self.assertEqual(reopened.call('document.get')['data']['instruments'], document['instruments'])

    def test_nonempty_unicode_sequence_name_is_an_api_string(self):
        exe = Path(os.environ.get('SCREAMSEQ_TEST_EXE', str(ROOT / 'bin/windows-editor/Release/ScreamSeq.exe')))
        subprocess.run([str(exe.with_name('document-controller-tests.exe')), '--fixtures', str(self.directory)], check=True)
        client, _ = self.launch(self.directory / 'unicode.screamseq')
        self.assertEqual(client.call('document.get')['data']['sequences'][0]['name'], '序列 Café')

    def test_large_project_incremental_edits_and_controlled_worker_failures(self):
        exe = Path(os.environ.get('SCREAMSEQ_TEST_EXE', str(ROOT / 'bin/windows-editor/Release/ScreamSeq.exe')))
        worker = str(exe.with_name('document-controller-tests.exe'))
        for mode in ('--fixtures', '--publication', '--large-fixture', '--cache'):
            subprocess.run([worker, mode, str(self.directory)], check=True, timeout=120)
        client, _ = self.launch(self.directory / 'large.screamseq')
        before = client.call('document.get')
        cells = [{'pattern': 0, 'row': 0, 'channel': 0, 'note': 61}]
        self.write(client, 'pattern.apply', cells=cells, dryRun=True)
        self.assertEqual(client.call('document.get'), before)
        self.assertTrue(self.write(client, 'pattern.apply', cells=cells)['changed'])
        self.assertEqual(self.cell(client)['note'], 61)
        after = client.call('document.get')
        self.assertFalse(self.write(client, 'pattern.apply', cells=cells)['changed'])
        self.assertEqual(client.call('document.get'), after)
        self.assertEqual(client.call('sample.waveform.get', {'sample': 1, 'bins': 32})['data']['bins'], 32)

    def test_cache_budget_rejection_preserves_dirty_path_cursor_and_worker(self):
        exe = Path(os.environ.get('SCREAMSEQ_TEST_EXE', str(ROOT / 'bin/windows-editor/Release/ScreamSeq.exe')))
        subprocess.run([str(exe.with_name('document-controller-tests.exe')), '--oversized-fixture', str(self.directory)], check=True, timeout=120)
        source = self.directory / 'original.mod'
        shutil.copyfile(ROOT / 'test/test.mod', source)
        client, _ = self.launch(source)
        self.write(client, 'document.patch', title='Keep dirty source')
        self.navigate(client, row=7, channel=1)
        before, context = client.call('document.get'), client.call('context.get')
        sample = client.call('sample.get', {'sample': 1})
        with self.assertRaises(ApiError) as error:
            self.write(client, 'document.open', path=str(self.directory / 'oversized.screamseq'), discard=True)
        self.assertIn('cache', str(error.exception))
        self.assertEqual(client.call('document.get'), before)
        self.assertEqual(client.call('context.get'), context)
        self.assertEqual(client.call('sample.get', {'sample': 1}), sample)
        self.write(client, 'pattern.apply', cells=[{'pattern': 0, 'row': 0, 'channel': 0, 'note': 61}])
        self.assertEqual(self.cell(client)['note'], 61)
        self.write(client, 'document.open', path=str(source), discard=True)
        self.assertFalse(client.call('context.get')['data']['dirty'])

    def test_native_controls_retain_real_keyboard_focus(self):
        from private_desktop import PrivateDesktop, user
        exe = os.environ.get('SCREAMSEQ_TEST_EXE', str(ROOT / 'bin/windows-editor/Release/ScreamSeq.exe'))
        with PrivateDesktop() as desktop:
            pid = desktop.launch([exe, '--inspection', '--automation', '--seconds', '120'])
            client = Client(r'\\.\pipe\ScreamSeq.Api.' + str(pid), timeout=10)
            for _ in range(100):
                try:
                    client.call('document.get')
                    break
                except TransportError:
                    time.sleep(.1)
            hwnd = desktop.hwnd(pid)
            for control_id, key in ((132, 'octave'), (133, 'editStep'), (200, None)):
                control = user.GetDlgItem(hwnd, control_id)
                desktop.send(control, 0x201, 1, 5 | (5 << 16))  # targeted click, private desktop
                desktop.send(control, 0x202, 0, 5 | (5 << 16))
                if key:
                    desktop.send(control, 0x14F, 0)  # close dropdown; native keys still route to combo
                self.assertEqual(desktop.focus(hwnd), control)
                before = client.call('workspace.get')['data']
                row = client.call('context.get')['data']['row']
                desktop.send(desktop.focus(hwnd), 0x100, 0x28)
                self.assertEqual(desktop.focus(hwnd), control)
                desktop.send(desktop.focus(hwnd), 0x100, 0x28)
                self.assertEqual(desktop.focus(hwnd), control)
                after = client.call('workspace.get')['data']
                if key:
                    self.assertEqual(after[key], before[key] + 2)
                else:
                    self.assertEqual(after['inspection']['samples']['sample'], before['inspection']['samples']['sample'] + 2)
                self.assertEqual(client.call('context.get')['data']['row'], row)
            desktop.send(hwnd, 0x100, 0x1B)  # Explicit Escape returns to pattern.
            self.assertEqual(desktop.focus(hwnd), hwnd)
            self.assertEqual(client.call('workspace.get')['data']['focus'], 'pattern')
            desktop.send(desktop.focus(hwnd), 0x100, 0x28)
            self.assertEqual(client.call('context.get')['data']['row'], row + 1)

    def test_native_keyboard_history_save_and_failed_open(self):
        client, process = self.launch()
        before = self.cell(client)
        # Z is C in the lower tracker keyboard; default octave 4, step 1.
        self.send(process, 0x100, ord('Z'))
        self.assertEqual(self.cell(client)['note'], 49)
        self.assertEqual(client.call('context.get')['data']['row'], 1)
        self.send(process, 0x111, 122)  # same Undo command as native button/palette
        self.assertEqual(self.cell(client), before)
        self.send(process, 0x111, 123)
        self.assertEqual(self.cell(client)['note'], 49)
        output = self.directory / 'ui-save.screamseq'
        self.write(client, 'document.save', path=str(output))
        self.send(process, 0x100, ord('X'))
        self.assertTrue(client.call('context.get')['data']['dirty'])
        self.send(process, 0x111, 120)  # Save current native path, no dialog
        self.assertFalse(client.call('context.get')['data']['dirty'])
        self.send(process, 0x100, ord('C'))
        snapshot = client.call('document.get')
        context = client.call('context.get')
        with self.assertRaises(ApiError):
            self.write(client, 'document.open', path=str(output))
        invalid = self.directory / 'invalid.screamseq'
        invalid.write_bytes(b'not a project')
        with self.assertRaises(ApiError):
            self.write(client, 'document.open', path=str(invalid), discard=True)
        self.assertEqual(client.call('document.get'), snapshot)
        self.assertEqual(client.call('context.get'), context)
        self.write(client, 'document.open', path=str(output), discard=True)
        self.assertEqual(self.cell(client)['note'], 49)
        self.assertFalse(client.call('context.get')['data']['dirty'])
        self.assertEqual(Path(client.call('context.get')['data']['file']), output)

    def navigate(self, client, **fields):
        context = client.call('context.get')
        return client.call('context.set', {'expectedRevision': context['revision'],
            'expectedContext': context['data']['contextRevision'], **fields})

    def test_structural_operations_catalogs_and_failed_save_preserve_state(self):
        client, _ = self.launch()
        catalog = client.call('pattern.commands')['data']
        self.assertTrue(catalog['effect'])
        self.assertTrue(catalog['volume'])
        sample = client.call('sample.get', {'sample': 1})['data']
        wave = client.call('sample.waveform.get', {'sample': 1, 'bins': 32})['data']
        self.assertEqual(len(wave['peaks']), 64)
        self.assertEqual(wave['end'], sample['frames'])
        before = client.call('document.get')
        result = self.write(client, 'pattern.create', rows=64)
        created = result['data']['pattern']
        self.assertTrue(result['changed'])
        self.write(client, 'order.edit', order=0, pattern=created, operation='assign')
        self.assertEqual(client.call('document.get')['data']['orders'][0], created)
        self.write(client, 'document.patch', title='Actual API title', tempo=143, speed=5)
        changed = client.call('document.get')
        self.assertEqual(changed['data']['title'], 'Actual API title')
        self.assertEqual(changed['data']['tempo'], 143)
        self.assertEqual(changed['data']['speed'], 5)
        self.assertFalse(self.write(client, 'sequence.select', sequence=0)['changed'])
        with self.assertRaises(ApiError):
            self.write(client, 'sequence.select', sequence=999)
        self.assertEqual(client.call('document.get'), changed)
        saved = self.directory / 'baseline.screamseq'
        self.write(client, 'document.save', path=str(saved))
        self.write(client, 'document.patch', title='Unsaved title')
        context = client.call('context.get')
        self.assertTrue(context['data']['dirty'])
        for fields in ({'path': str(self.directory / 'missing' / 'failure.screamseq')},
                       {'path': str(self.directory), 'overwrite': True},
                       {'path': str(saved), 'dryRun': True}):
            with self.assertRaises(ApiError):
                self.write(client, 'document.save', **fields)
            self.assertEqual(client.call('context.get'), context)
        self.write(client, 'history.undo', domain='document')
        self.assertEqual(client.call('document.get')['data']['title'], 'Actual API title')
        self.assertNotEqual(before['revision'], client.call('document.get')['revision'])

    def test_native_hex_volume_effect_delete_and_choosers(self):
        client, process = self.launch()
        self.navigate(client, row=1, channel=0, column=1)
        for key in '02':
            self.send(process, 0x100, ord(key))
        cell = client.call('pattern.get', {'pattern': 0, 'startRow': 1, 'rowCount': 1, 'channelCount': 1})['data']['cells'][0]
        self.assertEqual(cell['instrument'], 2)
        self.navigate(client, column=2)
        for key in '32':
            self.send(process, 0x100, ord(key))
        cell = client.call('pattern.get', {'pattern': 0, 'startRow': 1, 'rowCount': 1, 'channelCount': 1})['data']['cells'][0]
        self.assertEqual((cell['volumeCommand'], cell['volume']), (1, 32))
        self.navigate(client, column=3)
        self.send(process, 0x100, ord('A'))
        self.navigate(client, column=4)
        self.send(process, 0x100, ord('0'))
        self.send(process, 0x100, ord('6'))
        cell = client.call('pattern.get', {'pattern': 0, 'startRow': 1, 'rowCount': 1, 'channelCount': 1})['data']['cells'][0]
        self.assertEqual(cell['parameter'], 6)
        self.assertNotEqual(cell['effect'], 0)
        self.send(process, 0x100, 0x2E)
        cell = client.call('pattern.get', {'pattern': 0, 'startRow': 1, 'rowCount': 1, 'channelCount': 1})['data']['cells'][0]
        self.assertTrue(all(cell[k] == 0 for k in ('note', 'instrument', 'volumeCommand', 'volume', 'effect', 'parameter')))
        user = ctypes.WinDLL('user32')
        user.GetDlgItem.argtypes = [wintypes.HWND, ctypes.c_int]
        user.GetDlgItem.restype = wintypes.HWND
        user.SendMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
        for control_id, chosen in ((132, 5), (133, 2)):
            control = user.GetDlgItem(self.hwnd(process), control_id)
            self.assertTrue(control)
            user.SendMessageW(control, 0x14E, chosen, 0)  # CB_SETCURSEL
            self.send(process, 0x111, control_id | (1 << 16), control)
        self.navigate(client, column=0)
        self.send(process, 0x100, ord('Z'))
        cell = client.call('pattern.get', {'pattern': 0, 'startRow': 1, 'rowCount': 1, 'channelCount': 1})['data']['cells'][0]
        self.assertEqual(cell['note'], 61)
        self.assertEqual(client.call('context.get')['data']['row'], 3)

    def dialog(self, process, title=None):
        user = ctypes.WinDLL('user32')
        user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
        user.GetClassNameW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
        user.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
        user.IsWindowVisible.argtypes = [wintypes.HWND]
        callback = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
        found = []
        @callback
        def visit(hwnd, _):
            pid = wintypes.DWORD()
            user.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
            name, text = ctypes.create_unicode_buffer(128), ctypes.create_unicode_buffer(512)
            user.GetClassNameW(hwnd, name, 128)
            user.GetWindowTextW(hwnd, text, 512)
            if pid.value == process.pid and name.value == '#32770' and user.IsWindowVisible(hwnd) and (title is None or text.value == title):
                found.append(hwnd)
            return True
        for _ in range(100):
            found.clear()
            user.EnumWindows(visit, 0)
            if found:
                return found[0]
            time.sleep(.02)
        self.fail('task-owned native dialog not found')

    def post(self, process, command):
        user = ctypes.WinDLL('user32')
        user.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
        self.assertTrue(user.PostMessageW(self.hwnd(process), 0x111, command, 0))

    def choose_dialog_path(self, process, path):
        dlg = self.dialog(process)
        user = ctypes.WinDLL('user32')
        user.GetDlgItem.argtypes = [wintypes.HWND, ctypes.c_int]
        user.GetDlgItem.restype = wintypes.HWND
        user.SendMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
        user.PostMessageW.argtypes = user.SendMessageW.argtypes
        name = None  # Ignore legacy hidden compatibility fields in the Vista dialog.
        if not name:
            callback = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
            candidates = []
            user.GetClassNameW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
            @callback
            def visit(hwnd, _):
                kind = ctypes.create_unicode_buffer(64)
                user.GetClassNameW(hwnd, kind, 64)
                user.GetDlgCtrlID.argtypes = [wintypes.HWND]
                user.IsWindowVisible.argtypes = [wintypes.HWND]
                if kind.value == 'Edit' and user.GetDlgCtrlID(hwnd) in (1001, 1148) and user.IsWindowVisible(hwnd):
                    user.GetParent.argtypes = [wintypes.HWND]
                    user.GetParent.restype = wintypes.HWND
                    parent_kind = ctypes.create_unicode_buffer(64)
                    user.GetClassNameW(user.GetParent(hwnd), parent_kind, 64)
                    if parent_kind.value == 'ComboBox':
                        candidates.append(hwnd)
                return True
            user.EnumChildWindows.argtypes = [wintypes.HWND, callback, wintypes.LPARAM]
            user.EnumChildWindows(dlg, visit, 0)
            self.assertEqual(len(candidates), 1, 'Vista common dialog filename editor')
            name = candidates[0]
        self.assertTrue(name, 'real filename input')
        value = ctypes.create_unicode_buffer(str(path))
        # DirectUI populates the initial name asynchronously. Do not confirm
        # until the exact disposable path survived initialization/readback.
        stable = 0
        for _ in range(40):
            readback = ctypes.create_unicode_buffer(32768)
            user.SendMessageW(name, 0xD, 32768, ctypes.addressof(readback))
            if readback.value == str(path):
                stable += 1
                if stable == 4:
                    break
            else:
                stable = 0
                user.SendMessageW(name, 0xB1, 0, -1)  # EM_SETSEL
                user.SendMessageW(name, 0xC2, 1, ctypes.addressof(value))  # EM_REPLACESEL
            time.sleep(.08)
        self.assertEqual(stable, 4, 'filename path must settle before confirmation: ' + repr(readback.value))
        button = user.GetDlgItem(dlg, 1)
        self.assertTrue(button, 'real Save/Open button')
        user.PostMessageW(button, 0xF5, 0, 0)  # BM_CLICK real Save/Open button

    def test_native_save_as_open_and_unsaved_cancel_dialogs(self):
        client, process = self.launch()
        self.send(process, 0x100, ord('X'))
        path = self.directory / 'native-dialog.screamseq'
        self.post(process, 121)
        self.choose_dialog_path(process, path)
        for _ in range(100):
            if path.exists():
                break
            time.sleep(.03)
        self.assertTrue(path.exists(), 'native Save As published file')
        self.assertEqual(Path(client.call('context.get')['data']['file']), path)
        self.send(process, 0x100, ord('C'))
        before = client.call('context.get')
        self.post(process, 119)
        dlg = self.dialog(process, 'Unsaved ScreamSeq project')
        user = ctypes.WinDLL('user32')
        user.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
        user.PostMessageW(dlg, 0x111, 2, 0)  # Cancel; no file dialog and no loss
        time.sleep(.1)
        self.assertEqual(client.call('context.get'), before)
        self.post(process, 119)
        dlg = self.dialog(process, 'Unsaved ScreamSeq project')
        user.PostMessageW(dlg, 0x111, 7, 0)  # Explicit Discard
        time.sleep(.1)
        self.choose_dialog_path(process, path)
        for _ in range(100):
            state = client.call('context.get')
            if not state['data']['dirty']:
                break
            time.sleep(.02)
        self.assertFalse(state['data']['dirty'])
        self.assertEqual(self.cell(client)['note'], 51)
        self.assertFalse(client.call('document.get')['data']['canUndo'])

    def test_rectangular_copy_paste_and_delete_keep_one_undo_step(self):
        # Inspection keeps the clipboard private to the task-owned document.
        sequence = ctypes.windll.user32.GetClipboardSequenceNumber()
        client, process = self.launch()
        self.write(client, 'pattern.apply', cells=[
            {'pattern': 0, 'row': 0, 'channel': 0, 'note': 61},
            {'pattern': 0, 'row': 1, 'channel': 1, 'note': 65}])
        state = client.call('workspace.get')['data']
        grid, scale = state['geometry']['pattern'], state['dpi'] / 96
        def mouse(msg, x, y, flags=0):
            self.send(process, msg, flags, round(x*scale) | (round(y*scale) << 16))
        x, y = grid['x']+44, grid['y']+56
        mouse(0x201, x, y, 1)
        mouse(0x200, x+112, y+18, 1)
        mouse(0x202, x+112, y+18)
        self.assertEqual(client.call('context.get')['data']['selection'],
            {'startRow': 0, 'endRow': 1, 'startChannel': 0, 'endChannel': 1})
        source = client.call('pattern.get', {'pattern': 0, 'rowCount': 2, 'channelCount': 2})['data']['cells']
        self.send(process, 0x111, 124)
        self.navigate(client, row=8, channel=2)
        original = client.call('pattern.get', {'pattern': 0, 'startRow': 8, 'rowCount': 2, 'startChannel': 2, 'channelCount': 2})['data']['cells']
        self.send(process, 0x111, 125)
        copied = client.call('pattern.get', {'pattern': 0, 'startRow': 8, 'rowCount': 2, 'startChannel': 2, 'channelCount': 2})['data']['cells']
        for a,b in zip(source,copied):
            self.assertEqual({k:v for k,v in a.items() if k not in ('row','channel')},
                             {k:v for k,v in b.items() if k not in ('row','channel')})
        self.send(process, 0x111, 122)
        self.assertEqual(client.call('pattern.get', {'pattern': 0, 'startRow': 8, 'rowCount': 2, 'startChannel': 2, 'channelCount': 2})['data']['cells'], original)
        self.assertEqual(ctypes.windll.user32.GetClipboardSequenceNumber(), sequence)

    def test_worker_keeps_stop_and_navigation_responsive(self):
        import concurrent.futures
        client, process = self.launch()
        self.write(client, 'pattern.create', rows=1024)
        user = ctypes.WinDLL('user32')
        user.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
        hwnd = self.hwnd(process)
        with concurrent.futures.ThreadPoolExecutor(1) as pool:
            work = pool.submit(self.write, client, 'document.patch', channels=96)
            observed = False
            for _ in range(1000):
                title = ctypes.create_unicode_buffer(512)
                user.GetWindowTextW(hwnd, title, 512)
                if 'Document worker busy' in title.value:
                    observed = True
                    break
                if work.done():
                    break
                time.sleep(.001)
            self.assertTrue(observed, 'observe actual outstanding worker work, not an idle responsiveness test')
            start = time.monotonic()
            self.send(process, 0x111, 102)  # Stop while structural validation/cache work runs
            self.send(process, 0x100, 0x28)  # Down remains navigation, independent of worker
            self.assertLess(time.monotonic() - start, .5)
            self.assertTrue(work.result(timeout=15)['changed'])
        self.assertEqual(client.call('context.get')['data']['row'], 1)
        self.assertEqual(client.call('document.get')['data']['channels'], 96)
        self.assertFalse(client.call('transport.get')['data']['playing'])

    def test_failed_atomic_replace_keeps_file_path_and_dirty(self):
        client, _ = self.launch()
        path = self.directory / 'locked.screamseq'
        self.write(client, 'document.save', path=str(path))
        baseline = path.read_bytes()
        self.write(client, 'document.patch', title='Unpublished changes')
        before = client.call('context.get')
        kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        kernel.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
        kernel.CreateFileW.restype = wintypes.HANDLE
        kernel.CloseHandle.argtypes = [wintypes.HANDLE]
        handle = kernel.CreateFileW(str(path), 0x80000000, 1, None, 3, 0, None)
        self.assertNotEqual(handle, ctypes.c_void_p(-1).value)
        try:
            with self.assertRaises(ApiError):
                self.write(client, 'document.save', path=str(path), overwrite=True)
        finally:
            kernel.CloseHandle(handle)
        self.assertEqual(path.read_bytes(), baseline)
        self.assertEqual(client.call('context.get'), before)
        self.assertTrue(before['data']['dirty'])
        self.assertEqual(sorted(p.name for p in self.directory.iterdir()), ['locked.screamseq'])

    def test_stale_inspector_return_cannot_destroy_cursor_after_undo(self):
        client, _ = self.launch()
        created = self.write(client, 'pattern.create', rows=64)['data']['pattern']
        self.navigate(client, pattern=created)
        client.call('workspace.panel', {'panel': 'samples', 'focus': True, 'pinned': True})
        self.write(client, 'history.undo', domain='document')
        before = client.call('context.get')
        with self.assertRaises(ApiError):
            client.call('workspace.panel', {'panel': 'samples', 'return': True})
        self.assertEqual(client.call('context.get'), before)
        self.assertNotEqual(before['data']['pattern'], created)

    def test_native_sample_list_selection_survives_document_refresh(self):
        client, process = self.launch()
        user = ctypes.WinDLL('user32')
        user.GetDlgItem.argtypes = [wintypes.HWND, ctypes.c_int]
        user.GetDlgItem.restype = wintypes.HWND
        user.SendMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
        control = user.GetDlgItem(self.hwnd(process), 200)
        self.assertTrue(control)
        user.SendMessageW(control, 0x186, 1, 0)  # LB_SETCURSEL
        self.send(process, 0x111, 200 | (1 << 16), control)
        before = client.call('workspace.get')['data']
        self.assertEqual(before['inspection']['samples']['sample'], 2)
        self.write(client, 'pattern.apply', cells=[{'pattern': 0, 'row': 3, 'channel': 0, 'note': 72}])
        self.assertEqual(user.SendMessageW(control, 0x188, 0, 0), 1)  # LB_GETCURSEL
        after = client.call('workspace.get')['data']
        self.assertEqual(after['inspection']['samples'], before['inspection']['samples'])
        self.assertEqual(after['pins'], before['pins'])
        self.assertEqual(after['focus'], before['focus'])

    def test_shared_long_title_survives_structural_undo_and_native_reopen(self):
        client, _ = self.launch()
        title = 'Long native title ' + 'x' * 82
        self.assertEqual(len(title), 100)
        self.write(client, 'document.patch', title=title)
        before = client.call('document.get')['data']
        self.write(client, 'pattern.create', rows=64)
        self.write(client, 'history.undo', domain='document')
        self.assertEqual(client.call('document.get')['data']['title'], title)
        self.assertEqual(client.call('document.get')['data']['patterns'], before['patterns'])
        target = self.directory / 'long-title.screamseq'
        self.write(client, 'document.save', path=str(target))
        reopened, _ = self.launch(target)
        self.assertEqual(reopened.call('document.get')['data']['title'], title)
        self.assertEqual(reopened.call('document.get')['data']['patterns'], before['patterns'])

    def test_legacy_native_dry_run_reports_actual_output_version(self):
        import plistlib
        legacy = self.directory / 'legacy.resonance'
        legacy.write_bytes(plistlib.dumps({'version': 1, 'module': (ROOT / 'test/test.mod').read_bytes(), 'plugins': [], 'automation': []}, fmt=plistlib.FMT_BINARY))
        client, _ = self.launch(legacy)
        before = client.call('context.get')
        target = self.directory / 'upgraded.screamseq'
        result = self.write(client, 'document.save', path=str(target), dryRun=True)
        self.assertEqual(result['data']['projectVersion'], 4)
        self.assertFalse(target.exists())
        self.assertEqual(client.call('context.get'), before)

    def test_timeline_worker_save_reopen(self):
        client, _ = self.launch()
        before = client.call('pattern.notes.get', {'pattern': 0})['data']['events']
        events = [{'position': 16384, 'channel': 0, 'instrument': 1, 'note': 61, 'velocity': 100}]
        self.write(client, 'pattern.notes.set', pattern=0, events=events, dryRun=True)
        self.assertEqual(client.call('pattern.notes.get', {'pattern': 0})['data']['events'], before)
        self.write(client, 'pattern.notes.set', pattern=0, events=events)
        self.assertEqual(client.call('pattern.notes.get', {'pattern': 0})['data']['events'], events)
        self.write(client, 'history.undo', domain='document')
        self.assertEqual(client.call('pattern.notes.get', {'pattern': 0})['data']['events'], before)
        self.write(client, 'history.redo', domain='document')
        self.write(client, 'document.timing.set', mode='modern', tempo=137.125, rowsPerBeat=4, rowsPerMeasure=16, groove=[1, 1, 1, 1])
        timing = client.call('document.timing.get')['data']
        self.assertAlmostEqual(timing['tempo'], 137.125)
        reference = client.call('automation.formula.reference')['data']
        self.assertTrue(reference['symbols'])
        preview = client.call('automation.formula.preview', {'rows': 4, 'points': [
            {'position': 0, 'value': 0, 'curve': 'linear'}, {'position': 512, 'value': 1}], 'samples': 3})['data']
        self.assertEqual(len(preview['values']), 3)
        target = self.directory / 'timeline.screamseq'
        self.write(client, 'document.save', path=str(target))
        reopened, _ = self.launch(target)
        self.assertEqual(reopened.call('pattern.notes.get', {'pattern': 0})['data']['events'], events)
        self.assertEqual(reopened.call('document.timing.get')['data'], timing)

    def test_module_open_uses_real_format_and_filename(self):
        source = self.directory / 'module-copy.mod'
        shutil.copyfile(ROOT / 'test/test.mod', source)
        client, process = self.launch(source)
        self.assertEqual(client.call('document.get')['data']['format'], 'MOD')
        self.assertEqual(Path(client.call('context.get')['data']['file']), source)
        self.assertNotEqual(client.call('document.get')['data']['title'], 'Midnight Circuit')
        user = ctypes.WinDLL('user32')
        user.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
        title = ctypes.create_unicode_buffer(512)
        user.GetWindowTextW(self.hwnd(process), title, 512)
        self.assertIn('module-copy.mod', title.value)

    def test_edit_undo_redo_save_reopen_and_guards(self):
        reference = os.environ.get('SCREAMSEQ_REFERENCE_PROJECT')
        source = self.directory / 'input.screamseq'
        if reference:
            original = Path(reference).read_bytes()
            shutil.copyfile(reference, source)
        client, _ = self.launch(source if reference else None)
        before = client.call('document.get')
        first = self.cell(client)
        params = {'expectedRevision': before['revision'], 'cells': [{'pattern': 0, 'row': 0, 'channel': 0, 'note': 61}]}
        result = client.call('pattern.apply', params, request_id='editor-dedupe')
        self.assertTrue(result['changed'])
        self.assertEqual(self.cell(client)['note'], 61)
        self.assertEqual(client.call('pattern.apply', params, request_id='editor-dedupe'), result)
        with self.assertRaises(ApiError) as stale:
            client.call('pattern.apply', params)
        self.assertEqual(stale.exception.code, -32001)
        self.write(client, 'history.undo', domain='document')
        self.assertEqual(self.cell(client), first)
        self.write(client, 'history.redo', domain='document')
        self.assertEqual(self.cell(client)['note'], 61)
        current = client.call('document.get')
        no_op = self.write(client, 'pattern.apply', cells=params['cells'])
        self.assertFalse(no_op['changed'])
        with self.assertRaises(ApiError):
            self.write(client, 'pattern.apply', cells=[{'pattern': 0, 'row': 0, 'channel': 0, 'note': True}])
        self.assertEqual(client.call('document.get'), current)
        output = self.directory / 'saved-曲.screamseq'
        context = client.call('context.get')
        dry = self.write(client, 'document.save', path=str(output), dryRun=True)
        self.assertFalse(dry['data']['written'])
        self.assertFalse(output.exists())
        self.assertEqual(client.call('context.get'), context)
        self.write(client, 'document.save', path=str(output))
        self.assertTrue(output.exists())
        self.assertFalse(client.call('context.get')['data']['dirty'])
        self.assertEqual(Path(client.call('context.get')['data']['file']), output)
        saved = output.read_bytes()
        for fields in ({'path': str(output)}, {'path': 'relative.screamseq'}, {'path': str(self.directory / 'bad.mod')}, {'path': str(output), 'overwrite': 1}):
            with self.assertRaises(ApiError):
                self.write(client, 'document.save', **fields)
        self.assertEqual(output.read_bytes(), saved)
        reopened, _ = self.launch(output)
        self.assertEqual(self.cell(reopened)['note'], 61)
        for key in ('nativeSummary', 'instruments', 'nativePlugins', 'samples', 'patterns'):
            self.assertEqual(reopened.call('document.get')['data'][key], current['data'][key])
        if reference:
            self.assertEqual(Path(reference).read_bytes(), original)

if __name__ == '__main__':
    unittest.main()
