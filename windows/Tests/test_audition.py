"""Native preview notes through the PID pipe and owned piano window."""
import ctypes
from ctypes import wintypes
import json
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import time
import unittest

import private_desktop
import test_parameter_automation_ui as support
import test_song_routing as routing
from client import Client, ApiError, TransportError


class AuditionTests(unittest.TestCase):
    setUp = routing.SongRoutingTests.setUp
    launch = routing.SongRoutingTests.launch
    doc = support.ParameterAutomationUITests.doc
    read = support.ParameterAutomationUITests.read
    write = support.ParameterAutomationUITests.write

    def window(self,kind='ScreamSeq.Audition'):
        return support.ParameterAutomationUITests.window(self,kind)

    def control(self,identifier,kind='ScreamSeq.Audition'):
        return support.ParameterAutomationUITests.control(self,identifier,kind)

    def field(self,identifier,value,kind='ScreamSeq.Audition'):
        return support.ParameterAutomationUITests.field(self,identifier,value,kind)

    def state(self): return self.read('workspace.get')['audition']

    def start(self):
        self.desktop.send(self.desktop.hwnd(self.pid),0x111,506)
        self.assertTrue(self.state()['visible'])

    def press(self,identifier,kind='ScreamSeq.Audition'):
        self.desktop.send(self.window(kind),0x111,identifier,self.control(identifier,kind))

    def mouse(self,message,pitch):
        key=next(k for k in self.state()['keys'] if k['note']==pitch)
        scale=self.read('workspace.get')['dpi']/96
        x=int((key['x']+key['width']/2)*scale)
        y=int((key['y']+key['height']*.85)*scale)
        self.desktop.send(self.window(),message,0 if message==0x202 else 1,x|(y<<16))

    def remember(self,stem,evidence,files=()):
        if not os.environ.get('SCREAMSEQ_AUDITION_EVIDENCE_DIR'): return
        folder=Path(os.environ['SCREAMSEQ_AUDITION_EVIDENCE_DIR']);folder.mkdir(parents=True,exist_ok=True)
        evidence['executableSHA256']=hashlib.sha256(Path(os.environ['SCREAMSEQ_TEST_EXE']).read_bytes()).hexdigest()
        evidence['foregroundVisualQualified']=False
        (folder/(stem+'.json')).write_text(json.dumps(evidence,indent=2),encoding='utf-8')
        for path in files: shutil.copy2(path,folder/path.name)

    def render(self,stem,sample=None,instrument=None):
        path=self.folder/(stem+'.screamseq');report=self.folder/(stem+'-pcm.json')
        self.write('document.save',path=str(path))
        before=self.doc()
        result=subprocess.run([os.environ['SCREAMSEQ_TEST_EXE'],
            '--offline-audition-sample' if sample else '--offline-audition-instrument',str(sample or instrument),
            '--project',str(path),'--vst3-test-cache',str(self.cache),'--report',str(report)],timeout=90)
        self.assertEqual(result.returncode,0,report.read_text(encoding='utf-8') if report.exists() else 'no report')
        evidence=json.loads(report.read_text(encoding='utf-8'))
        self.assertTrue(evidence['finite'] and evidence['documentUnchanged'] and evidence['songPositionStationary'])
        self.assertTrue(evidence['audition']);self.assertGreater(evidence['energy'],1)
        self.assertLess(evidence['maxPartitionDelta'],1e-6)
        for render in evidence['renders']:
            self.assertEqual(render['quarterSecondEnergy'][0],0)
            self.assertGreater(sum(render['quarterSecondEnergy'][1:3]),.01)
            self.assertGreaterEqual(render['firstAudibleFrame'],render['rate']//4)
        self.assertEqual(before,self.doc())
        self.remember(stem,evidence,(path,report))
        return evidence

    def live(self):
        path = self.folder / 'audition.screamseq'
        self.live_report = self.folder / 'wasapi-host.json'
        self.write('document.save', path=str(path))
        self.pid = self.desktop.launch([
            os.environ['SCREAMSEQ_TEST_EXE'], '--audio-test-silent',
            '--audio-test-allow-stop', '--automation', '--seconds', '120',
            '--project', str(path), '--vst3-test-cache', str(self.cache),
            '--report', str(self.live_report)])
        self.client = Client(r'\\.\pipe\ScreamSeq.Api.' + str(self.pid), timeout=20)
        end = time.monotonic() + 30
        while time.monotonic() < end:
            try:
                if self.read('transport.get')['audioActive']:
                    self.write('transport.stop')
                    return
            except TransportError:
                pass
            process = self.desktop.processes[-1]
            code = wintypes.DWORD()
            private_desktop.check(private_desktop.kernel.GetExitCodeProcess(process.hProcess, ctypes.byref(code)))
            self.assertEqual(code.value, 259, f'QA app exited: {code.value:#x}')
            time.sleep(.1)
        self.fail('Silent audition app did not start')

    def tearDown(self):
        if not hasattr(self,'live_report'): return
        # Complete the owned QA loop so the host records its actual endpoint
        # rate, period, elapsed time and callback stats before fixture cleanup.
        user=private_desktop.user
        user.PostMessageW.argtypes=[wintypes.HWND,wintypes.UINT,wintypes.WPARAM,wintypes.LPARAM]
        private_desktop.check(user.PostMessageW(self.desktop.hwnd(self.pid),0x12,0,0))
        self.assertEqual(private_desktop.kernel.WaitForSingleObject(self.desktop.processes[-1].hProcess,10000),0)
        report=json.loads(self.live_report.read_text(encoding='utf-8'))
        self.assertTrue(report['audio']['silentOutput'])
        self.remember(self._testMethodName+'-host',report)

    def note(self, pitch=49, on=True, **fields):
        return self.write('transport.note', note=pitch, on=on, **(fields or dict(sample=1)))

    def settled(self):
        time.sleep(.12)
        return self.read('transport.get')

    def test_validation_inspection_and_no_device_for_release(self):
        before = self.doc()
        for fields in [dict(), dict(sample=1,instrument=1), dict(sample=True),
                       dict(sample=65536), dict(sample=1,note=0), dict(sample=1,note=121),
                       dict(sample=1,note=True), dict(sample=1,velocity=128),
                       dict(sample=1,on=1), dict(sample=1,other=0)]:
            params = dict(expectedRevision=before['revision'],note=49,on=True)
            params.update(fields)
            with self.assertRaises(ApiError): self.client.call('transport.note', params)
        with self.assertRaises(ApiError) as error: self.note()
        self.assertIn('Inspection',str(error.exception))
        self.assertFalse(self.note(on=False)['audioActive'])
        self.assertFalse(self.note(pitch=49.0,on=False,sample=1.0,velocity=100.0)['audioActive'])
        self.assertFalse(self.note(sample=1,velocity=0)['audioActive'])
        self.assertFalse(self.write('transport.panic')['audioActive'])
        self.assertEqual(before,self.doc())
        self.write('document.patch',title='Revision changed')
        with self.assertRaises(ApiError):
            self.client.call('transport.note',dict(expectedRevision=before['revision'],sample=1,note=49,on=False))

    @unittest.skipUnless(os.environ.get('SCREAMSEQ_TEST_LIVE_AUDIO')=='1','explicit silent WASAPI qualification')
    def test_stopped_preview_clock_release_panic_and_document_preservation(self):
        self.live()
        before = self.doc()
        result = self.note()
        self.assertTrue(result['queued'] and result['audioActive'] and result['audition'])
        self.assertFalse(result['playing'])
        first = self.settled()
        self.assertTrue(first['voicePositions'], first)
        second = self.settled()
        self.assertEqual((first['order'],first['row']), (second['order'],second['row']))
        self.assertGreater(second['frames'],first['frames'])
        self.note(on=False)
        self.assertFalse(self.settled()['voicePositions'])
        self.note(pitch=52)
        self.assertTrue(self.settled()['voicePositions'])
        self.write('transport.panic')
        self.assertFalse(self.settled()['voicePositions'])
        self.assertEqual(before,self.doc())
        self.write('transport.stop')
        self.assertFalse(self.read('transport.get')['audioActive'])
        self.remember('sample-live',dict(first=first,second=second,stopped=self.read('transport.get')))

    def test_sample_and_mapped_instrument_pcm_across_rates_and_partitions(self):
        sample=self.render('sample-audition',sample=1)
        for r in sample['renders']: self.assertLess(sum(r['quarterSecondEnergy'][4:]),1e-10)
        index=self.write('instrument.create',sample=1)['instrument']
        self.render('mapped-instrument-audition',instrument=index)

    def plugin_instrument(self,class_id):
        descriptor=next(p for p in self.read('plugin.discover',format='VST3') if p['classID']==class_id)
        self.write('plugin.add',descriptor=descriptor)
        plugin=self.doc()['data']['nativePlugins'][-1]['instanceID']
        index=self.write('instrument.create',empty=True)['instrument']
        self.write('plugin.instruments.set',plugin=plugin,assignments=[dict(instrument=index,channel=3)])
        if descriptor['name']=='Surge XT':
            changes=[dict(id=p['id'],value=1) for p in self.read('plugin.parameters.get',slot=0) if p['name'].endswith(' Retrigger')]
            self.assertEqual(len(changes),6)
            self.write('plugin.parameters.set',slot=0,values=changes)
        return index

    @unittest.skipUnless(os.environ.get('SCREAMSEQ_TEST_PROVIDER_CACHE'),'private VST3 provider fixture')
    def test_vst3_provider_instrument_audition_pcm(self):
        index=self.plugin_instrument('5245534F4E414E43494E535452550001')
        self.render('provider-instrument-audition',instrument=index)

    @unittest.skipUnless(os.environ.get('SCREAMSEQ_TEST_INSTRUMENT_CACHE'),'installed Windows Surge XT')
    def test_installed_surge_saved_state_reopen_and_audition_pcm(self):
        index=self.plugin_instrument('ABCDEF019182FAEB566D624153675854')
        before=self.read('plugin.state.get',slot=0)
        self.render('surge-instrument-audition',instrument=index)
        self.write('document.open',path=str(self.folder/'surge-instrument-audition.screamseq'),discard=True)
        self.assertEqual(before,self.read('plugin.state.get',slot=0))
        self.render('surge-reopened-audition',instrument=index)

    @unittest.skipUnless(os.environ.get('SCREAMSEQ_TEST_INSTRUMENT_CACHE') and os.environ.get('SCREAMSEQ_TEST_LIVE_AUDIO')=='1','installed Surge XT and explicit silent WASAPI qualification')
    def test_installed_surge_live_audition_remove_and_plugin_undo(self):
        index=self.plugin_instrument('ABCDEF019182FAEB566D624153675854')
        self.live();before=self.doc();opaque=self.read('plugin.state.get',slot=0)
        self.note(instrument=index);first=self.settled()
        self.assertTrue(first['audition']);self.assertGreater(max(first['left'],first['right']),1e-6)
        self.note(on=False,instrument=index);self.settled();self.write('transport.panic')
        after=self.settled();self.assertFalse(after['fault']);self.assertEqual(after['overruns'],0)
        self.assertEqual(before,self.doc());self.assertEqual(opaque,self.read('plugin.state.get',slot=0))
        self.write('plugin.remove',slot=0);self.assertFalse(self.read('transport.get')['audioActive'])
        self.write('history.undo',domain='plugins');self.assertEqual(opaque,self.read('plugin.state.get',slot=0))
        self.note(instrument=index);restored=self.settled()
        self.assertGreater(max(restored['left'],restored['right']),1e-6)
        self.write('transport.stop')
        self.remember('surge-live-audition',dict(first=first,after=after,restored=restored))

    @unittest.skipUnless(os.environ.get('SCREAMSEQ_TEST_LIVE_AUDIO')=='1','explicit silent WASAPI qualification')
    def test_song_preview_panic_and_space_transition(self):
        self.live();self.note();self.desktop.send(self.desktop.hwnd(self.pid),0x100,0x20)
        self.assertTrue(self.read('transport.get')['playing']);self.assertFalse(self.read('transport.get')['audition'])
        before=self.doc();first=self.settled();epoch=first['playbackEpoch']
        self.note(pitch=57);self.settled();self.write('transport.panic');last=self.settled()
        self.assertTrue(last['playing']);self.assertEqual(last['playbackEpoch'],epoch)
        self.assertGreater(last['frames'],first['frames']);self.assertTrue(last['voicePositions'])
        self.assertEqual(before,self.doc());self.write('transport.stop')

    @unittest.skipUnless(os.environ.get('SCREAMSEQ_TEST_LIVE_AUDIO')=='1','explicit silent WASAPI qualification')
    def test_piano_same_pitch_ownership_focus_release_and_old_epoch(self):
        self.live();self.start();before=self.doc()
        self.desktop.send(self.window(),0x100,ord('Z'))
        self.desktop.send(self.window(),0x100,ord('Z'))
        self.assertEqual(len(self.state()['held']),1)
        self.mouse(0x201,49);self.assertEqual(len(self.state()['held']),2)
        self.mouse(0x202,49);self.assertEqual(len(self.state()['held']),1)
        self.assertTrue(self.settled()['voicePositions'])
        self.desktop.send(self.control(5103),0x101,ord('Z'))
        self.assertFalse(self.state()['held']);self.assertFalse(self.settled()['voicePositions'])
        self.press(5105);self.assertTrue(self.state()['held'])
        self.desktop.send(self.window(),0x6,0)
        self.assertFalse(self.state()['held']);self.assertFalse(self.settled()['voicePositions'])
        self.press(5105);self.write('transport.stop');self.note()
        self.press(5106)  # Old UI note-off cannot cut a new transport's same pitch.
        self.assertTrue(self.settled()['voicePositions'])
        self.note(on=False);self.assertFalse(self.settled()['voicePositions'])
        self.press(5105);self.press(5110);self.assertFalse(self.state()['visible'])
        self.assertFalse(self.settled()['voicePositions']);self.assertEqual(before,self.doc())
        self.write('transport.stop')

    @unittest.skipUnless(os.environ.get('SCREAMSEQ_TEST_LIVE_AUDIO')=='1','explicit silent WASAPI qualification')
    def test_sample_markers_preview_replay_and_idle_presentation(self):
        self.live();self.desktop.send(self.desktop.hwnd(self.pid),0x111,505)
        before=self.doc();params=dict(expectedRevision=before['revision'],sample=1,note=49,on=True)
        first=self.client.call('transport.note',params,request_id='preview-replay')
        playing=self.settled();self.assertTrue(playing['voicePositions'])
        generations=[p['generation'] for p in playing['voicePositions']]
        self.assertEqual(self.client.call('transport.note',params,request_id='preview-replay'),first)
        self.assertEqual(generations,[p['generation'] for p in self.settled()['voicePositions']])
        self.assertTrue(self.read('workspace.get')['sampleDetail']['playbackFrames'])
        self.note(on=False);self.settled();self.settled()
        stopped=self.read('workspace.get');self.assertFalse(stopped['sampleDetail']['playbackFrames'])
        frames=self.read('transport.get')['presentation']['frames'];time.sleep(.3)
        quiet=self.read('transport.get')['presentation'];self.assertEqual(quiet['frames'],frames)
        self.assertTrue(self.read('transport.get')['audioActive'])
        self.assertEqual(before,self.doc());self.write('transport.stop')
        self.remember('audition-idle',dict(playing=playing,quiet=quiet['frames'],beforeQuiet=frames))

    def test_connected_editors_stale_capture_and_minimum_geometry(self):
        self.desktop.send(self.desktop.hwnd(self.pid),0x111,505)
        self.press(4855,'ScreamSeq.SampleDetail');self.assertEqual(self.state()['kind'],'sample')
        source=self.state()['id'];self.write('document.patch',title='External change')
        self.press(5105);self.assertIn('Song changed',self.state()['status']);self.assertEqual(self.state()['id'],source)
        self.press(5109);self.assertFalse(self.state()['stale'])
        self.write('instrument.create',sample=1)
        self.desktop.send(self.desktop.hwnd(self.pid),0x111,503)
        self.press(4454,'ScreamSeq.InstrumentEnvelope');self.assertEqual(self.state()['kind'],'instrument')
        user=private_desktop.user
        user.SetWindowPos.argtypes=[wintypes.HWND,wintypes.HWND,ctypes.c_int,ctypes.c_int,ctypes.c_int,ctypes.c_int,wintypes.UINT]
        user.GetClientRect.argtypes=[wintypes.HWND,ctypes.POINTER(wintypes.RECT)];user.GetWindowRect.argtypes=user.GetClientRect.argtypes
        user.MapWindowPoints.argtypes=[wintypes.HWND,wintypes.HWND,ctypes.POINTER(wintypes.POINT),wintypes.UINT]
        scale=self.read('workspace.get')['dpi']/96
        private_desktop.check(user.SetWindowPos(self.window(),None,0,0,int(800*scale),int(380*scale),0x16))
        client=wintypes.RECT();user.GetClientRect(self.window(),ctypes.byref(client));rects=[]
        for identifier in range(5101,5113):
            r=wintypes.RECT();user.GetWindowRect(self.control(identifier),ctypes.byref(r))
            p=(wintypes.POINT*2)(wintypes.POINT(r.left,r.top),wintypes.POINT(r.right,r.bottom));user.MapWindowPoints(None,self.window(),p,2)
            self.assertGreaterEqual(p[0].x,0);self.assertGreaterEqual(p[0].y,0)
            self.assertLessEqual(p[1].x,client.right,identifier);self.assertLessEqual(p[1].y,client.bottom,identifier)
            rects.append((identifier,p[0].x,p[0].y,p[1].x,p[1].y))
        for i,a in enumerate(rects):
            for b in rects[i+1:]: self.assertFalse(max(a[1],b[1])<min(a[3],b[3]) and max(a[2],b[2])<min(a[4],b[4]),(a,b))
        self.remember('piano-scene',dict(workspace=self.state(),controls=rects,privateDesktop=True))


if __name__ == '__main__': unittest.main()
