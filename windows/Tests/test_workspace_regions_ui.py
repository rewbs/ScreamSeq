"""Independent retained workspace hosts through the real isolated application."""
import ctypes
from ctypes import wintypes as w
import unittest

import private_desktop
import test_workspace_docking as support


user = private_desktop.user
user.GetWindowRect.argtypes = [w.HWND, ctypes.POINTER(w.RECT)]
user.GetClientRect.argtypes = user.GetWindowRect.argtypes
user.MapWindowPoints.argtypes = [w.HWND, w.HWND, ctypes.POINTER(w.POINT), w.UINT]
user.GetDlgCtrlID.argtypes = [w.HWND]
user.IsWindowEnabled.argtypes = [w.HWND]


class WorkspaceRegionsUITests(unittest.TestCase):
    # Share only fixture helpers, never inherit another module's test methods.
    setUp = support.WorkspaceDockingTests.setUp
    doc = support.WorkspaceDockingTests.doc
    read = support.WorkspaceDockingTests.read
    write = support.WorkspaceDockingTests.write
    add_gain = support.WorkspaceDockingTests.add_gain
    navigate = support.WorkspaceDockingTests.navigate
    resize_client = support.WorkspaceDockingTests.resize_client
    state = support.WorkspaceDockingTests.state
    ready = support.WorkspaceDockingTests.ready
    native_window = support.WorkspaceDockingTests.native_window
    control = support.WorkspaceDockingTests.control
    command = support.WorkspaceDockingTests.command
    panel = support.WorkspaceDockingTests.panel
    field = support.WorkspaceDockingTests.field
    text = support.WorkspaceDockingTests.text
    focus_control = support.WorkspaceDockingTests.focus_control
    queued_key = support.WorkspaceDockingTests.queued_key

    def click(self, owner, identifier):
        self.ready()
        control = self.control(owner, identifier)
        self.assertTrue(user.IsWindowVisible(control), identifier)
        self.assertTrue(user.IsWindowEnabled(control), identifier)
        self.desktop.send(control, 0xF5)  # BM_CLICK follows native notification routing.
        return self.ready()

    def choose(self, owner, identifier, index, listbox=False):
        self.ready()
        control = self.control(owner, identifier)
        self.assertTrue(user.IsWindowVisible(control), identifier)
        self.assertTrue(user.IsWindowEnabled(control), identifier)
        count = self.desktop.send(control, 0x18B if listbox else 0x146)
        self.assertLess(index, count, (identifier, index, count))
        self.desktop.send(control, 0x186 if listbox else 0x14E, index)
        self.desktop.send(owner, 0x111, identifier | (1 << 16), control)
        return self.ready()

    def connected(self):
        self.add_gain()
        self.write('instrument.create', sample=1)
        self.write('graph.create', name='Connected region routing')
        self.resize_client(1440, 852)
        state = self.command(562)
        self.assertEqual(state['editorDock']['mode'], 'regions')
        self.assertEqual(state['locations']['automation'], 'secondary')
        self.assertEqual(state['locations']['instruments'], 'right')
        return state

    def visible_geometry(self, owner):
        """Check actual direct control rectangles, excluding independent tools."""
        client = w.RECT()
        private_desktop.check(user.GetClientRect(owner, ctypes.byref(client)))
        rectangles = {}

        @private_desktop.callback
        def visit(control, _):
            if user.GetParent(control) != owner or not user.IsWindowVisible(control):
                return True
            kind = ctypes.create_unicode_buffer(100)
            user.GetClassNameW(control, kind, len(kind))
            if kind.value.startswith('ScreamSeq'):
                return True
            rectangle = w.RECT()
            private_desktop.check(user.GetWindowRect(control, ctypes.byref(rectangle)))
            points = (w.POINT * 2)(w.POINT(rectangle.left, rectangle.top),
                                   w.POINT(rectangle.right, rectangle.bottom))
            user.MapWindowPoints(None, owner, points, 2)
            rectangles[user.GetDlgCtrlID(control)] = (
                points[0].x, points[0].y, points[1].x, points[1].y)
            return True

        user.EnumChildWindows(owner, visit, 0)
        self.assertTrue(rectangles)
        for identifier, (left, top, right, bottom) in rectangles.items():
            self.assertGreater(right, left, (identifier, rectangles[identifier]))
            self.assertGreater(bottom, top, (identifier, rectangles[identifier]))
            self.assertGreaterEqual(left, -1, identifier)
            self.assertGreaterEqual(top, -1, identifier)
            self.assertLessEqual(right, client.right + 1, (identifier, rectangles[identifier]))
            self.assertLessEqual(bottom, client.bottom + 1, (identifier, rectangles[identifier]))
        pairs = list(rectangles.items())
        for index, (first, a) in enumerate(pairs):
            for second, b in pairs[index + 1:]:
                overlap = min(a[2], b[2]) - max(a[0], b[0]) > 1 and min(a[3], b[3]) - max(a[1], b[1]) > 1
                self.assertFalse(overlap, (first, a, second, b))
        return rectangles

    @staticmethod
    def overlaps(a, b):
        return (min(a['x'] + a['width'], b['x'] + b['width']) > max(a['x'], b['x'])
                and min(a['y'] + a['height'], b['y'] + b['height']) > max(a['y'], b['y']))

    def test_connected_preset_shows_four_disjoint_surfaces_and_all_short_pages(self):
        state = self.connected()
        before = self.doc()
        self.assertTrue(state['editorDock']['trackerVisible'])
        self.assertTrue(state['graphEditor']['visible'])
        self.assertFalse(state['pins']['automation'] or state['pins']['instruments'])
        rectangles = [state['geometry']['pattern']]
        for region, selected in (('right', 'instruments'), ('bottom', 'graph'), ('secondary', 'automation')):
            host = state['editorDock']['hosts'][region]
            self.assertTrue(host['visible'], region)
            self.assertEqual(host['selected'], selected)
            self.assertGreaterEqual(host['body']['height'], 300)
            self.assertGreaterEqual(host['body']['width'], 780 if region == 'bottom' else 440)
            rectangles.append(host['body'])
        for index, rectangle in enumerate(rectangles):
            for other in rectangles[index + 1:]:
                self.assertFalse(self.overlaps(rectangle, other))
        self.visible_geometry(self.native_window())
        for kind, key, pages in (
                ('ScreamSeq.ParameterAutomation', 'parameterAutomation', range(4241, 4245)),
                ('ScreamSeq.InstrumentEnvelope', 'instrumentEnvelope', range(4600, 4605))):
            owner = self.native_window(kind)
            self.assertTrue(state[key]['shortDock'])
            for page in pages:
                with self.subTest(editor=key, page=page):
                    self.click(owner, page)
                    self.visible_geometry(owner)
            if key == 'instrumentEnvelope':
                self.click(owner, 4600)
                self.click(owner, 4605)  # Retained Envelope Options.
                self.visible_geometry(owner)
        self.assertEqual(self.doc(), before)

    def test_compact_resize_saved_regions_and_five_placements_retain_drafts_and_caret(self):
        self.connected()
        automation = self.native_window('ScreamSeq.ParameterAutomation')
        instrument = self.native_window('ScreamSeq.InstrumentEnvelope')
        self.click(automation, 4241)
        state = self.choose(automation, 4204, 1, listbox=True)
        self.assertEqual(state['parameterAutomation']['parameter'], 1)
        self.click(automation, 4243)  # Short-dock ramps share the Formula page.
        self.click(automation, 4212)
        self.click(automation, 4242)
        row_field = self.field(automation, 4207, '3.')
        self.click(instrument, 4603)
        name_field = self.field(instrument, 4439, 'Retained region draft / 音色')
        self.panel('instruments', pinned=True)
        self.panel('automation', focus=True)
        self.focus_control(row_field)
        self.desktop.send(row_field, 0xB1, 1, 2)
        before = self.doc()
        captured = self.ready()
        self.client.call('workspace.layout', {'name': 'Save custom', 'savedName': 'Independent regions'})
        for width, height in ((900, 620), (1440, 852), (1000, 700), (1440, 852)):
            with self.subTest(size=(width, height)):
                self.resize_client(width, height)
                current = self.ready()
                self.assertEqual(self.text(row_field), '3.')
                self.assertEqual(self.text(name_field), 'Retained region draft / 音色')
                self.assertEqual(self.desktop.focus(automation), row_field)
                self.assertEqual(self.desktop.send(row_field, 0xB0), 1 | (2 << 16))
                self.assertEqual(current['editorDock']['mode'], 'regions' if width == 1440 else 'tabs')
                for key in ('parameterAutomation', 'instrumentEnvelope'):
                    for field in ('document', 'expectedRevision', 'generation', 'dirty'):
                        self.assertEqual(current[key][field], captured[key][field], (key, field))
                self.visible_geometry(self.native_window())
                self.visible_geometry(automation)
        # A native field may own focus while the saved compact preference
        # still names Pattern. Resize must retain the actual docked host.
        for panel, tool, field, raw, selection in (
                ('automation', automation, row_field, '3.', (1, 2)),
                ('instruments', instrument, name_field, 'Retained region draft / 音色', (2, 6))):
            with self.subTest(resize_native=panel):
                self.resize_client(1440, 852)
                self.assertEqual(self.command(113)['editorDock']['compactSelection'], 'pattern')
                self.focus_control(field)
                self.desktop.send(field, 0xB1, *selection)
                previous = self.ready()
                self.assertEqual(previous['editorDock']['mode'], 'regions')
                self.assertEqual(previous['editorDock']['compactSelection'], 'pattern')
                self.resize_client(1000, 720)
                current = self.ready()
                self.assertEqual(current['editorDock']['mode'], 'tabs')
                self.assertEqual(current['editorDock']['compactSelection'], panel)
                expected_config = dict(previous['editorDock']['configuration'], compactSelection=panel)
                self.assertEqual(current['editorDock']['configuration'], expected_config)
                self.assertTrue(user.IsWindowVisible(tool))
                self.assertTrue(user.IsWindowVisible(field))
                self.assertEqual(self.desktop.focus(tool), field)
                self.assertEqual(self.text(field), raw)
                self.assertEqual(self.desktop.send(field, 0xB0), selection[0] | (selection[1] << 16))
                for key in ('parameterAutomation', 'instrumentEnvelope'):
                    for value in ('document', 'expectedRevision', 'generation', 'dirty'):
                        self.assertEqual(current[key][value], previous[key][value], (key, value))
                self.assertEqual(self.doc(), before)

        # An actual native chooser outside Main is not a Main-body focus hit.
        # Keep the explicit tab preference and the chooser's native selection.
        self.resize_client(1440, 852)
        self.panel('automation', focus=True)
        chooser = self.control(self.native_window(), 132)
        self.focus_control(chooser)
        self.desktop.send(chooser, 0x14F, 0)  # Close dropdown without selection.
        self.assertEqual(self.desktop.focus(chooser), chooser)
        selection = self.desktop.send(chooser, 0x147)
        previous = self.ready()
        self.assertEqual(previous['editorDock']['compactSelection'], 'automation')
        self.resize_client(1000, 720)
        current = self.ready()
        self.assertEqual(current['editorDock']['mode'], 'tabs')
        self.assertEqual(current['editorDock']['configuration'], previous['editorDock']['configuration'])
        self.assertTrue(user.IsWindowVisible(automation))
        self.assertEqual(self.desktop.focus(chooser), chooser)
        self.assertEqual(self.desktop.send(chooser, 0x147), selection)
        self.assertEqual(self.text(row_field), '3.')
        self.assertEqual(self.text(name_field), 'Retained region draft / 音色')
        self.assertEqual(self.doc(), before)
        self.resize_client(1440, 852)
        self.focus_control(row_field)
        for placement in ('bottom', 'right', 'secondary', 'float', 'hide'):
            self.panel('automation', placement=placement, focus=placement != 'hide')
            self.assertEqual(self.native_window('ScreamSeq.ParameterAutomation'), automation)
            self.assertEqual(self.text(row_field), '3.')
        self.client.call('workspace.layout', {'name': 'Restore custom', 'savedName': 'Independent regions'})
        current = self.ready()
        self.assertEqual(current['editorDock']['configuration'], captured['editorDock']['configuration'])
        self.assertEqual(current['returnPoints'], captured['returnPoints'])
        self.assertEqual(current['pins'], captured['pins'])
        self.assertEqual(current['parameterAutomation']['points'], captured['parameterAutomation']['points'])
        self.assertEqual(current['instrumentEnvelope']['instrument'], captured['instrumentEnvelope']['instrument'])
        self.assertEqual(self.text(name_field), 'Retained region draft / 音色')
        self.assertEqual(self.doc(), before)

    def test_minimum_compact_main_pages_keep_native_controls_inside_the_owner(self):
        self.connected()
        self.resize_client(900, 620)
        before = self.doc()
        owner = self.native_window()
        for command, selected, key in ((107, 'notes', 'noteEditor'), (108, 'samples', None),
                                       (340, 'effects', 'effectEditor'), (316, 'plugins', None),
                                       (400, 'mixer', 'mixerEditor'), (430, 'graph', 'graphEditor')):
            with self.subTest(editor=command):
                state = self.command(command)
                self.assertEqual(state['editorDock']['mode'], 'tabs')
                self.assertFalse(state['editorDock']['trackerVisible'])
                self.assertTrue(state['editorDock']['hosts']['bottom']['visible'])
                self.assertEqual(state['editorDock']['hosts']['bottom']['selected'], selected)
                if key:
                    self.assertTrue(state[key]['visible'])
                if command == 108:
                    for identifier in (230, 231, 209):
                        self.assertTrue(user.IsWindowVisible(self.control(owner, identifier)), identifier)
                self.visible_geometry(owner)
                if command in (316, 430):
                    for page in range(6):
                        self.choose(owner, 318 if command == 316 else 443, page)
                        self.visible_geometry(owner)
        self.assertEqual(self.doc(), before)

    def test_legacy_inspector_focus_and_return_reveal_pattern_from_native_tabs(self):
        self.connected()
        self.resize_client(900, 620)
        owner = self.native_window()
        automation = self.native_window('ScreamSeq.ParameterAutomation')
        before = self.doc()
        for panel in ('notes', 'samples'):
            with self.subTest(panel=panel):
                hidden = self.panel('automation', focus=True)
                self.assertFalse(hidden['editorDock']['trackerVisible'])
                self.assertEqual(hidden['editorDock']['compactSelection'], 'automation')
                position = self.read('context.get')
                focused = self.panel(panel, focus=True, pinned=True)
                self.assertTrue(focused['editorDock']['trackerVisible'])
                self.assertEqual(focused['focus'], panel)
                self.assertGreater(focused['geometry']['inspector']['width'], 0)
                self.assertFalse(user.IsWindowVisible(automation))
                self.assertEqual(self.desktop.focus(owner), owner)
                self.assertEqual(self.read('context.get'), position)
                origin = focused['returnPoints'][panel]
                self.panel('automation', focus=True)
                self.navigate(row=(origin['row'] + 7) % 64, following=False)
                for no_op in (False, True):
                    with self.subTest(no_op=no_op):
                        hidden = self.panel('automation', focus=True)
                        self.assertFalse(hidden['editorDock']['trackerVisible'])
                        returned = self.panel(panel, **{'return': True})
                        cursor = self.read('context.get')
                        self.assertTrue(returned['editorDock']['trackerVisible'])
                        self.assertEqual(returned['focus'], 'pattern')
                        self.assertEqual(self.desktop.focus(owner), owner)
                        self.assertFalse(user.IsWindowVisible(automation))
                        self.assertEqual(returned['returnPoints'][panel], origin)
                        self.assertTrue(returned['pins'][panel])
                        for field in ('pattern', 'row', 'channel', 'column'):
                            self.assertEqual(cursor[field], origin[field], field)
                        self.assertFalse(cursor['following'])
        self.assertEqual(self.doc(), before)

    def test_docking_fallback_retains_focused_main_canvas_and_raw_control(self):
        self.add_gain()
        self.write('instrument.create', sample=1)
        self.write('graph.create', name='Retained Main graph')
        before = self.doc()
        owner = self.native_window()
        self.resize_client(1000, 720)

        def graph_identity(state):
            return {key: state['graphEditor'][key] for key in (
                'graph', 'node', 'wire', 'modulation', 'dirty', 'fieldDraft', 'expectedRevision')}

        def assert_main_retained(state, focus, captured):
            self.assertEqual(state['editorDock']['mode'], 'tabs')
            self.assertEqual(state['editorDock']['compactSelection'], 'graph')
            self.assertTrue(state['graphEditor']['visible'])
            self.assertFalse(state['editorDock']['trackerVisible'])
            self.assertEqual(state['focus'], 'graph')
            self.assertEqual(self.desktop.focus(owner), focus)
            self.assertTrue(user.IsWindowVisible(focus))
            self.assertEqual(graph_identity(state), captured)
            self.assertEqual(self.doc(), before)

        for placement in ('right', 'secondary'):
            with self.subTest(canvas_placement=placement):
                self.panel('automation', placement='hide')
                initial = self.command(430)
                self.assertEqual(initial['editorDock']['mode'], 'none')
                self.assertTrue(initial['graphEditor']['visible'])
                self.assertEqual(initial['focus'], 'graph')
                self.assertEqual(self.desktop.focus(owner), owner)
                state = self.panel('automation', placement=placement, focus=False)
                assert_main_retained(state, owner, graph_identity(initial))

        # The real Graph-name edit is an independent unapplied field draft.
        self.panel('automation', placement='hide')
        self.command(430)
        self.choose(owner, 444, 1)  # Graph name; no selected node is required.
        raw = 'Unapplied Main graph / 保留'
        field = self.field(owner, 445, raw)
        self.focus_control(field)
        self.desktop.send(field, 0xB1, 2, 9)
        captured = graph_identity(self.ready())
        self.assertTrue(captured['fieldDraft'])
        for placement in ('right', 'secondary'):
            with self.subTest(field_placement=placement):
                self.panel('automation', placement='hide')
                self.assertEqual(self.desktop.focus(owner), field)
                state = self.panel('automation', placement=placement, focus=False)
                assert_main_retained(state, field, captured)
                self.assertEqual(self.text(field), raw)
                self.assertEqual(self.desktop.send(field, 0xB0), 2 | (9 << 16))

        # Width accommodates both columns; adding a second native row forces
        # fallback because of height, with Graph still the focused Main body.
        self.panel('automation', placement='hide')
        self.resize_client(1440, 700)
        self.command(430)
        initial = self.panel('instruments', placement='right', focus=False)
        self.assertEqual(initial['editorDock']['mode'], 'regions')
        self.assertTrue(initial['graphEditor']['visible'])
        self.assertEqual(initial['focus'], 'graph')
        self.assertEqual(self.desktop.focus(owner), owner)
        state = self.panel('automation', placement='secondary', focus=False)
        assert_main_retained(state, owner, captured)
        self.assertEqual(self.text(field), raw)

        # Bottom explicitly replaces the Main host; it must select the native
        # editor even though focus=False ordinarily retains the focused body.
        self.resize_client(1000, 720)
        state = self.panel('automation', placement='bottom', focus=False)
        automation = self.native_window('ScreamSeq.ParameterAutomation')
        self.assertEqual(state['editorDock']['mode'], 'tabs')
        self.assertEqual(state['editorDock']['compactSelection'], 'automation')
        self.assertEqual(state['editorDock']['hosts']['bottom']['selected'], 'automation')
        self.assertFalse(state['graphEditor']['visible'])
        self.assertTrue(user.IsWindowVisible(automation))
        self.assertEqual(graph_identity(state), captured)

        self.panel('automation', placement='hide')
        self.panel('instruments', placement='hide')
        self.resize_client(1000, 720)
        self.command(430)
        top = self.control(owner, 132)  # Top-bar octave chooser, outside Main.
        self.focus_control(top)
        self.desktop.send(top, 0x14F, 0)  # Close the native dropdown, retaining focus.
        self.assertEqual(self.desktop.focus(owner), top)
        top_selection = self.desktop.send(top, 0x147)
        state = self.panel('automation', placement='right', focus=False)
        self.assertEqual(state['editorDock']['mode'], 'tabs')
        self.assertEqual(state['editorDock']['compactSelection'], 'automation')
        self.assertFalse(state['graphEditor']['visible'])
        self.assertTrue(user.IsWindowVisible(automation))
        self.assertEqual(self.desktop.focus(owner), top)
        self.assertEqual(self.desktop.send(top, 0x147), top_selection)

        self.command(430)
        self.focus_control(field)
        self.desktop.send(field, 0xB1, 2, 9)
        state = self.panel('automation', placement='secondary', focus=True)
        self.assertEqual(state['editorDock']['compactSelection'], 'automation')
        self.assertEqual(state['focus'], 'automation')
        self.assertEqual(self.desktop.focus(owner), automation)
        self.assertTrue(user.IsWindowVisible(automation))
        self.assertFalse(state['graphEditor']['visible'])
        self.assertEqual(graph_identity(state), captured)
        self.assertEqual(self.text(field), raw)
        self.assertEqual(self.desktop.send(field, 0xB0), 2 | (9 << 16))
        self.assertEqual(self.doc(), before)

    def test_region_splitter_cancel_and_f6_focus_do_not_edit_the_song(self):
        state = self.connected()
        owner = self.native_window()
        before = self.doc()
        config = state['editorDock']['configuration']
        scale = state['dpi'] / 96

        def point(x, y):
            return round(x * scale) | (round(y * scale) << 16)

        right = state['editorDock']['hosts']['right']['header']
        bottom = state['editorDock']['hosts']['bottom']['header']
        for x, y, dx, dy, field in ((right['x'] - 3, right['y'] + 20, -30, 0, 'rightWidth'),
                                    (bottom['x'] + 100, bottom['y'] - 3, 0, -30, 'bottomHeight')):
            with self.subTest(splitter=field):
                self.desktop.send(owner, 0x201, 1, point(x, y))
                self.desktop.send(owner, 0x200, 1, point(x + dx, y + dy))
                self.assertNotEqual(self.state()['editorDock']['configuration'][field], config[field])
                self.queued_key(owner, 0x1B)
                self.assertEqual(self.state()['editorDock']['configuration'], config)
                self.desktop.send(owner, 0x202, 0, point(x, y))
        self.command(113)
        for expected in ('graph', 'instruments', 'automation', 'pattern'):
            state = self.command(114)
            self.assertEqual(state['focus'], expected)
            target = self.native_window('ScreamSeq.InstrumentEnvelope' if expected == 'instruments'
                                        else 'ScreamSeq.ParameterAutomation') if expected in ('instruments', 'automation') else owner
            self.assertEqual(self.desktop.focus(owner), target)
        self.assertEqual(self.doc(), before)

    def test_graph_curve_canvas_focus_survives_layout_fallback_and_native_placement(self):
        """Curve HWND focus survives unrelated placements and compact fallback."""
        self.add_gain()
        self.write('instrument.create', sample=1)
        graph = self.write('graph.create', name='Retained graph curve focus')['graph']
        node = self.write('graph.node.add', graph=graph, kind='automation')['node']
        points = [dict(position=4 * 256, value=.4, curve='linear'),
                  dict(position=48 * 256, value=.7, curve='linear')]
        self.write('graph.automation.set', graph=graph, node=node, pattern=0,
                   points=points)
        owner = self.native_window()
        self.resize_client(1000, 720)
        self.command(430)
        definition = next(value for value in self.read('graph.get', includeState=False)['library']
                          if value['id'] == graph)
        index = next(index for index, value in enumerate(definition['nodes'])
                     if value['id'] == node)
        self.choose(owner, 442, index)
        self.choose(owner, 443, 5)
        curve_owner = self.native_window('ScreamSeq.GraphCurve')
        # The independent curve replaces the Main lower body only when placed
        # there explicitly. Other native placements must retain this selection.
        self.panel('graphCurve', placement='bottom', pinned=True, focus=True)
        self.desktop.send(curve_owner, 0x113, 3)  # Retained owner's preview timer.
        state = self.ready()
        self.assertFalse(state['graphCurve']['pending'])
        self.assertGreater(state['graphCurve']['previewSamples'], 0)
        self.assertEqual(state['graphCurve']['points'], points)
        self.assertEqual(state['editorDock']['mode'], 'regions')
        before = self.doc()
        cursor = self.read('context.get')
        curve_identity = {key: state['graphCurve'][key]
                          for key in ('graph', 'node', 'pattern', 'patternID', 'expectedRevision')}

        def assert_curve(state, focus=curve_owner):
            self.assertTrue(state['graphCurve']['visible'])
            self.assertEqual(state['focus'], 'graphCurve')
            self.assertEqual(self.desktop.focus(owner), focus)
            self.assertTrue(user.IsWindowVisible(focus))
            self.assertEqual({key: state['graphCurve'][key] for key in curve_identity},
                             curve_identity)
            self.assertEqual(self.doc(), before)
            self.assertEqual(self.read('context.get'), cursor)

        def click_first_point():
            state = self.ready()
            handle = state['graphCurve']['handles'][0]
            user.GetDpiForWindow.argtypes = [w.HWND]
            scale = user.GetDpiForWindow(curve_owner) / 96
            at = round(handle['x'] * scale) | (round(handle['y'] * scale) << 16)
            self.desktop.send(curve_owner, 0x201, 1, at)
            self.desktop.send(curve_owner, 0x202, 0, at)
            state = self.ready()
            self.assertEqual(state['graphCurve']['selectedPoint'], 0)
            assert_curve(state)
            return state

        # Native mouse-up and preview must keep the captured owner focused.
        # Observe logical focus before dispatching any keyboard command.
        state = click_first_point()
        self.desktop.send(curve_owner, 0x113, 3)
        assert_curve(self.ready())
        old = state['graphCurve']['points'][0]['position']
        state = self.queued_key(curve_owner, 0x27)  # Right: one curve snap step.
        assert_curve(state)
        self.assertEqual(state['graphCurve']['points'][0]['position'], old + 256)
        self.assertFalse(state['graphEditor']['dirty'], 'Arrow moved a hidden routing node')

        # An unrelated native placement may force compact fallback, but must
        # retain the selected Curve region, its HWND and captured source.
        state = self.panel('automation', placement='right', focus=False)
        self.assertEqual(state['editorDock']['mode'], 'tabs')
        self.assertEqual(state['editorDock']['compactSelection'], 'graphCurve')
        assert_curve(state)
        retained_points = state['graphCurve']['points']

        # Invalid native curve text must retain its actual HWND/caret while
        # placement and dimension changes preserve the captured source.
        raw = self.control(curve_owner, 483)
        self.focus_control(raw)
        self.desktop.send(raw, 0xB1, 0, -1)
        raw_text = ctypes.create_unicode_buffer('--.25')
        self.desktop.send(raw, 0xC2, 1, ctypes.addressof(raw_text))
        self.desktop.send(raw, 0xB1, 1, 4)
        self.assertTrue(self.ready()['graphCurve']['fieldDraft'])

        def assert_raw(state):
            assert_curve(state, raw)
            self.assertEqual(self.text(raw), '--.25')
            self.assertEqual(self.desktop.send(raw, 0xB0), 1 | (4 << 16))
            self.assertTrue(state['graphCurve']['fieldDraft'])
            self.assertEqual(state['graphCurve']['points'], retained_points)

        assert_raw(self.panel('automation', placement='secondary', focus=False))
        self.panel('automation', placement='hide')
        self.resize_client(1440, 700)
        state = self.panel('instruments', placement='right', focus=False)
        self.assertEqual(state['editorDock']['mode'], 'regions')
        assert_raw(state)
        state = self.panel('automation', placement='secondary', focus=False)
        self.assertEqual(state['editorDock']['mode'], 'tabs')  # Height fallback.
        assert_raw(state)
        for width, height, mode in ((1440, 852, 'regions'), (1000, 720, 'tabs'),
                                    (1440, 700, 'tabs'), (1440, 852, 'regions')):
            with self.subTest(client=(width, height)):
                self.resize_client(width, height)
                self.desktop.send(curve_owner, 0x113, 3)
                state = self.ready()
                self.assertEqual(state['editorDock']['mode'], mode)
                assert_raw(state)

        # A layout stores the independent Curve host, not a replacement target
        # or field value. Restore must not relabel a visible curve as routing.
        self.client.call('workspace.layout', {'name': 'Save custom',
                                              'savedName': 'Retained graph curve'})
        saved_config = self.ready()['editorDock']['configuration']
        self.resize_client(1000, 720)
        self.panel('automation', focus=True)
        self.assertFalse(self.ready()['graphCurve']['visible'])
        self.client.call('workspace.layout', {'name': 'Restore custom',
                                              'savedName': 'Retained graph curve'})
        state = self.ready()
        self.assertEqual(state['editorDock']['configuration'], saved_config)
        assert_curve(state)
        self.assertEqual(self.text(raw), '--.25')
        self.assertEqual(self.desktop.send(raw, 0xB0), 1 | (4 << 16))
        self.assertTrue(state['graphCurve']['fieldDraft'])
        self.assertEqual(state['graphCurve']['points'], retained_points)

        # Discard only the pending raw field, then exercise point keyboard
        # actions. Neither hidden graph topology nor tracker data may change.
        self.focus_control(raw)
        self.queued_key(raw, 0x1B)
        click_first_point()
        self.desktop.send(curve_owner, 0x113, 3)
        state = self.queued_key(curve_owner, 0x27)
        assert_curve(state)
        self.assertEqual(state['graphCurve']['points'][0]['position'], old + 512)
        state = self.queued_key(curve_owner, 0x2E)  # Delete point, never source node.
        assert_curve(state)
        self.assertEqual(len(state['graphCurve']['points']), 1)
        self.assertFalse(state['graphEditor']['dirty'])
        self.assertEqual(next(value for value in self.read('graph.get', includeState=False)['library']
                              if value['id'] == graph), definition)

        # Global region cycling includes the independent Curve after the
        # other native editors. Native F6 stays local to Curve point controls.
        self.resize_client(1440, 852)
        self.command(113)
        state = self.queued_key(owner, 0x75)
        self.assertEqual(state['focus'], 'instruments')
        self.assertEqual(self.command(114)['focus'], 'automation')
        assert_curve(self.command(114))
        assert_curve(self.queued_key(curve_owner, 0x75), raw)
        assert_curve(self.queued_key(raw, 0x75))
        self.command(114)
        self.assertEqual(self.ready()['focus'], 'pattern')
        self.assertEqual(self.queued_key(owner, 0x75)['focus'], 'instruments')
        self.assertEqual(self.command(114)['focus'], 'automation')
        assert_curve(self.command(114))

        # Resize itself must retain the visible curve, before another panel
        # request has any opportunity to repair the selected compact host.
        self.resize_client(1000, 720)
        state = self.ready()
        self.assertEqual(state['editorDock']['mode'], 'tabs')
        self.assertEqual(state['editorDock']['compactSelection'], 'graphCurve')
        assert_curve(state)
        self.resize_client(1440, 852)
        self.command(113)
        self.assertEqual(self.queued_key(owner, 0x75)['focus'], 'instruments')
        self.assertEqual(self.command(114)['focus'], 'automation')
        assert_curve(self.command(114))

        # Keep the original graph-property raw-field case distinct from the
        # curve fields: explicitly reveal routing before its Main page changes.
        self.command(430)
        self.choose(owner, 443, 0)
        self.choose(owner, 444, 1)
        graph_raw = self.control(owner, 445)
        self.focus_control(graph_raw)
        self.desktop.send(graph_raw, 0xB1, 0, -1)
        name = ctypes.create_unicode_buffer('Unapplied routing name / 保留')
        self.desktop.send(graph_raw, 0xC2, 1, ctypes.addressof(name))
        self.desktop.send(graph_raw, 0xB1, 2, 8)
        self.assertTrue(self.ready()['graphEditor']['fieldDraft'])
        self.resize_client(1000, 720)
        state = self.ready()
        self.assertEqual(state['editorDock']['mode'], 'tabs')
        self.assertEqual(state['editorDock']['compactSelection'], 'graph')
        self.assertTrue(state['graphEditor']['visible'])
        self.assertEqual(self.desktop.focus(owner), graph_raw)
        self.assertTrue(user.IsWindowVisible(graph_raw))
        self.assertEqual(self.text(graph_raw), 'Unapplied routing name / 保留')
        self.assertEqual(self.desktop.send(graph_raw, 0xB0), 2 | (8 << 16))
        self.assertTrue(state['graphEditor']['fieldDraft'])
        self.assertEqual(self.doc(), before)
        state = self.panel('automation', placement='right', focus=False)
        self.assertEqual(state['editorDock']['compactSelection'], 'graph')
        self.assertEqual(state['focus'], 'graph')
        self.assertEqual(self.desktop.focus(owner), graph_raw)
        self.assertEqual(self.text(graph_raw), 'Unapplied routing name / 保留')
        self.assertEqual(self.desktop.send(graph_raw, 0xB0), 2 | (8 << 16))
        self.assertTrue(state['graphEditor']['fieldDraft'])
        self.assertEqual(self.doc(), before)

    def test_inspector_and_main_host_focus_survive_placement_only_requests(self):
        self.add_gain()
        owner = self.native_window()
        self.resize_client(1000, 720)
        before = self.doc()
        cursor = self.read('context.get')

        def click_canvas(rect, x_fraction=.5, y_fraction=.5):
            self.assertGreater(rect['width'], 0)
            self.assertGreater(rect['height'], 0)
            scale = self.state()['dpi'] / 96
            x = round((rect['x'] + rect['width'] * x_fraction) * scale)
            y = round((rect['y'] + rect['height'] * y_fraction) * scale)
            self.desktop.send(owner, 0x201, 1, x | (y << 16))
            self.desktop.send(owner, 0x202, 0, x | (y << 16))
            self.assertEqual(self.desktop.focus(owner), owner)
            return self.ready()

        for panel, open_command, field_id in (('notes', 107, 364), ('samples', 108, 230)):
            for placement in ('right', 'secondary'):
                with self.subTest(panel=panel, placement=placement):
                    self.panel('automation', placement='hide')
                    state = self.command(open_command)
                    self.assertEqual(state['editorDock']['mode'], 'none')
                    self.assertEqual(state['lowerEditor'], panel)
                    self.assertTrue(state['lowerVisible'])
                    self.assertGreater(state['geometry']['inspector']['width'], 0)

                    # Same logical focus name, but a real click in the inspector
                    # must retain the Pattern host when the first dock forces tabs.
                    state = click_canvas(state['geometry']['inspector'])
                    self.assertEqual(state['focus'], panel)
                    # Inspector keys cannot edit the separate retained Main draft.
                    main_key = 'noteEditor' if panel == 'notes' else 'sampleEditor'
                    keys = ('draftCount', 'selectedEvent') if panel == 'notes' else ('start', 'end', 'draft')
                    retained_main = {key: state[main_key][key] for key in keys}
                    for key in (0x27, 0x2E):
                        state = self.queued_key(owner, key)
                        self.assertEqual({name: state[main_key][name] for name in keys}, retained_main)
                        self.assertEqual(self.doc(), before)
                    state = self.panel('automation', placement=placement, focus=False)
                    self.assertEqual(state['editorDock']['mode'], 'tabs')
                    self.assertEqual(state['editorDock']['compactSelection'], 'pattern')
                    self.assertTrue(state['editorDock']['trackerVisible'])
                    self.assertGreater(state['geometry']['inspector']['width'], 0)
                    self.assertEqual(state['focus'], panel)
                    self.assertEqual(self.desktop.focus(owner), owner)

                    # Select the matching Main canvas by mouse, not by changing
                    # its public focus string or issuing a synthetic focus command.
                    self.panel('automation', placement='hide')
                    state = self.command(open_command)
                    rectangle = (state['noteEditor']['timeline'] if panel == 'notes'
                                 else state['sampleEditor']['waveform'])
                    state = click_canvas(rectangle, .85, .6)
                    self.assertEqual(state['focus'], panel)
                    state = self.panel('automation', placement=placement, focus=False)
                    self.assertEqual(state['editorDock']['compactSelection'], panel)
                    self.assertFalse(state['editorDock']['trackerVisible'])
                    self.assertTrue(state['editorDock']['hosts']['bottom']['visible'])
                    self.assertEqual(state['editorDock']['hosts']['bottom']['selected'], panel)
                    self.assertEqual(state['focus'], panel)
                    self.assertEqual(self.desktop.focus(owner), owner)

                    # Visible native Main fields override stale inspector intent.
                    # Retain the HWND, invalid raw text, caret, and captured revision.
                    self.panel('automation', placement='hide')
                    state = self.command(open_command)
                    if panel == 'notes':
                        state = self.choose(owner, 360, 0, listbox=True)
                    click_canvas(state['geometry']['inspector'])
                    field = self.control(owner, field_id)
                    self.assertTrue(user.IsWindowVisible(field))
                    self.assertTrue(user.IsWindowEnabled(field))
                    original = self.text(field)
                    self.focus_control(field)
                    self.field(owner, field_id, '00--.125')
                    self.desktop.send(field, 0xB1, 2, 5)
                    captured = self.ready()
                    state = self.panel('automation', placement=placement, focus=False)
                    self.assertEqual(state['editorDock']['compactSelection'], panel)
                    self.assertFalse(state['editorDock']['trackerVisible'])
                    self.assertEqual(self.desktop.focus(owner), field)
                    self.assertTrue(user.IsWindowVisible(field))
                    self.assertEqual(self.text(field), '00--.125')
                    self.assertEqual(self.desktop.send(field, 0xB0), 2 | (5 << 16))
                    if panel == 'notes':
                        for key in ('pattern', 'row', 'channel', 'expectedRevision',
                                    'draftCount', 'selected', 'selectedEvent'):
                            self.assertEqual(state['noteEditor'][key], captured['noteEditor'][key], key)
                    else:
                        for key in ('id', 'start', 'end', 'draft'):
                            self.assertEqual(state['sampleEditor'][key], captured['sampleEditor'][key], key)
                    self.field(owner, field_id, original)
                    # Main field HWND ownership determines the next region,
                    # even if its previous logical name came from the inspector.
                    self.resize_client(1440, 852)
                    state = self.panel('automation', placement='right', focus=False)
                    self.assertEqual(state['editorDock']['mode'], 'regions')
                    self.assertEqual(self.desktop.focus(owner), field)
                    state = self.queued_key(field, 0x75)
                    self.assertEqual(state['focus'], 'automation')
                    self.assertEqual(self.text(field), original)
                    self.resize_client(1000, 720)
                    self.assertEqual(self.doc(), before)
                    self.assertEqual(self.read('context.get'), cursor)


        # The Effects editor intentionally focuses a native field on opening.
        # Region cycling must classify that HWND, even if logical focus still
        # names the Pattern surface from which the editor was opened.
        self.resize_client(1440, 852)
        self.panel('automation', placement='right', focus=False)
        self.command(113)
        state = self.command(340)
        effect_field = self.control(owner, 342)
        self.assertTrue(user.IsWindowVisible(effect_field))
        self.assertEqual(self.desktop.focus(owner), effect_field)
        effect_text = self.text(effect_field)
        effect_document = self.doc()
        state = self.queued_key(effect_field, 0x75)
        self.assertEqual(state['focus'], 'automation')
        self.assertEqual(self.text(effect_field), effect_text)
        self.assertEqual(self.doc(), effect_document)

if __name__ == '__main__':
    unittest.main()
