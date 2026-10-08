"""Arrangement matrix contracts through a specific owned app's named pipe.

These cases qualify serialization, dispatch, revision/replay and publication.
Shared transform/renderer and exact cache-admission details stay in their native
tests. No physical MIDI, audio device, system clipboard or existing song is used.
"""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest

import private_desktop

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'windows/Api'))
from client import ApiError, Client, TransportError

ROW = 65536


class MatrixAPITests(unittest.TestCase):
    maxDiff = None

    def setUp(self):
        self.folder = Path(self.enterContext(tempfile.TemporaryDirectory(prefix='matrix-api-', dir=os.environ['TMPDIR'])))
        self.desktop = self.enterContext(private_desktop.PrivateDesktop())
        self.pid = self.desktop.launch([os.environ['SCREAMSEQ_TEST_EXE'], '--inspection', '--automation',
            '--seconds', '180', '--recovery-test-directory', str(self.folder / 'PrivateRecovery')])
        self.client = Client(r'\\.\pipe\ScreamSeq.Api.' + str(self.pid), timeout=30)
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            try:
                if not self.read('workspace.get')['documentBusy']:
                    self.assertFalse(self.read('transport.get')['playing'])
                    return
            except TransportError:
                pass
            time.sleep(.03)
        self.fail('Owned matrix inspection app did not become ready')

    def doc(self):
        return self.client.call('document.get')

    def read(self, method, **params):
        return self.client.call(method, params)['data']

    def write(self, method, **params):
        return self.client.call(method, dict(expectedRevision=self.doc()['revision'], **params))

    def reject(self, code, method, **params):
        with self.assertRaises(ApiError) as raised:
            self.client.call(method, params)
        self.assertEqual(raised.exception.code, code, str(raised.exception))

    @staticmethod
    def note(position, note, channel=0):
        return dict(channel=channel, position=position, note=note,
                    instrument=1 if note <= 120 else 0, velocity=100 if note <= 120 else 127)

    def fixture(self, channels=4, target_rows=64, shared_target=False):
        self.write('document.patch', channels=channels)
        source = self.write('pattern.create', rows=64)['data']['pattern']
        target = self.write('pattern.create', rows=target_rows)['data']['pattern']
        orders = self.doc()['data']['orders']
        source_order, target_order = orders.index(source), orders.index(target)
        if shared_target:
            self.write('order.edit', operation='after', order=len(orders) - 1, pattern=target)
        return source, target, source_order, target_order

    def cells(self, pattern):
        rows = next(item['rows'] for item in self.doc()['data']['patterns'] if item['index'] == pattern)
        return self.read('pattern.get', pattern=pattern, startRow=0, rowCount=rows)['cells']

    def notes(self, pattern):
        return self.read('pattern.notes.get', pattern=pattern)['events']

    def effects(self, pattern):
        return self.read('pattern.effects.get', pattern=pattern)

    def matrix(self, **params):
        return self.read('arrangement.matrix', **params)

    def assert_counts(self, block, tracker, precise, fx, notes, bins):
        self.assertEqual({key: block[key] for key in ('trackerEvents', 'preciseEvents', 'nativeFxEvents', 'events', 'notes')},
                         dict(trackerEvents=tracker, preciseEvents=precise, nativeFxEvents=fx,
                              events=tracker + precise + fx, notes=notes))
        expected = [0] * 16
        for index, count in bins.items():
            expected[index] = count
        self.assertEqual(block['bins'], expected)

    def test_paged_metadata_native_density_readonly_and_parameter_limits(self):
        source, _, source_order, _ = self.fixture(channels=40)
        self.write('order.edit', operation='after', order=len(self.doc()['data']['orders']) - 1, pattern=source)
        repeated_order = len(self.doc()['data']['orders']) - 1
        doc = self.doc()['data']
        self.write('song.annotate', id=doc['orderMetadata'][source_order]['id'], name='Verse / 序奏', annotation='Line 1\nLine 2', color=0x395B6D)
        self.write('song.annotate', id=doc['tracks'][0]['id'], name='Lead / 旋律', annotation='Stored track notes', color=0x624C38)
        self.write('pattern.apply', cells=[dict(pattern=source, row=0, channel=0, note=64, instrument=1)])
        self.write('pattern.notes.set', pattern=source, events=[self.note(4 * ROW - 1, 61), self.note(4 * ROW, 255),
            self.note(64 * ROW - 1, 65), self.note(0, 255, 1), self.note(0, 67, 1)])
        self.write('pattern.effects.set', pattern=source, columns=[dict(channel=0, count=3), dict(channel=2, count=2)], commands=[
            dict(channel=0, position=2 * ROW, column=2, kind='note-cut'),
            dict(channel=2, position=32 * ROW, column=1, kind='pitch-set', value=2)])
        before = self.doc(); context = self.read('context.get')
        description = self.read('api.describe')
        self.assertIn('arrangement.matrix', description['reads'])
        self.assertIn('arrangement.copyBlock', description['writes'])
        self.assertEqual(description['revisionGuards']['arrangement.copyBlock'], ['expectedRevision'])
        self.assertEqual({key: description['arrangementMatrix'][key] for key in (
            'maximumOrders', 'maximumChannels', 'defaultOrders', 'defaultChannels', 'densityBins')},
            dict(maximumOrders=128, maximumChannels=32, defaultOrders=64, defaultChannels=16, densityBins=16))
        matrix = self.matrix()
        self.assertEqual((matrix['totalOrders'], matrix['totalChannels'], matrix['startOrder'], matrix['startChannel']),
                         (len(before['data']['orders']), 40, 0, 0))
        self.assertEqual(len(matrix['tracks']), 16)
        first = matrix['orders'][source_order]
        for key in ('id', 'name', 'annotation', 'color'):
            self.assertEqual(first[key], before['data']['orderMetadata'][source_order][key])
            self.assertEqual(matrix['tracks'][0][key], before['data']['tracks'][0][key])
        self.assert_counts(first['blocks'][0], 1, 3, 1, 3, {0: 3, 1: 1, 15: 1})
        self.assert_counts(first['blocks'][1], 0, 2, 0, 1, {0: 2})
        self.assert_counts(first['blocks'][2], 0, 0, 1, 0, {8: 1})
        repeated = matrix['orders'][repeated_order]
        self.assertNotEqual(first['id'], repeated['id'])
        self.assertEqual(first['patternID'], repeated['patternID'])
        self.assertEqual(first['blocks'], repeated['blocks'])
        page = self.matrix(startOrder=source_order, orderCount=1, startChannel=1, channelCount=2)
        self.assertEqual(page['orders'][0]['blocks'], first['blocks'][1:3])
        self.assertEqual([item['channel'] for item in page['tracks']], [1, 2])
        self.assertEqual(len(self.matrix(channelCount=32)['tracks']), 32)
        self.assertEqual([item['channel'] for item in self.matrix(startChannel=36)['tracks']], [36, 37, 38, 39])
        empty = self.matrix(startOrder=matrix['totalOrders'], channelCount=32)
        self.assertEqual(empty['orders'], []); self.assertEqual(len(empty['tracks']), 32)
        for params in ({'unexpected': 1}, {'startOrder': True}, {'orderCount': 0}, {'orderCount': 129},
                       {'startOrder': matrix['totalOrders'] + 1}, {'startChannel': 40}, {'channelCount': 0},
                       {'channelCount': 33}, {'channelCount': 1.5}, {'startOrder': matrix['totalOrders'], 'channelCount': 0}):
            with self.subTest(params=params):
                self.reject(-32602, 'arrangement.matrix', **params)
        self.assertEqual(self.doc(), before)
        after_context = self.read('context.get')
        for key in ('contextRevision', 'pattern', 'row', 'channel', 'column', 'following'):
            self.assertEqual(after_context[key], context[key], key)

    def test_native_only_clone_preview_request_replay_undo_noop_redo_and_reopen(self):
        source, target, source_order, target_order = self.fixture(shared_target=True)
        self.write('pattern.notes.set', pattern=source, events=[self.note(17, 61), self.note(2 * ROW, 255)])
        self.write('pattern.effects.set', pattern=source, columns=[dict(channel=0, count=4)],
                   commands=[dict(channel=0, position=ROW + 7, column=3, kind='note-cut')])
        self.write('pattern.notes.set', pattern=target, events=[self.note(ROW, 70, 3)])
        self.write('pattern.effects.set', pattern=target, columns=[dict(channel=3, count=2)],
                   commands=[dict(channel=3, position=3 * ROW, column=1, kind='pitch-set', value=7)])
        target_entity = next(item for item in self.doc()['data']['patterns'] if item['index'] == target)
        self.write('song.annotate', id=target_entity['id'], name='Destination / 独立', annotation='Retain this destination metadata', color=0x573E28)
        before = self.doc(); before_matrix = self.matrix()
        before_notes, before_fx, before_cells = self.notes(target), self.effects(target), self.cells(target)
        params = dict(expectedRevision=before['revision'], sourceOrder=source_order, targetOrder=target_order,
                      sourceChannel=0, targetChannel=2)
        self.reject(-32602, 'arrangement.copyBlock', **{key: value for key, value in params.items() if key != 'expectedRevision'})
        preview = self.client.call('arrangement.copyBlock', dict(params, dryRun=True))
        self.assertFalse(preview['changed']); self.assertFalse(preview['playbackStopped'])
        self.assertTrue(preview['data']['wouldChange']); self.assertTrue(preview['data']['clonesPattern'])
        self.assertEqual(preview['data']['changedCells'], 0)
        self.assertEqual(self.doc(), before); self.assertEqual(self.matrix(), before_matrix)
        applied = self.client.call('arrangement.copyBlock', params, request_id='matrix-native-copy-once')
        self.assertTrue(applied['changed']); self.assertTrue(applied['data']['wouldChange'])
        self.assertEqual(applied['data']['changedCells'], 0)
        self.assertEqual(applied['data']['targetPattern'], preview['data']['targetPattern'])
        self.assertEqual(applied, self.client.call('arrangement.copyBlock', params, request_id='matrix-native-copy-once'))
        changed = self.doc(); clone = applied['data']['targetPattern']
        self.reject(-32001, 'arrangement.copyBlock', **params)
        self.assertEqual(self.doc(), changed)
        self.assertNotEqual(clone, target)
        self.assertEqual(changed['data']['orders'][target_order], clone)
        self.assertEqual(changed['data']['orders'][-1], target)
        self.assertEqual(changed['data']['orderMetadata'], before['data']['orderMetadata'])
        self.assertEqual(self.cells(clone), before_cells)
        self.assertEqual(self.notes(target), before_notes)
        # FX column counts are song-wide per track, while the original alias's
        # notes and commands must remain unchanged by the independent copy.
        expected_columns = [dict(item, count=4) if item['channel'] == 2 else item for item in before_fx['columns']]
        self.assertEqual(self.effects(target), dict(before_fx, columns=expected_columns))
        self.assertEqual(self.notes(clone), [self.note(17, 61, 2), self.note(ROW, 70, 3), self.note(2 * ROW, 255, 2)])
        cloned_fx = self.effects(clone)
        self.assertEqual(sorted((item['channel'], item['column'], item['position'], item['kind']) for item in cloned_fx['commands']),
                         [(2, 3, ROW + 7, 'note-cut'), (3, 1, 3 * ROW, 'pitch-set')])
        self.assertEqual(next(item['count'] for item in cloned_fx['columns'] if item['channel'] == 2), 4)
        clone_entity = next(item for item in changed['data']['patterns'] if item['index'] == clone)
        original_entity = next(item for item in before['data']['patterns'] if item['index'] == target)
        self.assertNotEqual(clone_entity['id'], original_entity['id'])
        for key in ('name', 'annotation', 'color', 'rows'):
            self.assertEqual(clone_entity[key], original_entity[key])
        after_matrix = self.matrix()
        self.write('history.undo', domain='document')
        undone = self.doc(); self.assertTrue(undone['data']['canRedo'])
        self.assertEqual(self.matrix(), before_matrix)
        # The same occupied block through two order aliases is still a no-op;
        # default makeUnique must not allocate a clone or consume Redo.
        noop = self.write('arrangement.copyBlock', sourceOrder=target_order,
                          targetOrder=len(undone['data']['orders']) - 1, sourceChannel=3, targetChannel=3)
        self.assertFalse(noop['changed']); self.assertFalse(noop['data']['wouldChange'])
        self.assertFalse(noop['data']['clonesPattern'])
        self.assertFalse(noop['playbackStopped']); self.assertEqual(self.doc(), undone)
        self.write('history.redo', domain='document')
        self.assertEqual(self.matrix(), after_matrix)
        path = self.folder / 'Matrix native copy.screamseq'
        self.write('document.save', path=str(path)); self.assertTrue(path.is_file())
        self.write('document.open', path=str(path), discard=True)
        self.assertEqual(self.matrix(), after_matrix)
        self.assertEqual(self.effects(clone), cloned_fx)
        self.assertEqual(self.notes(clone), [self.note(17, 61, 2), self.note(ROW, 70, 3), self.note(2 * ROW, 255, 2)])

    def test_copy_modes_preserve_precise_partitions_and_resolve_legacy_fx1(self):
        source, target, source_order, target_order = self.fixture()
        self.write('pattern.notes.set', pattern=source, events=[self.note(17, 61), self.note(1000, 255),
            self.note(1000, 62), self.note(5000, 64)])
        self.write('pattern.notes.set', pattern=target, events=[self.note(17, 70, 1), self.note(1000, 254, 1), self.note(3000, 73, 1)])
        speed = next(item['command'] for item in self.read('pattern.commands')['effect'] if item['name'] == 'Set Speed')
        self.write('pattern.effects.set', pattern=source, columns=[dict(channel=0, count=4)], commands=[
            dict(channel=0, position=0, column=0, kind='pitch-set', value=2),
            dict(channel=0, position=0, column=2, kind='pitch-set', value=1.5),
            dict(channel=0, position=ROW, column=3, kind='note-cut')])
        self.write('pattern.effects.set', pattern=target, columns=[dict(channel=1, count=3)], commands=[
            dict(channel=1, position=0, column=2, kind='pitch-set', value=7),
            dict(channel=1, position=2 * ROW, column=1, kind='note-cut')])
        # Whole-list effects.set replaces the primary FX layer too. Add the
        # legacy collision afterward so this really exercises shared FX-1.
        self.write('pattern.apply', cells=[dict(pattern=target, row=0, channel=1, effect=speed, parameter=6)])
        baseline = self.matrix(); original_notes, original_fx, original_cells = self.notes(target), self.effects(target), self.cells(target)
        expected = {'overwrite': [(17, 61), (1000, 255), (1000, 62), (5000, 64)],
                    'merge': [(17, 61), (1000, 255), (1000, 62), (3000, 73), (5000, 64)],
                    'mix': [(17, 70), (1000, 254), (1000, 62), (3000, 73), (5000, 64)]}
        for mode in ('overwrite', 'merge', 'mix'):
            with self.subTest(mode=mode):
                reply = self.write('arrangement.copyBlock', sourceOrder=source_order, targetOrder=target_order,
                                   sourceChannel=0, targetChannel=1, mode=mode, makeUnique=False)
                self.assertTrue(reply['changed']); self.assertFalse(reply['data']['clonesPattern'])
                self.assertEqual(self.notes(target), [self.note(position, note, 1) for position, note in expected[mode]])
                commands = {(item['position'], item['column']): item for item in self.effects(target)['commands'] if item['channel'] == 1}
                self.assertEqual(set(commands), {(0, 0), (0, 2), (ROW, 3)} | ({(2 * ROW, 1)} if mode != 'overwrite' else set()))
                self.assertEqual(commands[(0, 2)]['value'], 7 if mode == 'mix' else 1.5)
                self.assertEqual(commands[(ROW, 3)]['kind'], 'note-cut')
                first = next(cell for cell in self.cells(target) if cell['row'] == 0 and cell['channel'] == 1)
                if mode == 'mix':
                    self.assertEqual((commands[(0, 0)]['kind'], first['effect'], first['parameter']), ('tracker', speed, 6))
                    self.assertEqual(reply['data']['changedCells'], 0)
                else:
                    self.assertEqual((commands[(0, 0)]['kind'], commands[(0, 0)]['value']), ('pitch-set', 2))
                    self.assertEqual((first['effect'], first['parameter']), (0, 0))
                self.write('history.undo', domain='document')
                self.assertEqual(self.matrix(), baseline)
                self.assertEqual(self.notes(target), original_notes)
                self.assertEqual(self.effects(target), original_fx)
                self.assertEqual(self.cells(target), original_cells)

    def test_unequal_lengths_require_clip_and_exclude_exact_end_without_noteoff(self):
        source, target, source_order, target_order = self.fixture(target_rows=32)
        self.write('pattern.notes.set', pattern=source, events=[self.note(32 * ROW - 1, 61),
            self.note(32 * ROW, 255), self.note(33 * ROW, 65)])
        self.write('pattern.effects.set', pattern=source, columns=[dict(channel=0, count=2)], commands=[
            dict(channel=0, position=31 * ROW + ROW // 2, column=1, kind='pitch-slide', value=7,
                 duration=2 * ROW, pitchRange=12), dict(channel=0, position=32 * ROW, column=0, kind='note-cut')])
        before = self.doc(); baseline = self.matrix()
        params = dict(expectedRevision=before['revision'], sourceOrder=source_order, targetOrder=target_order,
                      sourceChannel=0, targetChannel=1)
        for patch in ({}, {'dryRun': True}, {'clip': 1}, {'makeUnique': 1}, {'mode': 'invalid'},
                      {'sourceOrder': True}, {'channelCount': 4}, {'unexpected': 1}):
            with self.subTest(patch=patch):
                self.reject(-32602, 'arrangement.copyBlock', **dict(params, **patch))
                self.assertEqual(self.doc(), before); self.assertEqual(self.matrix(), baseline)
        preview = self.client.call('arrangement.copyBlock', dict(params, clip=True, dryRun=True))
        self.assertTrue(preview['data']['wouldChange']); self.assertEqual(preview['data']['changedCells'], 0)
        self.assertEqual(self.doc(), before)
        self.client.call('arrangement.copyBlock', dict(params, clip=True))
        self.assertEqual(self.notes(target), [self.note(32 * ROW - 1, 61, 1)])
        commands = self.effects(target)['commands']; self.assertEqual(len(commands), 1)
        self.assertEqual({key: commands[0][key] for key in ('channel', 'position', 'column', 'kind', 'duration', 'value')},
                         dict(channel=1, position=31 * ROW + ROW // 2, column=1, kind='pitch-slide', duration=ROW // 2, value=7))
        self.assert_counts(self.matrix()['orders'][target_order]['blocks'][1], 0, 1, 1, 1, {15: 2})

    def test_uint32_density_counts_and_bins_survive_named_pipe_serialization(self):
        source, _, source_order, _ = self.fixture()
        events = [dict(channel=0, position=position, note=61, instrument=1) for position in range(65536)]
        applied = self.write('pattern.notes.set', pattern=source, events=events)
        self.assertEqual(applied['data']['events'], 65536)
        self.write('pattern.apply', cells=[dict(pattern=source, row=0, channel=0, note=65, instrument=1)])
        self.write('pattern.effects.set', pattern=source, columns=[dict(channel=0, count=2)],
                   commands=[dict(channel=0, position=0, column=1, kind='note-cut')])
        revision = self.doc()['revision']
        block = self.matrix(startOrder=source_order, orderCount=1, channelCount=1)['orders'][0]['blocks'][0]
        self.assert_counts(block, 1, 65536, 1, 65537, {0: 65538})
        self.assertIs(type(block['events']), int); self.assertIs(type(block['bins'][0]), int)
        self.assertEqual(self.doc()['revision'], revision)


class MatrixControllerTests(unittest.TestCase):
    def test_native_matrix_controller_admission_history_and_persistence(self):
        executable = Path(os.environ['SCREAMSEQ_TEST_EXE']).with_name('document-controller-tests.exe')
        with tempfile.TemporaryDirectory(prefix='matrix-controller-', dir=os.environ['TMPDIR']) as directory:
            result = subprocess.run([str(executable), '--matrix', directory], capture_output=True, text=True,
                                    encoding='utf-8', errors='replace', timeout=120)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        for marker in ('PASS matrix native/cell copies, density, alias identities, one Undo, no-op Redo and native persistence',
                       'PASS matrix strict parameters, sentinel/sequence guards and rejection atomicity',
                       'PASS matrix exact cache admission before stop, rejected IDs/history preservation and bounded escaped output'):
            self.assertIn(marker, result.stdout)


if __name__ == '__main__':
    unittest.main()
