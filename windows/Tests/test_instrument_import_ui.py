"""Native instrument imports and visible keymaps preserve captured drafts/history."""
import ctypes
import hashlib
import json
import os
from pathlib import Path
import time
from types import SimpleNamespace
import unittest

import private_desktop
import test_instrument_envelope_ui as support
import test_sample_settings_ui as files
from client import Client, TransportError


class InstrumentImportUITests(unittest.TestCase):
    setUp=support.InstrumentEnvelopeUITests.setUp
    doc=support.InstrumentEnvelopeUITests.doc
    read=support.InstrumentEnvelopeUITests.read
    write=support.InstrumentEnvelopeUITests.write
    state=support.InstrumentEnvelopeUITests.state
    window=support.InstrumentEnvelopeUITests.window
    control=support.InstrumentEnvelopeUITests.control
    field=support.InstrumentEnvelopeUITests.field
    idle=support.InstrumentEnvelopeUITests.idle
    press=support.InstrumentEnvelopeUITests.press
    select=support.InstrumentEnvelopeUITests.select
    start=support.InstrumentEnvelopeUITests.start
    create=support.InstrumentEnvelopeUITests.create
    settings=support.InstrumentEnvelopeUITests.settings
    key=support.InstrumentEnvelopeUITests.key
    text=files.SampleSettingsUITests.text
    audio_file=files.SampleSettingsUITests.audio_file
    post=files.SampleSettingsUITests.post
    dialog=files.SampleSettingsUITests.dialog
    choose_dialog_path=files.SampleSettingsUITests.choose_dialog_path
    choose=files.SampleSettingsUITests.choose
    cancel=files.SampleSettingsUITests.cancel
    wait_dialog_close=files.SampleSettingsUITests.wait_dialog_close

    def fixture(self):
        self.audio_file('Soft 音.wav')
        self.audio_file('Bright sample.wav',(3000,-3000,1800,-1800))
        path=self.folder/'Keyboard ñ.sfz'
        path.write_text('<region> sample=Soft 音.wav key=60\n<region> sample=Bright sample.wav key=61\n',encoding='utf-8')
        return path

    def pick(self,key):
        self.idle();self.desktop.send(self.control(4456),0x186,key)
        self.desktop.send(self.window(),0x111,4456|(1<<16),self.control(4456));self.idle()

    def map_text(self,key):
        value=ctypes.create_unicode_buffer(1024)
        self.desktop.send(self.control(4456),0x189,key,ctypes.addressof(value))
        return value.value

    def reopen(self,name):
        path=self.folder/name;self.write('document.save',path=str(path))
        self.write('document.open',path=str(path),discard=True);self.press(4452)

    def test_native_sfz_import_selects_sound_preserves_song_history_and_reopen(self):
        path=self.fixture();before=self.read('document.get');pattern=self.read('pattern.get',pattern=0,rowCount=8)
        self.post(510,main=True);self.choose(path,'Import instrument');scene=self.state();self.assertTrue(scene['visible'])
        sound=self.read('workspace.get')['musicalTyping'];self.assertFalse(sound['sample']);self.assertEqual(sound['id'],scene['instrument'])
        instrument=self.settings();self.assertNotEqual(instrument['mapping'][60],instrument['mapping'][61])
        for note in (60,61):
            sample=self.read('sample.get',sample=instrument['mapping'][note]);self.assertEqual(sample['frames'],4)
            self.assertIn(str(instrument['mapping'][note]),self.map_text(note))
        self.assertEqual(self.desktop.send(self.control(4456),0x18B),128)
        # Sample-only conversion may create preserving instruments, but it must
        # leave note pitches, volumes and effects in the existing pattern intact.
        after_pattern=self.read('pattern.get',pattern=0,rowCount=8)
        for original,after in zip(pattern['cells'],after_pattern['cells']):
            self.assertEqual({k:v for k,v in original.items() if k!='instrument'},
                             {k:v for k,v in after.items() if k!='instrument'})
        saved=self.read('document.get');self.write('history.undo',domain='document')
        undo=self.read('document.get');self.assertEqual(undo['instruments'],before['instruments']);self.assertEqual(undo['samples'],before['samples'])
        self.write('history.redo',domain='document');self.assertEqual(self.read('document.get')['instruments'],saved['instruments'])
        slot=scene['index'];project=self.folder/'imported-instrument.screamseq';self.write('document.save',path=str(project));self.write('document.open',path=str(project),discard=True)
        self.assertEqual(self.read('instrument.get',instrument=slot),instrument)
        if os.environ.get('SCREAMSEQ_INSTRUMENT_IMPORT_EVIDENCE_DIR'):
            import shutil
            directory=Path(os.environ['SCREAMSEQ_INSTRUMENT_IMPORT_EVIDENCE_DIR']);directory.mkdir(parents=True,exist_ok=True)
            shutil.copy2(project,directory/project.name)
            (directory/'instrument-import.json').write_text(json.dumps(dict(executableSHA256=hashlib.sha256(Path(os.environ['SCREAMSEQ_TEST_EXE']).read_bytes()).hexdigest(),workspace=scene,instrument=instrument,privateDesktop=True,foregroundVisualQualified=False),indent=2),encoding='utf-8')

    def test_cancel_corrupt_stale_and_replaced_document_do_not_import(self):
        self.start();path=self.fixture();before=self.doc()
        self.post(4455);self.cancel('Import instrument');self.assertEqual(self.doc(),before)
        bad=self.folder/'Broken.sfz';bad.write_bytes(b'not an instrument')
        self.post(4455);self.choose(bad,'Import instrument');self.assertEqual(self.doc(),before)
        self.post(4455);self.dialog(SimpleNamespace(pid=self.pid),'Import instrument');self.write('document.patch',title='Changed with chooser open');before=self.doc();self.choose(path)
        self.assertEqual(self.doc(),before);self.assertIn('changed while choosing',self.state()['status'])
        project=self.folder/'replacement.screamseq';self.write('document.save',path=str(project))
        self.post(4455);self.dialog(SimpleNamespace(pid=self.pid),'Import instrument');self.write('document.open',path=str(project),discard=True);before=self.doc();self.choose(path)
        self.assertEqual(self.doc(),before);self.assertIn('changed while choosing',self.state()['status'])

    def test_import_buttons_keep_instrument_and_bank_drafts(self):
        self.create();self.field(4439,'Uncommitted instrument');before=self.doc();self.press(4455)
        self.assertEqual(self.doc(),before);self.assertEqual(self.text(4439),'Uncommitted instrument');self.assertIn('draft',self.state()['status'])
        self.post(510,main=True);self.idle();self.assertEqual(self.doc(),before);self.assertEqual(self.text(4439),'Uncommitted instrument')
        self.press(4451);self.press(4410);self.press(4450);self.press(4404)
        bank='ScreamSeq.EnvelopeBank';self.field(1004,'Captured envelope',bank);self.press(1005,bank)
        self.field(1003,'Bank master draft',bank);before=self.doc();self.press(4455)
        self.assertEqual(self.doc(),before);self.assertTrue(self.state()['envelopeBank']['dirty']);self.assertIn('draft',self.state()['status'])

    def test_visible_keymap_stages_only_range_with_one_undo_and_persistence(self):
        self.create();original=self.settings();other=self.read('instrument.get',instrument=2)
        self.pick(60);self.assertEqual((self.text(4446),self.text(4447)),('60','60'));self.assertIn('C-5',self.map_text(60))
        self.field(4447,71);self.select(4448,2);doc=self.doc();self.press(4449)
        expected=list(original['mapping']);expected[60:72]=[2]*12
        self.assertEqual(self.state()['mapping'],expected);self.assertEqual(self.settings(),original);self.assertEqual(self.doc(),doc)
        self.assertTrue(self.map_text(60).startswith('* '));self.assertFalse(self.map_text(59).startswith('* '))
        self.press(4450);saved=self.settings();self.assertEqual(saved['mapping'],expected);self.assertEqual(saved['envelopes'],original['envelopes']);self.assertEqual(self.read('instrument.get',instrument=2),other)
        self.assertFalse(self.map_text(60).startswith('* '));doc=self.doc();self.press(4450);self.assertEqual(self.doc(),doc)
        self.write('history.undo',domain='document');self.assertEqual(self.settings(),original);self.write('history.redo',domain='document');self.assertEqual(self.settings(),saved)
        slot=self.state()['index'];self.reopen('keymap.screamseq');self.assertEqual(self.read('instrument.get',instrument=slot),saved)

    def test_range_drafts_invalid_stale_close_and_selection_guards(self):
        self.create();original=self.settings();self.pick(127);self.assertIn('G-10',self.map_text(127))
        self.select(4448,2);self.press(4449);self.assertEqual(self.state()['mapping'][127],2);self.press(4451)
        for first,last in [('1.5',20),(0,128),(72,60),('nan',64)]:
            self.field(4446,first);self.field(4447,last);before=self.doc();mapping=self.state()['mapping'];self.press(4449)
            self.assertEqual(self.doc(),before);self.assertEqual(self.state()['mapping'],mapping);self.assertTrue(self.state()['mappingFields']);self.key(0x1B)
        self.pick(60);self.field(4447,71);self.pick(61);self.assertEqual(self.state()['selectedKey'],60);self.assertEqual(self.text(4447),'71')
        self.select(4401,1);self.assertEqual(self.state()['index'],1);self.press(4453);self.start();self.assertEqual(self.text(4447),'71')
        before=self.doc();self.press(4450);self.assertEqual(self.doc(),before);self.assertTrue(self.state()['mappingFields'])
        self.select(4448,2);self.press(4449);draft=self.state()['mapping'];self.write('document.patch',title='External edit');before=self.doc();self.press(4450)
        self.assertEqual(self.doc(),before);self.assertTrue(self.state()['stale']);self.assertEqual(self.state()['mapping'],draft);self.assertEqual(self.settings(),original)
        self.press(4451);self.assertEqual(self.state()['mapping'],original['mapping'])

    def test_keymap_keyboard_and_all_controls_fit_minimum(self):
        self.create();self.pick(60);self.select(4429,5);user=private_desktop.user;w=ctypes.wintypes
        user.GetClientRect.argtypes=[w.HWND,ctypes.POINTER(w.RECT)];user.GetWindowRect.argtypes=user.GetClientRect.argtypes;user.MapWindowPoints.argtypes=[w.HWND,w.HWND,ctypes.POINTER(w.POINT),w.UINT];user.SetWindowPos.argtypes=[w.HWND,w.HWND,ctypes.c_int,ctypes.c_int,ctypes.c_int,ctypes.c_int,w.UINT]
        scale=self.read('workspace.get')['dpi']/96;private_desktop.check(user.SetWindowPos(self.window(),None,0,0,int(1100*scale),int(820*scale),0x16));self.idle();client=w.RECT();user.GetClientRect(self.window(),ctypes.byref(client));rects=[]
        for identifier in range(4401,4457):
            rect=w.RECT();user.GetWindowRect(self.control(identifier),ctypes.byref(rect));p=(w.POINT*2)(w.POINT(rect.left,rect.top),w.POINT(rect.right,rect.bottom));user.MapWindowPoints(None,self.window(),p,2)
            self.assertGreaterEqual(p[0].x,0,identifier);self.assertGreaterEqual(p[0].y,0,identifier);self.assertLessEqual(p[1].x,client.right,identifier);self.assertLessEqual(p[1].y,client.bottom,identifier);rects.append((identifier,p[0].x,p[0].y,p[1].x,p[1].y))
        for i,a in enumerate(rects):
            for b in rects[i+1:]:self.assertFalse(max(a[1],b[1])<min(a[3],b[3]) and max(a[2],b[2])<min(a[4],b[4]),(a,b))
        # Native list navigation changes selection/fields without mutating music.
        listing=self.control(4456);self.desktop.send(listing,0x201,1,8|(8<<16));self.desktop.send(listing,0x202,0,8|(8<<16));key=self.state()['selectedKey'];before=self.doc()
        self.desktop.send(listing,0x100,0x28);self.idle();self.assertEqual(self.state()['selectedKey'],key+1);self.assertEqual(self.doc(),before)
        self.desktop.send(listing,0x100,0x75);self.idle();self.assertEqual(self.desktop.focus(self.window()),self.window())

    @unittest.skipUnless(os.environ.get('SCREAMSEQ_TEST_LIVE_AUDIO')=='1','explicit short silent WASAPI qualification')
    def test_mapping_noop_keeps_audio_running_actual_change_stops(self):
        self.create();project=self.folder/'keymap-live.screamseq';self.write('document.save',path=str(project))
        self.pid=self.desktop.launch([os.environ['SCREAMSEQ_TEST_EXE'],'--audio-test-silent','--audio-test-allow-stop','--automation','--seconds','120','--project',str(project),'--vst3-test-cache',str(self.cache)])
        self.client=Client(r'\\.\pipe\ScreamSeq.Api.'+str(self.pid),timeout=20)
        for _ in range(100):
            try:
                if self.read('transport.get')['audioActive']:break
            except TransportError:pass
            time.sleep(.1)
        else:self.fail('silent keymap fixture did not start')
        self.start();self.pick(60);before=self.doc();slot=self.settings()['mapping'][60]
        self.press(4449);self.press(4450);self.assertEqual(self.doc(),before);playing=self.read('transport.get');self.assertTrue(playing['audioActive']);self.assertEqual(playing['overruns'],0);self.assertFalse(playing['fault'])
        self.select(4448,0 if slot else 1);self.press(4449);self.press(4450);after=self.read('transport.get');self.assertFalse(after['audioActive']);self.assertNotEqual(self.settings()['mapping'][60],slot)


if __name__=='__main__':unittest.main()
