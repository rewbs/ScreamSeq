"""Native parameter curves, tool drafts and connected reusable editors."""
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
import unittest
import private_desktop
import test_parameter_automation as api
from client import Client, TransportError


class ParameterAutomationUITests(unittest.TestCase):
    def setUp(self):
        self.folder=Path(self.enterContext(tempfile.TemporaryDirectory(prefix='parameter-ui-',dir=os.environ['TMPDIR'])))
        self.desktop=self.enterContext(private_desktop.PrivateDesktop());self.cache=self.folder/'plugins.json';modules={}
        for key in ('SCREAMSEQ_TEST_PROVIDER_CACHE','SCREAMSEQ_TEST_PLUGIN_CACHE'):
            if os.environ.get(key):
                for item in json.loads(Path(os.environ[key]).read_text(encoding='utf-8-sig')):modules[item['path']]=item
        self.cache.write_text(json.dumps(list(modules.values())))
        self.pid=self.desktop.launch([os.environ['SCREAMSEQ_TEST_EXE'],'--inspection','--automation','--seconds','120','--vst3-test-cache',str(self.cache),'--envelope-test-catalogue',str(self.folder/'catalogue.json')])
        self.client=Client(r'\\.\pipe\ScreamSeq.Api.'+str(self.pid),timeout=20)
        for _ in range(100):
            try:self.doc();return
            except TransportError:time.sleep(.1)
        self.fail('parameter editor app did not start')
    doc=api.ParameterAutomationTests.doc
    read=api.ParameterAutomationTests.read
    write=api.ParameterAutomationTests.write
    add_gain=api.ParameterAutomationTests.add_gain
    def state(self):return self.read('workspace.get')['parameterAutomation']
    def window(self,kind='ScreamSeq.ParameterAutomation'):
        found=[]
        @private_desktop.callback
        def visit(hwnd,_):
            pid=wintypes.DWORD();private_desktop.user.GetWindowThreadProcessId(hwnd,ctypes.byref(pid));name=ctypes.create_unicode_buffer(100);private_desktop.user.GetClassNameW(hwnd,name,100)
            if pid.value==self.pid and name.value==kind:found.append(hwnd)
            return True
        private_desktop.check(private_desktop.user.EnumDesktopWindows(self.desktop.desktop,visit,0));self.assertEqual(len(found),1,found);return found[0]
    def control(self,identifier,kind='ScreamSeq.ParameterAutomation'):
        value=private_desktop.user.GetDlgItem(self.window(kind),identifier);self.assertTrue(value);return value
    def idle(self):
        end=time.monotonic()+10;quiet=None
        while time.monotonic()<end:
            state=self.read('workspace.get');local=state['parameterAutomation']
            if state['documentBusy'] or local.get('pending') or local.get('envelopeBank',{}).get('pending') or local.get('formulaWorkbench',{}).get('pending'):quiet=None
            elif quiet is None:quiet=time.monotonic()
            elif time.monotonic()-quiet>=.2:return
            time.sleep(.025)
        self.fail(str(local))
    def press(self,identifier,kind='ScreamSeq.ParameterAutomation'):
        self.idle();self.desktop.send(self.window(kind),0x111,identifier,self.control(identifier,kind));self.idle()
    def field(self,identifier,value,kind='ScreamSeq.ParameterAutomation'):
        text=ctypes.create_unicode_buffer(str(value));self.desktop.send(self.control(identifier,kind),0xC,0,ctypes.addressof(text))
    def select(self,identifier,index):
        self.idle();self.desktop.send(self.control(identifier),0x186 if identifier==4204 else 0x14E,index);self.desktop.send(self.window(),0x111,identifier|(1<<16),self.control(identifier));self.idle()
    def start(self):
        self.idle();hwnd=self.desktop.hwnd(self.pid);self.desktop.send(hwnd,0x111,502,private_desktop.user.GetDlgItem(hwnd,502));self.idle();self.assertTrue(self.state()['visible'])
    def saved(self,pattern=0):return self.read('automation.pattern.get',pattern=pattern)['lanes']
    def mouse(self,message,x,y):
        scale=self.read('workspace.get')['dpi']/96;self.desktop.send(self.window(),message,1 if message!=0x202 else 0,(int(x*scale)&65535)|((int(y*scale)&65535)<<16))
    def key(self,key):self.desktop.send(self.window(),0x100,key);self.idle()
    def setup_curve(self):plugin=self.add_gain();self.start();self.select(4204,1);self.assertEqual(self.state()['parameter'],1);self.press(4212);return plugin
    def route_master(self,plugin):
        self.write('mixer.enable');master=self.read('mixer.get')['buses'][-1]['id'];self.write('mixer.bus.set',bus=master,inserts=[plugin])
    def render(self,path,report):
        result=subprocess.run([os.environ['SCREAMSEQ_TEST_EXE'],'--offline-hosted-test','--project',str(path),'--vst3-test-cache',str(self.cache),'--report',str(report)],timeout=90)
        self.assertEqual(result.returncode,0,report.read_text() if report.exists() else 'no report');evidence=json.loads(report.read_text());self.assertTrue(evidence['finite']);self.assertTrue(evidence['documentUnchanged']);self.assertGreater(evidence['energy'],1);self.assertLess(evidence['maxPartitionDelta'],1e-6);return evidence
    def compare_without_curve(self,lane,active,name):
        self.write('automation.pattern.set',pattern=0,plugin=lane['plugin'],parameter=lane['parameter'],points=lane['points'],enabled=False)
        path=self.folder/(name+'-disabled.screamseq');report=self.folder/(name+'-disabled-pcm.json');self.write('document.save',path=str(path));baseline=self.render(path,report)
        difference=max(abs(a-b) for first,second in zip(active['renders'],baseline['renders']) for a,b in zip(first['quarterSecondEnergy'],second['quarterSecondEnergy']))
        self.assertGreater(difference,.01,'Saved automation must alter the rendered audio, not just metadata')
        if os.environ.get('SCREAMSEQ_PARAMETER_EVIDENCE_DIR'):
            folder=Path(os.environ['SCREAMSEQ_PARAMETER_EVIDENCE_DIR']);folder.mkdir(parents=True,exist_ok=True)
            for file in [path,report]:shutil.copy2(file,folder/file.name)
            (folder/(name+'-comparison.json')).write_text(json.dumps(dict(maxQuarterEnergyDifference=difference,activeEnergy=active['energy'],disabledEnergy=baseline['energy'])),encoding='utf-8')

    def test_native_points_guards_verify_history_and_reopen(self):
        plugin=self.setup_curve();self.field(4207,.25);self.field(4208,25);self.select(4205,2);self.press(4210);self.assertEqual(self.state()['points'][0],dict(position=64,value=.25,curve='smooth'))
        before=self.doc();self.press(4216);self.assertEqual(self.doc(),before);self.assertTrue(self.state()['dirty']);self.press(4215);saved=self.saved();self.assertEqual(saved[0]['plugin'],plugin)
        self.field(4207,'1e100');self.press(4210);self.assertEqual(self.saved(),saved);self.assertTrue(self.state()['fieldDraft']);self.key(0x1B)
        self.field(4207,63.99609375);self.press(4210);self.assertIn('occupies',self.state()['status']);self.key(0x1B)
        self.press(4214);self.press(4215);self.assertFalse(self.saved()[0]['enabled']);self.write('history.undo',domain='document');self.assertEqual(self.saved(),saved);self.write('history.redo',domain='document');self.assertFalse(self.saved()[0]['enabled'])
        path=self.folder/'native-parameters.screamseq';self.write('document.save',path=str(path));self.write('document.open',path=str(path),discard=True);self.press(4219);self.select(4204,1);self.assertEqual(self.state()['points'],saved[0]['points']);self.assertFalse(self.state()['enabled']);self.press(4217);self.assertEqual(self.saved(),[])

    def test_search_target_stale_close_and_independent_cursor(self):
        first=self.add_gain();second=self.add_gain();self.start();self.press(4212);draft=self.state()['points'];self.select(4202,1);self.assertEqual(self.state()['plugin'],first);self.assertEqual(self.state()['points'],draft)
        self.field(4203,'nonexistent parameter');self.assertEqual(self.state()['filteredCount'],0);self.assertEqual(self.state()['points'],draft);self.field(4203,'');self.press(4239);self.start();self.assertTrue(self.state()['dirty']);self.assertEqual(self.state()['points'],draft)
        self.write('document.patch',title='External change');before=self.doc();self.press(4215);self.assertEqual(self.doc(),before);self.assertTrue(self.state()['stale']);self.press(4218);self.select(4202,1);self.assertEqual(self.state()['plugin'],second)
        self.press(4212);self.press(4215);other=self.write('pattern.create',rows=32)['pattern'];cursor=self.read('context.get');self.press(4218);self.select(4201,1);self.assertEqual(self.state()['pattern'],other);self.assertEqual(self.state()['points'],[]);self.assertEqual(self.read('context.get'),cursor)

    def test_transform_preview_copy_paste_and_local_keyboard_zoom(self):
        self.setup_curve();self.press(4215);baseline=self.saved()[0];self.select(4230,1);self.press(4238);self.assertTrue(self.state()['dirty']);self.assertEqual(self.saved()[0],baseline);self.assertEqual(self.state()['points'][0]['value'],1);self.press(4215)
        self.field(4231,0);self.field(4232,2);self.press(4237);self.select(4230,7);self.field(4231,4);self.field(4232,8);self.press(4238);self.assertTrue(self.state()['dirty']);self.assertTrue(any(p['position']==1024 for p in self.state()['points']));self.press(4218)
        self.press(4227);state=self.state();self.assertLess(state['end']-state['start'],16384);self.press(4229);self.assertGreater(self.state()['start'],state['start']);self.press(4225)
        point=self.state()['handles'][0];self.mouse(0x201,point['x'],point['y']);self.mouse(0x202,point['x'],point['y']);self.key(0x27);self.assertEqual(self.state()['points'][0]['position'],256);self.key(0x25);self.assertEqual(self.state()['points'][0]['position'],0)
        before=self.state()['points'];p=self.state()['handles'][0];self.mouse(0x201,p['x'],p['y']);self.mouse(0x200,p['x']+30,p['y']+20);self.key(0x1B);self.assertEqual(self.state()['points'],before)

    def test_formula_bank_linked_guard_and_last_touched_rack(self):
        plugin=self.setup_curve();self.select(4205,8);self.field(4209,'mix(start,end,t)');self.press(4210);self.press(4221)
        formula='ScreamSeq.FormulaWorkbench';self.field(2001,'start + (end-start)*t*t',formula);self.press(2006,formula);self.press(2007,formula);self.assertEqual(self.state()['points'][0]['formula'],'start + (end-start)*t*t');self.press(4215)
        self.press(4220);bank='ScreamSeq.EnvelopeBank';self.field(1004,'Parameter rise',bank);self.press(1005,bank);self.press(1008,bank);self.assertTrue(self.state()['envelopeBank']['sourceCurrent']);self.assertTrue(self.read('envelope.bank.list')['links'])
        self.field(4208,30);self.press(4210);before=self.doc();self.press(4215);self.assertEqual(self.doc(),before);self.assertIn('linked',self.state()['status'].lower());self.press(4218)
        # A closed bank captures the reloaded source when explicitly reopened.
        self.press(4220);self.assertTrue(self.state()['envelopeBank']['linkedTemplate']);self.press(1009,bank);self.assertFalse(self.read('envelope.bank.list')['links'])
        self.write('plugin.parameters.set',slot=0,values=[dict(id=1,value=-12)]);self.press(4223);self.assertEqual((self.state()['plugin'],self.state()['parameter']),(plugin,1));self.press(4224);self.assertEqual(self.read('workspace.get')['focus'],'plugins')

    def test_minimum_control_bounds_vertical_zoom_and_toolbar_separation(self):
        self.setup_curve();self.select(4205,8);self.press(4210);self.select(4230,5)
        user=private_desktop.user;user.SetWindowPos.argtypes=[wintypes.HWND,wintypes.HWND,ctypes.c_int,ctypes.c_int,ctypes.c_int,ctypes.c_int,wintypes.UINT];user.GetClientRect.argtypes=[wintypes.HWND,ctypes.POINTER(wintypes.RECT)];user.GetWindowRect.argtypes=user.GetClientRect.argtypes;user.MapWindowPoints.argtypes=[wintypes.HWND,wintypes.HWND,ctypes.POINTER(wintypes.POINT),wintypes.UINT];user.ClientToScreen.argtypes=[wintypes.HWND,ctypes.POINTER(wintypes.POINT)]
        scale=self.read('workspace.get')['dpi']/96;private_desktop.check(user.SetWindowPos(self.window(),None,0,0,int(1040*scale),int(760*scale),0x16));client=wintypes.RECT();user.GetClientRect(self.window(),ctypes.byref(client));rects=[]
        for identifier in range(4201,4240):
            rect=wintypes.RECT();user.GetWindowRect(self.control(identifier),ctypes.byref(rect));p=(wintypes.POINT*2)(wintypes.POINT(rect.left,rect.top),wintypes.POINT(rect.right,rect.bottom));user.MapWindowPoints(None,self.window(),p,2)
            self.assertGreaterEqual(p[0].x,0,identifier);self.assertGreaterEqual(p[0].y,0,identifier);self.assertLessEqual(p[1].x,client.right,identifier);self.assertLessEqual(p[1].y,client.bottom,identifier);rects.append((identifier,p[0].x,p[0].y,p[1].x,p[1].y))
        for i,a in enumerate(rects):
            for b in rects[i+1:]:self.assertFalse(max(a[1],b[1])<min(a[3],b[3]) and max(a[2],b[2])<min(a[4],b[4]),(a,b))
        x,y,w,h=self.state()['canvas'];point=wintypes.POINT(int((x+w/2)*scale),int((y+h/2)*scale));user.ClientToScreen(self.window(),ctypes.byref(point));self.desktop.send(self.window(),0x20A,(120<<16)|12,(point.x&65535)|((point.y&65535)<<16));self.assertLess(self.state()['valueHigh']-self.state()['valueLow'],1);self.press(4225);self.assertEqual((self.state()['valueLow'],self.state()['valueHigh']),(0,1))
        toolbar=[];main=self.desktop.hwnd(self.pid)
        for identifier in [104,105,106,316,400,502,430]:
            rect=wintypes.RECT();user.GetWindowRect(user.GetDlgItem(main,identifier),ctypes.byref(rect));toolbar.append((identifier,rect.left,rect.right))
        for i,a in enumerate(toolbar):
            for b in toolbar[i+1:]:self.assertFalse(max(a[1],b[1])<min(a[2],b[2]),(a,b))

    def test_native_saved_curve_controls_audio_and_preserves_partitioning(self):
        plugin=self.add_gain();self.route_master(plugin);self.start();self.select(4204,1);self.assertEqual(self.state()['parameter'],1);self.press(4212);self.field(4208,75);self.select(4205,8);self.field(4209,'.72+.12*sin(beat*6.283185307179586)');self.press(4210);self.press(4215);path=self.folder/'native-parameter-automation.screamseq';self.write('document.save',path=str(path));report=self.folder/'native-parameter-automation-pcm.json'
        evidence=self.render(path,report)
        if os.environ.get('SCREAMSEQ_PARAMETER_EVIDENCE_DIR'):
            folder=Path(os.environ['SCREAMSEQ_PARAMETER_EVIDENCE_DIR']);folder.mkdir(parents=True,exist_ok=True)
            for file in [path,report]:shutil.copy2(file,folder/file.name)
            (folder/'native-parameter-scene.json').write_text(json.dumps(dict(executableSHA256=hashlib.sha256(Path(os.environ['SCREAMSEQ_TEST_EXE']).read_bytes()).hexdigest(),workspace=self.state(),privateDesktop=True,foregroundVisualQualified=False),indent=2),encoding='utf-8')
        self.compare_without_curve(self.saved()[0],evidence,'native-parameter-automation')

    def test_deleted_targets_and_stale_formula_keep_captured_drafts(self):
        plugin=self.setup_curve();self.select(4205,8);self.field(4209,'mix(start,end,t)');self.press(4210);self.press(4221)
        formula='ScreamSeq.FormulaWorkbench';self.field(2001,'.25',formula);self.press(2006,formula)
        self.field(4208,35);self.press(4210);points=self.state()['points'];self.press(2007,formula);self.assertEqual(self.state()['points'],points);self.assertTrue(self.state()['formulaWorkbench']['dirty'])
        self.press(4215);saved=self.saved();self.write('plugin.remove',slot=0);self.press(4218);self.assertEqual(self.state()['plugin'],plugin);self.assertEqual(self.state()['points'],saved[0]['points']);self.assertFalse(self.saved()[0]['resolved'])
        before=self.doc();self.press(4215);self.assertEqual(self.doc(),before);self.press(4217);self.assertEqual(self.saved(),[]);self.assertEqual(self.state()['lane'],'');self.assertEqual(self.state()['parameterCount'],0);self.assertIn('removed',self.state()['status'])

    @unittest.skipUnless(os.environ.get('SCREAMSEQ_TEST_PLUGIN_CACHE'),'installed Contourtonist')
    def test_installed_vst3_curve_native_reopen_state_and_pcm(self):
        descriptor=next(p for p in self.read('plugin.discover',format='VST3') if p['classID']=='ABCDEF019182FAEB416C736743746E31')
        # This vendor starts with a flat filter until a calibrated measurement
        # is received, so offline PCM cannot prove audible parameter modulation.
        self.exercise_plugin_curve(descriptor,'Reference level','contour-parameter-automation',False)

    @unittest.skipUnless(os.environ.get('SCREAMSEQ_TEST_PROVIDER_CACHE'),'private VST3 provider fixture')
    def test_vst3_provider_curve_changes_pcm_and_preserves_partitioning(self):
        descriptor=next(p for p in self.read('plugin.discover',format='VST3') if p['classID']=='5245534F4E414E434546464543540001')
        self.exercise_plugin_curve(descriptor,'Gain','vst3-parameter-automation',True)

    def exercise_plugin_curve(self,descriptor,parameterName,stem,audible):
        self.write('plugin.add',descriptor=descriptor);plugin=self.doc()['data']['nativePlugins'][0]['instanceID'];self.route_master(plugin);opaque=self.read('plugin.state.get',slot=0)
        catalog=self.read('plugin.parameters.get',slot=0);target=next(p for p in catalog if p['name']==parameterName);self.assertTrue(target['writable'] and target['canSlide'])
        self.start();self.field(4203,target['name']);filtered=[p for p in catalog if target['name'].lower() in p['name'].lower()];self.select(4204,next(i for i,p in enumerate(filtered) if p['id']==target['id']));self.assertEqual(self.state()['parameter'],target['id'])
        self.press(4212);self.field(4208,25);self.select(4205,0);self.press(4210)
        point=self.state()['handles'][-1];self.mouse(0x201,point['x'],point['y']);self.mouse(0x202,point['x'],point['y']);self.field(4207,4);self.field(4208,75);self.press(4210);self.press(4215)
        saved=self.saved();self.assertEqual(saved[0]['parameter'],target['id']);self.assertEqual(self.read('plugin.state.get',slot=0),opaque)
        path=self.folder/(stem+'.screamseq');self.write('document.save',path=str(path));self.write('document.open',path=str(path),discard=True);self.assertEqual(self.saved(),saved);self.assertEqual(self.read('plugin.state.get',slot=0),opaque)
        report=self.folder/(stem+'-pcm.json');evidence=self.render(path,report)
        if audible:self.compare_without_curve(saved[0],evidence,stem)
        if os.environ.get('SCREAMSEQ_PARAMETER_EVIDENCE_DIR'):
            folder=Path(os.environ['SCREAMSEQ_PARAMETER_EVIDENCE_DIR']);folder.mkdir(parents=True,exist_ok=True)
            for file in [path,report]:shutil.copy2(file,folder/file.name)
            (folder/(stem+'-target.json')).write_text(json.dumps(dict(descriptor=descriptor,parameter=target,lanes=saved,audibleModulationQualified=audible),indent=2),encoding='utf-8')


if __name__=='__main__':unittest.main()
