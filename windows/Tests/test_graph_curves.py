"""Graph curve drafts through actual native controls and the private PID API."""
import ctypes
from ctypes import wintypes
import json
import os
from pathlib import Path
import shutil
import subprocess
import time
import unittest
import test_graph_editor as support


class GraphCurveTests(unittest.TestCase):
    setUp = support.GraphEditorTests.setUp
    doc = support.GraphEditorTests.doc
    read = support.GraphEditorTests.read
    write = support.GraphEditorTests.write
    control = support.GraphEditorTests.control
    command = support.GraphEditorTests.command
    field = support.GraphEditorTests.field
    select = support.GraphEditorTests.select
    mouse = support.GraphEditorTests.mouse
    point = support.GraphEditorTests.point
    key = support.GraphEditorTests.key
    add_gain = support.GraphEditorTests.add_gain

    def state(self):
        return self.read('workspace.get')['graphCurve']

    def settle(self):
        self.desktop.send(self.desktop.hwnd(self.pid), 0x113, 5)
        deadline = time.monotonic()+5
        while time.monotonic()<deadline:
            state = self.state()
            if not state['pending'] and (state['previewSamples'] or not state['graph']):
                return state
            time.sleep(.02)
        self.fail(str(self.state()))

    def setup_curve(self):
        graph = self.write('graph.create')['graph']
        node = self.write('graph.node.add', graph=graph, kind='automation')['node']
        self.command(430)
        self.select(442, 2)
        self.select(443, 5)
        self.settle()
        self.assertEqual((self.state()['graph'], self.state()['node']), (graph, node))
        return graph, node

    def saved(self, graph, node, pattern=0):
        return self.read('graph.automation.get', graph=graph, node=node, pattern=pattern)

    def test_native_curve_points_rejection_history_other_pattern_and_reopen(self):
        graph, node = self.setup_curve()
        pattern = self.write('pattern.create', rows=32)['pattern']
        other = [dict(position=3*256, value=.4, curve='step')]
        self.write('graph.automation.set', graph=graph, node=node, pattern=pattern, points=other)
        self.command(487)
        self.command(490)  # Ramp.
        self.field(483, '.25')
        self.field(484, '125')
        before = self.doc()
        self.command(488)
        self.assertEqual(self.doc(), before)
        self.assertTrue(self.state()['fieldDraft'])
        self.field(484, '25')
        self.select(481, 2)  # Smooth.
        self.command(488)
        retained = self.state()['points']
        self.field(483, str(retained[-1]['position']/256))
        self.command(488)  # A collision must not silently delete the other point.
        self.assertEqual(self.state()['points'], retained)
        self.assertTrue(self.state()['fieldDraft'])
        self.field(483, '.25')
        self.command(488)
        self.command(486)
        points = self.saved(graph, node)['points']
        self.assertEqual(points[0], dict(position=64, value=.25, curve='smooth'))
        self.assertEqual(self.saved(graph, node, pattern)['points'], other)
        self.assertFalse(self.state()['dirty'])
        self.write('history.undo', domain='document')
        self.assertEqual(self.saved(graph, node)['points'], [])
        self.write('history.redo', domain='document')
        self.assertEqual(self.saved(graph, node)['points'], points)
        path = self.folder / 'curve.screamseq'
        self.write('document.save', path=str(path))
        self.write('graph.automation.set', graph=graph, node=node, pattern=0, points=[])
        self.write('document.open', path=str(path), discard=True)
        self.assertEqual(self.saved(graph, node)['points'], points)
        self.assertEqual(self.saved(graph, node, pattern)['points'], other)

    def test_canvas_insert_drag_cancel_snap_and_keyboard(self):
        graph, node = self.setup_curve()
        self.command(490)
        endpoint = self.state()['handles'][-1]
        self.mouse(0x201, endpoint, 1)
        self.mouse(0x202, endpoint)
        self.assertEqual(self.state()['selected'], 1)
        self.command(491)
        state = self.state()
        c = state['canvas']
        at = dict(x=c['x']+c['width']*.25, y=c['y']+c['height']*.4)
        self.mouse(0x201, at, 1)
        self.mouse(0x202, at)
        self.assertEqual(len(self.state()['points']), 1)
        original = self.state()['points']
        handle = self.state()['handles'][0]
        moved = dict(x=handle['x']+36, y=handle['y']+17)
        self.mouse(0x201, handle, 1)
        self.mouse(0x200, moved, 1)
        self.key(0x1B)
        self.assertEqual(self.state()['points'], original)
        self.mouse(0x201, handle, 1)
        self.mouse(0x200, moved, 1)
        self.mouse(0x202, moved)
        self.assertNotEqual(self.state()['points'], original)
        self.assertEqual(self.state()['points'][0]['position'] % 256, 0)
        old = self.state()['points'][0]
        self.key(0x27)
        self.assertEqual(self.state()['points'][0]['position'], old['position']+256)
        self.command(486)
        self.assertEqual(self.saved(graph, node)['points'], self.state()['points'])
        self.key(0x2E)
        self.assertEqual(self.state()['points'], [])
        self.command(486)
        self.assertEqual(self.saved(graph, node)['points'], [])

    def test_stale_curve_keeps_captured_target_and_pattern(self):
        graph, node = self.setup_curve()
        self.command(490)
        retained = self.state()['points']
        self.select(442, 0)  # Input selected; curve stays on its source.
        self.assertEqual(self.state()['node'], node)
        self.assertEqual(self.state()['points'], retained)
        self.write('document.patch', title='Changed outside the curve')
        before = self.doc()
        self.command(486)
        self.assertEqual(self.doc(), before)
        self.assertTrue(self.state()['stale'])
        self.assertTrue(self.state()['dirty'])
        self.select(442, 2)
        self.command(487)
        self.assertFalse(self.state()['dirty'])
        self.command(490)
        self.command(486)
        self.assertEqual(self.saved(graph, node)['points'], retained)

    def test_pattern_picker_retains_dirty_source_then_loads_exact_target(self):
        pattern = self.write('pattern.create', rows=32)['pattern']
        graph, node = self.setup_curve()
        self.command(490)
        original = self.state()['points']
        self.select(480, 1)
        self.assertEqual(self.state()['pattern'], 0)
        self.assertEqual(self.desktop.send(self.control(480), 0x147), 0)
        self.assertEqual(self.state()['points'], original)
        self.command(486)
        self.select(480, 1)
        self.settle()
        self.assertEqual(self.state()['pattern'], pattern)
        self.assertFalse(self.state()['points'])
        self.assertEqual(self.state()['end'], 32*256)
        self.field(483, '31.99609375')
        self.field(484, '75')
        self.command(488)
        self.command(486)
        self.assertEqual(self.saved(graph, node, pattern)['points'], [dict(position=8191, value=.75, curve='linear')])
        self.assertEqual(self.saved(graph, node)['points'], original)

    def test_scripted_preview_error_retains_draft_and_matches_shared_values(self):
        graph, node = self.setup_curve()
        self.command(490)
        self.select(481, 8)
        self.field(485, 'mix(start,end,t*t)')
        self.command(488)
        self.command(495)
        self.assertEqual(self.state()['previewSamples'], 1024)
        self.command(486)
        points = self.saved(graph, node)['points']
        self.assertEqual(points[0]['formula'], 'mix(start,end,t*t)')
        preview = self.read('automation.formula.preview', points=points, rows=64, samples=3, end=points[-1]['position'])
        self.assertAlmostEqual(preview['values'][1][1], .25)
        self.field(485, 'not_a_function(t)')
        self.command(488)
        before = self.doc()
        self.command(495)
        self.assertEqual(self.state()['previewSamples'], 0)
        self.command(486)
        self.assertEqual(self.doc(), before)
        self.assertTrue(self.state()['dirty'])
        self.assertIn('not_a_function', self.state()['points'][0]['formula'])

    def test_native_curve_fields_focus_bounds_and_zoom(self):
        self.setup_curve()
        user = ctypes.WinDLL('user32')
        user.SetWindowPos.argtypes = [wintypes.HWND, wintypes.HWND, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, wintypes.UINT]
        user.GetWindowRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
        user.IsWindowVisible.argtypes = [wintypes.HWND]
        hwnd = self.desktop.hwnd(self.pid)
        dpi = self.read('workspace.get')['dpi']/96
        self.assertTrue(user.SetWindowPos(hwnd, None, 0, 0, int(900*dpi), int(620*dpi), 0x16))
        frame = wintypes.RECT()
        user.GetWindowRect(hwnd, ctypes.byref(frame))
        self.select(481, 8)
        for identifier in range(480, 498):
            control = self.control(identifier)
            if user.IsWindowVisible(control):
                with self.subTest(control=identifier):
                    rect = wintypes.RECT()
                    user.GetWindowRect(control, ctypes.byref(rect))
                    self.assertGreater(rect.right, rect.left)
                    self.assertGreaterEqual(rect.left, frame.left)
                    self.assertLessEqual(rect.right, frame.right)
                    self.assertLessEqual(rect.bottom, frame.bottom)
        span = self.state()['end']-self.state()['start']
        self.command(493)
        self.assertEqual(self.state()['end']-self.state()['start'], span/2)
        self.command(492)
        self.assertEqual(self.state()['end']-self.state()['start'], span)
        field = self.control(485)
        self.desktop.send(field, 0x201, 1, 5 | (5 << 16))
        self.desktop.send(field, 0x202, 0, 5 | (5 << 16))
        self.assertEqual(self.desktop.focus(hwnd), field)
        self.field(485, 'mix(start,end,t)')
        self.command(488)
        self.settle()
        self.assertEqual(self.desktop.focus(hwnd), field)

    def test_native_scripted_curve_drives_saved_graph_audio_across_partitions(self):
        self.add_gain()
        graph, node = self.setup_curve()
        definition = self.read('graph.get', includeState=False)['library'][0]
        effect = self.write('graph.node.add', graph=graph, kind='plugin', slot=0,
                            insertAfter=definition['nodes'][0]['id'])['node']
        definition = self.read('graph.get', includeState=False)['library'][0]
        definition['modulation'].append(dict(source=node, target=effect, parameter=1,
                                             minimum=.7, maximum=.9, base=0, enabled=True))
        self.write('graph.update', definition=definition)
        self.write('mixer.enable')
        master = next(b['id'] for b in self.read('mixer.get')['buses'] if b['kind'] == 'master')
        self.write('graph.assign', target=master, graph=graph)
        self.command(487)
        self.command(490)
        self.select(481, 8)
        self.field(485, '.5 + .4*sin(beat*6.283185307179586)')
        self.command(488)
        self.command(486)
        self.assertEqual(self.saved(graph, node)['points'][0]['curve'], 'scripted')
        path = self.folder / 'native-graph-curve.screamseq'
        self.write('document.save', path=str(path))
        report = self.folder / 'native-graph-curve-pcm.json'
        result = subprocess.run([os.environ['SCREAMSEQ_TEST_EXE'], '--offline-hosted-test',
                                 '--project', str(path), '--report', str(report)], timeout=90)
        if os.environ.get('SCREAMSEQ_CURVE_EVIDENCE_DIR'):
            directory = Path(os.environ['SCREAMSEQ_CURVE_EVIDENCE_DIR'])
            directory.mkdir(parents=True, exist_ok=True)
            if report.exists():
                shutil.copy2(report, directory / report.name)
            shutil.copy2(path, directory / path.name)
        self.assertEqual(result.returncode, 0, report.read_text() if report.exists() else 'no render report')
        evidence = json.loads(report.read_text())
        self.assertTrue(evidence['finite'])
        self.assertTrue(evidence['documentUnchanged'])
        self.assertGreater(evidence['energy'], 1)
        self.assertLess(evidence['maxPartitionDelta'], 1e-6)
        self.assertEqual(evidence['rates'], [44100, 48000, 96000])
        self.assertEqual(evidence['partitions'], [17, 128, 4096, 8193])


if __name__ == '__main__':
    unittest.main()
