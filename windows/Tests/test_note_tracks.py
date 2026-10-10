"""Track API through the real application pipe, using a disposable desktop.

Functional API/history/codec evidence only: no device or foreground UI claim.
"""
import unittest

import test_graph_mixer_app as support
from client import ApiError


class NoteTrackAppTests(unittest.TestCase):
    # Reuse only launch/read/write helpers, never another TestCase's inventory.
    setUp = support.GraphMixerAppTests.setUp
    doc = support.GraphMixerAppTests.doc
    read = support.GraphMixerAppTests.read
    write = support.GraphMixerAppTests.write
    add_gain = support.GraphMixerAppTests.add_gain

    def layout(self):
        result = self.read('track.get')
        self.assertEqual(self.doc()['data']['trackLayout'], result)
        return result

    def reject(self, method, code=-32602, **params):
        before = self.doc()
        with self.assertRaises(ApiError) as caught:
            self.client.call(method, params)
        self.assertEqual(caught.exception.code, code)
        self.assertEqual(self.doc(), before)

    def test_catalog_revision_validation_preview_append_and_single_history_step(self):
        description = self.read('api.describe')
        self.assertIn('track.get', description['reads'])
        for method in ('track.group', 'track.create', 'track.ungroup', 'track.column.set'):
            self.assertIn(method, description['writes'])
            self.assertEqual(description['revisionGuards'][method], ['expectedRevision'])
        original = self.layout()
        before = self.doc()
        self.reject('track.create', columns=2)
        self.reject('track.create', -32001, columns=2, expectedRevision='stale')
        for params in ({'columns': True}, {'columns': 1.5}, {'columns': 0},
                       {'columns': 128}, {'columns': 1, 'extra': 1},
                       {'columns': 1, 'output': 'n01'}, {'columns': 1, 'dryRun': 1}):
            with self.subTest(params=params):
                self.reject('track.create', expectedRevision=before['revision'], **params)
        preview = self.write('track.create', columns=2, name='Chords / 和音', dryRun=True)
        self.assertTrue(preview['wouldChange'])
        self.assertEqual(preview['appendedColumns'], 2)
        self.assertEqual(self.doc(), before)
        created = self.write('track.create', columns=2, name='Chords / 和音')
        self.assertEqual(created, preview)  # DryRun did not consume identities.
        appended = self.layout()
        self.assertEqual(appended, created['layout'])
        self.assertEqual(appended['columns'][:-2], original['columns'])
        self.assertEqual(len(appended['columns']), len(original['columns']) + 2)
        group = next(t for t in appended['noteTracks'] if t['id'] == created['affectedID'])
        self.assertEqual(group['columns'], [c['id'] for c in appended['columns'][-2:]])
        self.write('history.undo', domain='plugins')
        self.assertEqual(self.layout(), original)
        self.write('history.redo', domain='document')
        self.assertEqual(self.layout(), appended)

    def test_group_ungroup_mute_noop_redo_and_native_reopen_preserve_routing(self):
        self.write('mixer.enable')
        columns = self.layout()['columns']
        effect = self.add_gain()
        self.write('mixer.bus.set', bus=columns[0]['id'], inserts=[effect], gainDB=-3)
        self.write('mixer.sends.set', bus=columns[0]['id'], sends=[
            dict(target=columns[2]['id'], gainDB=-9, preFader=True, enabled=False)])
        old_buses = {b['id']: b for b in self.read('mixer.get')['buses']}
        plugin = self.doc()['data']['nativePlugins']
        grouped = self.write('track.group', channels=[0, 1], name='Lead columns')
        layout = self.layout()
        for bus in self.read('mixer.get')['buses']:
            if bus['id'] in old_buses:
                expected = dict(old_buses[bus['id']])
                if bus['id'] in [c['id'] for c in columns[:2]]:
                    expected['output'] = grouped['affectedID']
                self.assertEqual(bus, expected)
        self.assertEqual(self.doc()['data']['nativePlugins'], plugin)
        routes = self.read('mixer.get')
        self.write('track.ungroup', track=grouped['affectedID'])
        self.assertEqual(self.read('mixer.get'), routes)
        self.assertFalse(any(t['id'] == grouped['affectedID'] for t in self.layout()['noteTracks']))
        self.write('history.undo')
        self.assertEqual(self.layout(), layout)
        column = columns[0]['id']
        original_mute = columns[0]['mute']
        self.write('track.column.set', column=column, mute=not original_mute)
        self.write('history.undo')
        before_noop = self.doc()
        result = self.write('track.column.set', column=column, mute=original_mute)
        self.assertFalse(result['wouldChange'])
        self.assertEqual(self.doc(), before_noop)
        self.write('history.redo')  # Successful no-op must retain the Redo entry.
        saved = self.layout()
        self.assertEqual(saved['columns'][0]['mute'], not original_mute)
        self.assertEqual(self.read('mixer.get'), routes)
        path = self.folder / 'note-tracks.screamseq'
        self.write('document.save', path=str(path))
        self.write('track.column.set', column=column, mute=original_mute)
        self.write('document.open', path=str(path), discard=True)
        self.assertEqual(self.layout(), saved)
        self.assertEqual(self.read('mixer.get'), routes)
        self.assertEqual(self.doc()['data']['nativePlugins'], plugin)

    def test_different_column_outputs_require_explicit_destination_in_both_orders(self):
        self.write('mixer.enable')
        columns = self.layout()['columns']
        master = next(b['id'] for b in self.read('mixer.get')['buses'] if b['kind'] == 'master')
        for disconnected in (0, 1):
            with self.subTest(disconnected=disconnected):
                self.write('mixer.bus.set', bus=columns[disconnected]['id'], output=None)
                before = self.doc()
                self.reject('track.group', channels=[0, 1], expectedRevision=before['revision'])
                self.write('mixer.bus.set', bus=columns[disconnected]['id'], output=master)
        self.write('mixer.bus.set', bus=columns[0]['id'], output=None)
        grouped = self.write('track.group', channels=[0, 1], output=master)
        self.assertEqual(next(t for t in self.layout()['noteTracks'] if t['id'] == grouped['affectedID'])['output'], master)


if __name__ == '__main__':
    unittest.main()
