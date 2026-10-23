"""Actual-app timeline editing on an owned silent WASAPI process."""
import os
import time
import unittest

from fixture_paths import temporary_directory
from private_desktop import PrivateDesktop
from precise_note_native_support import PreciseNoteNativeMixin
import test_pattern_performance as support
import test_recording as recording_support


class PreciseNoteLiveTests(PreciseNoteNativeMixin, unittest.TestCase):
    doc = support.PatternPerformanceTests.doc
    read = support.PatternPerformanceTests.read
    write = support.PatternPerformanceTests.write
    cells = support.PatternPerformanceTests.cells
    navigate = support.PatternPerformanceTests.navigate

    def setUp(self):
        self.folder = self.enterContext(temporary_directory(prefix='precise-live-', root=os.environ['TMPDIR']))
        self.desktop = self.enterContext(PrivateDesktop())
        self.pid, self.client = recording_support.RecordingTests.launch(self, audio=True, midi=False)
        self.write('transport.stop')

    def test_immediate_timeline_composing_journey_preserves_playback_history_and_song(self):
        other = dict(channel=2, position=8*65536+123, note=71, instrument=2, velocity=87)
        self.write('pattern.notes.set', pattern=0, events=[other])
        before_cells = self.cells()
        self.open_row(row=4)
        self.native_panel(pinned=True)
        self.write('transport.play', pattern=0, startRow=4, endRow=5, cursorRow=4, loop=True)
        transport = self.read('transport.get')
        self.assertTrue(transport['playing'])
        epoch, start_frames = transport['playbackEpoch'], transport['frames']

        def continuous():
            current = self.read('transport.get')
            self.assertTrue(current['playing'])
            self.assertFalse(current['fault'])
            self.assertEqual(current['playbackEpoch'], epoch)
            self.assertGreaterEqual(current['frames'], start_frames)
            self.assertEqual([e for e in self.read('pattern.notes.get', pattern=0)['events']
                              if e['channel'] == 2], [other])

        field = self.text(363, '96')
        self.wait_note_saved()
        self.assertEqual(self.desktop.focus(field), field)
        self.assertEqual(self.field_text(field), '96')
        converted = self.read('pattern.notes.get', pattern=0)['events']
        self.assertEqual(next(e for e in converted if e['channel'] == 0)['velocity'], 96)
        self.assertEqual(next(c for c in self.cells() if c['row'] == 4 and c['channel'] == 0)['note'], 0)
        continuous()
        # Conversion is one history operation even though it changes both the
        # ordinary tracker cell and the precise schedule on the audio owner.
        self.write('history.undo', domain='all')
        self.assertEqual(self.cells(), before_cells)
        self.assertEqual(self.read('pattern.notes.get', pattern=0)['events'], [other])
        self.assertFalse(self.editor()['stale'])
        self.assertEqual((self.editor()['row'], self.editor()['channel']), (4, 0))
        continuous()
        self.write('history.redo', domain='all')
        self.assertEqual(self.read('pattern.notes.get', pattern=0)['events'], converted)
        self.assertFalse(self.editor()['stale'])
        self.assertEqual(self.editor()['selectedEvent']['velocity'], 96)
        self.assertEqual(self.desktop.focus(field), field)
        continuous()

        positions = set()
        deadline = time.monotonic() + 4
        while time.monotonic() < deadline and len(positions) < 4:
            position = self.editor()['playbackOffset']
            if position is not None:
                self.assertGreaterEqual(position, 0)
                self.assertLess(position, 1)
                positions.add(round(position, 2))
            time.sleep(.02)
        self.assertGreaterEqual(len(positions), 4, 'Playback cursor did not advance inside the looped row')

        plot = self.editor()['timeline']
        x, y = plot['x'] + plot['width']*.5, plot['y'] + plot['height']*.5
        self.mouse(0x201, x, y, 1)
        self.mouse(0x202, x, y)
        self.mouse(0x203, x, y, 1)
        self.mouse(0x202, x, y)
        inserted = self.editor()['selectedEvent']
        self.assertEqual(self.editor()['draftCount'], 2)
        self.assertIn(inserted, self.read('pattern.notes.get', pattern=0)['events'])
        continuous()
        self.focus_canvas()
        self.key(0x08)
        self.assertEqual(self.editor()['draftCount'], 1)
        self.assertNotIn(inserted, self.read('pattern.notes.get', pattern=0)['events'])
        continuous()
        self.write('history.undo', domain='all')
        self.assertIn(inserted, self.read('pattern.notes.get', pattern=0)['events'])
        self.assertEqual(self.editor()['draftCount'], 2)
        self.write('history.redo', domain='all')
        self.assertEqual(self.editor()['draftCount'], 1)
        self.assertFalse(self.editor()['stale'])
        continuous()
        final = self.read('pattern.notes.get', pattern=0)
        final_cells = self.cells()
        self.write('transport.stop')
        self.command(9203)
        path = self.folder / 'precise-live-journey.screamseq'
        self.write('document.save', path=str(path))
        self.write('document.open', path=str(path))
        self.assertEqual(self.read('pattern.notes.get', pattern=0), final)
        self.assertEqual(self.cells(), final_cells)

    def test_native_undo_includes_queued_field_and_preserves_live_playback(self):
        self.open_row(row=4)
        self.native_panel(pinned=True, focus=True)
        before_notes, before_cells = self.read('pattern.notes.get', pattern=0), self.cells()
        self.write('transport.play', pattern=0, startRow=4, endRow=5, cursorRow=4, loop=True)
        before_transport = self.read('transport.get')
        self.text(363, '96')
        self.focus_canvas()  # Song Undo, rather than native Edit text Undo.
        self.main_command(122)
        self.assertEqual(self.read('pattern.notes.get', pattern=0), before_notes)
        self.assertEqual(self.cells(), before_cells)
        self.assertFalse(self.editor()['stale'])
        self.assertFalse(self.editor()['retainedDraft'])
        self.main_command(123)
        notes = self.read('pattern.notes.get', pattern=0)
        self.assertEqual(next(e['velocity'] for e in notes['events']
                              if e['channel'] == 0 and e['position']//65536 == 4), 96)
        self.assertEqual(self.editor()['selectedEvent']['velocity'], 96)
        revision = self.doc()['revision']
        time.sleep(.35)  # The consumed field timer must not replay after history.
        self.assertEqual(self.doc()['revision'], revision)
        current = self.read('transport.get')
        self.assertTrue(current['playing'])
        self.assertFalse(current['fault'])
        self.assertEqual(current['playbackEpoch'], before_transport['playbackEpoch'])
        self.assertGreater(current['frames'], before_transport['frames'])
        self.write('transport.stop')
        path = self.folder / 'precise-live-native-history.screamseq'
        self.write('document.save', path=str(path))
        self.write('document.open', path=str(path))
        self.assertEqual(self.read('pattern.notes.get', pattern=0), notes)

    def test_native_save_includes_latest_precise_field_without_stopping_playback(self):
        path = self.folder / 'native-save-precise.screamseq'
        other = dict(channel=2, position=8*65536+123, note=71, instrument=2, velocity=87)
        self.write('pattern.notes.set', pattern=0, events=[other])
        self.write('document.save', path=str(path))
        self.open_row(row=4)
        self.native_panel(pinned=True, focus=True)
        self.write('transport.play', pattern=0, startRow=4, endRow=5, cursorRow=4, loop=True)
        before = self.read('transport.get')
        field = self.text(363, '96')
        # Native Save, deliberately without waiting for the typing debounce.
        # The deterministic owner test separately proves timer-free admission;
        # this checks the actual shell command and saved file while playing.
        self.main_command(120)
        self.assertFalse(self.editor()['retainedDraft'])
        self.assertEqual(self.desktop.focus(field), field)
        self.assertEqual(self.field_text(field), '96')
        notes = self.read('pattern.notes.get', pattern=0)
        self.assertEqual(next(e['velocity'] for e in notes['events'] if e['channel'] == 0), 96)
        self.assertEqual([e for e in notes['events'] if e['channel'] == 2], [other])
        revision = self.doc()['revision']
        self.main_command(120)
        self.assertEqual(self.doc()['revision'], revision)
        current = self.read('transport.get')
        self.assertTrue(current['playing'])
        self.assertFalse(current['fault'])
        self.assertEqual(current['playbackEpoch'], before['playbackEpoch'])
        self.assertGreaterEqual(current['frames'], before['frames'])
        self.write('transport.stop')
        self.write('document.open', path=str(path))
        self.assertEqual(self.read('pattern.notes.get', pattern=0), notes)


if __name__ == '__main__':
    unittest.main()
