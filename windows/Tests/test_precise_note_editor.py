"""Native precise-note drafts, canvas and history in a private owned process."""
import unittest
import test_pattern_performance as support
from precise_note_native_support import PreciseNoteNativeMixin


class PreciseNoteEditorTests(PreciseNoteNativeMixin, unittest.TestCase):
    setUp = support.PatternPerformanceTests.setUp
    doc = support.PatternPerformanceTests.doc
    read = support.PatternPerformanceTests.read
    write = support.PatternPerformanceTests.write
    cells = support.PatternPerformanceTests.cells
    navigate = support.PatternPerformanceTests.navigate

    def test_retrigger_draft_local_fx_check_one_undo_and_reopen(self):
        local = next(e for e in self.read('pattern.notes.get', pattern=0)['effects'] if e['command'])
        self.write('pattern.apply', cells=[dict(pattern=0, row=4, channel=0, note=65, instrument=1,
            volumeCommand=1, volume=50, effect=local['command'], parameter=local['suggestedParameter'])])
        unrelated = [dict(channel=2, position=8*65536+100, note=67, instrument=2, velocity=80)]
        self.write('pattern.notes.set', pattern=0, events=unrelated)
        before, old_cells = self.doc(), self.cells()
        self.open_row()
        self.assertEqual(self.editor()['draftCount'], 1)
        self.assertEqual(self.doc(), before)
        self.text(364, '1/8')  # Musical fraction: half a row at four rows per beat.
        self.text(363, '100')
        self.text(376, '3')
        self.text(377, '40')
        self.command(375)
        self.assertEqual(self.editor()['draftCount'], 3)
        self.command(371)
        self.assertEqual(self.doc(), before)
        self.command(372)
        result = self.read('pattern.notes.get', pattern=0)
        notes = [e for e in result['events'] if e['channel'] == 0]
        self.assertEqual([e['position'] % 65536 for e in notes], [32768, 43690, 54613])
        self.assertEqual([e['velocity'] for e in notes], [100, 70, 40])
        self.assertTrue(all(e['effect'] == local['command'] for e in notes))
        self.assertEqual([e for e in result['events'] if e['channel'] == 2], unrelated)
        cell = next(c for c in self.cells() if c['row'] == 4 and c['channel'] == 0)
        self.assertEqual((cell['note'], cell['instrument'], cell['effect'], cell['parameter']), (0, 0, 0, 0))
        saved = self.doc()
        self.command(372)
        self.assertEqual(self.doc(), saved)
        self.write('history.undo', domain='document')
        self.assertEqual(self.cells(), old_cells)
        self.assertEqual(self.read('pattern.notes.get', pattern=0)['events'], unrelated)
        self.write('history.redo', domain='document')
        path = self.folder / 'precise-native.screamseq'
        self.write('document.save', path=str(path))
        self.write('document.open', path=str(path))
        self.assertEqual(self.read('pattern.notes.get', pattern=0), result)

    def test_stale_draft_and_pinned_target_survive_navigation_and_rack(self):
        self.write('pattern.notes.set', pattern=0, events=[dict(channel=0, position=4*65536, note=65)])
        self.open_row()
        self.text(363, '92')
        self.native_panel(pinned=True)  # Pin the independent editable row owner.
        self.navigate(row=10, channel=1)
        self.command(316)  # Rack and return must preserve the captured row draft.
        self.command(107)
        self.assertEqual((self.editor()['row'], self.editor()['channel']), (4, 0))
        self.assertEqual(self.editor()['selectedEvent']['velocity'], 92)
        self.write('pattern.apply', cells=[dict(pattern=0, row=20, channel=1, note=60)])
        before = self.doc()
        self.command(372)
        self.assertEqual(self.doc(), before)
        self.assertTrue(self.editor()['stale'])
        self.assertEqual(self.editor()['selectedEvent']['velocity'], 92)
        self.command(9203)  # Explicit Reload captured, not Load selection.
        self.assertFalse(self.editor()['stale'])
        self.assertEqual(self.editor()['row'], 4)
        self.assertEqual(self.editor()['selectedEvent']['velocity'], 127)
        self.native_panel(pinned=False)  # Unpin the native row owner, not the inspector.
        self.command(373)
        self.assertEqual((self.editor()['row'], self.editor()['channel']), (10, 1))

    def test_unit_conversion_invalid_fields_off_and_collision_are_atomic(self):
        self.write('pattern.notes.set', pattern=0, events=[dict(channel=0, position=4*65536+16384, note=65),
            dict(channel=0, position=4*65536+32768, note=67)])
        self.open_row()
        self.combo(365, 1)
        self.assertEqual(self.editor()['selectedEvent']['position'] % 65536, 16384)
        self.text(364, '3/16')
        self.assertEqual(self.editor()['selectedEvent']['position'] % 65536, 12288)
        self.combo(365, 0)
        self.assertEqual(self.editor()['selectedEvent']['position'] % 65536, 12288)
        self.combo(365, 1)
        before_invalid = self.doc()
        for raw in ('1/0', '1//8', '/8', '1/', 'nan', 'inf', '1e309', '1/-8', '1/1'):
            with self.subTest(offset=raw):
                self.text(364, raw)
                selected = self.editor()['selectedEvent']
                self.command(371)
                self.command(372)
                self.assertEqual(self.editor()['selectedEvent'], selected)
                self.assertEqual(self.editor()['raw']['offset'], raw)
                self.assertEqual(self.doc(), before_invalid)
        self.text(364, '1.5')
        self.combo(365, 0)  # Invalid text must not silently clamp when changing units.
        self.assertEqual(self.desktop.send(self.control(365), 0x147), 1)
        before = self.doc()
        self.command(372)
        self.assertEqual(self.doc(), before)
        self.text(364, '.5')  # Collision with the other onset.
        self.command(372)
        self.assertEqual(self.doc(), before)
        self.assertIn('already occupies', self.editor()['status'])
        self.text(364, '.25')
        self.combo(361, 120)  # Note off: canonical instrument/velocity, no local FX.
        self.command(372)
        notes = self.read('pattern.notes.get', pattern=0)['events']
        off = next(e for e in notes if e['note'] == 255)
        self.assertEqual((off['position'], off['instrument'], off['velocity']), (4*65536+16384, 0, 127))
        self.assertNotIn('effect', off)
        self.assertEqual(next(e['note'] for e in notes if e['position'] % 65536 == 32768), 67)

    def test_canvas_insert_drag_cancel_keyboard_and_delete(self):
        self.open_row(row=5)  # Empty demo row; row 4 has an ordinary note to seed.
        plot = self.editor()['timeline']
        x, y, w, h = (plot[k] for k in ('x', 'y', 'width', 'height'))
        before = self.doc()
        self.mouse(0x203, x+w*.25, y+h*.25)  # Double click adds a draft hit.
        initial = self.editor()['selectedEvent']
        self.assertEqual(self.editor()['draftCount'], 1)
        self.assertAlmostEqual(initial['position'] % 65536, 16384, delta=128)
        self.assertAlmostEqual(initial['velocity'], 95, delta=2)
        xx = x+w*(initial['position'] % 65536)/65536
        yy = y+h*(1-initial['velocity']/127)
        self.mouse(0x201, xx, max(y+1, yy), 1)
        self.mouse(0x200, x+w*.75, y+h*.5, 1)
        self.assertGreater(self.editor()['selectedEvent']['position'], initial['position'])
        self.key(0x1B)
        self.assertEqual(self.editor()['selectedEvent'], initial)
        self.assertEqual(self.doc(), before)
        self.key(0x27)
        self.assertEqual(self.editor()['selectedEvent']['position'], initial['position']+256)
        self.command(372)
        self.assertEqual(len(self.read('pattern.notes.get', pattern=0)['events']), 1)
        self.focus_canvas()  # Delete must target the canvas after the native Apply action.
        self.key(0x2E)
        self.assertEqual(self.editor()['draftCount'], 0)
        self.command(372)
        self.assertEqual(self.read('pattern.notes.get', pattern=0)['events'], [])

    def test_dense_virtual_list_selects_late_hit_without_losing_other_events(self):
        events = [dict(channel=0, position=4*65536+i*12, note=61+i%12, velocity=64) for i in range(5000)]
        self.write('pattern.notes.set', pattern=0, events=events)
        self.open_row()
        self.assertEqual(self.desktop.send(self.control(360), 0x18B), 5000)
        self.desktop.send(self.control(360), 0x186, 4999)
        self.command(360, 1)
        self.text(363, '110')
        self.command(372)
        result = self.read('pattern.notes.get', pattern=0)['events']
        self.assertEqual(len(result), 5000)
        self.assertEqual([e['velocity'] for e in result[:-1]], [64]*4999)
        self.assertEqual(result[-1]['velocity'], 110)
        # Reload the same occurrence after its saved non-identity fields change.
        # Full-JSON equality or resetting to row zero would select the wrong hit.
        external = [dict(e) for e in result]
        external[-1]['velocity'] = 93
        self.write('pattern.notes.set', pattern=0, events=external)
        self.desktop.send(self.control(360), 0x197, 4980)  # LB_SETTOPINDEX
        top = self.desktop.send(self.control(360), 0x18E)
        before_reload = self.doc()
        self.command(9203)
        self.assertEqual(self.doc(), before_reload)
        self.assertEqual(self.editor()['selected'], 4999)
        self.assertEqual(self.editor()['selectedEvent'], external[-1])
        self.assertEqual(self.desktop.send(self.control(360), 0x18E), top)
        self.text(363, '111')
        self.command(372)
        revised = self.read('pattern.notes.get', pattern=0)['events']
        self.assertEqual(revised[:-1], external[:-1])
        self.assertEqual(revised[-1]['velocity'], 111)
        self.write('history.undo', domain='document')
        self.assertEqual(self.read('pattern.notes.get', pattern=0)['events'], external)
        self.write('history.redo', domain='document')
        path = self.folder / 'precise-reloaded-occurrence.screamseq'
        self.write('document.save', path=str(path))
        self.write('document.open', path=str(path))
        self.assertEqual(self.read('pattern.notes.get', pattern=0)['events'], revised)

    def test_grid_clear_removes_precise_hits_and_preserves_other_rows_and_fx(self):
        self.write('pattern.notes.set', pattern=0, events=[dict(channel=0, position=4*65536+200, note=65),
            dict(channel=0, position=5*65536, note=67), dict(channel=1, position=4*65536, note=69)])
        self.write('pattern.effects.set', pattern=0, commands=[dict(channel=0, position=4*65536, column=0, kind='pitch-set', value=2)])
        before, cells, effects = self.read('pattern.notes.get', pattern=0), self.cells(), self.read('pattern.effects.get', pattern=0)
        self.navigate(row=4, channel=0, column=0)
        self.key(0x2E)
        result = self.read('pattern.notes.get', pattern=0)
        self.assertEqual(len(result['events']), 2)
        self.assertTrue(all(e['channel'] != 0 or e['position']//65536 != 4 for e in result['events']))
        self.assertEqual(self.read('pattern.effects.get', pattern=0), effects)
        self.write('history.undo', domain='document')
        self.assertEqual(self.read('pattern.notes.get', pattern=0), before)
        self.assertEqual(self.cells(), cells)

    def test_beat_offsets_and_snap_follow_six_row_signature(self):
        self.write('document.timing.set', rowsPerBeat=6, rowsPerMeasure=24)
        self.open_row(row=5)
        self.command(369)
        self.text(364, ' +1e0 / +8 ')  # Signed scientific fraction uses the same offset parser.
        self.assertEqual(self.editor()['selectedEvent']['position'], 5*65536+49152)
        self.combo(365, 1)
        self.assertEqual(self.editor()['selectedEvent']['position'], 5*65536+49152)
        self.combo(366, 1)  # 1/16 beat, aligned to the actual six-row beat.
        plot = self.editor()['timeline']
        x, y, w, h = (plot[k] for k in ('x', 'y', 'width', 'height'))
        self.mouse(0x201, x+w*.75, y+1, 1)
        self.mouse(0x200, x+w*.30, y+h*.5, 1)
        self.mouse(0x202, x+w*.30, y+h*.5)
        self.assertEqual(self.editor()['selectedEvent']['position'], 5*65536+16384)
        self.key(0x27)
        self.assertEqual(self.editor()['selectedEvent']['position'], 5*65536+40960)
        self.command(372)
        self.assertEqual(self.read('pattern.notes.get', pattern=0)['events'][0]['position'], 5*65536+40960)

    def test_empty_row_captures_selected_sample_and_instrument_with_history(self):
        def choose_sound(slot):
            control = self.main_control(135)
            self.note_idle(control)
            entries = self.desktop.send(control, 0x146)  # CB_GETCOUNT
            index = next(i for i in range(entries)
                         if self.desktop.send(control, 0x150, i) == slot)  # CB_GETITEMDATA
            self.desktop.send(control, 0x14E, index)
            self.main_command(135, 1)
            self.assertEqual(self.read('workspace.get')['musicalTyping']['slot'], slot)

        # Exact published sample IDs establish that the chosen sound is real.
        samples = self.doc()['data']['samples']
        self.assertGreaterEqual(len(samples), 2)
        wanted, other = samples[1], samples[0]
        self.write('pattern.apply', cells=[dict(pattern=0, row=5, channel=0,
            note=0, instrument=0, volumeCommand=0, volume=0, effect=0, parameter=0)])
        choose_sound(wanted['index'])
        self.assertTrue(self.read('workspace.get')['musicalTyping']['sample'])
        self.assertEqual(self.read('workspace.get')['musicalTyping']['id'], wanted['id'])
        self.open_row(row=5)
        self.native_panel(pinned=True)
        self.assertEqual(self.editor()['draftCount'], 0)
        self.assertEqual(self.editor()['raw']['instrument'], str(wanted['index']))
        before = self.doc()
        choose_sound(other['index'])  # Existing captured insertion fields stay independent.
        self.assertEqual(self.editor()['raw']['instrument'], str(wanted['index']))
        self.command(369)
        self.assertEqual(self.editor()['selectedEvent']['instrument'], wanted['index'])
        self.command(371)
        self.assertEqual(self.doc(), before)
        self.command(372)
        captured = self.read('pattern.notes.get', pattern=0)['events']
        hit = next(e for e in captured if e['channel'] == 0 and e['position'] == 5*65536)
        self.assertEqual(hit['instrument'], wanted['index'])
        self.write('history.undo', domain='document')
        self.assertEqual(self.read('pattern.notes.get', pattern=0)['events'], [])
        self.write('history.redo', domain='document')
        path = self.folder / 'captured-precise-sound.screamseq'
        self.write('document.save', path=str(path))
        self.write('document.open', path=str(path))
        self.assertEqual(self.read('pattern.notes.get', pattern=0)['events'], captured)

        # Switch module representation: selected instrument identity must win,
        # never the coincidentally numbered sample from the preceding phase.
        self.write('instrument.create', sample=other['index'])
        instrument = self.write('instrument.create', sample=wanted['index'])['instrument']
        self.write('pattern.apply', cells=[dict(pattern=0, row=6, channel=0,
            note=0, instrument=0, volumeCommand=0, volume=0, effect=0, parameter=0)])
        choose_sound(instrument)
        chosen = self.read('workspace.get')['musicalTyping']
        self.assertFalse(chosen['sample'])
        self.assertEqual(chosen['id'], next(i['id'] for i in self.doc()['data']['instruments']
                                           if i['index'] == instrument))
        self.navigate(row=6, channel=0)
        self.command(373)  # Explicit new capture adopts the new document/sound.
        self.assertEqual(self.editor()['raw']['instrument'], str(instrument))
        before_events = self.read('pattern.notes.get', pattern=0)['events']
        self.command(369)
        self.command(372)
        saved = self.read('pattern.notes.get', pattern=0)['events']
        hit = next(e for e in saved if e['channel'] == 0 and e['position'] == 6*65536)
        self.assertEqual(hit['instrument'], instrument)
        self.assertEqual([e for e in saved if e is not hit], before_events)
        self.write('history.undo', domain='document')
        self.assertEqual(self.read('pattern.notes.get', pattern=0)['events'], before_events)
        self.write('history.redo', domain='document')
        self.write('document.save', path=str(path), overwrite=True)
        self.write('document.open', path=str(path))
        self.assertEqual(self.read('pattern.notes.get', pattern=0)['events'], saved)


if __name__ == '__main__':
    unittest.main()
