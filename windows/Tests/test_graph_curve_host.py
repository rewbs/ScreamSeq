"""Independent native graph curve hosts, captured drafts and logical children."""
import ctypes
from ctypes import wintypes as w
import time
import unittest

import private_desktop
import test_graph_curves as curves
import test_envelope_bank_ui as banks
import test_formula_workbench as formulas
import test_workspace
import test_workspace_docking as docks
from client import ApiError
from graph_curve_native_support import GraphCurveNativeMixin


user = private_desktop.user


class GraphCurveHostTests(GraphCurveNativeMixin, unittest.TestCase):
    setUp = curves.GraphCurveTests.setUp
    doc = curves.GraphCurveTests.doc
    read = curves.GraphCurveTests.read
    add_gain = curves.GraphCurveTests.add_gain
    setup_curve = curves.GraphCurveTests.setup_curve
    saved = curves.GraphCurveTests.saved
    navigate = test_workspace.WorkspaceTests.navigate
    native_window = docks.WorkspaceDockingTests.native_window
    resize_client = test_workspace.WorkspaceTests.resize_client
    focus_control = docks.WorkspaceDockingTests.focus_control
    text = docks.WorkspaceDockingTests.text
    bank = banks.EnvelopeBankUITests.bank
    bank_hwnd = banks.EnvelopeBankUITests.bank_hwnd
    bcontrol = banks.EnvelopeBankUITests.bcontrol
    bcommand = banks.EnvelopeBankUITests.bcommand
    bfield = banks.EnvelopeBankUITests.bfield
    bmouse = banks.EnvelopeBankUITests.bmouse
    idle = banks.EnvelopeBankUITests.idle
    select_first_point = banks.EnvelopeBankUITests.select_first_point
    workbench = formulas.FormulaWorkbenchTests.workbench
    whwnd = formulas.FormulaWorkbenchTests.whwnd
    wcontrol = formulas.FormulaWorkbenchTests.wcontrol
    wcommand = formulas.FormulaWorkbenchTests.wcommand
    wfield = formulas.FormulaWorkbenchTests.wfield
    wait_preview = formulas.FormulaWorkbenchTests.wait_preview
    modified_key = formulas.FormulaWorkbenchTests.modified_key

    def write(self, method, **fields):
        # Every mutation gets its own read-only preflight: a successful graph
        # create can schedule another visible editor's preview before node.add.
        self.curve_api_idle()
        return curves.GraphCurveTests.write(self, method, **fields)

    def ready(self):
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            state = self.read('workspace.get')
            pending = state['documentBusy'] or state['pendingViewCommands']
            pending |= any(state[key].get('pending', False) for key in
                ('graphCurve', 'graphEditor', 'parameterAutomation', 'instrumentEnvelope'))
            if not pending:
                return state
            time.sleep(.02)
        self.fail(str(state))

    def main_command(self, identifier):
        self.ready()
        self.desktop.send(self.desktop.hwnd(self.pid), 0x111, identifier)
        return self.ready()

    def panel(self, **fields):
        self.ready()
        self.read('workspace.panel', panel='graphCurve', **fields)
        return self.ready()

    def content(self):
        state = self.state()
        return {k: state[k] for k in ('document', 'expectedRevision', 'graph', 'node',
            'patternID', 'pattern', 'generation', 'points', 'selectedPoint', 'raw',
            'dirty', 'fieldDraft', 'start', 'end')}

    def choose_source(self, graph, node):
        self.main_command(430)
        self.select(443, 0)
        self.curve_api_idle()  # Observe the retained preview timer; do not retry the read or an edit.
        definition = next(g for g in self.read('graph.get', includeState=False)['library'] if g['id'] == graph)
        self.select(442, next(i for i, n in enumerate(definition['nodes']) if n['id'] == node))
        self.assertEqual(self.ready()['graphEditor']['node'], node)

    def open_source(self, graph, node):
        self.choose_source(graph, node)
        self.main_command(563)
        self.settle()
        self.assertEqual((self.state()['graph'], self.state()['node']), (graph, node))
        return self.curve_hwnd()

    def create_source(self):
        graph = self.write('graph.create', name='Independent graph')['graph']
        node = self.write('graph.node.add', graph=graph, kind='automation')['node']
        return graph, node

    def press_native(self, owner, identifier, pages=()):
        self.ready()
        control = user.GetDlgItem(owner, identifier)
        self.assertTrue(control, identifier)
        for page in pages:
            if user.IsWindowVisible(control):
                break
            tab = user.GetDlgItem(owner, page)
            if tab and user.IsWindowVisible(tab):
                self.desktop.send(tab, 0xF5)
        self.assertTrue(user.IsWindowVisible(control), identifier)
        self.assertTrue(user.IsWindowEnabled(control), identifier)
        self.desktop.send(control, 0xF5)
        return self.ready()

    def test_routing_and_curve_keep_independent_raw_drafts_and_targets(self):
        self.resize_client(1440, 852)
        graph, first = self.create_source()
        second = self.write('graph.node.add', graph=graph, kind='automation')['node']
        owner = self.open_source(graph, first)
        self.command(490)
        self.field(483, 'not a row')
        captured = self.content()
        song = self.doc()
        self.choose_source(graph, second)
        self.field(445, 'Second source renamed')
        self.assertTrue(self.ready()['graphEditor']['fieldDraft'])
        self.assertEqual(self.content(), captured)
        self.command(486)  # Invalid raw curve point is not a routing write.
        self.assertEqual(self.doc(), song)
        self.assertEqual(self.content(), captured)
        self.main_command(430)
        self.command(446)
        self.assertTrue(self.ready()['graphEditor']['dirty'])
        self.command(440)
        self.ready()
        definition = next(g for g in self.read('graph.get')['library'] if g['id'] == graph)
        self.assertEqual(next(n['name'] for n in definition['nodes'] if n['id'] == second), 'Second source renamed')
        self.assertEqual(self.content(), captured)
        self.assertTrue(self.state()['stale'])
        self.assertEqual(self.saved(graph, first)['points'], [])
        self.assertEqual(self.saved(graph, second)['points'], [])
        self.panel(pinned=True)
        self.command(487)  # Reload remains bound to first, not selected second.
        self.assertEqual(self.state()['node'], first)
        self.command(9103)  # Explicit Load selection captures second.
        self.assertEqual(self.state()['node'], second)
        self.assertEqual(self.curve_hwnd(), owner)

    def test_graph_editing_preset_placements_and_compact_focus_retain_curve(self):
        self.add_gain()
        self.write('instrument.create', sample=1)
        graph, node = self.create_source()
        self.resize_client(1440, 852)
        owner = self.open_source(graph, node)
        self.read('workspace.layout', name='Graph editing')
        state = self.ready()
        self.assertEqual(state['editorDock']['mode'], 'regions')
        self.assertTrue(state['editorDock']['trackerVisible'])
        self.assertTrue(state['graphEditor']['visible'])
        for region, selected in (('right', 'graphCurve'), ('bottom', 'graph'), ('secondary', 'automation')):
            self.assertEqual(state['editorDock']['hosts'][region]['selected'], selected)
            self.assertTrue(state['editorDock']['hosts'][region]['visible'])
        self.panel(pinned=True, focus=True)
        self.command(490)
        self.field(483, '3.')
        captured, song = self.content(), self.doc()
        field = self.control(483)
        for placement in ('right', 'secondary', 'bottom', 'float', 'hide'):
            with self.subTest(placement=placement):
                state = self.panel(placement=placement, focus=placement != 'hide')
                self.assertEqual(state['locations']['graphCurve'], placement)
                self.assertEqual(self.curve_hwnd(), owner)
                self.assertEqual(self.content(), captured)
                self.assertEqual(self.doc(), song)
                if placement != 'hide':
                    self.assertEqual(self.desktop.focus(owner), owner)
                else:
                    self.assertFalse(self.state()['visible'])
        self.panel(placement='right', focus=True)
        self.curve_page(0)
        self.focus_control(field)
        self.desktop.send(field, 0xB1, 1, 2)
        caret = self.desktop.send(field, 0xB0)
        self.resize_client(900, 620)
        self.assertEqual(self.ready()['editorDock']['mode'], 'tabs')
        self.assertEqual(self.desktop.focus(owner), field)
        self.assertEqual(self.desktop.send(field, 0xB0), caret)
        self.resize_client(1440, 852)
        self.assertEqual(self.ready()['editorDock']['mode'], 'regions')
        self.assertEqual(self.content(), captured)
        self.read('workspace.layout', name='Save custom', savedName='Graph host retained')
        self.read('workspace.layout', name='Connected')
        self.assertEqual(self.ready()['locations']['graphCurve'], 'hide')
        self.assertEqual(self.content(), captured)
        self.read('workspace.layout', name='Restore custom', savedName='Graph host retained')
        self.assertEqual(self.curve_hwnd(), owner)
        self.assertEqual(self.content(), captured)
        self.assertEqual(self.doc(), song)

    def test_guide_only_pinned_empty_restore_captures_first_explicit_source(self):
        self.add_gain()
        self.resize_client(1440, 852)
        self.panel(pinned=True)
        self.assertEqual(self.curve_windows(), [])
        song, cursor = self.doc(), self.read('context.get')
        self.command(499)
        owner, reference = self.curve_hwnd(), self.whwnd(reference=True)
        self.assertFalse(self.state()['initialized'])
        self.assertEqual(self.state()['graph'], '')
        self.read('workspace.layout', name='Graph editing')
        self.ready()
        self.assertTrue(self.state()['visible'])
        self.assertFalse(self.state()['initialized'])
        self.assertEqual(self.doc(), song)
        self.assertEqual(self.read('context.get'), cursor)
        self.command(499)
        self.assertEqual(self.whwnd(reference=True), reference)
        graph, node = self.create_source()
        before = self.doc()
        self.open_source(graph, node)
        self.assertTrue(self.state()['initialized'])
        self.assertTrue(self.ready()['pins']['graphCurve'])
        self.assertEqual(self.curve_hwnd(), owner)
        self.assertEqual(self.whwnd(reference=True), reference)
        self.assertEqual(self.doc(), before)

    def test_removed_pattern_and_reused_index_cannot_redirect_captured_curve(self):
        graph, node = self.create_source()
        pattern = self.write('pattern.create', rows=32)['pattern']
        self.navigate(pattern=pattern, row=3)
        self.open_source(graph, node)
        self.panel(pinned=True)
        self.command(490)
        self.field(483, 'bad row')
        captured = self.content()
        identity = captured['patternID']
        self.write('history.undo', domain='document')  # Removes only pattern.create.
        for reuse in (False, True):
            with self.subTest(reused_index=reuse):
                if reuse:
                    self.assertEqual(self.write('pattern.create', rows=32)['pattern'], pattern)
                    replacement = next(p for p in self.doc()['data']['patterns'] if p['index'] == pattern)
                    self.assertNotEqual(replacement['id'], identity)
                song = self.doc()
                self.command(486)
                self.command(487)
                self.assertEqual(self.doc(), song)
                self.assertEqual(self.content(), captured)
                state = self.ready()
                with self.assertRaises(ApiError):
                    self.read('workspace.panel', panel='graphCurve', placement='float', **{'return': True})
                self.assertEqual(self.ready()['locations'], state['locations'])
                self.assertEqual(self.content(), captured)
        self.navigate(pattern=pattern, row=0)
        self.choose_source(graph, node)
        self.command(9103)
        self.assertNotEqual(self.state()['patternID'], identity)
        self.assertEqual(self.state()['pattern'], pattern)
        self.assertEqual(self.state()['points'], [])

    def test_child_handles_keep_creation_owner_across_dock_float_with_other_bank(self):
        self.add_gain()
        self.resize_client(1440, 852)
        self.main_command(502)
        self.read('workspace.panel', panel='automation', placement='right', focus=True)
        parameter = self.native_window('ScreamSeq.ParameterAutomation')
        selector = user.GetDlgItem(parameter, 4204)
        if not user.IsWindowVisible(selector):
            self.press_native(parameter, 4241)
        self.desktop.send(selector, 0x186, 1)
        self.desktop.send(parameter, 0x111, 4204 | (1 << 16), selector)
        self.press_native(parameter, 4212, range(4241, 4245))
        before = self._top_level_class('ScreamSeq.EnvelopeBank')
        self.press_native(parameter, 4220, range(4241, 4245))
        other = self._top_level_class('ScreamSeq.EnvelopeBank') - before
        self.assertEqual(len(other), 1)
        parameter_bank = next(iter(other))
        self.setup_curve()
        self.panel(placement='right', pinned=True, focus=True)
        self.command(490)
        self.command(497)
        bank = self.bank_hwnd()
        self.assertNotEqual(bank, parameter_bank)
        bank_owner = self._native_identity(bank)[2]
        self.assertEqual(bank_owner, self.desktop.hwnd(self.pid))
        self.bfield(1004, 'Retained host test')
        self.bcommand(1005)
        self.select_first_point()
        self.bfield(1017, '25')
        self.bcommand(1020)
        bank_draft = self.bank()['shape']
        self.desktop.send(bank, 0x10)
        self.panel(placement='float', focus=True)
        self.command(497)
        self.assertEqual(self.bank_hwnd(), bank)
        self.assertEqual(self._native_identity(bank)[2], bank_owner)
        self.assertEqual(self.bank()['shape'], bank_draft)
        self.assertTrue(user.IsWindow(parameter_bank))
        self.select(481, 8)
        self.field(485, 'mix(start,end,t)')
        self.command(498)
        formula = self.whwnd()
        formula_owner = self._native_identity(formula)[2]
        self.assertEqual(formula_owner, self.curve_hwnd())
        self.wfield(2001, '.25')
        self.wait_preview()
        song = self.doc()
        self.desktop.send(formula, 0x10)
        self.panel(placement='secondary', focus=True)
        self.command(498)
        self.assertEqual(self.whwnd(), formula)
        self.assertEqual(self._native_identity(formula)[2], formula_owner)
        self.assertEqual(self.workbench()['source'], '.25')
        self.assertEqual(self.bank_hwnd(), bank)
        self.assertEqual(self.doc(), song)

    def test_local_curve_and_formula_keys_do_not_type_notes_or_start_transport(self):
        self.setup_curve()
        self.panel(pinned=True, focus=True)
        self.command(490)
        self.curve_canvas()
        self.main_command(507)  # Live keys is deliberately on.
        self.panel(focus=True)
        self.curve_page(0)
        field = self.control(484)
        self.focus_control(field)
        self.desktop.send(field, 0xB1, 0, -1)
        song = self.doc()
        # Posted keys cross the real app pump before reaching the retained EDIT.
        # TranslateMessage supplies the characters; do not inject WM_CHAR too.
        for key in (ord('Q'), 0x20):
            private_desktop.check(user.PostMessageW(field, 0x100, key, 1))
            # A released key has repeat count 1 and previous/transition bits set.
            private_desktop.check(user.PostMessageW(field, 0x101, key, 0xC0000001))
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline and self.state()['raw']['value'].lower() != 'q ':
            time.sleep(.01)
        self.assertEqual(self.state()['raw']['value'].lower(), 'q ')
        self.assertEqual(self.doc(), song)
        self.assertFalse(self.read('transport.get')['playing'])
        self.key(0x1B)  # Discard only the raw point text.
        self.desktop.send(field, 0x100, 0x75)  # Local F6: point field -> curve.
        self.assertEqual(self.desktop.focus(self.curve_hwnd()), self.curve_hwnd())
        self.key(0x27)
        self.assertEqual(self.state()['points'][0]['position'], 256)
        self.assertEqual(self.doc(), song)
        self.modified_key(self.curve_hwnd(), 0xD, ctrl=True)
        self.ready()
        self.assertEqual(self.saved(self.state()['graph'], self.state()['node'])['points'], self.state()['points'])
        self.select(481, 8)
        self.field(485, 'mix(start,end,t)')
        self.command(498)
        self.wfield(2001, 'si')
        self.desktop.send(self.wcontrol(2001), 0xB1, 2, 2)
        song = self.doc()
        self.modified_key(self.wcontrol(2001), 0x20, ctrl=True)
        self.assertEqual(self.workbench()['completions'], ['sin(tau*beats)'])
        self.assertEqual(self.doc(), song)
        self.assertFalse(self.read('transport.get')['playing'])


if __name__ == '__main__':
    unittest.main()
