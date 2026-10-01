"""Real sample controls on a never-switched desktop, with exact-PID API readback."""
import base64
import ctypes
import os
from pathlib import Path
import struct
import sys
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'windows/Api'))
from client import Client, TransportError
from private_desktop import PrivateDesktop, user

ctypes.windll.user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))


class SampleEditorTests(unittest.TestCase):
    def setUp(self):
        self.folder = Path(self.enterContext(tempfile.TemporaryDirectory(prefix='sample-ui-', dir=os.environ['TMPDIR'])))
        self.desktop = self.enterContext(PrivateDesktop())
        exe = os.environ['SCREAMSEQ_TEST_EXE']
        pid = self.desktop.launch([exe, '--inspection', '--automation', '--seconds', '120'])
        self.client = Client(r'\\.\pipe\ScreamSeq.Api.' + str(pid), timeout=10)
        for _ in range(100):
            try:
                self.client.call('document.get')
                break
            except TransportError:
                time.sleep(.1)
        else:
            self.fail('owned app did not start')
        self.hwnd = self.desktop.hwnd(pid)
        self.raw = (1000, -2000, 3000, -4000, 5000, -6000, 7000, -8000)
        self.write('sample.pcm.set', sample=1, format='s16le', channels=1, rate=48000,
                   data=base64.b64encode(struct.pack('<8h', *self.raw)).decode())
        self.client.call('workspace.layout', {'name': 'Sound design'})

    def write(self, method, **params):
        return self.client.call(method, {'expectedRevision': self.client.call('document.get')['revision'], **params})

    def state(self):
        return self.client.call('workspace.get')['data']['sampleEditor']

    def pcm(self):
        data = base64.b64decode(self.client.call('sample.pcm.get', {'sample': 1})['data']['data'])
        return struct.unpack('<' + str(len(data) // 2) + 'h', data)

    def command(self, control_id):
        control = user.GetDlgItem(self.hwnd, control_id)
        self.assertTrue(control)
        self.desktop.send(control, 0xF5)  # actual native button BM_CLICK

    def field(self, control_id, text):
        control = user.GetDlgItem(self.hwnd, control_id)
        value = ctypes.create_unicode_buffer(text)
        self.desktop.send(control, 0xC, 0, ctypes.addressof(value))  # WM_SETTEXT + EN_CHANGE

    def field_value(self, control_id):
        value = ctypes.create_unicode_buffer(32)
        self.desktop.send(user.GetDlgItem(self.hwnd, control_id), 0xD, len(value), ctypes.addressof(value))
        return value.value

    def mouse(self, message, fraction, buttons=0):
        state = self.client.call('workspace.get')['data']
        rect = state['sampleEditor']['waveform']
        scale = state['dpi'] / 96
        x = round((rect['x'] + rect['width'] * fraction) * scale)
        y = round((rect['y'] + rect['height'] * .5) * scale)
        self.desktop.send(self.hwnd, message, buttons, x | (y << 16))

    def test_waveform_selection_process_loop_history_and_save(self):
        self.mouse(0x201, .25, 1)
        self.mouse(0x200, .75, 1)
        self.mouse(0x202, .75)
        self.assertEqual((self.state()['start'], self.state()['end']), (2, 6))
        self.command(203)  # Reverse
        expected = self.raw[:2] + tuple(reversed(self.raw[2:6])) + self.raw[6:]
        self.assertEqual(self.pcm(), expected)
        self.command(122)  # Undo
        self.assertEqual(self.pcm(), self.raw)
        self.command(123)  # Redo
        self.assertEqual(self.pcm(), expected)
        self.command(208)  # Set loop
        info = self.client.call('sample.get', {'sample': 1})['data']
        self.assertEqual((info['loopStart'], info['loopEnd'], info['loop']), (2, 6, True))
        path = self.folder / 'selection.screamseq'
        self.write('document.save', path=str(path))
        self.command(207)  # Trim
        self.assertEqual(self.pcm(), expected[2:6])
        self.assertEqual((self.state()['start'], self.state()['end'], self.state()['frames']), (2, 4, 4))
        self.write('document.open', path=str(path), discard=True)
        self.assertEqual(self.pcm(), expected)
        self.assertEqual((self.state()['start'], self.state()['end']), (0, 8))

    def test_range_draft_never_retargets_after_external_change(self):
        self.field(230, '2')
        self.field(231, '6')
        self.assertTrue(self.state()['draft'])
        self.write('sample.process', sample=1, operation='reverse')
        before = self.client.call('document.get')
        self.command(203)
        self.assertEqual(self.client.call('document.get'), before)
        self.assertTrue(self.state()['draft'])
        self.assertEqual(self.field_value(230), '2')
        self.assertEqual(self.field_value(231), '6')
        samples = user.GetDlgItem(self.hwnd, 200)
        self.desktop.send(samples, 0x186, 1)  # LB_SETCURSEL, select sample 2
        self.desktop.send(self.hwnd, 0x111, 200 | (1 << 16), samples)
        self.command(203)
        self.assertEqual(self.client.call('document.get'), before)
        self.assertEqual(self.state()['sample'], 2)
        self.assertTrue(self.state()['draft'])
        self.command(202)  # explicit Select all discards the stale draft
        self.assertFalse(self.state()['draft'])

    def test_range_fields_validation_and_drag_cancellation(self):
        before = self.client.call('document.get')
        for a, b in [('6', '2'), ('0', '99'), ('', '3')]:
            self.field(230, a)
            self.field(231, b)
            self.command(203)
            self.assertEqual(self.client.call('document.get'), before)
        self.field(230, '1')
        self.field(231, '7')
        self.command(209)
        self.assertEqual((self.state()['start'], self.state()['end']), (1, 7))
        self.assertFalse(self.state()['draft'])
        self.mouse(0x201, .25, 1)
        self.mouse(0x200, .5, 1)
        self.desktop.send(self.hwnd, 0x100, 0x1B)  # Escape restores pre-drag selection
        self.assertEqual((self.state()['start'], self.state()['end']), (1, 7))
        self.mouse(0x201, .25, 1)
        self.write('document.patch', title='Concurrent edit')
        selected = self.state()
        self.mouse(0x200, .75, 1)
        self.mouse(0x202, .75)
        self.assertEqual(self.state(), selected)

    def test_normal_and_sustain_loop_controls_preserve_pcm(self):
        self.field(230, '1')
        self.field(231, '7')
        self.command(209)
        self.command(219)  # Normal loop from selection.
        self.command(215)  # Forward -> ping pong.
        info = self.client.call('sample.get', {'sample': 1})['data']
        self.assertTrue(info['pingpong'])
        self.command(215)  # Ping pong -> reverse.
        info = self.client.call('sample.get', {'sample': 1})['data']
        self.assertTrue(info['reverseLoop'])
        self.assertFalse(info['pingpong'])
        self.command(214)
        self.assertFalse(self.client.call('sample.get', {'sample': 1})['data']['loop'])
        self.command(214)
        self.command(216)  # Sustain loop from selection.
        self.command(218)
        info = self.client.call('sample.get', {'sample': 1})['data']
        self.assertEqual((info['loopStart'], info['loopEnd'], info['loop'], info['reverseLoop']), (1, 7, True, False))
        self.assertEqual((info['sustainStart'], info['sustainEnd'], info['sustainLoop'], info['sustainPingpong']), (1, 7, True, True))
        self.command(217)
        self.assertFalse(self.client.call('sample.get', {'sample': 1})['data']['sustainLoop'])
        self.command(122)
        self.assertTrue(self.client.call('sample.get', {'sample': 1})['data']['sustainLoop'])
        self.assertEqual(self.pcm(), self.raw)
        path = self.folder / 'loops.screamseq'
        self.write('document.save', path=str(path))
        self.write('document.open', path=str(path))
        self.assertEqual(self.client.call('sample.get', {'sample': 1})['data'], info)


if __name__ == '__main__':
    unittest.main()
