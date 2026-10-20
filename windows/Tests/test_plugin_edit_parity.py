"""Rack order and recipe-gesture contracts through a disposable PID pipe."""
import copy
import plistlib
import sys
import unittest

import test_graph_mixer_app as support
from client import ApiError
sys.path.insert(0, str(support.ROOT / 'editor/Tests'))
from parameter_preview_contract import parameter_preview_contract
from recipe_parameter_contract import recipe_parameter_contract
from recipe_preview_contract import recipe_preview_contract
from rack_dry_run_contract import rack_dry_run_contract
from integer_api_contract import integer_api_contract
from request_replay_contract import request_replay_contract


class PluginEditParityTests(unittest.TestCase):
    setUp = support.GraphMixerAppTests.setUp
    doc = support.GraphMixerAppTests.doc
    read = support.GraphMixerAppTests.read
    write = support.GraphMixerAppTests.write
    rejected = support.GraphMixerAppTests.rejected
    add_gain = support.GraphMixerAppTests.add_gain

    def test_request_replay_contract_and_reopen(self):
        path, title = request_replay_contract(self.client, self.folder, ApiError)
        self.write('document.open', path=str(path), discard=True)
        self.assertEqual(self.doc()['data']['title'], title)

    def test_integer_api_contract_and_reopen(self):
        def expect_error(code, action):
            with self.assertRaises(ApiError) as error:
                action()
            self.assertEqual(error.exception.code, code)
        path, plugin, accepted = integer_api_contract(self.client, self.folder, expect_error)
        self.write('document.open', path=str(path), discard=True)
        self.assertEqual(self.read('plugin.state.get', plugin=plugin), accepted)

    def test_rack_dry_run_contract(self):
        def expect_error(code, action):
            with self.assertRaises(ApiError) as error:
                action()
            self.assertEqual(error.exception.code, code)
        rack_dry_run_contract(self.client, self.folder, expect_error)

    def test_transient_recipe_preview_contract_and_reopen(self):
        def expect_error(code, action):
            with self.assertRaises(ApiError) as error:
                action()
            self.assertEqual(error.exception.code, code)
        path, graph, accepted = recipe_preview_contract(self.client, self.folder, expect_error)
        self.write('document.open', path=str(path), discard=True)
        self.assertEqual(self.read('graph.get'), accepted)
        self.assertFalse(self.read('graph.plugin.preview.get')['active'])

    def test_recipe_manual_noop_preserves_exact_bases_and_redo(self):
        def expect_error(code, action):
            with self.assertRaises(ApiError) as error:
                action()
            self.assertEqual(error.exception.code, code)
        path, graph, expected = recipe_parameter_contract(self.client, self.folder, expect_error)
        self.write('document.open', path=str(path), discard=True)
        actual = next(g for g in self.read('graph.get')['library'] if g['id'] == graph)
        self.assertEqual(actual, expected)

    def ids(self):
        return [p['instanceID'] for p in self.doc()['data']['nativePlugins']]

    def test_transient_parameter_preview_contract_and_reopen(self):
        def expect_error(code, action):
            with self.assertRaises(ApiError) as error:
                action()
            self.assertEqual(error.exception.code, code)
        path, plugin, accepted = parameter_preview_contract(self.client, self.folder, expect_error)
        self.write('document.open', path=str(path), discard=True)
        self.assertEqual(self.read('plugin.state.get', plugin=plugin), accepted)
        self.assertFalse(self.read('plugin.parameters.preview.get')['active'])

    def saved(self, name):
        path = self.folder / (name + '.screamseq')
        self.write('document.save', path=str(path))
        return path, plistlib.loads(path.read_bytes())

    def recipe(self):
        plugin = self.add_gain()
        graph = self.write('graph.create', name='Gesture fixture')['graph']
        entry = self.read('graph.get')['library'][0]['nodes'][0]['id']
        node = self.write('graph.node.add', graph=graph, kind='plugin', slot=0,
                          insertAfter=entry)['node']
        return plugin, graph, node

    def test_position_preserves_opaque_state_routes_automation_and_reopen(self):
        ids = [self.add_gain() for _ in range(4)]
        for slot, plugin in enumerate(ids):
            self.write('plugin.parameters.set', plugin=plugin,
                       values=[dict(id=1, value=-3 * (slot + 1))])
            self.write('automation.replaceLane', slot=slot, id=1,
                       points=[dict(frame=slot * 1000, value=-slot)])
        self.write('mixer.enable')
        _, before = self.saved('before')
        snapshot = self.doc()
        dry = self.write('plugin.move', plugin=ids[0], position=3, dryRun=True)
        self.assertTrue(dry['wouldChange'])
        self.assertEqual(self.doc(), snapshot)
        request = dict(expectedRevision=snapshot['revision'], plugin=ids[0], position=3)
        receipt = self.client.call('plugin.move', request, request_id='move-once')
        self.assertEqual(self.client.call('plugin.move', request, request_id='move-once'), receipt)
        self.assertEqual(self.ids(), ids[1:] + ids[:1])
        path, after = self.saved('after')
        self.assertEqual(after['plugins'], before['plugins'][1:] + before['plugins'][:1])
        expected_points = copy.deepcopy(before['automation'])
        for point in expected_points:
            point[0] = 3 if point[0] == 0 else point[0] - 1
        self.assertEqual(after['automation'], expected_points)
        self.assertEqual({k: v for k, v in after.items() if k not in ('plugins', 'automation')},
                         {k: v for k, v in before.items() if k not in ('plugins', 'automation')})
        self.write('history.undo', domain='all')
        self.assertEqual(self.ids(), ids)
        self.assertEqual(self.saved('undone')[1], before)
        self.write('history.redo', domain='document')
        self.assertEqual(self.saved('redone')[1], after)
        self.write('document.open', path=str(path), discard=True)
        self.assertEqual(self.saved('reopened')[1], after)
        self.write('plugin.move', plugin=ids[0], position=0)
        self.assertEqual(self.saved('backward')[1], before)

    def test_position_noop_invalid_stale_and_redo(self):
        ids = [self.add_gain() for _ in range(3)]
        stale = self.doc()['revision']
        self.write('plugin.move', slot=0, position=2)
        self.write('history.undo')
        baseline = self.doc()
        for fields in (dict(slot=0, position=0), dict(plugin=ids[0], direction=-1),
                       dict(slot=2, direction=1), dict(slot=1, position=1, dryRun=True)):
            self.assertFalse(self.write('plugin.move', **fields)['wouldChange'])
            self.assertEqual(self.doc(), baseline)
        for fields in (dict(slot=0), dict(slot=0, position=1, direction=1),
                       dict(plugin=ids[0], slot=0, position=1), dict(plugin='missing', position=1),
                       *(dict(slot=0, position=v) for v in (-1, 3, True, .5, '1')),
                       dict(slot=0, direction=0), dict(slot=0, direction=True)):
            self.rejected('plugin.move', **fields)
        with self.assertRaises(ApiError):
            self.client.call('plugin.move', dict(expectedRevision=stale, plugin=ids[0], position=2))
        self.assertEqual(self.doc(), baseline)
        self.write('history.redo')
        self.assertEqual(self.ids(), ids[1:] + ids[:1])

    def test_recipe_gesture_one_undo_noop_rejections_and_save(self):
        plugin, graph, node = self.recipe()
        rack = self.read('plugin.state.get', plugin=plugin)
        recipe_catalog = self.read('graph.plugin.get', graph=graph, node=node)['parameters']
        rack_catalog = self.read('plugin.parameters.get', plugin=plugin)
        for recipe, instance in zip(recipe_catalog, rack_catalog, strict=True):
            for key in ('id', 'unit', 'unitLabel', 'choices', 'step', 'writable'):
                self.assertEqual(recipe[key], instance[key])
        before = self.read('graph.get')['library']
        def change(value, **extra):
            return self.write('graph.plugin.set', graph=graph, node=node,
                              parameters=[dict(id=1, value=value)], gesture='drag', **extra)
        change(-3)
        accepted = self.doc()
        change(-3)
        change(-12, dryRun=True)
        self.assertEqual(self.doc(), accepted)
        self.rejected('graph.plugin.set', graph=graph, node=node, gesture='drag',
                      parameters=[dict(id=1, value=-6), dict(id=9999, value=0)])
        for token in ('', 'x' * 81, False):
            self.rejected('graph.plugin.set', graph=graph, node=node, gesture=token,
                          parameters=[dict(id=1, value=-6)])
        self.rejected('graph.plugin.set', graph=graph, node=node, gesture='drag', inputs=[],
                      parameters=[dict(id=1, value=-6)])
        change(-6)
        change(-12)
        after = self.read('graph.get')['library']
        self.assertNotEqual(after, before)
        self.assertEqual(self.read('plugin.state.get', plugin=plugin), rack)
        path, _ = self.saved('gesture')
        self.write('history.undo')
        self.assertEqual(self.read('graph.get')['library'], before)
        self.write('history.redo')
        self.assertEqual(self.read('graph.get')['library'], after)
        self.write('document.open', path=str(path), discard=True)
        self.assertEqual(self.read('graph.get')['library'], after)
        self.assertEqual(self.read('plugin.state.get', plugin=plugin), rack)

    def test_recipe_gesture_boundaries_after_history_and_other_edits(self):
        _, graph, node = self.recipe()
        def change(value, token='drag'):
            self.write('graph.plugin.set', graph=graph, node=node,
                       parameters=[dict(id=1, value=value)], gesture=token)
        def library():
            return self.read('graph.get')['library']
        original = library()
        change(-3)
        change(-6)
        first = library()
        self.write('history.undo')
        self.write('history.redo')
        change(-9)  # Reusing a token after Undo/Redo must start a new entry.
        self.write('history.undo')
        self.assertEqual(library(), first)
        self.write('history.undo')
        self.assertEqual(library(), original)
        self.write('history.redo')
        self.write('document.patch', title='Intervening document edit')
        change(-12)
        self.write('history.undo')
        self.assertEqual(library(), first)
        self.assertEqual(self.doc()['data']['title'], 'Intervening document edit')
        self.write('history.undo')
        self.assertNotEqual(self.doc()['data']['title'], 'Intervening document edit')
        change(-15, 'new-drag')
        second = library()
        change(-18, 'another-drag')
        self.write('history.undo')
        self.assertEqual(library(), second)


if __name__ == '__main__':
    unittest.main()
