"""Native graph workflow controls with real guarded API/history/persistence."""
import ctypes
import time
import unittest
import uuid

import private_desktop
import test_graph_editor as support
import test_workspace_docking as docks
from client import ApiError, Client

user = private_desktop.user


class GraphWorkflowTests(unittest.TestCase):
    setUp = support.GraphEditorTests.setUp
    doc = support.GraphEditorTests.doc
    read = support.GraphEditorTests.read
    write = support.GraphEditorTests.write
    add_gain = support.GraphEditorTests.add_gain
    native_window = docks.WorkspaceDockingTests.native_window

    def ready(self):
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            state = self.read('workspace.get')
            if not state['documentBusy'] and not state.get('pendingViewCommands') and not state.get('graphWorkflowWindow', {}).get('pending') and not state.get('graphCurve', {}).get('pending'):
                return state
            time.sleep(.02)
        self.fail(str(state))

    def open_tool(self):
        self.ready()
        self.desktop.send(self.desktop.hwnd(self.pid), 0x111, 574)
        self.ready()
        return self.native_window('ScreamSeq.GraphWorkflows')

    def state(self):
        return self.ready()['graphWorkflowWindow']

    def control(self, identifier):
        h = user.GetDlgItem(self.native_window('ScreamSeq.GraphWorkflows'), identifier)
        self.assertTrue(h, identifier)
        return h

    def choose(self, identifier, index):
        h = self.control(identifier)
        self.assertTrue(user.IsWindowVisible(h), identifier)
        self.desktop.send(h, 0x14E, index)
        self.desktop.send(self.native_window('ScreamSeq.GraphWorkflows'), 0x111, identifier | (1 << 16), h)
        return self.ready()

    def press(self, identifier):
        self.ready()
        h = self.control(identifier)
        self.assertTrue(user.IsWindowVisible(h) and user.IsWindowEnabled(h), identifier)
        self.desktop.send(h, 0xF5)
        return self.state()

    def field(self, identifier, value):
        self.ready()
        h = self.control(identifier)
        self.assertTrue(user.IsWindowVisible(h), identifier)
        buffer = ctypes.create_unicode_buffer(value)
        self.desktop.send(h, 0x000C, 0, ctypes.addressof(buffer))

    def select_nodes(self, *indices):
        h = self.control(10003)
        self.desktop.send(h, 0x185, 0, -1)
        for index in indices:
            self.desktop.send(h, 0x185, 1, index)

    def test_groups_visual_annotations_and_reroutes_keep_one_undo_and_reopen(self):
        self.add_gain()
        graph = self.write('graph.create', name='Workflow recipe')['graph']
        effect = self.write('graph.node.add', graph=graph, kind='plugin', slot=0, insertEdge=0)['node']
        self.open_tool()
        self.choose(10001, 1)
        definition = next(d for d in self.read('graph.get')['library'] if d['id'] == graph)
        index = next(i for i, node in enumerate(definition['nodes']) if node['id'] == effect)
        self.select_nodes(index)
        self.field(10102, 'Group from native controls')
        self.press(10105)
        grouped = next(d for d in self.read('graph.get')['library'] if d['id'] == graph)
        self.assertEqual(grouped['groups'][0]['nodes'], [effect])
        self.write('history.undo', domain='document')
        self.assertEqual(next(d for d in self.read('graph.get')['library'] if d['id'] == graph)['groups'], [])
        self.write('history.redo', domain='document')
        self.press(10002)
        self.choose(10000, 4)
        self.choose(10501, 1)  # Comment, which never claims processor membership.
        self.field(10502, 'Native comment')
        self.field(10503, 'Routing remains musical; this is presentation only.')
        self.press(10511)
        annotated = next(d for d in self.read('graph.get')['library'] if d['id'] == graph)
        self.assertEqual(annotated['audio'], grouped['audio'])
        self.assertEqual(annotated['presentation']['regions'][0]['text'], 'Routing remains musical; this is presentation only.')
        self.choose(10000, 5)
        self.field(10601, '250')
        self.field(10602, '120')
        self.press(10604)
        final = next(d for d in self.read('graph.get')['library'] if d['id'] == graph)
        self.assertEqual(final['presentation']['cables'][0]['points'], [[250, 120]])
        self.assertEqual(final['audio'], grouped['audio'])
        path = self.folder / 'native-graph-workflow.screamseq'
        self.write('document.save', path=str(path))
        self.write('graph.remove', graph=graph)
        self.write('document.open', path=str(path), discard=True)
        self.assertEqual(next(d for d in self.read('graph.get')['library'] if d['id'] == graph), final)

    def test_song_source_curve_uses_sole_retained_owner_and_saved_identity(self):
        def after_preview(method, **fields):
            # A scheduled Curve preview can start after ready() observes idle.
            # Only the explicit pre-queue refusal is safe to retry: preserve
            # this exact request and revision, and never retry transport errors.
            self.assertIn(method, ('graph.automation.get', 'history.undo', 'history.redo'))
            captured = self.doc()
            params = dict(fields)
            if method in ('history.undo', 'history.redo'):
                params['expectedRevision'] = captured['revision']
            request_id = uuid.uuid4().hex
            deadline = time.monotonic() + 8
            last_error = None
            while time.monotonic() < deadline:
                remaining = deadline-time.monotonic()
                if remaining <= 0:
                    break
                client = Client(self.client.pipe, timeout=remaining)
                current = client.call('document.get')
                self.assertEqual((current['documentId'], current['revision']),
                                 (captured['documentId'], captured['revision']))
                remaining = deadline-time.monotonic()
                if remaining <= 0:
                    break
                client = Client(self.client.pipe, timeout=remaining)
                try:
                    return client.call(method, params, request_id=request_id)['data']
                except ApiError as error:
                    if error.code != -32002 or str(error) != 'Document worker is busy; no mutation was queued':
                        raise
                    last_error = error
                time.sleep(min(.02, max(0, deadline-time.monotonic())))
            self.fail(f'{method} never passed the preview busy gate: {last_error}')

        self.open_tool()
        self.choose(10000, 2)
        self.choose(10301, 0)  # Automation source in song scope.
        self.field(10302, 'Song source')
        self.press(10315)
        source = self.read('graph.get')['songSources'][-1]
        self.assertEqual(source['kind'], 'automation')
        self.press(10318)
        curve = self.ready()['graphCurve']
        self.assertTrue(curve['visible'])
        self.assertEqual(curve['node'], source['id'])
        self.assertEqual(curve['graph'], '')  # Empty owner scope maps to API null.
        hwnd = self.native_window('ScreamSeq.GraphCurve')
        self.desktop.send(user.GetDlgItem(hwnd, 9102), 0xF5)  # Retained Tools page.
        self.assertTrue(user.IsWindowVisible(user.GetDlgItem(hwnd, 490)))
        self.desktop.send(user.GetDlgItem(hwnd, 490), 0xF5)  # Existing owner Ramp action.
        self.ready()
        self.assertTrue(user.IsWindowEnabled(user.GetDlgItem(hwnd, 486)))
        self.desktop.send(user.GetDlgItem(hwnd, 486), 0xF5)
        self.ready()
        saved = after_preview('graph.automation.get', graph=None, node=source['id'], pattern=0)
        self.assertTrue(saved['points'])
        after_preview('history.undo', domain='document')
        self.assertEqual(after_preview('graph.automation.get', graph=None, node=source['id'], pattern=0)['points'], [])
        after_preview('history.redo', domain='document')
        self.assertEqual(after_preview('graph.automation.get', graph=None, node=source['id'], pattern=0)['points'], saved['points'])

    def test_raw_target_fields_survive_external_revision_and_reopen(self):
        graph = self.write('graph.create')['graph']
        self.open_tool()
        self.choose(10001, 1)
        self.field(10102, 'Raw group draft')
        before = self.state()['fields']
        self.write('graph.node.add', graph=graph, kind='lfo')
        state = self.press(10105)
        self.assertTrue(state['stale'])
        self.assertEqual(state['fields'], before)
        self.assertEqual(next(d for d in self.read('graph.get')['library'] if d['id'] == graph)['groups'], [])
        hwnd = self.native_window('ScreamSeq.GraphWorkflows')
        self.press(10004)
        self.open_tool()
        self.assertEqual(self.native_window('ScreamSeq.GraphWorkflows'), hwnd)
        self.assertEqual(self.state()['fields'], before)
