"""Native absolute automation: retained drafts, bounded drawing and real audio."""
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


class AbsoluteAutomationUITests(unittest.TestCase):
    setUp=support.ParameterAutomationUITests.setUp
    doc=support.ParameterAutomationUITests.doc
    read=support.ParameterAutomationUITests.read
    write=support.ParameterAutomationUITests.write
    add_gain=support.ParameterAutomationUITests.add_gain
    render=support.ParameterAutomationUITests.render
    route_master=support.ParameterAutomationUITests.route_master
    def state(self):return self.read('workspace.get')['absoluteAutomation']
    def window(self,kind='ScreamSeq.AbsoluteAutomation'):return support.ParameterAutomationUITests.window(self,kind)
    def control(self,identifier,kind='ScreamSeq.AbsoluteAutomation'):return support.ParameterAutomationUITests.control(self,identifier,kind)
    def field(self,identifier,value,kind='ScreamSeq.AbsoluteAutomation'):return support.ParameterAutomationUITests.field(self,identifier,value,kind)
    def idle(self):
        end=time.monotonic()+12;quiet=None
        while time.monotonic()<end:
            state=self.read('workspace.get');local=state['absoluteAutomation']
            if state['documentBusy'] or local.get('pending') or state['parameterAutomation'].get('pending'):quiet=None
            elif quiet is None:quiet=time.monotonic()
            elif time.monotonic()-quiet>=.12:return
            time.sleep(.025)
        self.fail(str(local))
    def press(self,identifier,kind='ScreamSeq.AbsoluteAutomation'):
        self.idle();self.desktop.send(self.window(kind),0x111,identifier,self.control(identifier,kind));self.idle()
    def select(self,identifier,index):
        self.idle();self.desktop.send(self.control(identifier),0x186 if identifier==4603 else 0x14E,index);self.desktop.send(self.window(),0x111,identifier|(1<<16),self.control(identifier));self.idle()
    def start(self):
        self.idle();self.desktop.send(self.desktop.hwnd(self.pid),0x111,504);self.idle();self.assertTrue(self.state()['visible'])
    def saved(self):return [dict(frame=p['frame'],value=p['value']) for p in self.read('automation.get',limit=4096)['points'] if p['slot']==self.state()['slot'] and p['id']==self.state()['parameter']]
    def setup_curve(self):
        plugin=self.add_gain();self.start();self.select(4603,1);self.assertEqual(self.state()['parameter'],1);self.field(4608,0);self.field(4609,-18);self.press(4610);self.press(4611);self.field(4608,.5);self.field(4609,-3);self.press(4610);return plugin
    def mouse(self,message,x,y):return support.ParameterAutomationUITests.mouse(self,message,x,y)
    def key(self,key):self.desktop.send(self.window(),0x100,key);self.idle()

    def test_native_seconds_frames_history_noop_and_reopen(self):
        self.setup_curve();self.assertEqual(self.saved(),[]);self.press(4614);saved=self.saved();self.assertEqual(saved,[dict(frame=0,value=-18),dict(frame=24000,value=-3)]);before=self.doc();self.press(4614);self.assertEqual(self.doc(),before)
        self.select(4604,1);self.field(4608,24001);self.press(4610);self.press(4614);self.assertEqual(self.saved()[-1]['frame'],24001);self.press(4631);self.assertEqual(self.saved(),saved);self.press(4632);self.assertEqual(self.saved()[-1]['frame'],24001)
        self.field(4608,1.5);self.press(4610);self.assertTrue(self.state()['fieldDraft']);self.assertIn('whole frames',self.state()['status']);self.key(0x1B);self.field(4609,25);self.press(4610);self.assertTrue(self.state()['fieldDraft']);self.key(0x1B)
        path=self.folder/'native-absolute.screamseq';saved=self.saved();self.write('document.save',path=str(path));self.write('document.open',path=str(path),discard=True);self.press(4625);self.select(4603,1);self.assertEqual(self.saved(),saved);self.press(4613);self.assertTrue(self.state()['dirty']);self.press(4614);self.assertEqual(self.saved(),[])

    def test_stale_fields_target_removal_close_and_replacement_preserve_drafts(self):
        first=self.setup_curve();self.press(4614);self.add_gain();self.press(4615);self.field(4609,-6);self.select(4601,1);self.assertEqual(self.state()['plugin'],first);self.assertTrue(self.state()['fieldDraft']);self.press(4624);self.start();self.assertTrue(self.state()['fieldDraft']);self.press(4610);draft=self.state()['points']
        self.write('document.patch',title='Outside edit');before=self.doc();self.press(4614);self.assertEqual(self.doc(),before);self.assertTrue(self.state()['stale']);self.assertEqual(self.state()['points'],draft);self.press(4615);self.write('plugin.remove',slot=0);self.press(4615);self.assertEqual(self.state()['plugin'],first);self.assertIn('removed',self.state()['status']);self.press(4625);self.assertNotEqual(self.state()['plugin'],first)
        self.select(4603,1);self.field(4609,-6);self.press(4610);draft=self.state()['points'];path=self.folder/'replacement.screamseq';self.write('document.save',path=str(path));self.write('document.open',path=str(path),discard=True);self.press(4615);self.assertEqual(self.state()['points'],draft);self.assertTrue(self.state()['stale'])

    def test_last_touched_cross_view_and_pattern_conflict(self):
        plugin=self.setup_curve();self.press(4614);self.press(4618);pattern='ScreamSeq.ParameterAutomation';local=self.read('workspace.get')['parameterAutomation'];self.assertTrue(local['visible']);self.assertEqual((local['plugin'],local['parameter']),(plugin,1));self.press(4212,pattern);before=self.doc();self.press(4215,pattern);self.assertEqual(self.doc(),before);self.assertIn('absolute',self.read('workspace.get')['parameterAutomation']['status'].lower())
        self.press(4240,pattern);self.assertEqual(self.state()['plugin'],plugin);self.press(4617);self.assertEqual(self.read('workspace.get')['focus'],'plugins');self.write('plugin.parameters.set',slot=0,values=[dict(id=0,value=1)]);self.press(4616);self.assertEqual(self.state()['parameter'],0)

    def test_mouse_keyboard_zoom_and_native_controls_fit_minimum(self):
        self.setup_curve();p=self.state()['handles'][0];self.mouse(0x201,p['x'],p['y']);self.mouse(0x202,p['x'],p['y']);self.key(0x27);self.assertEqual(self.state()['points'][0]['frame'],480);before=self.state()['points'];p=self.state()['handles'][0];self.mouse(0x201,p['x'],p['y']);self.mouse(0x200,p['x']+20,p['y']+15);self.key(0x1B);self.assertEqual(self.state()['points'],before)
        x,y,w,h=self.state()['canvas'];self.mouse(0x203,x+w/2,y+h/2);self.assertTrue(any(p['frame']==4*48000 for p in self.state()['points']));self.key(0x2E);self.assertFalse(any(p['frame']==4*48000 for p in self.state()['points']));self.press(4621);self.assertLess(self.state()['endSeconds']-self.state()['startSeconds'],8);self.field(4627,2);self.field(4628,3);self.press(4629);self.assertEqual((self.state()['startSeconds'],self.state()['endSeconds']),(2,3))
        user=private_desktop.user;user.SetWindowPos.argtypes=[wintypes.HWND,wintypes.HWND,ctypes.c_int,ctypes.c_int,ctypes.c_int,ctypes.c_int,wintypes.UINT];user.GetClientRect.argtypes=[wintypes.HWND,ctypes.POINTER(wintypes.RECT)];user.GetWindowRect.argtypes=user.GetClientRect.argtypes;user.MapWindowPoints.argtypes=[wintypes.HWND,wintypes.HWND,ctypes.POINTER(wintypes.POINT),wintypes.UINT];scale=self.read('workspace.get')['dpi']/96;private_desktop.check(user.SetWindowPos(self.window(),None,0,0,int(1040*scale),int(700*scale),0x16));client=wintypes.RECT();user.GetClientRect(self.window(),ctypes.byref(client));rects=[]
        for identifier in range(4601,4633):
            rect=wintypes.RECT();user.GetWindowRect(self.control(identifier),ctypes.byref(rect));p=(wintypes.POINT*2)(wintypes.POINT(rect.left,rect.top),wintypes.POINT(rect.right,rect.bottom));user.MapWindowPoints(None,self.window(),p,2);self.assertGreaterEqual(p[0].x,0,identifier);self.assertGreaterEqual(p[0].y,0,identifier);self.assertLessEqual(p[1].x,client.right,identifier);self.assertLessEqual(p[1].y,client.bottom,identifier);rects.append((identifier,p[0].x,p[0].y,p[1].x,p[1].y))
        for i,a in enumerate(rects):
            for b in rects[i+1:]:self.assertFalse(max(a[1],b[1])<min(a[3],b[3]) and max(a[2],b[2])<min(a[4],b[4]),(a,b))

    def test_hundred_thousand_points_retain_data_and_bound_drawing(self):
        self.add_gain();self.write('automation.replaceLane',slot=0,id=1,points=[dict(frame=i*4800,value=(-18 if i%2 else -3)) for i in range(100000)]);self.start();self.select(4603,1);state=self.state();self.assertEqual(state['pointCount'],100000);self.assertTrue(state['pointsTruncated']);self.assertLessEqual(len(state['handles']),2049);self.assertLessEqual(state['drawSegments'],2*(int(state['canvas'][2])+2)+1)
        self.field(4605,99999);self.press(4626);self.assertEqual(self.state()['selectedPoint'],99999);self.field(4609,-9);self.press(4610);self.press(4614);self.assertEqual(self.read('automation.get',offset=99999)['points'][0],dict(slot=0,id=1,frame=99999*4800,value=-9));self.assertEqual(self.state()['pointCount'],100000)

    def audio_curve(self,descriptor,parameter,name,values):
        if descriptor:self.write('plugin.add',descriptor=descriptor);plugin=self.doc()['data']['nativePlugins'][0]['instanceID']
        else:plugin=self.add_gain()
        self.route_master(plugin);opaque=self.read('plugin.state.get',slot=0);self.start();catalog=self.read('plugin.parameters.get',slot=0);self.select(4603,next(i for i,p in enumerate(catalog) if p['id']==parameter));self.select(4604,1)
        for i,(frame,value) in enumerate(values):
            if i:self.press(4611)
            self.field(4608,frame);self.field(4609,value);self.press(4610)
        self.press(4614);saved=self.saved();self.assertEqual(len(saved),len(values));path=self.folder/(name+'.screamseq');report=self.folder/(name+'-pcm.json');self.write('document.save',path=str(path));self.write('document.open',path=str(path),discard=True);self.assertEqual(self.read('plugin.state.get',slot=0),opaque);self.assertEqual([dict(frame=p['frame'],value=p['value']) for p in self.read('automation.get')['points']],saved);active=self.render(path,report);scene=self.state()
        self.write('automation.replaceLane',slot=0,id=parameter,points=[]);disabled=self.folder/(name+'-disabled.screamseq');disabled_report=self.folder/(name+'-disabled-pcm.json');self.write('document.save',path=str(disabled));baseline=self.render(disabled,disabled_report);delta=max(abs(a-b) for x,y in zip(active['renders'],baseline['renders']) for a,b in zip(x['quarterSecondEnergy'],y['quarterSecondEnergy']));self.assertGreater(delta,.01)
        if os.environ.get('SCREAMSEQ_ABSOLUTE_EVIDENCE_DIR'):
            folder=Path(os.environ['SCREAMSEQ_ABSOLUTE_EVIDENCE_DIR']);folder.mkdir(parents=True,exist_ok=True)
            for file in [path,report,disabled,disabled_report]:shutil.copy2(file,folder/file.name)
            (folder/(name+'-scene.json')).write_text(json.dumps(dict(executableSHA256=hashlib.sha256(Path(os.environ['SCREAMSEQ_TEST_EXE']).read_bytes()).hexdigest(),workspace=scene,activeEnergy=active['energy'],disabledEnergy=baseline['energy'],maxQuarterEnergyDifference=delta,foregroundVisualQualified=False),indent=2),encoding='utf-8')

    def test_native_gain_absolute_lane_changes_audio_and_keeps_partitioning(self):self.audio_curve(None,1,'absolute-gainer',[(0,-18),(9600,-6),(24000,0)])

    @unittest.skipUnless(os.environ.get('SCREAMSEQ_TEST_LIVE_AUDIO')=='1','explicit short silent WASAPI qualification')
    def test_live_reads_noop_invalid_preserve_playback_and_native_apply_stops(self):
        plugin=self.setup_curve();self.press(4614);self.route_master(plugin);saved=[dict(frame=p['frame'],value=p['value']) for p in self.read('automation.get')['points']];path=self.folder/'live-absolute.screamseq';self.write('document.save',path=str(path));self.pid=self.desktop.launch([os.environ['SCREAMSEQ_TEST_EXE'],'--audio-test-silent','--audio-test-allow-stop','--automation','--seconds','120','--project',str(path),'--vst3-test-cache',str(self.cache)]);self.client=Client(r'\\.\pipe\ScreamSeq.Api.'+str(self.pid),timeout=20)
        for _ in range(100):
            try:
                if self.read('transport.get')['audioActive']:break
            except TransportError:pass
            time.sleep(.1)
        else:self.fail('silent automation app did not start')
        before=self.read('transport.get');self.read('automation.get');self.write('automation.replaceLane',slot=0,id=1,points=list(reversed(saved)))
        with self.assertRaises(ApiError):self.write('automation.replaceLane',slot=0,id=1,points=[saved[0],saved[0]])
        self.start();self.select(4603,1);self.press(4614);time.sleep(.1);playing=self.read('transport.get');self.assertTrue(playing['audioActive']);self.assertGreater(playing['frames'],before['frames']);self.assertFalse(playing['fault']);self.assertEqual(playing['overruns'],0)
        self.field(4609,-12);self.press(4610);self.press(4614);self.assertFalse(self.read('transport.get')['audioActive']);self.assertEqual(self.saved()[0]['value'],-12)
        if os.environ.get('SCREAMSEQ_ABSOLUTE_EVIDENCE_DIR'):
            folder=Path(os.environ['SCREAMSEQ_ABSOLUTE_EVIDENCE_DIR']);folder.mkdir(parents=True,exist_ok=True);(folder/'absolute-live.json').write_text(json.dumps(dict(before=before,playing=playing,after=self.read('transport.get'),workspace=self.state())),encoding='utf-8')

    @unittest.skipUnless(os.environ.get('SCREAMSEQ_TEST_PROVIDER_CACHE'),'private VST3 gain fixture')
    def test_vst3_absolute_lane_changes_audio_and_preserves_opaque_state(self):
        descriptor=next(p for p in self.read('plugin.discover',format='VST3') if p['classID']=='5245534F4E414E434546464543540001');self.audio_curve(descriptor,7,'absolute-vst3',[(0,.125),(9600,.75),(24000,.5)])


if __name__=='__main__':unittest.main()
