"""Native graph canvas geometry, draft/history and isolated recipe editing."""
import os
import ctypes
from ctypes import wintypes
import unittest
import test_graph_mixer_app as support
import private_desktop


class GraphEditorTests(unittest.TestCase):
    doc = support.GraphMixerAppTests.doc
    read = support.GraphMixerAppTests.read
    write = support.GraphMixerAppTests.write
    rejected = support.GraphMixerAppTests.rejected
    add_gain = support.GraphMixerAppTests.add_gain
    control = support.GraphMixerAppTests.control
    command = support.GraphMixerAppTests.command
    field = support.GraphMixerAppTests.field

    def setUp(self):
        original = os.environ.get('SCREAMSEQ_TEST_PROVIDER_CACHE')
        if self._testMethodName == 'test_installed_graph_editor_draft_commit_conflict_and_close':
            os.environ['SCREAMSEQ_TEST_PROVIDER_CACHE'] = os.environ['SCREAMSEQ_TEST_PLUGIN_CACHE']
        try:
            support.GraphMixerAppTests.setUp(self)
        finally:
            if original is None:
                os.environ.pop('SCREAMSEQ_TEST_PROVIDER_CACHE', None)
            else:
                os.environ['SCREAMSEQ_TEST_PROVIDER_CACHE'] = original

    def select(self, identifier, index):
        self.desktop.send(self.control(identifier), 0x14E, index)
        self.command(identifier, 1)

    def state(self):
        return self.read('workspace.get')['graphEditor']

    def setup_graph(self):
        self.add_gain()
        self.command(430)
        self.command(432)
        self.command(438)
        graph = self.read('graph.get')['library'][0]
        return graph['id'], next(n['id'] for n in graph['nodes'] if n['kind'] == 'plugin')

    def point(self, item):
        scale = self.read('workspace.get')['dpi'] / 96
        return round(item['x']*scale) & 65535 | ((round(item['y']*scale) & 65535) << 16)

    def mouse(self, message, point, buttons=0):
        self.desktop.send(self.desktop.hwnd(self.pid), message, buttons, self.point(point))

    def key(self, key):
        self.desktop.send(self.desktop.hwnd(self.pid), 0x100, key)

    def test_recipe_parameters_atomic_dry_noop_history_persistence_and_rack_independence(self):
        graph, node = self.setup_graph()
        rack = self.read('plugin.state.get', slot=0)
        before = self.doc()
        self.write('graph.plugin.set', graph=graph, node=node, parameters=[dict(id=1, value=-12)], dryRun=True)
        self.assertEqual(self.doc(), before)
        self.rejected('graph.plugin.set', graph=graph, node=node,
                      parameters=[dict(id=1, value=-12), dict(id=4294967295, value=0)])
        self.write('graph.plugin.set', graph=graph, node=node, parameters=[dict(id=1, value=-12)])
        self.assertEqual(self.read('plugin.state.get', slot=0), rack)
        parameters = self.read('graph.plugin.get', graph=graph, node=node)['parameters']
        self.assertEqual(next(p['value'] for p in parameters if p['id'] == 1), -12)
        before = self.doc()
        self.write('graph.plugin.set', graph=graph, node=node, parameters=[dict(id=1, value=-12)])
        self.assertEqual(self.doc(), before)
        self.write('history.undo', domain='document')
        self.assertEqual(next(p['value'] for p in self.read('graph.plugin.get', graph=graph, node=node)['parameters'] if p['id'] == 1), 0)
        self.write('history.redo', domain='document')
        path = self.folder / 'graph-parameters.screamseq'
        self.write('document.save', path=str(path))
        self.write('graph.remove', graph=graph)
        self.write('document.open', path=str(path), discard=True)
        self.assertEqual(self.read('graph.plugin.get', graph=graph, node=node)['parameters'], parameters)

    def test_canvas_modulation_socket_drag_wire_update_preserves_neighbors(self):
        graph, plugin = self.setup_graph()
        self.select(435, 1)  # LFO.
        self.command(436)
        definition = self.read('graph.get')['library'][0]
        source = next(n['id'] for n in definition['nodes'] if n['kind'] == 'lfo')
        self.select(442, 2)  # Plugin.
        self.select(443, 3)  # Plugin controls.
        self.command(460)
        self.select(458, 1)  # The stable Gain parameter, after Enabled.
        sockets = self.state()['sockets']
        output = next(s for s in sockets if s['node'] == source and s['modulation'] and s['output'])
        target = next(s for s in sockets if s['node'] == plugin and s['modulation'] and not s['output'])
        self.mouse(0x201, output, 1)
        self.mouse(0x200, target, 1)
        self.mouse(0x202, target)
        self.assertTrue(self.state()['dirty'])
        self.assertEqual(self.read('graph.get')['library'][0], definition)
        self.command(440)
        changed = self.read('graph.get')['library'][0]
        self.assertEqual(len(changed['modulation']), 1)
        self.assertEqual(changed['audio'], definition['audio'])
        # Actual Bezier midpoint hit test selects exactly one audio connection.
        wire = next(w for w in self.state()['wires'] if not w['modulation'] and w['index'] == 0)
        self.mouse(0x201, wire, 1)
        self.mouse(0x202, wire)
        self.assertEqual((self.state()['wire'], self.state()['modulation']), (0, False))
        self.field(455, '.5')
        self.command(456)
        self.command(440)
        updated = self.read('graph.get')['library'][0]
        self.assertEqual(updated['audio'][0]['gain'], .5)
        self.assertEqual(updated['audio'][1:], changed['audio'][1:])
        self.assertEqual(updated['modulation'], changed['modulation'])
        self.write('history.undo', domain='document')
        self.assertEqual(self.read('graph.get')['library'][0], changed)

    def test_canvas_drag_cancel_stale_draft_and_keyboard_history(self):
        graph, plugin = self.setup_graph()
        original = self.read('graph.get')['library'][0]
        node = next(n for n in self.state()['nodes'] if n['id'] == plugin)
        start = dict(x=node['x']+20, y=node['y']+10)
        end = dict(x=start['x']+40, y=start['y']+20)
        self.mouse(0x201, start, 1)
        self.mouse(0x200, end, 1)
        self.key(0x1B)
        self.assertFalse(self.state()['dirty'])
        self.assertEqual(self.read('graph.get')['library'][0], original)
        self.key(0x27)
        self.assertTrue(self.state()['dirty'])
        self.write('document.patch', title='Changed while dragging')
        before = self.doc()
        self.command(440)
        self.assertEqual(self.doc(), before)
        self.assertTrue(self.state()['dirty'])
        self.assertTrue(self.state()['stale'])
        self.command(441)
        self.key(0x27)
        self.command(440)
        changed = self.read('graph.get')['library'][0]
        self.assertEqual(next(n['x'] for n in changed['nodes'] if n['id'] == plugin),
                         next(n['x'] for n in original['nodes'] if n['id'] == plugin)+8)
        self.write('history.undo', domain='document')
        self.assertEqual(self.read('graph.get')['library'][0], original)

    def test_native_parameter_fields_and_assignment_to_real_bus(self):
        graph, plugin = self.setup_graph()
        self.select(443, 3)
        self.command(460)
        self.select(458, 1)
        self.field(459, '-18')
        self.command(461)
        self.assertEqual(next(p['value'] for p in self.read('graph.plugin.get', graph=graph, node=plugin)['parameters'] if p['id'] == 1), -18)
        self.write('mixer.enable')
        self.desktop.send(self.desktop.hwnd(self.pid), 0x113, 4)
        self.select(443, 4)
        before = self.doc()
        self.field(468, '2')
        self.command(466)
        self.assertEqual(self.doc(), before)
        self.assertTrue(self.state()['fieldDraft'])
        self.select(465, 1)
        self.assertEqual(self.desktop.send(self.control(465), 0x147), 0)
        self.field(468, '.25')
        self.field(469, '.75')
        self.command(466)
        assignment = self.read('graph.get')['assignments'][0]
        self.assertEqual((assignment['graph'], assignment['amount'], assignment['wet']), (graph, .25, .75))
        self.assertFalse(self.state()['fieldDraft'])
        retained = ctypes.create_unicode_buffer(64)
        self.desktop.send(self.control(468), 0xD, 64, ctypes.addressof(retained))
        self.assertEqual(float(retained.value), .25)
        self.command(467)
        self.assertFalse(self.read('graph.get')['assignments'])

    def test_native_field_target_guards_small_window_and_invalid_cycle(self):
        graph, plugin = self.setup_graph()
        user = ctypes.WinDLL('user32')
        user.SetWindowPos.argtypes = [wintypes.HWND, wintypes.HWND, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, wintypes.UINT]
        user.GetWindowRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
        user.IsWindowVisible.argtypes = [wintypes.HWND]
        dpi = self.read('workspace.get')['dpi']/96
        hwnd = self.desktop.hwnd(self.pid)
        self.assertTrue(user.SetWindowPos(hwnd, None, 0, 0, int(900*dpi), int(620*dpi), 0x16))
        frame = wintypes.RECT()
        user.GetWindowRect(hwnd, ctypes.byref(frame))
        for page in range(5):
            self.select(443, page)
            for identifier in range(431, 471):
                control = self.control(identifier)
                if user.IsWindowVisible(control):
                    with self.subTest(page=page, control=identifier):
                        rect = wintypes.RECT()
                        user.GetWindowRect(control, ctypes.byref(rect))
                        self.assertGreater(rect.right, rect.left)
                        self.assertGreaterEqual(rect.left, frame.left)
                        self.assertLessEqual(rect.right, frame.right)
                        self.assertLessEqual(rect.bottom, frame.bottom)
        self.select(443, 0)
        self.field(445, 'Retained node name')
        self.select(442, 0)
        self.assertEqual(self.state()['node'], plugin)
        self.assertEqual(self.desktop.send(self.control(442), 0x147), 2)
        self.command(446)
        self.command(440)
        definition = self.read('graph.get')['library'][0]
        self.assertEqual(next(n['name'] for n in definition['nodes'] if n['id'] == plugin), 'Retained node name')
        self.select(443, 1)
        self.select(447, 2)
        self.select(448, 0)  # Effect -> Input is an invalid feedback route.
        self.command(456)
        before = self.doc()
        self.command(440)
        self.assertEqual(self.doc(), before)
        self.assertTrue(self.state()['dirty'])
        self.command(441)
        self.assertFalse(self.state()['dirty'])

    @unittest.skipUnless(os.environ.get('SCREAMSEQ_TEST_PROVIDER_CACHE'), 'native provider fixture')
    def test_graph_recipe_ports_validate_available_buses_and_connected_port_removal(self):
        effect = next(p for p in self.read('plugin.discover', format='VST3') if p['classID'] == '5245534F4E414E4350524F4752410001')
        self.write('plugin.add', descriptor=effect)
        graph = self.write('graph.create')['graph']
        node = self.write('graph.node.add', graph=graph, kind='plugin', slot=0)['node']
        before = self.doc()
        self.write('graph.plugin.set', graph=graph, node=node, inputs=[1], dryRun=True)
        self.assertEqual(self.doc(), before)
        self.rejected('graph.plugin.set', graph=graph, node=node, inputs=[1, 1])
        self.rejected('graph.plugin.set', graph=graph, node=node, outputs=[63])
        self.write('graph.plugin.set', graph=graph, node=node, inputs=[1])
        ports = self.read('graph.plugin.get', graph=graph, node=node)['buses']
        self.assertTrue(next(p['active'] for p in ports if p['direction'] == 'input' and p['index'] == 1))
        definition = self.read('graph.get')['library'][0]
        source = definition['nodes'][0]['id']
        definition['audio'].append(dict(source=source, target=node, output=1, input=1, gain=1))
        self.write('graph.update', definition=definition)
        self.rejected('graph.plugin.set', graph=graph, node=node, inputs=[])

    @unittest.skipUnless(os.environ.get('SCREAMSEQ_TEST_PLUGIN_CACHE'), 'installed ARM64 effects')
    def test_installed_graph_editor_draft_commit_conflict_and_close(self):
        orbit = next(p for p in self.read('plugin.discover', format='VST3') if p['classID'] == 'ABCDEF019182FAEB446361744F726274')
        self.write('plugin.add', descriptor=orbit)
        graph = self.write('graph.create')['graph']
        node = self.write('graph.node.add', graph=graph, kind='plugin', slot=0)['node']
        catalog = self.read('graph.plugin.get', graph=graph, node=node)
        parameter = next(p for p in catalog['parameters'] if p['name'] == 'Amp EQ')
        self.write('graph.plugin.set', graph=graph, node=node, parameters=[dict(id=parameter['id'], value=1)])
        before = self.doc()
        editor = self.write('graph.plugin.editor.open', graph=graph, node=node)['editor']
        # The installed editor's initialization gesture affects only its draft.
        self.assertEqual(self.doc()['revision'], before['revision'])
        self.assertEqual(next(p['value'] for p in self.read('graph.plugin.get', graph=graph, node=node)['parameters'] if p['id'] == parameter['id']), 1)
        windows = []
        @private_desktop.callback
        def find_editor(hwnd, unused):
            pid = wintypes.DWORD()
            private_desktop.user.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
            name = ctypes.create_unicode_buffer(256)
            private_desktop.user.GetClassNameW(hwnd, name, len(name))
            if pid.value == self.pid and name.value == 'ScreamSeq.VST3.PrivateEditor':
                windows.append(hwnd)
            return True
        self.assertTrue(private_desktop.user.EnumDesktopWindows(self.desktop.desktop, find_editor, 0))
        self.assertEqual(len(windows), 1)
        self.desktop.send(windows[0], 0x10)  # Native title-bar close, not API disposal.
        self.read('graph.get')  # Flush window presence without committing its draft.
        self.assertEqual(self.doc()['data']['openPluginEditors'], 0)
        self.write('graph.plugin.editor.commit', graph=graph, node=node, editor=editor, dryRun=True)
        self.assertEqual(self.doc()['revision'], before['revision'])
        self.write('graph.plugin.editor.commit', graph=graph, node=node, editor=editor)
        self.assertEqual(next(p['value'] for p in self.read('graph.plugin.get', graph=graph, node=node)['parameters'] if p['id'] == parameter['id']), 0)
        self.write('graph.plugin.set', graph=graph, node=node, parameters=[dict(id=parameter['id'], value=1)])
        self.rejected('graph.plugin.editor.commit', graph=graph, node=node, editor=editor)
        self.write('graph.plugin.editor.close', editor=editor)
        self.assertEqual(self.doc()['data']['openPluginEditors'], 0)


if __name__ == '__main__':
    unittest.main()
