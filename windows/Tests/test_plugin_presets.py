"""Mac preset wire format, guarded plugin state transactions, and native dialogs."""
import base64
import copy
import ctypes
import hashlib
import os
import plistlib
import time
from types import SimpleNamespace
import unittest
from unittest.mock import patch
import test_plugins_app as support
import test_editor_app as dialogs
from client import ApiError
from private_desktop import user


class PluginPresetTests(unittest.TestCase):
    def setUp(self):
        cache = os.environ.get('SCREAMSEQ_TEST_INSTRUMENT_CACHE') if self._testMethodName == 'test_installed_surge_sound_retains_aliases_and_ports' else os.environ.get('SCREAMSEQ_TEST_PLUGIN_CACHE')
        if self._testMethodName == 'test_load_preserves_enabled_ports_and_absolute_automation':
            cache = os.environ.get('SCREAMSEQ_TEST_PROVIDER_CACHE')
        with patch.dict(os.environ, SCREAMSEQ_TEST_PLUGIN_CACHE=cache or ''):
            support.PluginAppTests.setUp(self)
        self.process = SimpleNamespace(pid=self.pid)
    write = support.PluginAppTests.write
    doc = support.PluginAppTests.doc
    rack = support.PluginAppTests.rack
    add = support.PluginAppTests.add
    tick = support.PluginAppTests.tick
    select = support.PluginAppTests.select
    command = support.PluginAppTests.command
    parameters = support.PluginAppTests.parameters
    state = support.PluginAppTests.state
    dialog = dialogs.EditorAppTests.dialog
    choose_dialog_path = dialogs.EditorAppTests.choose_dialog_path

    def inspect(self, path):
        return self.client.call('plugin.preset.inspect', dict(path=str(path)))['data']

    def save(self, path, slot=0, **extra):
        return self.write('plugin.preset.save', plugin=self.rack()[slot]['instanceID'],
                          path=str(path), name='Sound 🎵', **extra)['data']

    def load(self, path, slot=0, **extra):
        fields = dict(plugin=self.rack()[slot]['instanceID'], path=str(path),
                      expectedPresetRevision=self.inspect(path)['presetRevision'])
        fields.update(extra)
        return self.write('plugin.preset.load', **fields)

    def change(self, slot=0):
        p = next(p for p in self.parameters(slot) if p['writable'] and p['canSlide'] and p['max'] > p['min'])
        value = p['min'] if p['value'] != p['min'] else p['max']
        self.write('plugin.parameters.set', slot=slot, values=[dict(id=p['id'], value=value)])

    def rejects(self, code, action):
        with self.assertRaises(ApiError) as caught:
            action()
        self.assertEqual(caught.exception.code, code, str(caught.exception))

    def test_binary_xml_tokens_dry_save_atomic_overwrite_and_no_history(self):
        self.add()
        path = self.folder / 'sound.screamseq-preset'
        before = self.doc()
        dry = self.save(path, dryRun=True)
        self.assertFalse(path.exists())
        self.assertFalse(dry['written'])
        saved = self.save(path)
        self.assertTrue(saved['written'])
        self.assertEqual(self.doc(), before)
        self.assertEqual(saved['presetRevision'], 'preset:' + hashlib.sha256(path.read_bytes()).hexdigest())
        self.assertEqual({k:v for k,v in saved.items() if k not in ('written', 'path')}, self.inspect(path))
        tree = plistlib.loads(path.read_bytes())
        self.assertEqual(set(tree), {'format','version','name','plugin','state'})
        self.assertEqual(tree['format'], 'Resonance plugin preset')
        self.assertEqual(tree['state'], base64.b64decode(self.state()))
        self.assertEqual(tree['name'], 'Sound 🎵')
        old = path.read_bytes()
        self.rejects(-32602, lambda:self.save(path))
        self.assertEqual(path.read_bytes(), old)
        self.change()
        self.save(path, overwrite=True, dryRun=True)
        self.assertEqual(path.read_bytes(), old)
        before_failure = self.doc()
        with path.open('rb'):  # Deny replacement while the old file is in use.
            self.rejects(-32003, lambda:self.save(path, overwrite=True))
        self.assertEqual(path.read_bytes(), old)
        self.assertEqual(self.doc(), before_failure)
        self.assertFalse(list(self.folder.glob('*.staged.*')))
        self.save(path, overwrite=True)
        self.assertNotEqual(path.read_bytes(), old)
        for fmt in (plistlib.FMT_BINARY, plistlib.FMT_XML):
            for magic in ('Resonance plugin preset', 'ScreamSeq plugin preset'):
                tree['format'] = magic
                legacy = self.folder / 'legacy.ReSoNaNcE-PrEsEt'
                legacy.write_bytes(plistlib.dumps(tree, fmt=fmt))
                info = self.inspect(legacy)
                self.assertEqual(info['name'], tree['name'])
                self.assertEqual(info['stateBytes'], len(tree['state']))
                self.assertEqual(info['presetRevision'], 'preset:' + hashlib.sha256(legacy.read_bytes()).hexdigest())
        self.assertFalse(list(self.folder.glob('*.tmp')))
        self.assertFalse(list(self.folder.glob('*.staged.*')))

    def test_xml_unicode_entities_and_state_size_boundaries(self):
        self.add()
        path = self.folder / 'unicode.screamseq-preset'; self.save(path)
        tree = plistlib.loads(path.read_bytes()); tree['name'] = 'Sound & < > " 🎵'
        xml = plistlib.dumps(tree).decode()
        for encoding in ('utf-8', 'utf-16', 'utf-16-be', 'utf-32'):
            path.write_bytes(xml.replace('encoding="UTF-8"', 'encoding="'+encoding+'"').encode(encoding))
            self.assertEqual(self.inspect(path)['name'], tree['name'])
        reference = xml.replace('🎵', '&#x1F3B5;').replace('&amp;', '&#38;')
        path.write_bytes(reference.encode()); self.assertEqual(self.inspect(path)['name'], tree['name'])
        for bad in ('&unknown;', '&#0;', '&#xD800;', '&#x110000;', '&#;', '&amp', '\x00', '\x01'):
            with self.subTest(invalid=repr(bad)):
                path.write_bytes(xml.replace('🎵', bad).encode())
                self.rejects(-32602, lambda:self.inspect(path))
        path.write_bytes(xml.encode().replace('🎵'.encode(), b'\xff'))
        self.rejects(-32602, lambda:self.inspect(path))
        path.write_bytes(xml.encode('utf-16').replace('🎵'.encode('utf-16-le'), b'\x00\xd8'))
        self.rejects(-32602, lambda:self.inspect(path))
        for size in (16*1024*1024,16*1024*1024+1):
            tree['state'] = b'x'*size
            path.write_bytes(plistlib.dumps(tree, fmt=plistlib.FMT_BINARY))
            if size == 16*1024*1024:
                self.assertEqual(self.inspect(path)['stateBytes'], size)
            else: self.rejects(-32602, lambda:self.inspect(path))

    def test_load_stale_file_document_identity_and_one_undo(self):
        self.add()
        path = self.folder / 'original.screamseq-preset'
        self.save(path)
        original = self.state()
        identity = self.rack()[0]['instanceID']
        self.change()
        changed = self.state()
        self.assertNotEqual(changed, original)
        self.add(1)
        self.write('plugin.move', slot=0, direction=1)
        self.assertEqual(self.rack()[1]['instanceID'], identity)
        before = self.doc()
        dry = self.load(path, slot=1, dryRun=True)['data']
        self.assertEqual(dry, dict(preset=self.inspect(path), plugin=identity, loaded=False, dryRun=True))
        self.assertEqual(self.doc(), before)
        self.rejects(-32602, lambda:self.load(path, slot=0))
        self.rejects(-32001, lambda:self.load(path, slot=1, expectedPresetRevision='preset:'+'0'*64))
        self.rejects(-32001, lambda:self.client.call('plugin.preset.load', dict(plugin=identity, path=str(path), expectedRevision='stale', expectedPresetRevision=self.inspect(path)['presetRevision'])))
        self.assertEqual(self.doc(), before)
        touch = self.client.call('automation.target.get')['data']
        self.assertTrue(self.load(path, slot=1)['changed'])
        self.assertEqual(self.state(1), original)
        self.assertEqual(self.client.call('automation.target.get')['data'], touch)
        self.assertFalse(self.load(path, slot=1)['changed'])
        self.write('history.undo', domain='plugins')
        self.assertEqual(self.state(1), changed)
        self.write('history.redo', domain='plugins')
        self.assertEqual(self.state(1), original)
        token = self.inspect(path)['presetRevision']
        tree = plistlib.loads(path.read_bytes()); tree['name'] = 'Replaced'
        path.write_bytes(plistlib.dumps(tree))
        self.rejects(-32001, lambda:self.load(path, slot=1, expectedPresetRevision=token))

    def test_malformed_schema_bounds_and_vendor_decode_are_non_mutating(self):
        self.add()
        path = self.folder / 'bad.screamseq-preset'
        self.save(path)
        valid = plistlib.loads(path.read_bytes())
        before = self.doc()
        malformed = [b'', b'bplist00', b'<plist><dict>', b'not a preset']
        for key, value in [('version', 2), ('version', True), ('state', 'no data'), ('name', 'x'*201), ('format','Other'), ('extra',1)]:
            tree = copy.deepcopy(valid); tree[key] = value
            malformed.append(plistlib.dumps(tree, fmt=plistlib.FMT_BINARY))
        for key, value in [('classID', ''), ('format','CLAP'), ('isInstrument',1), ('type', -1), ('extra',1), ('path','x'*8193)]:
            tree = copy.deepcopy(valid); tree['plugin'][key] = value
            malformed.append(plistlib.dumps(tree, fmt=plistlib.FMT_BINARY))
        xml = plistlib.dumps(valid).decode()
        malformed += [xml.replace('<key>version</key>', '<key>version</key><integer>1</integer><key>version</key>').encode(),
                      xml.replace('<string>Sound 🎵</string>', '<string><dict/></string>').encode(),
                      xml.replace('<dict>', '<dict foo="bar">', 1).encode(),
                      b'<?xml version="1.0"?><!DOCTYPE plist [<!ENTITY evil "foo">]><plist><string>&evil;</string></plist>',
                      b'<'*1025,
                      b'x'*(16*1024*1024+65537)]
        for index, data in enumerate(malformed):
            with self.subTest(case=index):
                path.write_bytes(data)
                self.rejects(-32602, lambda:self.inspect(path))
                self.assertEqual(self.doc(), before)
        for invalid in (self.folder / 'wrong.txt', self.folder / 'missing' / 'sound.screamseq-preset'):
            self.rejects(-32602, lambda:self.save(invalid))
        directory = self.folder / 'directory.screamseq-preset'; directory.mkdir()
        self.rejects(-32602, lambda:self.save(directory, overwrite=True))
        # The dry contract validates class identity, never invokes vendor decoding.
        valid['state'] = b'not a valid built-in state'
        path.write_bytes(plistlib.dumps(valid))
        self.assertFalse(self.load(path, dryRun=True)['data']['loaded'])
        with self.assertRaises(ApiError): self.load(path)
        self.assertEqual(self.doc(), before)
        # Empty XML data is a valid wire value; availability is checked on load.
        valid['state'] = b''; path.write_bytes(plistlib.dumps(valid))
        self.assertEqual(self.inspect(path)['stateBytes'], 0)

    def real_roundtrip(self, descriptor, aliases=False):
        self.write('plugin.add', descriptor=descriptor)
        identity = self.rack()[0]['instanceID']
        if aliases:
            self.write('instrument.create', sample=1)
            self.write('plugin.instruments.set', plugin=identity, assignments=[dict(instrument=1, channel=1), dict(instrument=2, channel=9)])
        path = self.folder / 'vendor.screamseq-preset'
        self.save(path, overwrite=path.exists())
        original = self.state()
        self.change()
        changed = self.state()
        self.assertNotEqual(changed, original)
        self.write('plugin.bypass', slot=0, bypass=True)
        project = self.folder / 'metadata.screamseq'
        self.write('document.save', path=str(project), overwrite=project.exists())
        tree = plistlib.loads(project.read_bytes()); tree['plugins'][0]['futurePresetTest'] = dict(retain='opaque')
        project.write_bytes(plistlib.dumps(tree, fmt=plistlib.FMT_BINARY))
        self.write('document.open', path=str(project))
        # A Mac installation path and lowercase class ID must match this instance.
        preset = plistlib.loads(path.read_bytes())
        preset['plugin']['path'] = '/Library/Audio/Plug-Ins/VST3/Moved.vst3'
        preset['plugin']['name'] = 'Different display name'
        preset['plugin']['classID'] = preset['plugin']['classID'].lower()
        path.write_bytes(plistlib.dumps(preset))
        self.load(path)
        self.assertEqual(self.state(), original)
        self.write('history.undo', domain='plugins'); self.assertEqual(self.state(), changed)
        self.write('history.redo', domain='plugins'); self.assertEqual(self.state(), original)
        saved = self.folder / 'after.screamseq'
        self.write('document.save', path=str(saved), overwrite=saved.exists())
        actual = plistlib.loads(saved.read_bytes())
        expected = copy.deepcopy(tree['plugins']); expected[0]['state'] = base64.b64decode(original)
        self.assertEqual(actual['plugins'], expected)
        self.assertEqual(actual['automation'], tree['automation'])
        self.assertEqual(actual['native'], tree['native'])
        self.write('document.open', path=str(saved)); self.assertEqual(self.state(), original)
        self.write('plugin.remove', slot=0)
        self.write('history.undo', domain='plugins'); self.assertEqual(self.state(), original)
        self.write('plugin.remove', slot=0)

    @unittest.skipUnless(os.environ.get('SCREAMSEQ_TEST_PLUGIN_CACHE'), 'installed Windows effects')
    def test_installed_effects_presets_preserve_identity_metadata_history_and_reopen(self):
        descriptors = self.client.call('plugin.discover', dict(format='VST3'))['data']
        self.assertGreaterEqual(len(descriptors), 2)
        for descriptor in descriptors:
            with self.subTest(plugin=descriptor['name']):
                self.real_roundtrip(descriptor)

    @unittest.skipUnless(os.environ.get('SCREAMSEQ_TEST_INSTRUMENT_CACHE'), 'installed Windows Surge XT')
    def test_installed_surge_sound_retains_aliases_and_ports(self):
        descriptors = self.client.call('plugin.discover', dict(format='VST3'))['data']
        self.real_roundtrip(next(d for d in descriptors if d['isInstrument']), aliases=True)

    @unittest.skipUnless(os.environ.get('SCREAMSEQ_TEST_PROVIDER_CACHE'), 'native provider fixture')
    def test_load_preserves_enabled_ports_and_absolute_automation(self):
        descriptors = self.client.call('plugin.discover', dict(format='VST3'))['data']
        descriptor = next(d for d in descriptors if d['classID'] == '5245534F4E414E4350524F4752410001')
        self.write('plugin.add', descriptor=descriptor)
        path = self.folder / 'ports.screamseq-preset'; self.save(path); original = self.state()
        self.change(); changed = self.state()
        self.write('plugin.buses.set', slot=0, inputs=[1])
        project = self.folder / 'ports.screamseq'; self.write('document.save', path=str(project))
        tree = plistlib.loads(project.read_bytes())
        tree['automation'] = [[0, self.parameters()[0]['id'], 0.2, 24000]]
        project.write_bytes(plistlib.dumps(tree, fmt=plistlib.FMT_BINARY))
        self.write('document.open', path=str(project))
        self.load(path); self.assertEqual(self.state(), original)
        self.assertEqual(self.rack()[0]['auxiliaryInputs'], [1])
        self.write('history.undo', domain='plugins'); self.assertEqual(self.state(), changed)
        self.write('history.redo', domain='plugins')
        after = self.folder / 'ports-after.screamseq'; self.write('document.save', path=str(after))
        actual = plistlib.loads(after.read_bytes()); tree['plugins'][0]['state'] = base64.b64decode(original)
        self.assertEqual(actual['plugins'], tree['plugins'])
        self.assertEqual(actual['automation'], tree['automation'])
        self.assertEqual(actual['native'], tree['native'])

    def post(self, identifier):
        user.PostMessageW.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_size_t, ctypes.c_ssize_t]
        self.assertTrue(user.PostMessageW(self.hwnd, 0x111, identifier, 0))

    def wait_done(self, condition):
        for _ in range(100):
            try:
                if condition(): return
            except ApiError as error:
                if error.code != -32002: raise  # Retry only this read-only observation.
            time.sleep(.04)
        self.fail('native preset operation did not finish')

    def test_native_dialog_save_load_cancel_and_document_guard(self):
        self.add(); self.tick(); self.select(318,4); self.tick()
        path = self.folder / 'native.screamseq-preset'
        before = self.doc(); original = self.state()
        self.post(324); self.choose_dialog_path(self.process, path)
        self.wait_done(path.exists)
        self.assertEqual(self.doc(), before)
        self.assertEqual(self.inspect(path)['name'], 'native')
        self.change(); changed = self.state(); self.tick()
        self.post(325); self.choose_dialog_path(self.process, path)
        self.wait_done(lambda:self.state() == original)
        self.command(309); self.assertEqual(self.state(), changed)
        self.command(310); self.assertEqual(self.state(), original)
        before = self.doc()
        self.post(325); dlg = self.dialog(self.process, 'Load plugin preset')
        user.PostMessageW(dlg, 0x111, 2, 0)
        self.tick(); self.assertEqual(self.doc(), before)
        self.post(324); self.dialog(self.process, 'Save plugin preset')
        self.write('document.patch', title='Changed while choosing preset')
        stale = self.folder / 'stale.screamseq-preset'
        self.choose_dialog_path(self.process, stale)
        self.tick(); self.assertFalse(stale.exists())
        self.assertEqual(self.state(), original)
        self.change(); changed = self.state()
        self.post(325); self.dialog(self.process, 'Load plugin preset')
        self.write('document.patch', title='Changed during load dialog')
        before = self.doc()
        self.choose_dialog_path(self.process, path)
        self.tick(); self.assertEqual(self.state(), changed)
        self.assertEqual(self.doc(), before)
