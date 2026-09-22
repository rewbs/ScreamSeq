"""Instrument settings/envelopes through native controls and the exact PID pipe."""
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import shutil
import time
import unittest
import private_desktop
import test_parameter_automation_ui as support
from client import ApiError, Client, TransportError


class InstrumentEnvelopeUITests(unittest.TestCase):
    setUp=support.ParameterAutomationUITests.setUp
    doc=support.ParameterAutomationUITests.doc
    read=support.ParameterAutomationUITests.read
    write=support.ParameterAutomationUITests.write
    render=support.ParameterAutomationUITests.render
    def state(self):return self.read('workspace.get')['instrumentEnvelope']
    def window(self,kind='ScreamSeq.InstrumentEnvelope'):return support.ParameterAutomationUITests.window(self,kind)
    def control(self,identifier,kind='ScreamSeq.InstrumentEnvelope'):return support.ParameterAutomationUITests.control(self,identifier,kind)
    def field(self,identifier,value,kind='ScreamSeq.InstrumentEnvelope'):return support.ParameterAutomationUITests.field(self,identifier,value,kind)
    def idle(self):
        end=time.monotonic()+10;quiet=None
        while time.monotonic()<end:
            state=self.read('workspace.get');local=state['instrumentEnvelope']
            if state['documentBusy'] or local.get('pending') or local.get('envelopeBank',{}).get('pending'):quiet=None
            elif quiet is None:quiet=time.monotonic()
            elif time.monotonic()-quiet>=.15:return
            time.sleep(.025)
        self.fail(str(local))
    def press(self,identifier,kind='ScreamSeq.InstrumentEnvelope'):
        self.idle();self.desktop.send(self.window(kind),0x111,identifier,self.control(identifier,kind));self.idle()
    def select(self,identifier,index):
        self.idle();self.desktop.send(self.control(identifier),0x14E,index);self.desktop.send(self.window(),0x111,identifier|(1<<16),self.control(identifier));self.idle()
    def start(self):
        self.idle();hwnd=self.desktop.hwnd(self.pid);self.desktop.send(hwnd,0x111,503,private_desktop.user.GetDlgItem(hwnd,503));self.idle();self.assertTrue(self.state()['visible'])
    def create(self):
        self.start();self.assertFalse(self.state()['instrument']);self.press(4403);self.assertTrue(self.state()['instrument']);return self.state()['index']
    def saved(self,kind=None):return self.read('instrument.envelope.get',instrument=self.state()['instrument'],envelope=kind or self.state()['kind'])
    def settings(self):return self.read('instrument.get',instrument=self.state()['index'])
    def mouse(self,message,x,y):return support.ParameterAutomationUITests.mouse(self,message,x,y)
    def key(self,key):self.desktop.send(self.window(),0x100,key);self.idle()
    def preset(self):self.create();self.press(4410);self.press(4450);self.assertEqual(len(self.saved()['points']),5)

    def test_api_carry_release_atomic_validation_noop_history_and_reopen(self):
        self.create();index=self.state()['index'];other=self.saved('pan');baseline=self.settings()
        values=dict(envelope=0,points=[[0,0],[2,64],[12,48],[32,48],[48,0]],enabled=True,carry=True,releaseNode=3,sustain=True,sustainPoint=1,sustainEnd=2,loop=True,loopStart=0,loopEnd=3)
        self.write('instrument.patch',instrument=index,values=values);saved=self.settings();self.assertEqual(self.saved('pan'),other);self.assertEqual(saved['envelopes'][0]['releaseNode'],3);self.assertTrue(saved['envelopes'][0]['carry'])
        before=self.doc();self.write('instrument.patch',instrument=index,values=values);self.assertEqual(self.doc(),before)
        for invalid in [dict(carry=1),dict(carry='yes'),dict(releaseNode=True),dict(releaseNode=-1),dict(releaseNode=256),dict(releaseNode=5),dict(releaseNode=2.5),dict(points=[],releaseNode=0),dict(name='Must roll back',releaseNode=99)]:
            with self.assertRaises(ApiError):self.write('instrument.patch',instrument=index,values=invalid)
            self.assertEqual(self.doc(),before);self.assertEqual(self.settings(),saved)
        self.write('history.undo',domain='document');self.assertEqual(self.settings(),baseline);self.write('history.redo',domain='document');self.assertEqual(self.settings(),saved)
        path=self.folder/'instrument-api.screamseq';self.write('document.save',path=str(path));self.write('document.open',path=str(path),discard=True);self.assertEqual(self.settings(),saved)
        self.write('instrument.patch',instrument=index,values=dict(carry=False,releaseNode=255));self.assertEqual(self.settings()['envelopes'][0]['releaseNode'],255);self.assertFalse(self.settings()['envelopes'][0]['carry'])

    def test_native_markers_settings_keymap_one_undo_and_all_envelope_kinds(self):
        self.create();before=self.settings();pan=before['envelopes'][1];pitch=before['envelopes'][2];self.press(4410)
        for identifier,value in [(4418,1),(4419,3),(4420,2),(4421,3),(4422,4)]:self.field(identifier,value)
        self.press(4423)
        for identifier in [4406,4407,4408]:self.press(identifier)
        self.field(4439,'Soft attack');self.field(4440,48);self.field(4441,190);self.field(4442,900);self.select(4443,1);self.select(4444,2);self.select(4445,2)
        self.field(4446,60);self.field(4447,71);self.select(4448,2);self.press(4449);self.assertEqual(self.settings(),before);self.press(4450)
        saved=self.settings();env=saved['envelopes'][0];self.assertEqual((env['loopStart'],env['loopEnd'],env['sustainPoint'],env['sustainEnd'],env['releaseNode']),(1,3,2,3,4));self.assertTrue(env['carry'] and env['loop'] and env['sustain']);self.assertEqual(saved['mapping'][60:72],[2]*12);self.assertEqual(saved['mapping'][:60],before['mapping'][:60]);self.assertEqual((saved['name'],saved['volume'],saved['pan'],saved['fadeout'],saved['nna'],saved['dct'],saved['dna']),('Soft attack',48,190,900,1,2,2));self.assertEqual(saved['envelopes'][1],pan);self.assertEqual(saved['envelopes'][2],pitch)
        self.write('history.undo',domain='document');self.assertEqual(self.settings(),before);self.write('history.redo',domain='document');self.assertEqual(self.settings(),saved)
        self.press(4451);self.select(4402,1);self.press(4410);self.press(4450);self.assertTrue(self.saved()['enabled']);self.assertEqual(self.settings()['envelopes'][0],env)
        self.select(4402,2);self.press(4410);self.press(4409);self.press(4450);self.assertTrue(self.saved()['filter']);path=self.folder/'all-envelopes.screamseq';saved=self.settings();self.write('document.save',path=str(path));self.write('document.open',path=str(path),discard=True);self.press(4452);self.assertEqual(self.settings(),saved)

    def test_pending_fields_stale_target_close_and_document_replacement(self):
        self.preset();first=self.state()['instrument'];self.field(4414,35);self.select(4402,1);self.assertEqual(self.state()['kind'],'volume');self.assertTrue(self.state()['fieldDraft']);self.press(4453);self.start();self.assertTrue(self.state()['fieldDraft'])
        self.field(4418,1);before=self.state()['envelope'];self.press(4415);self.assertEqual(self.state()['envelope'],before);self.assertTrue(self.state()['fieldDraft']);self.key(0x1B);self.assertFalse(self.state()['fieldDraft'])
        self.field(4414,35);self.press(4415);draft=self.state()['envelope'];self.write('document.patch',title='External edit');before=self.doc();self.press(4450);self.assertEqual(self.doc(),before);self.assertTrue(self.state()['stale']);self.assertEqual(self.state()['envelope'],draft)
        self.press(4451);self.assertFalse(self.state()['dirty']);self.select(4401,1);self.assertNotEqual(self.state()['instrument'],first)
        self.press(4410);draft=self.state()['envelope'];path=self.folder/'other-document.screamseq';self.write('document.save',path=str(path));self.write('document.open',path=str(path),discard=True);self.press(4451);self.assertEqual(self.state()['envelope'],draft);self.assertTrue(self.state()['stale']);self.press(4452);self.assertFalse(self.state()['stale'])

    def test_every_native_tool_preview_copy_paste_markers_and_seed(self):
        self.preset();original=self.saved()
        for tool,fields in [(0,{}),(1,{}),(2,{4431:49}),(3,{4432:1.5}),(4,{}),(5,{}),(6,{4434:4294967295})]:
            self.select(4429,tool)
            for identifier,value in fields.items():self.field(identifier,value)
            if tool==2:self.field(4430,2)
            before=self.doc();self.press(4438);self.assertEqual(self.doc(),before);self.assertTrue(self.state()['dirty'],self.state()['status']);preview=self.state()['envelope'];self.press(4450);self.assertEqual(self.saved()['points'],preview['points']);self.write('history.undo',domain='document');self.press(4451);self.assertEqual(self.saved(),original)
        self.field(4430,0);self.field(4431,3);self.press(4437)
        for operation in [7,8]:
            self.select(4429,operation);self.field(4430,16);self.field(4432,2);self.press(4438);self.assertTrue(self.state()['dirty'],self.state()['status']);self.assertTrue(any(p[0]==16 for p in self.state()['envelope']['points']));self.press(4451)

    def test_mouse_keyboard_insert_delete_zoom_and_geometry_at_minimum(self):
        self.preset();edge=self.state()['handles'][0];self.mouse(0x201,edge['x'],edge['y']);self.mouse(0x202,edge['x'],edge['y']);self.key(0x26);self.assertEqual(self.state()['envelope']['points'][0],[0,1]);self.key(0x28);p=self.state()['handles'][1];self.mouse(0x201,p['x'],p['y']);self.mouse(0x202,p['x'],p['y']);self.key(0x27);self.assertEqual(self.state()['envelope']['points'][1],[3,64]);self.key(0x28);self.assertEqual(self.state()['envelope']['points'][1],[3,63]);before=self.state()['envelope'];p=self.state()['handles'][1];self.mouse(0x201,p['x'],p['y']);self.mouse(0x200,p['x']+20,p['y']+20);self.key(0x1B);self.assertEqual(self.state()['envelope'],before)
        x,y,w,h=self.state()['canvas'];self.mouse(0x203,x+w*.375,y+h*.75);self.assertIn([24,16],self.state()['envelope']['points']);self.key(0x2E);self.assertNotIn([24,16],self.state()['envelope']['points']);self.press(4426);self.assertLess(self.state()['end']-self.state()['start'],64);self.press(4428);self.assertGreater(self.state()['start'],0);self.press(4424)
        self.select(4429,5);user=private_desktop.user;user.SetWindowPos.argtypes=[wintypes.HWND,wintypes.HWND,ctypes.c_int,ctypes.c_int,ctypes.c_int,ctypes.c_int,wintypes.UINT];user.GetClientRect.argtypes=[wintypes.HWND,ctypes.POINTER(wintypes.RECT)];user.GetWindowRect.argtypes=user.GetClientRect.argtypes;user.MapWindowPoints.argtypes=[wintypes.HWND,wintypes.HWND,ctypes.POINTER(wintypes.POINT),wintypes.UINT]
        scale=self.read('workspace.get')['dpi']/96;private_desktop.check(user.SetWindowPos(self.window(),None,0,0,int(1100*scale),int(820*scale),0x16));client=wintypes.RECT();user.GetClientRect(self.window(),ctypes.byref(client));rects=[]
        for identifier in range(4401,4455):
            rect=wintypes.RECT();user.GetWindowRect(self.control(identifier),ctypes.byref(rect));p=(wintypes.POINT*2)(wintypes.POINT(rect.left,rect.top),wintypes.POINT(rect.right,rect.bottom));user.MapWindowPoints(None,self.window(),p,2);self.assertGreaterEqual(p[0].x,0,identifier);self.assertGreaterEqual(p[0].y,0,identifier);self.assertLessEqual(p[1].x,client.right,identifier);self.assertLessEqual(p[1].y,client.bottom,identifier);rects.append((identifier,p[0].x,p[0].y,p[1].x,p[1].y))
        for i,a in enumerate(rects):
            for b in rects[i+1:]:self.assertFalse(max(a[1],b[1])<min(a[3],b[3]) and max(a[2],b[2])<min(a[4],b[4]),(a,b))

    def test_bank_linked_master_and_stale_parent_draft(self):
        self.preset();self.press(4408);self.field(4422,3);self.press(4423);self.press(4450);before=self.saved();self.press(4404);bank='ScreamSeq.EnvelopeBank';self.field(1004,'Instrument attack',bank);self.press(1005,bank);shape=self.read('envelope.bank.list')['entries'][0]['shape'];self.assertTrue(shape['flags']&8);self.assertEqual(shape['markers'][4],32*256)
        self.press(1008,bank);self.assertTrue(self.state()['envelopeBank']['sourceCurrent']);self.field(4414,25);self.press(4415);doc=self.doc();self.press(4450);self.assertEqual(self.doc(),doc);self.assertIn('linked',self.state()['status'].lower());self.press(4451);self.press(4404);self.press(1009,bank);self.assertFalse(self.read('envelope.bank.list')['links']);self.assertEqual(self.saved()['points'],before['points'])
        self.press(4404);self.field(4414,20);self.press(4415);draft=self.state()['envelope'];doc=self.doc();self.press(1007,bank);self.assertEqual(self.doc(),doc);self.assertEqual(self.state()['envelope'],draft)

    def test_saved_native_envelope_changes_audio_with_partition_invariance(self):
        self.preset();path=self.folder/'native-instrument-envelope.screamseq';report=self.folder/'native-instrument-envelope-pcm.json';self.write('document.save',path=str(path));active=self.render(path,report);scene=self.state();self.press(4405);self.press(4450);disabled=self.folder/'native-instrument-disabled.screamseq';disabled_report=self.folder/'native-instrument-disabled-pcm.json';self.write('document.save',path=str(disabled));baseline=self.render(disabled,disabled_report)
        delta=max(abs(a-b) for x,y in zip(active['renders'],baseline['renders']) for a,b in zip(x['quarterSecondEnergy'],y['quarterSecondEnergy']));self.assertGreater(delta,.01)
        if os.environ.get('SCREAMSEQ_INSTRUMENT_EVIDENCE_DIR'):
            folder=Path(os.environ['SCREAMSEQ_INSTRUMENT_EVIDENCE_DIR']);folder.mkdir(parents=True,exist_ok=True)
            for file in [path,report,disabled,disabled_report]:shutil.copy2(file,folder/file.name)
            (folder/'native-instrument-envelope-scene.json').write_text(json.dumps(dict(executableSHA256=hashlib.sha256(Path(os.environ['SCREAMSEQ_TEST_EXE']).read_bytes()).hexdigest(),workspace=scene,privateDesktop=True,foregroundVisualQualified=False,activeEnergy=active['energy'],disabledEnergy=baseline['energy'],maxQuarterEnergyDifference=delta),indent=2),encoding='utf-8')

    @unittest.skipUnless(os.environ.get('SCREAMSEQ_TEST_LIVE_AUDIO')=='1','explicit short silent WASAPI qualification')
    def test_live_envelope_voice_markers_clear_on_stop(self):
        self.preset();path=self.folder/'envelope-voices.screamseq';self.write('document.save',path=str(path));self.pid=self.desktop.launch([os.environ['SCREAMSEQ_TEST_EXE'],'--audio-test-silent','--audio-test-allow-stop','--automation','--seconds','120','--project',str(path),'--vst3-test-cache',str(self.cache)]);self.client=Client(r'\\.\pipe\ScreamSeq.Api.'+str(self.pid),timeout=20)
        for _ in range(100):
            try:
                if self.read('transport.get')['audioActive']:break
            except TransportError:pass
            time.sleep(.1)
        else:self.fail('silent hardware test did not start')
        self.start();observed=[]
        for _ in range(120):
            state=self.state()
            if state['playbackTicks']:observed.append(state['playbackTicks'])
            if len({tuple(x) for x in observed})>=3:break
            time.sleep(.025)
        self.assertGreaterEqual(len({tuple(x) for x in observed}),3,'Instrument envelope voice positions must advance in the retained window');self.write('transport.stop');self.idle();self.assertEqual(self.state()['playbackTicks'],[])
        if os.environ.get('SCREAMSEQ_INSTRUMENT_EVIDENCE_DIR'):
            folder=Path(os.environ['SCREAMSEQ_INSTRUMENT_EVIDENCE_DIR']);folder.mkdir(parents=True,exist_ok=True);(folder/'native-instrument-live-markers.json').write_text(json.dumps(dict(observed=observed,stopped=self.state(),transport=self.read('transport.get'))),encoding='utf-8')


if __name__=='__main__':unittest.main()
