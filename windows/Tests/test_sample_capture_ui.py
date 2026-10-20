"""Sample-recorder ownership through an owned pipe; never starts a microphone."""
import ctypes
from ctypes import wintypes
import unittest

import private_desktop
import test_pattern_performance as support


class SampleCaptureUiTests(unittest.TestCase):
    setUp = support.PatternPerformanceTests.setUp
    doc = support.PatternPerformanceTests.doc
    read = support.PatternPerformanceTests.read
    write = support.PatternPerformanceTests.write

    def recorder(self):
        found = []
        @private_desktop.callback
        def visit(hwnd, _):
            pid = wintypes.DWORD()
            private_desktop.user.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
            name = ctypes.create_unicode_buffer(100)
            private_desktop.user.GetClassNameW(hwnd, name, 100)
            if pid.value == self.pid and name.value == 'ScreamSeq.SampleRecording':
                found.append(hwnd)
            return True
        private_desktop.check(private_desktop.user.EnumDesktopWindows(self.desktop.desktop, visit, 0))
        self.assertEqual(len(found), 1)
        return found[0]

    def state(self):
        return self.read('workspace.get')['sampleRecording']

    def test_setup_retention_departure_and_fresh_owner_without_capture(self):
        path = self.folder / 'recorder-owner.screamseq'
        self.write('document.save', path=str(path))
        main = self.desktop.hwnd(self.pid)
        self.desktop.send(main, 0x111, 577)
        recorder = self.recorder()
        self.assertIsNone(self.state()['lifecycleReview'])
        user = private_desktop.user
        user.GetDlgItem.argtypes = [wintypes.HWND, ctypes.c_int]
        user.GetDlgItem.restype = wintypes.HWND
        name = user.GetDlgItem(recorder, 6508)
        text = ctypes.create_unicode_buffer('Retained recorder name')
        self.desktop.send(name, 0xC, 0, ctypes.addressof(text))
        captured = self.state()
        before = self.doc()
        self.desktop.send(recorder, 0x111, 6510)
        self.assertFalse(self.state()['visible'])
        with self.assertRaises(support.ApiError) as refused:
            self.write('document.open', path=str(path), discard=True)
        self.assertEqual(refused.exception.code, -32002)
        self.assertEqual(refused.exception.data, dict(writeOutcome='notCommitted'))
        self.assertEqual(self.doc(), before)
        self.desktop.send(main, 0x111, 577)
        reopened = self.state()
        self.assertEqual(self.recorder(), recorder)
        for key in ('name', 'generation', 'draftDocument', 'draftRevision'):
            self.assertEqual(reopened[key], captured[key])
        self.assertIsNone(reopened['lifecycleReview'])
        self.desktop.send(recorder, 0x111, 6512)  # No unresolved operation: no action.
        self.assertEqual(self.doc(), before)
        self.desktop.send(recorder, 0x111, 6511)  # Explicitly discard setup only.
        self.write('document.open', path=str(path), discard=True)
        self.assertNotEqual(self.doc()['documentId'], before['documentId'])
        self.desktop.send(main, 0x111, 577)
        fresh = self.state()
        self.assertEqual(fresh['name'], 'Recording')
        self.assertFalse(fresh['draft'])
        self.assertIsNone(fresh['lifecycleReview'])
        current_take = self.read('sample.recording.get')
        self.assertFalse(current_take['take'])
        self.assertFalse(current_take['capturing'])
        self.assertEqual(current_take['frames'], 0)
