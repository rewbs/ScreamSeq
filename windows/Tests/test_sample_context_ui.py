"""Native sample popup commands preserve captured PCM, drafts and Undo."""
import ctypes
from ctypes import wintypes as w
import unittest

import native_context_menu
import private_desktop
import test_sample_detail_ui as support


user = private_desktop.user


class SampleContextUITests(unittest.TestCase):
    setUp = support.SampleDetailUITests.setUp
    doc = support.SampleDetailUITests.doc
    read = support.SampleDetailUITests.read
    write = support.SampleDetailUITests.write
    install = support.SampleDetailUITests.install
    pcm = support.SampleDetailUITests.pcm
    state = support.SampleDetailUITests.state
    window = support.SampleDetailUITests.window
    control = support.SampleDetailUITests.control
    field = support.SampleDetailUITests.field
    idle = support.SampleDetailUITests.idle
    press = support.SampleDetailUITests.press
    page = support.SampleDetailUITests.page
    select = support.SampleDetailUITests.select
    start = support.SampleDetailUITests.start
    region = support.SampleDetailUITests.region
    view = support.SampleDetailUITests.view
    stage = support.SampleDetailUITests.stage

    def text(self, ident):
        text = ctypes.create_unicode_buffer(512)
        self.desktop.send(self.control(ident), 0xD, len(text), ctypes.addressof(text))
        return text.value

    def menu(self, source=None):
        return native_context_menu.Menu(self, self.window(), source=source)

    def commands(self, items):
        result = {}
        for item in items:
            if item['command']:
                result[item['command']] = item
            result.update(self.commands(item.get('children', [])))
        return result

    def choose(self, command):
        with self.menu() as menu:
            menu.choose(command)
        self.idle()

    def test_cancel_keeps_selection_view_and_settings_draft_with_disabled_audio(self):
        self.region(12, 40)
        self.view(8, 64)
        self.field(4856, 'Raw sample settings')
        before, document = self.state(), self.doc()
        with self.menu() as menu:
            commands = self.commands(menu.items())
            self.assertTrue(commands[4860]['enabled'])
            self.assertFalse(commands[4833]['enabled'])
            self.assertFalse(commands[4849]['enabled'])
            self.assertFalse(commands[4807]['enabled'])
            self.assertFalse(commands[4862]['enabled'])
            menu.cancel()
        self.idle()
        for key in ('id', 'sample', 'start', 'end', 'viewStart', 'viewEnd', 'fieldDraft', 'points'):
            self.assertEqual(self.state()[key], before[key], key)
        self.assertEqual(self.text(4856), 'Raw sample settings')
        self.assertEqual(self.doc(), document)
        self.choose(4861)
        self.assertFalse(self.state()['fieldDraft'])
        self.assertEqual(self.doc(), document)

    def test_processing_preview_apply_and_undo_use_selected_saved_audio(self):
        self.page('process')
        self.region(10, 20)
        before = self.doc()
        self.choose(4832)  # Existing Reverse process, dry run.
        self.assertEqual(self.doc(), before)
        self.assertTrue(self.state()['report']['dryRun'])
        self.choose(4833)
        expected = list(self.raw)
        frames = [self.raw[i * 2:i * 2 + 2] for i in range(10, 20)]
        expected[20:40] = [value for frame in reversed(frames) for value in frame]
        self.assertEqual(self.pcm(), tuple(expected))
        self.choose(4804)
        self.assertEqual(self.pcm(), self.raw)
        self.choose(4805)
        self.assertEqual(self.pcm(), tuple(expected))

    def test_stroke_is_retained_by_cancel_and_stale_menu_is_rejected(self):
        self.select(4813, 2)
        self.stage(4, .5)
        self.stage(8, -.5)
        before = self.state()
        with self.menu() as menu:
            commands = self.commands(menu.items())
            self.assertTrue(commands[4825]['enabled'])
            self.assertIn('Ctrl+Enter', commands[4825]['label'])
            self.assertFalse(commands[4833]['enabled'])
            self.assertFalse(commands[4860]['enabled'])
            menu.cancel()
        self.assertEqual(self.state()['points'], before['points'])
        with self.menu() as menu:
            self.write('document.patch', title='Changed during sample menu')
            document = self.doc()
            menu.choose(4825)
        self.idle()
        self.assertEqual(self.doc(), document)
        self.assertEqual(self.pcm(), self.raw)
        self.assertEqual(self.state()['points'], before['points'])
        self.assertIn('menu was open', self.state()['status'])
        self.press(4803)
        self.stage(4, .5)
        self.stage(8, -.5)
        self.choose(4825)
        self.assertEqual(self.pcm()[::2], self.raw[::2])
        self.assertEqual(self.pcm()[9:18:2], (16384, 8192, 0, -8192, -16384))

    def test_settings_action_targets_captured_sample_and_round_trips(self):
        first = self.read('sample.get', sample=1)
        self.select(4801, 1)
        second = self.read('sample.get', sample=2)
        identity = self.state()['id']
        self.field(4856, 'Context edited sample')
        self.field(4858, 41)
        self.choose(4860)
        saved = self.read('sample.get', sample=2)
        self.assertEqual(saved['name'], 'Context edited sample')
        self.assertEqual(saved['volume'], 41)
        for key in ('rate', 'pan', 'loopStart', 'loopEnd', 'sustainStart', 'sustainEnd'):
            self.assertEqual(saved[key], second[key], key)
        self.assertEqual(self.state()['id'], identity)
        self.assertEqual(self.read('sample.get', sample=1), first)
        self.choose(4804)
        self.assertEqual(self.read('sample.get', sample=2), second)
        self.choose(4805)
        self.assertEqual(self.read('sample.get', sample=2), saved)
        path = self.folder / 'sample-context.screamseq'
        self.write('document.save', path=str(path))
        self.write('document.open', path=str(path), discard=True)
        self.assertEqual(self.read('sample.get', sample=2), saved)

    def test_loop_crossfade_and_private_clipboard_submenus_use_existing_actions(self):
        self.page('loops')
        with self.menu() as menu:
            commands = self.commands(menu.items())
            self.assertIn('Ctrl+Enter', commands[5017]['label'])
            self.assertNotIn('Ctrl+Enter', commands[4825]['label'])
            self.assertNotIn('Ctrl+Enter', commands[4853]['label'])
            menu.cancel()
        self.region(16, 112)
        document = self.doc()
        self.choose(4844)
        self.assertEqual(self.doc(), document)
        self.choose(5016)
        self.assertEqual(self.doc(), document)
        self.choose(5017)
        info = self.read('sample.get', sample=1)
        self.assertTrue(info['loop'])
        self.assertEqual((info['loopStart'], info['loopEnd']), (16, 112))
        self.field(4837, 8)
        before = self.doc()
        self.choose(4838)
        self.assertEqual(self.doc(), before)
        self.assertTrue(self.state()['report']['dryRun'])
        self.choose(4839)
        self.assertNotEqual(self.pcm(), self.raw)
        self.choose(4804)
        self.assertEqual(self.pcm(), self.raw)
        self.page('clipboard')
        with self.menu() as menu:
            commands = self.commands(menu.items())
            self.assertIn('Ctrl+Enter', commands[4853]['label'])
            self.assertNotIn('Ctrl+Enter', commands[5017]['label'])
            self.assertNotIn('Ctrl+Enter', commands[4825]['label'])
            menu.cancel()
        self.region(0, 4)
        self.choose(4849)
        self.assertTrue(self.read('sample.clipboard.get')['available'])
        self.region(8, 12)
        self.choose(5019)
        self.choose(4853)
        self.assertEqual(self.pcm(), self.raw[:16] + self.raw[:8] + self.raw[16:])
        self.choose(4804)
        self.assertEqual(self.pcm(), self.raw)

    def test_native_text_menu_and_nested_raw_change_preserve_fields(self):
        self.field(4856, 'Native sample text')
        with self.menu(source=self.control(4856)) as menu:
            commands = self.commands(menu.items())
            self.assertNotIn(4860, commands)
            self.assertTrue(any('Copy' in item['label'] or 'Select All' in item['label'] for item in commands.values()))
            menu.cancel()
        before = self.doc()
        with self.menu() as menu:
            self.field(4856, 'Newer text while menu open')
            menu.choose(4860)
        self.idle()
        self.assertEqual(self.text(4856), 'Newer text while menu open')
        self.assertTrue(self.state()['fieldDraft'])
        self.assertEqual(self.doc(), before)
        self.assertIn('menu was open', self.state()['status'])


if __name__ == '__main__':
    unittest.main()
