"""Detailed sample controls against the worker and shared PCM operations."""
import base64
import ctypes
from ctypes import wintypes
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import struct
import time
import unittest

import private_desktop
import test_parameter_automation_ui as support
from client import Client, TransportError


class SampleDetailUITests(unittest.TestCase):
    doc=support.ParameterAutomationUITests.doc
    read=support.ParameterAutomationUITests.read
    write=support.ParameterAutomationUITests.write
    render=support.ParameterAutomationUITests.render

    def setUp(self):
        support.ParameterAutomationUITests.setUp(self)
        self.raw=tuple(value for i in range(128) for value in (round(math.sin(i*.37)*16000+1000),round(math.cos(i*.23)*12000)))
        self.install(1,self.raw,2);self.install(2,self.raw,2)
        self.start()

    def install(self,sample,values,channels=1):
        self.write('sample.pcm.set',sample=sample,format='s16le',channels=channels,rate=48000,data=base64.b64encode(struct.pack('<'+str(len(values))+'h',*values)).decode())
    def pcm(self,sample=1):
        data=base64.b64decode(self.read('sample.pcm.get',sample=sample)['data']);return struct.unpack('<'+str(len(data)//2)+'h',data)
    def state(self):return self.read('workspace.get')['sampleDetail']
    def window(self,kind='ScreamSeq.SampleDetail'):return support.ParameterAutomationUITests.window(self,kind)
    def control(self,identifier,kind='ScreamSeq.SampleDetail'):return support.ParameterAutomationUITests.control(self,identifier,kind)
    def field(self,identifier,value,kind='ScreamSeq.SampleDetail'):return support.ParameterAutomationUITests.field(self,identifier,value,kind)
    def idle(self):
        end=time.monotonic()+12;quiet=None
        while time.monotonic()<end:
            state=self.read('workspace.get');local=state['sampleDetail']
            if state['documentBusy'] or local.get('pending'):quiet=None
            elif quiet is None:quiet=time.monotonic()
            elif time.monotonic()-quiet>.12:return
            time.sleep(.025)
        self.fail(str(local))
    def press(self,identifier):
        self.idle();self.desktop.send(self.window(),0x111,identifier,self.control(identifier));self.idle()
    def select(self,identifier,index):
        self.idle();self.desktop.send(self.control(identifier),0x14E,index);self.desktop.send(self.window(),0x111,identifier|(1<<16),self.control(identifier));self.idle()
    def start(self):
        self.desktop.send(self.desktop.hwnd(self.pid),0x111,505);self.idle();self.assertTrue(self.state()['visible'])
    def mouse(self,message,x,y):return support.ParameterAutomationUITests.mouse(self,message,x,y)
    def key(self,key):self.desktop.send(self.window(),0x100,key);self.idle()
    def region(self,first,last):self.field(4815,first);self.field(4816,last);self.press(4817)
    def view(self,first,last):self.field(4819,first);self.field(4820,last);self.press(4821)
    def stage(self,frame,value):self.field(4822,frame);self.field(4823,value);self.press(4824)

    def test_zoom_exact_waveform_cache_selection_identity_and_bounds(self):
        values=[int(16000*math.sin(i*.13)) for i in range(20000)];self.install(1,values);self.press(4803)
        self.assertFalse(self.state()['precise']);self.assertLessEqual(self.state()['waveBins'],4096);self.assertLessEqual(len(self.state()['peaks']),8192)
        self.region(400,432);self.press(4810);state=self.state();self.assertEqual((state['viewStart'],state['viewEnd']),(400,432));self.assertTrue(state['precise']);self.assertEqual(len(state['peaks']),64)
        for i,value in enumerate(values[400:432]):self.assertAlmostEqual(state['peaks'][i*2],value/32768,places=7)
        self.press(4812);self.assertEqual(self.state()['viewStart'],408);self.select(4801,1);self.assertEqual(self.state()['sample'],2);self.select(4801,0);self.assertEqual((self.state()['viewStart'],self.state()['viewEnd']),(408,440))
        self.field(4819,100);self.field(4820,99);before=self.doc();self.press(4821);self.assertEqual(self.doc(),before);self.assertIn('nonempty',self.state()['status']);self.press(4803);self.select(4813,2);self.assertEqual(self.state()['channels'],'both');self.assertIn('no right',self.state()['status'])
        self.view(19999,20000);self.press(4812);self.assertEqual(self.state()['viewEnd'],20000);self.press(4807);self.assertEqual((self.state()['viewStart'],self.state()['viewEnd']),(0,20000))

    def test_drawing_native_units_channels_interpolation_history_and_reopen(self):
        self.select(4813,2);self.stage(4,.5);self.stage(8,-.5);self.assertEqual(self.pcm(),self.raw);self.press(4825);actual=self.pcm();self.assertEqual(actual[::2],self.raw[::2]);self.assertEqual(actual[9:18:2],(16384,8192,0,-8192,-16384));self.assertEqual(self.state()['points'],[])
        before=self.doc();self.stage(4,.5);self.stage(8,-.5);self.press(4825);self.assertEqual(self.doc(),before);self.press(4804);self.assertEqual(self.pcm(),self.raw);self.press(4805);self.assertEqual(self.pcm(),actual)
        path=self.folder/'sample-drawing.screamseq';self.write('document.save',path=str(path));self.write('document.open',path=str(path),discard=True);self.press(4802);self.assertEqual(self.pcm(),actual)
        self.select(4813,1);self.select(4827,1);self.stage(10,.25);self.stage(13,-.25);self.press(4825);self.assertEqual(self.pcm()[20:28:2],(8192,8192,8192,-8192));self.assertEqual(self.pcm()[1::2],actual[1::2]);self.assertFalse(self.state()['fieldDraft'])

    def test_mouse_drawing_cancellation_and_stale_gesture_preserve_audio(self):
        self.view(0,32);self.press(4814);x,y,w,h=self.state()['canvas'];before=self.doc()
        self.mouse(0x201,x+w*.125,y+h*.25);self.mouse(0x200,x+w*.375,y+h*.75);self.key(0x1B);self.assertEqual(self.state()['points'],[]);self.assertEqual(self.doc(),before)
        self.mouse(0x201,x+w*.125,y+h*.25);self.mouse(0x200,x+w*.375,y+h*.75);self.mouse(0x202,x+w*.375,y+h*.75);self.assertGreaterEqual(len(self.state()['points']),8);self.press(4825);self.assertNotEqual(self.pcm(),self.raw)
        self.press(4804);self.assertEqual(self.pcm(),self.raw);self.mouse(0x201,x+w*.125,y+h*.25);self.write('document.patch',title='Concurrent edit');before=self.doc();self.mouse(0x200,x+w*.375,y+h*.75);self.idle();self.assertFalse(self.state()['dragging']);self.assertEqual(self.state()['points'],[]);self.assertEqual(self.doc(),before)

    def test_loop_crossfade_preview_modes_curves_history_and_native_storage(self):
        for mode in range(2):
            for curve in range(2):
                self.install(1,self.raw,2);self.install(2,self.raw,2);self.press(4803);self.region(16,112);self.press(4844);self.field(4837,8);self.select(4835,mode);self.select(4836,curve);before=self.doc();self.press(4838);preview=self.state()['report'];self.assertTrue(preview['dryRun']);self.assertEqual(self.doc(),before);self.assertGreater(preview['changedFrames'],0)
                self.press(4839);actual=self.pcm();self.assertNotEqual(actual,self.raw);info=self.read('sample.get',sample=1);self.assertEqual(info['loopStart'],24 if mode else 16);self.press(4804);self.assertEqual(self.pcm(),self.raw);self.assertEqual(self.read('sample.get',sample=1)['loopStart'],16);self.press(4805);self.assertEqual(self.pcm(),actual)
                self.write('sample.loops.set',sample=2,normal=dict(start=16,end=112,enabled=True));self.write('sample.crossfade',sample=2,loop='normal',mode='overlap' if mode else 'preserve',curve='equal-power' if curve else 'linear',frames=8);self.assertEqual(self.pcm(2),actual);self.press(4803)
        self.region(16,112);self.press(4845);self.select(4834,1);before=self.doc();self.press(4838);self.assertEqual(self.doc(),before);self.press(4839);self.assertEqual(self.read('sample.get',sample=1)['sustainStart'],24)
        path=self.folder/'sample-crossfade.screamseq';saved=self.pcm();info=self.read('sample.get',sample=1);self.write('document.save',path=str(path));self.write('document.open',path=str(path),discard=True);self.assertEqual(self.pcm(),saved);self.assertEqual(self.read('sample.get',sample=1),info);self.press(4802);self.field(4837,1);before=self.doc();self.press(4839);self.assertEqual(self.doc(),before)

    def test_every_processing_operation_matches_shared_api_and_preserves_other_channel(self):
        operations=['reverse','normalize','gain','fade-in','fade-out','invert','remove-dc','smooth','trim','silence','swap-channels','copy-left','copy-right','stereo-average']
        for index,name in enumerate(operations):
            with self.subTest(operation=name):
                self.install(1,self.raw,2);self.install(2,self.raw,2);self.press(4803);self.region(10,40);channel='both' if index==8 or index>=10 else 'right';self.select(4813,0 if channel=='both' else 2);self.select(4828,index);params=dict(sample=2,operation=name,start=10,end=40,channels=channel)
                if name=='normalize':self.field(4830,-6);params['targetDB']=-6
                if name=='gain':self.field(4830,3);params['gainDB']=3
                if name=='smooth':self.field(4830,5);params['window']=5
                if name.startswith('fade-'):self.select(4829,2);self.field(4831,2);params.update(curve='exponential',exponent=2)
                before=self.doc();self.press(4832);self.assertEqual(self.doc(),before);self.assertTrue(self.state()['report']['dryRun']);self.press(4833);actual=self.pcm();self.assertNotEqual(actual,self.raw);self.write('sample.process',**params);self.assertEqual(actual,self.pcm(2))
                if channel=='right':self.assertEqual(actual[::2],self.raw[::2])

    def test_snapping_normal_sustain_directions_and_private_clipboard_modes(self):
        self.region(11,53);self.select(4840,1);self.field(4842,8);before=self.doc();self.press(4843);self.assertEqual(self.doc(),before);self.assertEqual((self.state()['start'],self.state()['end']),(8,56));self.select(4848,2);self.press(4844);info=self.read('sample.get',sample=1);self.assertTrue(info['reverseLoop']);self.assertEqual((info['loopStart'],info['loopEnd']),(8,56))
        self.region(16,96);self.select(4848,1);self.press(4845);info=self.read('sample.get',sample=1);self.assertTrue(info['sustainPingpong']);self.press(4846);self.press(4847);info=self.read('sample.get',sample=1);self.assertFalse(info['loop']);self.assertFalse(info['sustainLoop'])
        self.press(4803);self.select(4840,0);self.field(4842,32);self.region(11,53);expected=self.read('sample.snap.get',sample=1,positions=[11,53],radius=32);self.press(4843);self.assertEqual([self.state()['start'],self.state()['end']],[v['after'] for v in expected['positions']])
        self.region(0,4);self.press(4849);self.assertEqual(self.pcm(),self.raw)
        for mode in range(4):
            self.region(8,12);self.select(4852,mode);self.press(4853);actual=self.pcm();self.assertNotEqual(actual,self.raw)
            if mode==0:self.assertEqual(actual,self.raw[:16]+self.raw[:8]+self.raw[16:])
            if mode in (1,3):self.assertEqual(actual,self.raw[:16]+self.raw[:8]+self.raw[24:])
            self.press(4804);self.assertEqual(self.pcm(),self.raw)
        self.region(4,12);self.press(4854);new=self.state()['report']['sample'];self.assertEqual(self.pcm(new),self.raw[8:24]);self.assertEqual(self.pcm(),self.raw)
        self.press(4850);self.assertEqual(self.pcm(),self.raw[:8]+self.raw[24:]);self.press(4804);self.assertEqual(self.pcm(),self.raw);self.press(4851);self.assertEqual(self.pcm(),self.raw[:8]+self.raw[24:]);self.press(4804);self.assertEqual(self.pcm(),self.raw)

    def test_captured_drafts_survive_close_selection_changes_and_document_replacement(self):
        self.stage(10,.25);draft=self.state()['points'];self.press(4806);self.start();self.assertEqual(self.state()['points'],draft);self.select(4801,1);self.assertEqual(self.state()['sample'],1);self.assertIn('captured draft',self.state()['status'])
        self.write('document.patch',title='External change');before=self.doc();self.press(4825);self.assertEqual(self.doc(),before);self.assertTrue(self.state()['stale']);self.assertEqual(self.state()['points'],draft);self.press(4803);self.assertEqual(self.state()['points'],[]);self.stage(12,.5)
        path=self.folder/'other.screamseq';self.write('document.save',path=str(path));self.write('document.open',path=str(path),discard=True);before=self.doc();draft=self.state()['points'];self.press(4803);self.assertEqual(self.state()['points'],draft);self.assertIn('Document replaced',self.state()['status']);self.press(4825);self.assertEqual(self.doc(),before);self.press(4802);self.assertFalse(self.state()['stale']);self.assertEqual(self.state()['points'],[])

    def test_history_removal_retains_identity_and_rejects_replacement_slot(self):
        created=self.write('sample.copyToNew',sample=1,start=0,end=8);self.press(4803);catalog=self.doc()['data']['samples'];self.select(4801,next(i for i,s in enumerate(catalog) if s['id']==created['id']));self.assertEqual(self.state()['id'],created['id'])
        self.press(4804);self.assertTrue(self.state()['stale']);self.assertEqual(self.state()['id'],created['id']);self.assertIn('removed by history',self.state()['status']);before=self.doc();self.stage(0,.5);self.assertEqual(self.doc(),before);self.press(4803);self.assertTrue(self.state()['stale']);self.assertEqual(self.state()['id'],created['id']);self.press(4802);self.assertFalse(self.state()['stale']);self.assertNotEqual(self.state()['id'],created['id'])

    def test_dense_gesture_bounds_and_atomic_point_limit(self):
        self.install(1,[0]*6000);self.press(4803);self.view(0,512);self.assertTrue(self.state()['precise']);self.assertEqual(self.state()['waveBins'],512);self.assertEqual(len(self.state()['peaks']),1024)
        self.press(4814);x,y,w,h=self.state()['canvas'];self.mouse(0x201,x,y+h*.25);self.mouse(0x200,x+w,y+h*.75);self.mouse(0x202,x+w,y+h*.75);self.assertEqual(len(self.state()['points']),512)
        # Windows constrains the tool to this desktop's maximum track width.
        # Fill the remaining exact points through the real numeric controls,
        # without assuming a 4096-logical-pixel display or relaxing the cap.
        window=self.window();frame_control=self.control(4822);stage_control=self.control(4824)
        for frame in range(512,4096):
            text=ctypes.create_unicode_buffer(str(frame));self.desktop.send(frame_control,0xC,0,ctypes.addressof(text));self.desktop.send(window,0x111,4824,stage_control)
        self.idle();points=self.state()['points'];self.assertEqual(len(points),4096);self.stage(5000,.5);self.assertEqual(self.state()['points'],points);self.assertIn('4096',self.state()['status']);self.press(4825);self.assertNotEqual(self.pcm()[:4096],(0,)*4096);self.assertEqual(self.pcm()[4096:],(0,)*1904);self.press(4804);self.assertEqual(self.pcm(),(0,)*6000)

    @unittest.skipUnless(os.environ.get('SCREAMSEQ_TEST_LIVE_AUDIO')=='1','explicit short silent WASAPI qualification')
    def test_live_waveform_preview_noop_rejection_and_edit_stop(self):
        path=self.folder/'sample-live.screamseq';self.write('document.save',path=str(path));self.pid=self.desktop.launch([os.environ['SCREAMSEQ_TEST_EXE'],'--audio-test-silent','--audio-test-allow-stop','--automation','--seconds','120','--project',str(path),'--vst3-test-cache',str(self.cache)]);self.client=Client(r'\\.\pipe\ScreamSeq.Api.'+str(self.pid),timeout=20)
        for _ in range(100):
            try:
                if self.read('transport.get')['audioActive']:break
            except TransportError:pass
            time.sleep(.1)
        else:self.fail('silent sample test did not start')
        before=self.read('transport.get');self.start();self.view(0,32);self.select(4828,2);self.field(4830,0);self.press(4832);self.press(4833);self.field(4830,99);self.press(4833);playing=self.read('transport.get');self.assertTrue(playing['audioActive']);self.assertGreater(playing['frames'],before['frames']);self.assertFalse(playing['fault']);self.assertEqual(playing['overruns'],0)
        self.field(4830,-6);self.press(4833);after=self.read('transport.get');self.assertFalse(after['audioActive']);self.assertNotEqual(self.pcm(),self.raw)
        if os.environ.get('SCREAMSEQ_SAMPLE_EVIDENCE_DIR'):
            folder=Path(os.environ['SCREAMSEQ_SAMPLE_EVIDENCE_DIR']);folder.mkdir(parents=True,exist_ok=True);(folder/'sample-live.json').write_text(json.dumps(dict(before=before,playing=playing,after=after,workspace=self.state())),encoding='utf-8')

    def test_native_controls_fit_minimum_without_overlap(self):
        user=private_desktop.user;user.SetWindowPos.argtypes=[wintypes.HWND,wintypes.HWND,ctypes.c_int,ctypes.c_int,ctypes.c_int,ctypes.c_int,wintypes.UINT];user.GetClientRect.argtypes=[wintypes.HWND,ctypes.POINTER(wintypes.RECT)];user.GetWindowRect.argtypes=user.GetClientRect.argtypes;user.MapWindowPoints.argtypes=[wintypes.HWND,wintypes.HWND,ctypes.POINTER(wintypes.POINT),wintypes.UINT];scale=self.read('workspace.get')['dpi']/96;private_desktop.check(user.SetWindowPos(self.window(),None,0,0,int(1080*scale),int(790*scale),0x16));self.idle();client=wintypes.RECT();user.GetClientRect(self.window(),ctypes.byref(client));rects=[]
        for identifier in range(4801,4855):
            rect=wintypes.RECT();user.GetWindowRect(self.control(identifier),ctypes.byref(rect));p=(wintypes.POINT*2)(wintypes.POINT(rect.left,rect.top),wintypes.POINT(rect.right,rect.bottom));user.MapWindowPoints(None,self.window(),p,2);self.assertGreaterEqual(p[0].x,0,identifier);self.assertGreaterEqual(p[0].y,0,identifier);self.assertLessEqual(p[1].x,client.right,identifier);self.assertLessEqual(p[1].y,client.bottom,identifier);rects.append((identifier,p[0].x,p[0].y,p[1].x,p[1].y))
        for i,a in enumerate(rects):
            for b in rects[i+1:]:self.assertFalse(max(a[1],b[1])<min(a[3],b[3]) and max(a[2],b[2])<min(a[4],b[4]),(a,b))

    def test_native_sample_process_changes_rendered_audio_across_rates_and_blocks(self):
        values=[round(math.sin(i*2*math.pi/128)*16000) for i in range(4096)];self.install(1,values);self.press(4803);self.region(0,4096);self.press(4844);baseline_path=self.folder/'sample-before.screamseq';baseline_report=self.folder/'sample-before-pcm.json';self.write('document.save',path=str(baseline_path));baseline=self.render(baseline_path,baseline_report)
        self.press(4803);self.select(4828,2);self.field(4830,-6);self.press(4833);path=self.folder/'sample-gain.screamseq';report=self.folder/'sample-gain-pcm.json';saved=self.pcm();self.write('document.save',path=str(path));self.write('document.open',path=str(path),discard=True);self.assertEqual(self.pcm(),saved);active=self.render(path,report);delta=max(abs(a-b) for x,y in zip(active['renders'],baseline['renders']) for a,b in zip(x['quarterSecondEnergy'],y['quarterSecondEnergy']));self.assertGreater(delta,.01)
        if os.environ.get('SCREAMSEQ_SAMPLE_EVIDENCE_DIR'):
            folder=Path(os.environ['SCREAMSEQ_SAMPLE_EVIDENCE_DIR']);folder.mkdir(parents=True,exist_ok=True)
            for file in (path,report,baseline_path,baseline_report):shutil.copy2(file,folder/file.name)
            (folder/'sample-scene.json').write_text(json.dumps(dict(executableSHA256=hashlib.sha256(Path(os.environ['SCREAMSEQ_TEST_EXE']).read_bytes()).hexdigest(),activeEnergy=active['energy'],baselineEnergy=baseline['energy'],maxQuarterEnergyDifference=delta,workspace=self.state(),foregroundVisualQualified=False)),encoding='utf-8')


if __name__=='__main__':unittest.main()
