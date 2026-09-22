"""Captured sample settings and native import dialogs against the actual app."""
import ctypes
import hashlib
import json
import os
from pathlib import Path
import struct
import time
from types import SimpleNamespace
import unittest
import wave

import private_desktop
import test_editor_app as dialogs
import test_sample_detail_ui as detail
from client import Client, TransportError


class SampleSettingsUITests(unittest.TestCase):
    setUp=detail.SampleDetailUITests.setUp
    doc=detail.SampleDetailUITests.doc
    read=detail.SampleDetailUITests.read
    write=detail.SampleDetailUITests.write
    install=detail.SampleDetailUITests.install
    pcm=detail.SampleDetailUITests.pcm
    start=detail.SampleDetailUITests.start
    state=detail.SampleDetailUITests.state
    window=detail.SampleDetailUITests.window
    control=detail.SampleDetailUITests.control
    field=detail.SampleDetailUITests.field
    idle=detail.SampleDetailUITests.idle
    press=detail.SampleDetailUITests.press
    select=detail.SampleDetailUITests.select
    region=detail.SampleDetailUITests.region
    dialog=dialogs.EditorAppTests.dialog
    choose_dialog_path=dialogs.EditorAppTests.choose_dialog_path

    def text(self,identifier):
        value=ctypes.create_unicode_buffer(1024)
        self.desktop.send(self.control(identifier),0xD,1024,ctypes.addressof(value))
        return value.value
    def info(self,sample=1):return self.read('sample.get',sample=sample)
    def saved(self):return {key:self.info()[key] for key in ('name','rate','volume','pan')}
    def audio_file(self,name,values=(1200,-1200,2400,-2400),rate=44100):
        path=self.folder/name
        with wave.open(str(path),'wb') as output:
            output.setnchannels(1);output.setsampwidth(2);output.setframerate(rate)
            output.writeframes(struct.pack('<'+str(len(values))+'h',*values))
        return path
    def post(self,identifier,main=False):
        self.idle();user=private_desktop.user
        user.PostMessageW.argtypes=[ctypes.wintypes.HWND,ctypes.wintypes.UINT,ctypes.wintypes.WPARAM,ctypes.wintypes.LPARAM]
        private_desktop.check(user.PostMessageW(self.desktop.hwnd(self.pid) if main else self.window(),0x111,identifier,0))
    def choose(self,path,title=None):
        process=SimpleNamespace(pid=self.pid);dialog=self.dialog(process,title)
        self.choose_dialog_path(process,path)
        self.wait_dialog_close(dialog)
    def wait_dialog_close(self,dialog):
        user=private_desktop.user;user.IsWindow.argtypes=[ctypes.wintypes.HWND]
        for _ in range(150):
            if not user.IsWindow(dialog):self.idle();return
            time.sleep(.02)
        self.fail('sample chooser did not close')
    def cancel(self,title):
        dialog=self.dialog(SimpleNamespace(pid=self.pid),title)
        private_desktop.user.PostMessageW(dialog,0x111,2,0);self.wait_dialog_close(dialog)
    def reopen(self,name='sample-settings.screamseq'):
        path=self.folder/name;self.write('document.save',path=str(path));self.write('document.open',path=str(path),discard=True);self.press(4802)

    def test_settings_one_transaction_history_noop_and_native_reopen(self):
        before=self.saved();pcm=self.pcm();other=self.info(2);identity=self.state()['id']
        values=dict(name='Resonant sample',rate=32000,volume=37,pan=211)
        for identifier,value in zip(range(4856,4860),values.values()):self.field(identifier,value)
        original=self.doc();self.assertTrue(self.state()['fieldDraft']);self.assertEqual(self.doc(),original)
        self.press(4860);self.assertEqual(self.saved(),values);self.assertEqual(self.pcm(),pcm);self.assertEqual(self.info(2),other);self.assertEqual(self.state()['id'],identity)
        applied=self.doc();self.field(4858,37);self.press(4860);self.assertEqual(self.doc(),applied)
        self.press(4804);self.assertEqual(self.saved(),before);self.press(4805);self.assertEqual(self.saved(),values)
        self.reopen();self.assertEqual(self.saved(),values);self.assertEqual(self.pcm(),pcm)

    def test_invalid_stale_close_and_selection_preserve_settings_draft(self):
        for identifier,value in ((4857,99),(4857,192001),(4858,65),(4859,-1),(4858,'nan'),(4859,'1.5')):
            self.field(identifier,value);before=self.doc();self.press(4860);self.assertEqual(self.doc(),before);self.assertEqual(self.text(identifier),str(value));self.press(4861)
        self.field(4856,'Keep my draft');self.field(4858,25);self.press(4806);self.start();self.select(4801,1)
        self.assertEqual(self.state()['sample'],1);self.assertEqual(self.text(4856),'Keep my draft')
        self.write('document.patch',title='Unrelated edit');before=self.doc();self.press(4860);self.assertEqual(self.doc(),before);self.assertTrue(self.state()['stale']);self.assertEqual(self.text(4856),'Keep my draft')
        self.press(4803);self.assertFalse(self.state()['fieldDraft']);self.assertEqual(self.text(4856),self.info()['name'])
        self.field(4856,'Old document');self.reopen();self.assertEqual(self.text(4856),self.info()['name'])

    def test_settings_and_audio_drafts_do_not_silently_overwrite_each_other(self):
        self.field(4856,'Pending name');self.select(4828,5);before=self.pcm();self.press(4833)
        self.assertEqual(self.pcm(),before);self.assertEqual(self.text(4856),'Pending name');self.assertIn('settings',self.state()['status'])
        self.field(4815,8);self.field(4816,24);self.press(4860)
        self.assertEqual(self.info()['name'],'Pending name');self.assertEqual((self.text(4815),self.text(4816)),('8','24'));self.assertTrue(self.state()['fieldDraft'])
        self.press(4817);self.press(4833);self.assertNotEqual(self.pcm(),before);self.assertEqual(self.info()['name'],'Pending name')

    def test_replace_captured_sample_cancel_stale_guard_and_history(self):
        path=self.audio_file('Replacement 音.wav');before=self.info();pcm=self.pcm();other=self.info(2);identity=self.state()['id']
        self.post(4862);self.cancel('Replace captured sample');self.assertEqual(self.info(),before)
        self.post(4862);self.dialog(SimpleNamespace(pid=self.pid),'Replace captured sample');self.write('document.patch',title='Changed during chooser');doc=self.doc();self.choose(path)
        self.assertEqual(self.doc(),doc);self.assertEqual(self.info(),before);self.assertIn('Song changed',self.state()['status']);self.press(4803)
        main=self.desktop.hwnd(self.pid);listing=private_desktop.user.GetDlgItem(main,200);self.desktop.send(listing,0x186,1);self.desktop.send(main,0x111,200|(1<<16),listing)
        self.post(4862);self.choose(path,'Replace captured sample');self.assertEqual(self.pcm(),(1200,-1200,2400,-2400));self.assertEqual(self.state()['id'],identity);self.assertEqual(self.info(2),other)
        self.press(4804);self.assertEqual(self.pcm(),pcm);self.assertEqual(self.info(),before);self.press(4805);self.reopen('replacement.screamseq');self.assertEqual(self.pcm(),(1200,-1200,2400,-2400))

    def test_batch_import_samples_one_undo_and_corrupt_member_is_atomic(self):
        a=self.audio_file('First sample.wav');b=self.audio_file('Second 音.wav',(3000,-3000));before=self.doc();catalog=self.read('document.get')['samples']
        self.post(508,main=True);self.choose(f'"{a}" "{b}"','Import samples')
        after=self.read('document.get')['samples'];self.assertEqual(len(after),len(catalog)+2);indices=[s['index'] for s in after if s['id'] not in {s['id'] for s in catalog}]
        self.assertEqual({self.pcm(i) for i in indices},{(1200,-1200,2400,-2400),(3000,-3000)})
        self.write('history.undo',domain='document');self.assertEqual(self.read('document.get')['samples'],catalog);self.write('history.redo',domain='document');self.assertEqual(self.read('document.get')['samples'],after)
        corrupt=self.folder/'broken.wav';corrupt.write_bytes(b'not wave data');before=self.doc();self.post(508,main=True);self.choose(f'"{a}" "{corrupt}"','Import samples');self.assertEqual(self.doc(),before)
        self.post(508,main=True);self.dialog(SimpleNamespace(pid=self.pid),'Import samples');self.write('document.patch',title='Changed in batch chooser');before=self.doc();self.choose(a);self.assertEqual(self.doc(),before)
        self.post(508,main=True);self.cancel('Import samples');self.assertEqual(self.doc(),before)

    def test_import_as_instruments_selects_sound_and_create_from_captured_sample(self):
        path=self.audio_file('Mapped sound.wav');before=self.read('document.get');self.post(509,main=True);self.choose(path,'Import samples as instruments')
        after=self.read('document.get');self.assertTrue(after['instruments']);selected=self.read('workspace.get')['musicalTyping'];self.assertFalse(selected['sample'])
        instrument=self.read('instrument.get',instrument=selected['slot']);sample=self.read('workspace.get')['sampleEditor']['sample']
        self.assertTrue(all(slot==sample for slot in instrument['mapping']));self.assertEqual(self.pcm(sample),(1200,-1200,2400,-2400))
        self.write('history.undo',domain='document');self.assertEqual(self.read('document.get')['instruments'],before['instruments']);self.press(4803)
        self.press(4863);created=self.state()['report']['instrument'];mapping=self.read('instrument.get',instrument=created)['mapping'];self.assertTrue(all(slot==1 for slot in mapping))
        self.reopen('sample-instrument.screamseq');self.assertEqual(self.read('instrument.get',instrument=created)['mapping'],mapping)

    def test_settings_controls_fit_minimum_and_f6_leaves_main_sound_chooser(self):
        # Include every newly added interactive control in the established bounds check.
        user=private_desktop.user;w=ctypes.wintypes;user.GetClientRect.argtypes=[w.HWND,ctypes.POINTER(w.RECT)];user.GetWindowRect.argtypes=user.GetClientRect.argtypes;user.MapWindowPoints.argtypes=[w.HWND,w.HWND,ctypes.POINTER(w.POINT),w.UINT];user.SetWindowPos.argtypes=[w.HWND,w.HWND,ctypes.c_int,ctypes.c_int,ctypes.c_int,ctypes.c_int,w.UINT]
        scale=self.read('workspace.get')['dpi']/96;private_desktop.check(user.SetWindowPos(self.window(),None,0,0,int(1080*scale),int(790*scale),0x16));self.idle();client=w.RECT();user.GetClientRect(self.window(),ctypes.byref(client));rects=[]
        for identifier in range(4801,4864):
            rect=w.RECT();user.GetWindowRect(self.control(identifier),ctypes.byref(rect));p=(w.POINT*2)(w.POINT(rect.left,rect.top),w.POINT(rect.right,rect.bottom));user.MapWindowPoints(None,self.window(),p,2)
            self.assertGreaterEqual(p[0].x,0,identifier);self.assertGreaterEqual(p[0].y,0,identifier);self.assertLessEqual(p[1].x,client.right,identifier);self.assertLessEqual(p[1].y,client.bottom,identifier);rects.append((identifier,p[0].x,p[0].y,p[1].x,p[1].y))
        for i,a in enumerate(rects):
            for b in rects[i+1:]:self.assertFalse(max(a[1],b[1])<min(a[3],b[3]) and max(a[2],b[2])<min(a[4],b[4]),(a,b))
        self.press(4806);main=self.desktop.hwnd(self.pid);chooser=user.GetDlgItem(main,135);self.desktop.send(chooser,0x201,1,5|(5<<16));self.desktop.send(chooser,0x202,0,5|(5<<16));self.desktop.send(chooser,0x14F,0);self.assertEqual(self.desktop.focus(main),chooser)
        user.PostMessageW.argtypes=[w.HWND,w.UINT,w.WPARAM,w.LPARAM];private_desktop.check(user.PostMessageW(chooser,0x100,0x75,0));self.idle();self.assertEqual(self.desktop.focus(main),main)

    @unittest.skipUnless(os.environ.get('SCREAMSEQ_TEST_LIVE_AUDIO')=='1','explicit short silent WASAPI qualification')
    def test_settings_noop_and_invalid_keep_playing_real_change_stops(self):
        path=self.folder/'settings-live.screamseq';self.write('document.save',path=str(path))
        self.pid=self.desktop.launch([os.environ['SCREAMSEQ_TEST_EXE'],'--audio-test-silent','--audio-test-allow-stop','--automation','--seconds','120','--project',str(path),'--vst3-test-cache',str(self.cache)])
        self.client=Client(r'\\.\pipe\ScreamSeq.Api.'+str(self.pid),timeout=20)
        for _ in range(100):
            try:
                if self.read('transport.get')['audioActive']:break
            except TransportError:pass
            time.sleep(.1)
        else:self.fail('silent settings fixture did not start')
        self.start();before=self.read('transport.get');document=self.doc();volume=self.info()['volume']
        self.field(4858,volume);self.press(4860);self.assertEqual(self.doc(),document);self.field(4857,192001);self.press(4860);self.assertEqual(self.doc(),document)
        playing=self.read('transport.get');self.assertTrue(playing['audioActive']);self.assertGreater(playing['frames'],before['frames']);self.assertFalse(playing['fault']);self.assertEqual(playing['overruns'],0)
        self.press(4861);self.field(4858,32 if volume!=32 else 16);self.press(4860);after=self.read('transport.get');self.assertFalse(after['audioActive']);self.assertNotEqual(self.info()['volume'],volume)
        if os.environ.get('SCREAMSEQ_SAMPLE_EVIDENCE_DIR'):
            folder=Path(os.environ['SCREAMSEQ_SAMPLE_EVIDENCE_DIR']);folder.mkdir(parents=True,exist_ok=True)
            (folder/'sample-settings-live.json').write_text(json.dumps(dict(executableSHA256=hashlib.sha256(Path(os.environ['SCREAMSEQ_TEST_EXE']).read_bytes()).hexdigest(),before=before,playing=playing,after=after,workspace=self.state(),foregroundVisualQualified=False)),encoding='utf-8')


if __name__=='__main__':unittest.main()
