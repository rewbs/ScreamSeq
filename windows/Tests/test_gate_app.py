"""Advanced gate through the real keyboard, inspector, worker and native project."""
import ctypes
from ctypes import wintypes
import os
import base64
import math
import struct
import time
from pathlib import Path
import unittest

import private_desktop
import test_pattern_performance as support


class GateAppTests(unittest.TestCase):
    setUp = support.PatternPerformanceTests.setUp
    doc = support.PatternPerformanceTests.doc
    read = support.PatternPerformanceTests.read
    write = support.PatternPerformanceTests.write
    navigate = support.PatternPerformanceTests.navigate
    key = support.PatternPerformanceTests.key

    def gate_window(self):
        found = []
        @private_desktop.callback
        def visit(hwnd, _):
            pid = wintypes.DWORD()
            private_desktop.user.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
            name = ctypes.create_unicode_buffer(128)
            private_desktop.user.GetClassNameW(hwnd, name, len(name))
            if pid.value == self.pid and name.value == 'ScreamSeq.GateInspector':
                found.append(hwnd)
            return True
        private_desktop.check(private_desktop.user.EnumDesktopWindows(self.desktop.desktop, visit, 0))
        self.assertEqual(len(found), 1)
        return found[0]

    def press(self, identifier):
        window = self.gate_window()
        control = private_desktop.user.GetDlgItem(window, identifier)
        self.assertTrue(control)
        self.desktop.send(control, 0xF5)  # BM_CLICK, including native button notification.
        if identifier in (6107, 6108, 6109):
            deadline = time.monotonic() + 5
            while self.read('workspace.get').get('gateInspector', {}).get('applyQueued', False):
                self.assertLess(time.monotonic(), deadline, self.status())
                time.sleep(.02)

    def field(self, identifier, value):
        control = private_desktop.user.GetDlgItem(self.gate_window(), identifier)
        text = ctypes.create_unicode_buffer(str(value))
        self.desktop.send(control, 0xC, 0, ctypes.addressof(text))

    def wait_automatic(self):
        deadline=time.monotonic()+8
        while True:
            state=self.read('workspace.get')['gateInspector']
            if not state['retainedDraft'] and not state['pending'] and not state['applyQueued'] and not state['automaticPending']:
                return
            self.assertLess(time.monotonic(),deadline,self.status())
            time.sleep(.03)

    def render(self, name):
        path = self.folder / (name + '.wav')
        self.write('document.exportAudio', path=str(path))
        data = path.read_bytes()
        self.assertEqual(struct.unpack_from('<HHIIHH', data, 20), (3, 2, 48000, 384000, 8, 32))
        if os.environ.get('SCREAMSEQ_GATE_EVIDENCE_DIR'):
            evidence = Path(os.environ['SCREAMSEQ_GATE_EVIDENCE_DIR'])
            evidence.mkdir(parents=True, exist_ok=True)
            (evidence / path.name).write_bytes(data)
        return struct.unpack('<' + str((len(data)-44)//4) + 'f', data[44:])

    def status(self):
        control = private_desktop.user.GetDlgItem(self.gate_window(), 6112)
        value = ctypes.create_unicode_buffer(1024)
        self.desktop.send(control, 0xD, len(value), ctypes.addressof(value))
        return value.value

    def test_typed_gt_apply_reaches_pattern(self):
        self.navigate(pattern=0, row=0, channel=0, column=3, following=False)
        self.key('G')
        self.key('T')
        before = self.read('pattern.effects.get', pattern=0)
        self.press(6107)
        after = self.read('pattern.effects.get', pattern=0)
        self.assertNotEqual(after, before, self.status())
        gate = next(c for c in after['commands'] if c.get('native') == 'gate')
        self.assertEqual((gate['position'], gate['channel'], gate['column']), (0, 0, 0))
        self.write('history.undo', domain='all')
        self.assertEqual(self.read('pattern.effects.get', pattern=0), before)
        self.write('history.redo', domain='all')
        self.assertEqual(self.read('pattern.effects.get', pattern=0), after)
        path = self.folder / 'gate.screamseq'
        self.write('document.save', path=str(path))
        self.write('document.open', path=str(path), discard=True)
        self.assertEqual(self.read('pattern.effects.get', pattern=0), after)

    def test_changed_volume_gate_reaches_hosted_sample_audio(self):
        self.write('document.new', discard=True)
        pcm = struct.pack('<8192h', *(round(6000*math.sin(2*math.pi*i/32)) for i in range(8192)))
        sample = self.write('sample.pcm.set', format='s16le', channels=1, rate=8192,
                            data=base64.b64encode(pcm).decode(), name='Gate test tone')['sample']
        self.write('sample.loops.set', sample=sample, normal=dict(start=0, end=8192, enabled=True, pingpong=False))
        self.write('pattern.apply', cells=[dict(pattern=0, row=0, channel=0, note=61, instrument=sample)])
        self.write('mixer.enable')
        baseline = self.render('ungated')
        self.assertGreater(max(abs(v) for v in baseline), .001)
        self.navigate(pattern=0, row=0, channel=0, column=3, following=False)
        self.key('G'); self.key('T')
        self.field(6200, 25); self.field(6201, 25)
        self.field(6202, '3/8'); self.field(6205, '1/64')
        self.wait_automatic()
        commands = self.read('pattern.effects.get', pattern=0)['commands']
        self.assertTrue(any(c.get('native') == 'gate' for c in commands), self.status())
        gated = self.render('quarter-gain')
        self.assertEqual(len(gated), len(baseline))
        # Compare active song time; export's post-song tail has stopped gates.
        error = max(abs(g-.25*b) for g,b in zip(gated[400:48000], baseline[400:48000]))
        print('Gate PCM: 48 kHz float stereo, active-song quarter-gain maximum error', error)
        self.assertLess(error, 2e-6, f'Gate must change actual hosted sample PCM; maximum error {error}')
        self.field(6200, 0); self.field(6201, 100)
        self.field(6202, '.5'); self.field(6205, 0)
        self.wait_automatic()
        chopped = self.render('chopped')
        beat = 60 / self.doc()['data']['tempo']
        errors = []
        for fraction, gain in ((.125, 1), (.375, 0), (.625, 1), (.875, 0)):
            start = round(48000*beat*fraction)*2
            errors.append(max(abs(g-gain*b) for g,b in zip(chopped[start:start+400], baseline[start:start+400])))
        self.assertLess(max(errors), 2e-6, 'Half-beat gate must alternate audible and silent windows')
        print('Gate PCM: four alternating open/closed windows, maximum error', max(errors))

    @unittest.skipUnless(os.environ.get('SCREAMSEQ_GATE_REPRO_PROJECT'), 'optional disposable user-song reproduction')
    def test_typed_gt_in_existing_song(self):
        self.write('document.open', path=os.environ['SCREAMSEQ_GATE_REPRO_PROJECT'], discard=True)
        self.test_typed_gt_apply_reaches_pattern()


if __name__ == '__main__':
    unittest.main()
