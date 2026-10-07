"""Real workspace HMENUs: precise targets, drafts, modal guards and Undo."""
import base64
import ctypes
from ctypes import wintypes as w
import struct
import unittest

import native_context_menu
import private_desktop
import test_workspace_docking as docking


user = private_desktop.user
user.ClientToScreen.argtypes = [w.HWND, ctypes.POINTER(w.POINT)]


class WorkspaceContextMenuTests(unittest.TestCase):
    setUp = docking.WorkspaceDockingTests.setUp
    doc = docking.WorkspaceDockingTests.doc
    read = docking.WorkspaceDockingTests.read
    write = docking.WorkspaceDockingTests.write
    add_gain = docking.WorkspaceDockingTests.add_gain
    navigate = docking.WorkspaceDockingTests.navigate
    state = docking.WorkspaceDockingTests.state
    ready = docking.WorkspaceDockingTests.ready
    native_window = docking.WorkspaceDockingTests.native_window
    control = docking.WorkspaceDockingTests.control
    command = docking.WorkspaceDockingTests.command
    field = docking.WorkspaceDockingTests.field
    text = docking.WorkspaceDockingTests.text

    def screen(self, x, y):
        scale = self.state()['dpi'] / 96
        point = w.POINT(round(x * scale), round(y * scale))
        private_desktop.check(user.ClientToScreen(self.native_window(), ctypes.byref(point)))
        return point.x, point.y

    def mouse(self, message, point, flags=0):
        scale = self.state()['dpi'] / 96
        x, y = (round(value * scale) for value in point)
        self.desktop.send(self.native_window(), message, flags, (x & 65535) | ((y & 65535) << 16))

    def cell_point(self, row=0, channel=0, column=0):
        state = self.state()
        rect = state['geometry']['pattern']
        offset = (0, 28.8, 50.4, 79.2, 100.8)[column]
        return (rect['x'] + 44 + channel * 169.6 + offset - state['viewport']['horizontalScroll'],
                rect['y'] + 56 + (row - state['viewport']['firstRow']) * 18)

    def select_cells(self, first, last):
        self.mouse(0x201, self.cell_point(*first), 1)
        self.mouse(0x200, self.cell_point(*last), 1)
        self.mouse(0x202, self.cell_point(*last))

    def menu(self, point=None, source=None):
        return native_context_menu.Menu(self, self.native_window(),
                                        point=None if point is None else self.screen(*point), source=source)

    def choose(self, identifier, point=None):
        with self.menu(point) as menu:
            menu.choose(identifier)
        return self.ready()

    def commands(self, items):
        result = {}
        for item in items:
            if item['command']:
                result[item['command']] = item
            result.update(self.commands(item['children']))
        return result

    def cells(self, row=0, count=2, channel=0, channels=2):
        return self.read('pattern.get', pattern=0, startRow=row, rowCount=count,
                         startChannel=channel, channelCount=channels)['cells']

    def test_pattern_hit_selection_and_keyboard_cancel_preserve_document(self):
        self.ready()
        self.select_cells((0, 0), (3, 1))
        captured = self.read('context.get')
        document = self.doc()
        with self.menu(self.cell_point(1, 0)) as menu:
            actual = self.commands(menu.items())
            self.assertTrue(actual[124]['enabled'])
            self.assertTrue(actual[140]['enabled'])
            self.assertEqual(self.read('context.get'), captured)
            self.assertEqual(self.state()['contextMenu']['surface'], 'pattern')
            menu.cancel()
        self.assertEqual(self.read('context.get'), captured)
        self.assertEqual(self.doc(), document)
        with self.menu(self.cell_point(5, 0, 4)) as menu:
            target = self.read('context.get')
            self.assertEqual((target['row'], target['channel'], target['column']), (5, 0, 4))
            self.assertEqual(target['selection'], dict(startRow=5, endRow=5, startChannel=0, endChannel=0))
            menu.cancel()
        with self.menu() as menu:
            self.assertTrue(self.state()['contextMenu']['keyboard'])
            self.assertEqual(self.read('context.get'), target)
            menu.cancel()
        self.client.call('workspace.shortcut.set', dict(command='windows.command.540', keys=['ctrl+alt+q', 'ctrl+alt+r']))
        self.client.call('workspace.shortcut.set', dict(command='windows.command.112', keys=['ctrl+alt+t']))
        with self.menu() as menu:
            actual = self.commands(menu.items())
            self.assertEqual(actual[124]['label'].split('\t')[1], 'Ctrl+Alt+Q → Ctrl+Alt+R')
            self.assertEqual(actual[112]['label'].split('\t')[1], 'Ctrl+Alt+T')
            menu.cancel()
        self.client.call('workspace.shortcut.set', dict(command='windows.command.540', keys=[]))
        self.client.call('workspace.shortcut.set', dict(command='windows.command.112', keys=[]))
        with self.menu() as menu:
            actual = self.commands(menu.items())
            self.assertNotIn('\t', actual[124]['label'])
            self.assertNotIn('\t', actual[112]['label'])
            menu.cancel()
        self.assertEqual(self.doc(), document)

    def test_pattern_copy_paste_nested_transform_and_history(self):
        self.write('pattern.apply', cells=[dict(pattern=0, row=0, channel=0, note=61),
                                           dict(pattern=0, row=1, channel=1, note=65)])
        self.select_cells((0, 0), (1, 1))
        source = self.cells()
        before = self.doc()
        self.choose(124)
        self.assertEqual(self.doc(), before)
        self.navigate(row=8, channel=1, column=0, following=False)
        original = self.cells(8, channel=1)
        self.choose(125)
        copied = self.cells(8, channel=1)
        for expected, actual in zip(source, copied):
            self.assertEqual({k: v for k, v in actual.items() if k not in ('row', 'channel')},
                             {k: v for k, v in expected.items() if k not in ('row', 'channel')})
        self.choose(122)
        self.assertEqual(self.cells(8, channel=1), original)
        self.select_cells((0, 0), (1, 1))
        self.choose(140)  # A genuine nested submenu selection.
        changed = self.cells()
        self.assertEqual(changed[0]['note'], source[2]['note'])
        self.assertEqual(changed[3]['note'], source[1]['note'])
        self.choose(122)
        self.assertEqual(self.cells(), source)
        self.choose(126)
        self.assertTrue(all(cell['note'] == 0 for cell in self.cells()))
        self.choose(122)
        self.assertEqual(self.cells(), source)

    def prepare_sample(self):
        self.raw = (1000, -2000, 3000, -4000, 5000, -6000, 7000, -8000)
        self.write('sample.pcm.set', sample=1, format='s16le', channels=1, rate=48000,
                   data=base64.b64encode(struct.pack('<8h', *self.raw)).decode())
        self.client.call('workspace.layout', dict(name='Sound design'))
        self.ready()
        rect = self.state()['sampleEditor']['waveform']
        self.wave = (rect['x'] + rect['width'] * .5, rect['y'] + rect['height'] * .5)
        self.field(self.native_window(), 230, '2')
        self.field(self.native_window(), 231, '6')
        self.command(209)

    def pcm(self):
        raw = base64.b64decode(self.read('sample.pcm.get', sample=1)['data'])
        return struct.unpack('<' + str(len(raw) // 2) + 'h', raw)

    def test_sample_range_raw_fields_real_process_loop_and_undo(self):
        self.prepare_sample()
        before = self.doc()
        self.field(self.native_window(), 230, '')
        with self.menu(self.wave) as menu:
            self.assertEqual(self.text(self.control(self.native_window(), 230)), '')
            self.assertTrue(self.state()['sampleEditor']['draft'])
            menu.cancel()
        self.assertEqual(self.doc(), before)
        self.field(self.native_window(), 230, '2')
        self.choose(203, self.wave)
        expected = self.raw[:2] + tuple(reversed(self.raw[2:6])) + self.raw[6:]
        self.assertEqual(self.pcm(), expected)
        self.choose(122, self.wave)
        self.assertEqual(self.pcm(), self.raw)
        self.choose(208, self.wave)
        sample = self.read('sample.get', sample=1)
        self.assertEqual((sample['loopStart'], sample['loopEnd'], sample['loop']), (2, 6, True))
        with self.menu(self.wave) as menu:
            self.assertTrue(self.commands(menu.items())[214]['checked'])
            menu.cancel()
        path = self.folder / 'menu-sample.screamseq'
        self.write('document.save', path=str(path))
        self.write('document.open', path=str(path), discard=True)
        self.assertTrue(self.read('sample.get', sample=1)['loop'])

    def prepare_graph(self):
        self.add_gain()
        for command in (430, 432, 438):
            self.command(command)
        self.graph = self.read('graph.get')['library'][0]
        self.node = next(item['id'] for item in self.graph['nodes'] if item['kind'] == 'plugin')
        node = next(item for item in self.state()['graphEditor']['nodes'] if item['id'] == self.node)
        self.node_point = (node['x'] + 20, node['y'] + 10)

    def test_graph_node_wire_menu_selection_draft_and_history(self):
        self.prepare_graph()
        before = self.doc()
        with self.menu(self.node_point) as menu:
            self.assertEqual(self.state()['graphEditor']['node'], self.node)
            commands = self.commands(menu.items())
            self.assertTrue(commands[470]['enabled'])
            self.assertFalse(commands[462]['enabled'])  # Built-in effect has no native interface.
            menu.cancel()
        self.assertEqual(self.doc(), before)
        self.assertFalse(self.state()['graphEditor']['dirty'])
        wire = next(item for item in self.state()['graphEditor']['wires'] if not item['modulation'])
        self.choose(457, (wire['x'], wire['y']))
        self.assertTrue(self.state()['graphEditor']['dirty'])
        self.assertEqual(self.doc(), before)
        self.assertEqual(self.read('graph.get')['library'][0], self.graph)
        with self.menu() as menu:
            self.assertTrue(self.commands(menu.items())[440]['enabled'])
            menu.choose(440)
        self.ready()
        self.assertEqual(len(self.read('graph.get')['library'][0]['audio']), len(self.graph['audio']) - 1)
        self.choose(122)
        self.assertEqual(self.read('graph.get')['library'][0], self.graph)
        self.command(441)
        self.choose(470, self.node_point)
        self.assertNotIn(self.node, [node['id'] for node in self.read('graph.get')['library'][0]['nodes']])
        self.choose(122)
        self.assertEqual(self.read('graph.get')['library'][0], self.graph)

    def test_modal_document_context_and_sample_field_changes_reject_actions(self):
        self.ready()
        for change in ('document', 'context', 'focus'):
            with self.subTest(change=change):
                with self.menu(self.cell_point()) as menu:
                    if change == 'document':
                        self.write('document.patch', title='Changed during menu')
                    elif change == 'context':
                        self.navigate(row=7, following=False)
                    else:
                        self.client.call('workspace.panel', dict(panel='notes', focus=True))
                    before = self.doc()
                    menu.choose(126)
                state = self.ready()['contextMenu']
                self.assertTrue(state['rejected'])
                self.assertFalse(state['dispatched'])
                self.assertEqual(self.doc(), before)
        self.prepare_sample()
        with self.menu(self.wave) as menu:
            self.field(self.native_window(), 230, '3')
            before = self.doc()
            menu.choose(203)
        self.assertTrue(self.ready()['contextMenu']['rejected'])
        self.assertEqual(self.doc(), before)
        self.assertEqual(self.text(self.control(self.native_window(), 230)), '3')
        self.assertEqual(self.pcm(), self.raw)

    def test_graph_raw_draft_prevents_retarget_and_modal_generation_change(self):
        self.prepare_graph()
        self.field(self.native_window(), 445, 'unfinished name')
        captured = self.state()['graphEditor']
        other = next(item for item in captured['nodes'] if item['id'] != self.node)
        before = self.doc()
        with self.menu((other['x'] + 20, other['y'] + 10)) as menu:
            self.assertFalse(self.commands(menu.items())[470]['enabled'])
            self.assertEqual(self.state()['graphEditor']['node'], self.node)
            self.assertEqual(self.text(self.control(self.native_window(), 445)), 'unfinished name')
            menu.cancel()
        self.assertEqual(self.doc(), before)
        self.command(441)
        with self.menu(self.node_point) as menu:
            self.field(self.native_window(), 445, 'new nested draft')
            menu.choose(470)
        self.assertTrue(self.ready()['contextMenu']['rejected'])
        self.assertEqual(self.doc(), before)
        self.assertEqual(self.text(self.control(self.native_window(), 445)), 'new nested draft')

    def test_native_edit_keeps_its_standard_text_menu(self):
        self.prepare_sample()
        edit = self.control(self.native_window(), 230)
        self.field(self.native_window(), 230, '2')
        self.desktop.send(edit, 0xB1, 0, -1)  # EM_SETSEL.
        before = self.state()['contextMenu']['serial']
        with self.menu(source=edit) as menu:
            commands = self.commands(menu.items())
            self.assertNotIn(203, commands)
            self.assertTrue(any('Copy' in item['label'] for item in commands.values()))
            menu.cancel()
        self.assertEqual(self.state()['contextMenu']['serial'], before)
        self.assertEqual(self.text(edit), '2')
        # The painted note inspector has a different captured musical target;
        # its keyboard menu must not silently expose cursor-pattern actions.
        self.client.call('workspace.panel', dict(panel='notes', focus=True))
        before = self.state()['contextMenu']['serial']
        native_context_menu.post_context(self.native_window())
        state = self.ready()
        self.assertEqual(state['focus'], 'notes')
        self.assertEqual(state['contextMenu']['serial'], before)
        self.assertEqual(native_context_menu._popups(self), [])


if __name__ == '__main__':
    unittest.main()
