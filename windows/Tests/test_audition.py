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

    def live(self, midi=False):
        path = self.folder / 'audition.screamseq'
        self.live_report = self.folder / 'wasapi-host.json'
        self.write('document.save', path=str(path))
        self.pid = self.desktop.launch([
            os.environ['SCREAMSEQ_TEST_EXE'], '--audio-test-silent',
            '--audio-test-allow-stop', '--automation', '--seconds', '120',
            '--project', str(path), '--vst3-test-cache', str(self.cache),
            '--report', str(self.live_report), *(['--midi-test-input'] if midi else [])])
        self.client = Client(r'\\.\pipe\ScreamSeq.Api.' + str(self.pid), timeout=20)
        end = time.monotonic() + 30
        while time.monotonic() < end:
            try:
                if self.read('transport.get')['playing'] and not self.read('workspace.get')['documentBusy']:
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

    def wait_sample_detail_waveform(self, before):
        # First-show reflow can schedule a waveform read. Match its published
        # cache to the current canvas; an outstanding matching timer is a no-op.
        sample = next(s for s in before['data']['samples'] if s['index'] == 1)
        identity = (before['documentId'], before['revision'])
        deadline = time.monotonic() + 8
        last = None
        while time.monotonic() < deadline:
            response = self.client.call('workspace.get')
            self.assertEqual((response['documentId'], response['revision']), identity)
            workspace = response['data']; local = workspace['sampleDetail']
            last = dict(documentBusy=workspace['documentBusy'],
                        pendingViewCommands=workspace['pendingViewCommands'], status=workspace['status'],
                        sampleDetail={k: local.get(k) for k in ('visible', 'pending', 'stale', 'document',
                            'expectedRevision', 'sample', 'id', 'viewStart', 'viewEnd', 'waveStart',
                            'waveEnd', 'waveBins', 'canvas', 'status')}, peakCount=len(local.get('peaks', [])))
            if local.get('visible'):
                self.assertEqual((local['document'], local['expectedRevision'], local['sample'], local['id']),
                                 (*identity, 1, sample['id']), last)
                self.assertFalse(local['stale'], last)
                span = local['viewEnd'] - local['viewStart']; width = local['canvas'][2]
                wanted = min(span, int(max(1, min(4096, width))))
                if (not workspace['documentBusy'] and not workspace['pendingViewCommands']
                        and not local['pending'] and span > 0 and width > 0
                        and local['waveStart'] == local['viewStart'] and local['waveEnd'] == local['viewEnd']
                        and local['waveBins'] == wanted and len(local['peaks']) == 2 * wanted):
                    return
            time.sleep(.01)  # Poll backoff only; readiness is the cache/worker state above.
        self.fail(f'Sample Detail waveform did not become ready: {last}')

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
        before=self.doc();rack=before['data']['nativePlugins']
        try:
            self.client.call('plugin.add',dict(expectedRevision=before['revision'],descriptor=descriptor))
        except ApiError as error:
            if error.code != -32003 or str(error) != 'Request began but reply timed out; outcome uncertain, inspect before retry':
                raise
            # A cold vendor load can outlive the server's response deadline.
            # Its worker still owns the original request: never send another
            # add. Recover only after observing its exact, committed append.
            deadline=time.monotonic()+30
            while time.monotonic()<deadline:
                if not self.read('workspace.get')['documentBusy']:
                    after=self.doc()['data']['nativePlugins']
                    appended=after[-1] if len(after)==len(rack)+1 else {}
                    existing={p['instanceID'] for p in rack}
                    if (after[:-1]==rack and appended.get('classID')==class_id
                            and appended.get('format')=='VST3'
                            and appended.get('instanceID') and appended['instanceID'] not in existing):
                        break
                    raise
                time.sleep(.025)
            else:
                raise
        plugin=self.doc()['data']['nativePlugins'][-1]['instanceID']
        index=self.write('instrument.create',empty=True)['instrument']
        self.write('plugin.instruments.set',plugin=plugin,assignments=[dict(instrument=index,channel=3)])
        if descriptor['name']=='Surge XT':
            changes=[dict(id=p['id'],value=1) for p in self.read('plugin.parameters.get',slot=0) if p['name'].endswith(' Retrigger')]
            self.assertEqual(len(changes),6)
            self.write('plugin.parameters.set',slot=0,values=changes)
        return index

    @unittest.skipUnless(os.environ.get('SCREAMSEQ_TEST_PROVIDER_CACHE'),'private VST3 provider fixture')
    def test_plugin_fixture_recovers_committed_timeout_without_duplicate(self):
        original=self.client.call;adds=0
        def timed_out_reply(method,*args,**kwargs):
            nonlocal adds
            result=original(method,*args,**kwargs)
            if method=='plugin.add':
                adds+=1
                # Keep the real committed plugin but lose its successful reply,
                # matching the server's uncertain-outcome contract.
                raise ApiError(dict(code=-32003,message='Request began but reply timed out; outcome uncertain, inspect before retry'))
            return result
        self.client.call=timed_out_reply
        try:
            instrument=self.plugin_instrument('5245534F4E414E43494E535452550001')
        finally:
            self.client.call=original
        rack=self.doc()['data']['nativePlugins']
        self.assertEqual(adds,1);self.assertEqual(len(rack),1)
        self.assertEqual(rack[0]['instrumentAssignments'],[dict(instrument=instrument,channel=3)])

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
        self.remember('surge-live-audition',dict(first=first,after=after,restored=restored))
        self.assertFalse(restored['fault'],restored)
        self.assertTrue(restored['audioActive'] and restored['audition'],restored)
        self.assertGreater(max(restored['left'],restored['right']),1e-6,restored)
        self.write('transport.stop')

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
        self.wait_sample_detail_waveform(before)
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

    def typing(self): return self.read('workspace.get')['musicalTyping']

    def navigate(self, **fields):
        context=self.client.call('context.get')
        return self.client.call('context.set',dict(expectedRevision=context['revision'],
            expectedContext=context['data']['contextRevision'],**fields))['data']

    def root_key(self,key,up=False,repeat=False):
        self.desktop.send(self.desktop.hwnd(self.pid),0x101 if up else 0x100,
                          ord(key) if isinstance(key,str) else key,(1<<30) if repeat else 0)

    def root_select(self,identifier,index):
        root=self.desktop.hwnd(self.pid);control=private_desktop.user.GetDlgItem(root,identifier)
        self.assertTrue(control);self.desktop.send(control,0x14E,index)
        self.desktop.send(root,0x111,identifier|(1<<16),control)

    def cell_at(self,row):
        return self.read('pattern.get',pattern=0,startRow=row,rowCount=1,channelCount=1)['cells'][0]

    def test_pattern_typing_selected_sound_repeat_limits_history_and_reopen(self):
        root=self.desktop.hwnd(self.pid);self.root_select(135,1)
        selected=self.typing();self.assertEqual(selected['slot'],2)
        self.desktop.send(root,0x111,113);self.navigate(row=1,column=0,following=False)
        before=self.cell_at(1);self.root_key('Z');saved=self.cell_at(1)
        self.assertEqual((saved['note'],saved['instrument']),(49,2))
        after=self.doc();self.root_key('Z');self.root_key('Z',repeat=True)
        self.assertEqual(after,self.doc());self.assertEqual(self.read('context.get')['row'],2)
        self.root_key('Z',up=True);self.assertFalse(self.typing()['held'])
        self.write('history.undo',domain='document');self.assertEqual(before,self.cell_at(1))
        self.write('history.redo',domain='document');self.assertEqual(saved,self.cell_at(1))
        path=self.folder/'typed.screamseq';self.write('document.save',path=str(path))
        self.write('document.open',path=str(path),discard=True);self.assertEqual(saved,self.cell_at(1))
        self.root_select(132,9);self.desktop.send(root,0x111,113);self.navigate(row=2,column=0)
        self.root_key('U');self.root_key('U',up=True)
        self.assertEqual(self.cell_at(2)['note'],self.typing()['noteMax'])
        self.assertFalse(self.read('transport.get')['audioActive'])
        self.remember('typing-history',dict(selected=selected,cell=saved,high=self.cell_at(2)))

    def test_typing_routes_precise_rows_to_the_captured_editor(self):
        events=[dict(channel=0,position=65536+16384,note=65)]
        self.write('pattern.notes.set',pattern=0,events=events)
        self.navigate(row=1,column=0,following=False)
        self.desktop.send(self.desktop.hwnd(self.pid),0x111,113)
        before=self.doc();notes=self.read('pattern.notes.get',pattern=0)
        self.root_key('Z');self.root_key('Z',up=True)
        self.assertEqual(before,self.doc());self.assertEqual(notes,self.read('pattern.notes.get',pattern=0))
        state=self.read('workspace.get')['noteEditor'];self.assertTrue(state['visible'])
        self.assertEqual(state['row'],1);self.assertFalse(self.typing()['held'])

    @unittest.skipUnless(os.environ.get('SCREAMSEQ_TEST_LIVE_AUDIO')=='1','explicit silent WASAPI qualification')
    def test_pattern_typing_audition_release_and_external_voice_ownership(self):
        self.live();root=self.desktop.hwnd(self.pid);self.desktop.send(root,0x111,113)
        self.navigate(row=1,column=0,following=False);self.root_key('Z')
        self.assertEqual(self.cell_at(1)['note'],49)
        first=self.settled();self.assertTrue(first['audition']);self.assertTrue(first['voicePositions'])
        after=self.doc();self.root_key('Z',repeat=True);self.assertEqual(after,self.doc())
        self.note(sample=2);self.root_key('Z',up=True)
        newer=self.settled();self.assertTrue(any(v['sample']==2 for v in newer['voicePositions']))
        self.note(on=False,sample=2);self.assertFalse(self.settled()['voicePositions'])
        self.root_key('X');self.desktop.send(root,0x8,0)  # WM_KILLFOCUS
        self.assertFalse(self.typing()['held']);self.assertFalse(self.settled()['voicePositions'])
        self.root_key('X',repeat=True);self.assertFalse(self.typing()['held'])
        self.write('transport.stop');self.remember('typing-pattern-host',dict(first=first,newer=newer))

    @unittest.skipUnless(os.environ.get('SCREAMSEQ_TEST_LIVE_AUDIO')=='1','explicit silent WASAPI qualification')
    def test_sample_dock_typing_live_mode_and_text_focus(self):
        self.live();root=self.desktop.hwnd(self.pid)
        self.client.call('workspace.panel',dict(panel='samples',focus=True))
        before=self.doc();position=self.read('context.get')['row'];self.root_key('R');self.root_key('N')
        self.assertEqual(before,self.doc());self.assertEqual(self.read('context.get')['row'],position)
        self.assertEqual(len(self.typing()['held']),2);self.assertTrue(self.settled()['voicePositions'])
        self.root_key('R',up=True);self.root_key('N',up=True);self.assertFalse(self.settled()['voicePositions'])
        self.desktop.send(root,0x111,113);self.desktop.send(root,0x111,507)
        self.assertTrue(self.read('workspace.get')['liveKeyboard']);self.root_key('Z')
        self.assertEqual(before,self.doc());self.assertTrue(self.settled()['voicePositions'])
        held=self.typing()['held'];self.assertEqual([note['key'] for note in held],[ord('Z')])
        self.client.call('workspace.panel',dict(panel='samples',focus=True));self.root_key(9)
        # Inspector focus must not enter the separate Main sample editor.
        main_field=private_desktop.user.GetDlgItem(root,230);self.assertTrue(main_field)
        focus=self.desktop.focus(root);self.assertEqual(focus,root);self.assertNotEqual(focus,main_field)
        self.assertEqual(self.typing()['held'],held);self.assertEqual(before,self.doc())
        # Explicitly select Main Samples before checking its native text field.
        self.desktop.send(root,0x111,108)
        workspace=self.read('workspace.get')
        private_desktop.user.IsWindowVisible.argtypes=[wintypes.HWND]
        self.assertTrue(private_desktop.user.IsWindowVisible(main_field))
        self.assertEqual(workspace['focus'],'samples');self.assertEqual(self.desktop.focus(root),root)
        self.assertEqual(self.typing()['held'],held);self.root_key(9)
        focus=self.desktop.focus(root);self.assertEqual(focus,main_field)
        self.root_key('X');self.assertEqual(len(self.typing()['held']),1)
        # A release delivered to a different main control is handled by the pump.
        private_desktop.user.PostMessageW.argtypes=[wintypes.HWND,wintypes.UINT,wintypes.WPARAM,wintypes.LPARAM]
        private_desktop.check(private_desktop.user.PostMessageW(focus,0x101,ord('Z'),0))
        self.assertFalse(self.settled()['voicePositions']);self.assertFalse(self.typing()['held'])
        self.desktop.send(root,0x111,507);self.assertFalse(self.read('workspace.get')['liveKeyboard'])
        self.assertEqual(before,self.doc());self.write('transport.stop')

    @unittest.skipUnless(os.environ.get('SCREAMSEQ_TEST_LIVE_AUDIO')=='1','explicit silent WASAPI qualification')
    def test_midi_and_typing_share_sound_release_orders_and_panic(self):
        self.live(midi=True)
        sources=self.read('midi.devices.get')['devices']
        self.assertEqual(len(sources),1)
        settings=self.read('midi.settings.get')
        self.client.call('midi.settings.set',dict(expectedMidiRevision=settings['revision'],
            source=sources[0]['id'],armed=False,channelsCount=2,quantization=0,latencyMS=0))
        root=self.desktop.hwnd(self.pid);self.desktop.send(root,0x111,113);self.desktop.send(root,0x111,507)
        self.assertTrue(self.read('workspace.get')['liveKeyboard'])
        before=self.doc()

        def midi(on,channel=0):
            state=self.read('midi.settings.get');fixture=state['qualificationInput']
            elapsed=(int(state['hostTime'])-int(fixture['anchorHostTime']))//10000
            packed=(0x90 if on else 0x80)|channel|(48<<8)|((100 if on else 0)<<16)
            self.client.call('midi.test.inject',dict(expectedMidiRevision=state['revision'],
                generation=fixture['generation'],events=[dict(packed=packed,milliseconds=elapsed)]))
            return self.settled()

        self.root_key('Z');first=self.settled();self.assertTrue(first['voicePositions'])
        shared=midi(True)
        self.assertEqual([v['generation'] for v in shared['voicePositions']],
                         [v['generation'] for v in first['voicePositions']])
        self.root_key('Z',up=True);self.assertTrue(self.settled()['voicePositions'])
        self.assertFalse(midi(False)['voicePositions'])

        first=midi(True);self.assertTrue(first['voicePositions']);self.root_key('Z')
        self.assertEqual([v['generation'] for v in self.settled()['voicePositions']],
                         [v['generation'] for v in first['voicePositions']])
        self.assertTrue(midi(False)['voicePositions'])
        self.root_key('Z',up=True);self.assertFalse(self.settled()['voicePositions'])

        midi(True,0);midi(True,1);self.assertTrue(midi(False,1)['voicePositions'])
        self.assertFalse(midi(False,0)['voicePositions'])
        self.root_key('Z');midi(True);self.write('transport.panic')
        self.assertFalse(self.typing()['held']);self.assertFalse(self.settled()['voicePositions'])
        self.root_key('Z');self.assertTrue(self.settled()['voicePositions'])
        self.assertTrue(midi(False)['voicePositions'])  # Old MIDI release cannot stop the new typed voice.
        self.root_key('Z',up=True);self.assertFalse(self.settled()['voicePositions'])

        # Same pitch on another selected sound belongs to a new voice.
        self.root_key('Z');self.root_select(135,1);newer=midi(True)
        self.assertTrue(any(v['sample']==2 for v in newer['voicePositions']))
        self.root_key('Z',up=True);self.assertTrue(any(v['sample']==2 for v in self.settled()['voicePositions']))
        self.assertFalse(midi(False)['voicePositions']);self.assertEqual(before,self.doc())
        self.write('transport.stop')

    @unittest.skipUnless(os.environ.get('SCREAMSEQ_TEST_LIVE_AUDIO')=='1','explicit silent WASAPI qualification')
    def test_rejected_midi_step_batch_releases_previously_held_voice(self):
        self.live(midi=True)
        module=self.folder/'owned-narrow-note-range.mod'
        shutil.copyfile(Path(__file__).resolve().parents[2]/'test/test.mod',module)
        self.write('document.open',path=str(module),discard=True)
        self.assertEqual(self.doc()['data']['format'],'MOD')
        self.assertLess(self.typing()['noteMax'],120)
        self.root_select(135,0);self.assertEqual(self.typing()['slot'],1)
        self.navigate(row=1,channel=0,column=0,following=False)
        source=self.read('midi.devices.get')['devices'][0]['id'];settings=self.read('midi.settings.get')
        self.client.call('midi.settings.set',dict(expectedMidiRevision=settings['revision'],
            source=source,armed=False,channelsCount=1,quantization=0,latencyMS=0))

        baseline=self.read('transport.get');input_before=self.read('midi.settings.get')
        self.assertFalse(baseline['fault']);self.assertEqual(baseline['auditionDropped'],0)
        self.assertEqual(input_before['lost'],0)
        observations=[]
        def record_observation(sample):
            observations.append(sample)
            if len(observations)>16:del observations[1:-15]

        def wait_for_voice_state(phase,ready):
            deadline=time.monotonic()+8
            def observe(method):
                remaining=deadline-time.monotonic()
                if remaining<=0:raise TimeoutError('MIDI voice observation deadline expired')
                return Client(self.client.pipe,timeout=min(.5,remaining)).call(method)['data']
            while time.monotonic()<deadline:
                try:
                    workspace=observe('workspace.get');transport=observe('transport.get')
                    sample=dict(phase=phase,documentBusy=workspace['documentBusy'],status=workspace['status'],
                        midi={key:workspace['midi'][key] for key in ('connected','generation','lost')},
                        transport={key:transport[key] for key in ('audioActive','audition','playing','callbacks','frames',
                            'voices','voicePositions','fault','auditionDropped','playbackEpoch')})
                    record_observation(sample)
                    self.assertFalse(transport['fault'],sample)
                    self.assertEqual(transport['auditionDropped'],baseline['auditionDropped'],sample)
                    self.assertEqual(workspace['midi']['lost'],input_before['lost'],sample)
                    self.assertTrue(workspace['midi']['connected'],sample)
                    self.assertEqual(workspace['midi']['generation'],input_before['generation'],sample)
                    if ready(workspace,transport):return transport
                except (TransportError,TimeoutError) as error:
                    record_observation(dict(phase=phase,readError=str(error)))
                remaining=deadline-time.monotonic()
                if remaining>0:time.sleep(min(.03,remaining))
            self.fail(f'{phase} did not settle within 8 seconds: '+json.dumps(observations,sort_keys=True))

        def inject(packed):
            state=self.read('midi.settings.get');fixture=state['qualificationInput']
            elapsed=(int(state['hostTime'])-int(fixture['anchorHostTime']))//10000
            reply=self.client.call('midi.test.inject',dict(expectedMidiRevision=state['revision'],
                generation=fixture['generation'],events=[dict(packed=value,milliseconds=elapsed) for value in packed]))
            self.assertEqual(reply['data']['accepted'],len(packed))

        # This repository MOD has a looping sample 1, so a missed release stays
        # observable rather than disappearing because a one-shot sample ended.
        note=min(49,self.typing()['noteMax'])-1
        inject([0x90|(note<<8)|(100<<16)])
        # Queue admission and one fixed delay do not prove the first prepared
        # callback has consumed this note. Observe the actual looping voice.
        held=wait_for_voice_state('initial sample voice',lambda workspace,transport:
            not workspace['documentBusy'] and transport['audioActive'] and transport['audition'] and
            not transport['playing'] and transport['voices']>0 and
            any(voice['sample']==1 for voice in transport['voicePositions']))
        state=self.read('midi.settings.get')
        self.client.call('midi.settings.set',dict(expectedMidiRevision=state['revision'],
            source=source,armed=True,channelsCount=1,quantization=0,latencyMS=0))
        before=self.doc();cells=self.read('pattern.get',pattern=0,startRow=0,rowCount=64)
        invalid=self.typing()['noteMax']  # MIDI is zero-based; resulting tracker note exceeds this format.
        inject([0x80|(note<<8),0x90|(invalid<<8)|(100<<16)])
        rejected=False;silent_at=None
        def rejection_and_release(workspace,transport):
            nonlocal rejected,silent_at
            self.assertTrue(transport['audioActive'] and transport['audition'] and not transport['playing'],transport)
            self.assertEqual(transport['playbackEpoch'],held['playbackEpoch'],transport)
            if workspace['documentBusy']:return False
            if not rejected:
                if 'outside' not in workspace['status'] or 'range' not in workspace['status']:return False
                rejected=True
            # A single empty voicePositions read can be a concurrent snapshot;
            # require zero telemetry too, sustained over advancing callbacks.
            if transport['voices'] or transport['voicePositions']:
                silent_at=None;return False
            if silent_at is None:silent_at=(transport['callbacks'],transport['frames'])
            return transport['callbacks']>=silent_at[0]+2 and transport['frames']>silent_at[1]
        released=wait_for_voice_state('rejected step releases held voice',rejection_and_release)
        self.assertEqual(self.doc(),before)
        self.assertEqual(self.read('pattern.get',pattern=0,startRow=0,rowCount=64),cells)
        self.assertEqual(self.read('context.get')['row'],1)
        self.desktop.send(self.desktop.hwnd(self.pid),0x111,549)
        error=self.read('workspace.get')['midiWindow']['status']
        self.assertIn('outside',error);self.assertIn('range',error)
        self.assertEqual(self.read('midi.settings.get')['lost'],0)
        self.remember('midi-rejected-step-state-observations',dict(held=held,released=released,observations=observations))
        self.write('transport.stop')

    @unittest.skipUnless(os.environ.get('SCREAMSEQ_TEST_LIVE_AUDIO')=='1','explicit silent WASAPI qualification')
    def test_native_sample_and_instrument_typing_capture_text_and_close(self):
        self.write('instrument.create',sample=1)
        # The demo's mapped instrument has a looping sample and no fadeout.
        # Give this release fixture an explicit short fade; key-off must retain
        # instrument semantics rather than forcibly cutting every instrument.
        for instrument in self.doc()['data']['instruments']:
            self.write('instrument.patch',instrument=instrument['index'],values=dict(fadeout=32768))
        self.live();root=self.desktop.hwnd(self.pid)
        for command,kind,field in [(505,'ScreamSeq.SampleDetail','sampleDetail'),(503,'ScreamSeq.InstrumentEnvelope','instrumentEnvelope')]:
            self.desktop.send(root,0x111,command);window=self.window(kind)
            if self.desktop.focus(window)!=window:self.desktop.send(window,0x100,0x75)  # F6: canvas focus
            self.assertEqual(self.desktop.focus(window),window)
            before=self.doc();self.desktop.send(window,0x100,ord('Z'))
            self.assertTrue(self.settled()['voicePositions']);self.assertEqual(before,self.doc())
            self.desktop.send(window,0x100,0x75)  # F6: intentional text/control focus
            self.desktop.send(self.desktop.focus(window),0x100,ord('X'))
            self.assertEqual(len(self.typing()['held']),1)
            self.desktop.send(self.desktop.focus(window),0x101,ord('Z'))
            self.assertFalse(self.settled()['voicePositions'])
            self.desktop.send(window,0x100,0x75);self.desktop.send(window,0x100,ord('C'))
            self.desktop.send(window,0x10)  # WM_CLOSE releases this editor's voices
            self.assertFalse(self.typing()['held']);self.assertFalse(self.settled()['voicePositions'])
            self.assertEqual(before,self.doc())
        self.desktop.send(root,0x111,505)
        if self.desktop.focus(self.window('ScreamSeq.SampleDetail'))!=self.window('ScreamSeq.SampleDetail'):self.desktop.send(self.window('ScreamSeq.SampleDetail'),0x100,0x75)
        self.write('document.patch',title='Changed after opening editor')
        self.desktop.send(self.window('ScreamSeq.SampleDetail'),0x100,ord('Z'))
        self.assertFalse(self.typing()['held'])
        self.assertIn('Reload',self.read('workspace.get')['sampleDetail']['status'])
        self.write('transport.stop')

    @unittest.skipUnless(os.environ.get('SCREAMSEQ_TEST_INSTRUMENT_CACHE') and os.environ.get('SCREAMSEQ_TEST_LIVE_AUDIO')=='1','installed Surge XT and explicit silent WASAPI qualification')
    def test_installed_surge_pattern_typing_and_piano_retrigger_ownership(self):
        index=self.plugin_instrument('ABCDEF019182FAEB566D624153675854');self.live()
        catalog=self.doc()['data']['instruments'];self.root_select(135,next(n for n,v in enumerate(catalog) if v['index']==index))
        root=self.desktop.hwnd(self.pid);self.desktop.send(root,0x111,113);self.navigate(row=1,column=0)
        opaque=self.read('plugin.state.get',slot=0);self.root_key('Z')
        self.assertEqual(self.cell_at(1)['instrument'],index)
        first=self.settled();self.assertGreater(max(first['left'],first['right']),1e-6)
        self.root_key('Z',up=True);self.write('transport.panic')
        self.start();self.press(5105);self.note(sample=2);self.press(5106)
        self.assertTrue(any(v['sample']==2 for v in self.settled()['voicePositions']))
        self.note(on=False,sample=2);self.assertEqual(opaque,self.read('plugin.state.get',slot=0))
        self.write('transport.stop');self.remember('surge-typing',dict(first=first))

    @unittest.skipUnless(os.environ.get('SCREAMSEQ_TEST_LIVE_AUDIO')=='1','explicit silent WASAPI qualification')
    def test_keyup_during_document_wait_never_starts_a_late_voice(self):
        self.live();root=self.desktop.hwnd(self.pid);self.desktop.send(root,0x111,113)
        self.navigate(row=1,column=0)
        private_desktop.user.PostMessageW.argtypes=[wintypes.HWND,wintypes.UINT,wintypes.WPARAM,wintypes.LPARAM]
        for message in (0x100,0x101):private_desktop.check(private_desktop.user.PostMessageW(root,message,ord('Z'),0))
        end=time.monotonic()+10
        while time.monotonic()<end:
            state=self.read('workspace.get')
            if not state['documentBusy'] and self.read('context.get')['row']==2 and not state['musicalTyping']['held']:break
            time.sleep(.01)
        else:self.fail(str(state))
        self.assertEqual(self.cell_at(1)['note'],49);self.assertFalse(self.settled()['voicePositions'])
        private_desktop.check(private_desktop.user.PostMessageW(root,0x100,ord('X'),0))
        private_desktop.check(private_desktop.user.PostMessageW(root,0x111,102,0))
        end=time.monotonic()+10
        while time.monotonic()<end:
            state=self.read('workspace.get')
            if not state['documentBusy'] and self.read('context.get')['row']==3 and not state['musicalTyping']['held']:break
            time.sleep(.01)
        else:self.fail(str(state))
        self.assertEqual(self.cell_at(2)['note'],51)
        self.assertFalse(self.settled()['audioActive'])


if __name__ == '__main__': unittest.main()
