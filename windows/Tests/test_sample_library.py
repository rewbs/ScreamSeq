"""Real application library revisions, shared decoding and musical imports."""
import array
import concurrent.futures
import ctypes
from ctypes import wintypes
import json
import math
import os
from pathlib import Path
import sys
import tempfile
import time
import unittest
import wave

import private_desktop
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'Api'))
from client import Client,ApiError,TransportError


class SampleLibraryTests(unittest.TestCase):
    def setUp(self):
        self.folder=Path(self.enterContext(tempfile.TemporaryDirectory(prefix='sample-library-app-',dir=os.environ['TMPDIR'])))
        self.pack=self.folder/'Éléctric Pack';self.settings=self.folder/'Library'
        self.samples=[]
        for name in ['Drums/Kick 2.wav','Drums/Kick 10.wav','Keys/048 Piano C3.wav','Keys/052 Piano E3.wav','Keys/055 Piano G3.wav']:
            path=self.pack/name;self.wav(path);self.samples.append(path)
        self.desktop=self.enterContext(private_desktop.PrivateDesktop());self.pid,self.client=self.launch()

    def wav(self,path,rate=44100,channels=2,seconds=.1):
        path.parent.mkdir(parents=True,exist_ok=True)
        pcm=array.array('h',(int(12000*math.sin(f*.071))*(1 if c==0 else -1) for f in range(int(rate*seconds)) for c in range(channels)))
        with wave.open(str(path),'wb') as out:out.setparams((channels,2,rate,0,'NONE','not compressed'));out.writeframes(pcm.tobytes())

    def launch(self,audio=False):
        args=[os.environ['SCREAMSEQ_TEST_EXE'],'--audio-test-silent' if audio else '--inspection','--automation','--seconds','120','--sample-test-library',str(self.settings)]
        pid=self.desktop.launch(args);client=Client(r'\\.\pipe\ScreamSeq.Api.'+str(pid),timeout=20)
        for _ in range(100):
            try:
                client.call('document.get')
                if not audio or client.call('transport.get')['data']['audioActive']:return pid,client
            except TransportError:pass
            time.sleep(.05)
        self.fail('sample library app did not start')

    def read(self,method,client=None,**params):return (client or self.client).call(method,params)['data']
    def ready(self,client=None):
        end=time.monotonic()+10
        while time.monotonic()<end:
            state=self.read('sample.library.get',client)
            if not state['indexing']:
                self.assertTrue(state['ready']);self.assertIsNone(state['error']);return state
            time.sleep(.01)
        self.fail('library did not finish indexing')
    def configure(self):
        state=self.ready();reply=self.client.call('sample.library.roots.set',dict(roots=[str(self.pack)],expectedLibraryRevision=state['libraryRevision']))
        self.assertIndependent(reply);return self.ready()
    def assertIndependent(self,reply):
        self.assertTrue(reply['revision'].startswith('library:'));self.assertFalse(reply['changed']);self.assertFalse(reply['playbackStopped']);self.assertNotIn('documentId',reply)
    def reject(self,code,method,**params):
        with self.assertRaises(ApiError) as raised:self.client.call(method,params)
        self.assertEqual(raised.exception.code,code,str(raised.exception));return raised.exception

    def test_discovery_independent_revisions_search_tags_paging_and_multisamples(self):
        before=self.client.call('document.get');description=self.read('api.describe')
        for method in ('get','search','inspect','multisample.get'):self.assertIn('sample.library.'+method,description['reads'])
        for method in ('roots.set','rescan','preview','preview.stop'):self.assertIn('sample.library.'+method,description['writes'])
        self.assertEqual(description['revisionGuards']['sample.library.roots.set'],['expectedLibraryRevision'])
        self.assertEqual(description['revisionGuards']['sample.library.preview'],[])
        state=self.configure();self.assertEqual(state['count'],5)
        result=self.read('sample.library.search',query='electric kick',limit=1);self.assertEqual(result['total'],2);self.assertEqual(result['items'][0]['name'],'Kick 2.wav')
        page=self.read('sample.library.search',query='electric kick',limit=1,offset=1,expectedLibraryRevision=state['libraryRevision']);self.assertEqual(page['items'][0]['name'],'Kick 10.wav')
        tagged=self.read('sample.library.search',tags=['electric pack','Keys']);self.assertEqual(tagged['total'],3)
        group=self.read('sample.library.multisample.get',path=str(self.samples[2]))['group'];self.assertEqual(group['count'],3);self.assertEqual(group['suggestedOctaveShift'],1)
        self.assertEqual([s['semitone'] for s in group['samples']],[36,40,43]);self.assertEqual(before,self.client.call('document.get'))

    def test_rejections_replay_and_rescan_revision(self):
        state=self.configure();before=self.client.call('document.get')
        for method,params in [('search',dict(limit=True)),('search',dict(limit=0)),('search',dict(offset=250001)),('get',dict(expectedRevision=before['revision'])),('inspect',dict(path='relative.wav')),('preview',dict(path=str(self.samples[0]),gainDB=True)),('preview',dict(path=str(self.samples[0]),gainDB=1))]:
            self.reject(-32602,'sample.library.'+method,**params)
        self.reject(-32602,'sample.library.roots.set',roots=[str(self.pack)])
        failure=self.reject(-32001,'sample.library.rescan',expectedLibraryRevision='old');self.assertIsNone(failure.data)
        params=dict(roots=[str(self.pack)],expectedLibraryRevision=state['libraryRevision'])
        response=self.client.call('sample.library.roots.set',params,request_id='retained-library-write')
        self.ready();self.assertEqual(response,self.client.call('sample.library.roots.set',params,request_id='retained-library-write'))
        new=self.ready();self.assertNotEqual(state['libraryRevision'],new['libraryRevision'])
        self.reject(-32001,'sample.library.search',expectedLibraryRevision=state['libraryRevision']);self.assertEqual(before,self.client.call('document.get'))

    def test_inspection_preview_stop_is_bounded_silent_and_does_not_change_song(self):
        before=self.client.call('document.get');self.ready()
        self.assertFalse(self.read('workspace.get')['sampleLibrary']['preview']['deviceOpen'])
        response=self.client.call('sample.library.inspect',dict(path=str(self.samples[0])));self.assertIndependent(response);data=response['data']
        self.assertEqual((data['rate'],data['channels'],data['frames'],data['previewFrames']),(44100,2,4410,4410));self.assertEqual(len(data['peaks']),512);self.assertNotIn('pcm',data)
        preview=self.client.call('sample.library.preview',dict(path=str(self.samples[0]),gainDB=-12));self.assertIndependent(preview);self.assertFalse(preview['data']['audible'])
        self.assertEqual({k:v for k,v in preview['data'].items() if k!='audible'},data)
        self.assertFalse(self.read('workspace.get')['sampleLibrary']['preview']['deviceOpen'])
        self.assertEqual(self.read('sample.library.preview.stop'),dict(playing=False));self.assertEqual(before,self.client.call('document.get'))
        self.reject(-32003,'sample.library.inspect',path=str(self.pack/'Missing.wav'))

    def test_persistent_cache_reopens_and_removed_roots_preserve_files(self):
        state=self.configure();_,other=self.launch();reopened=self.ready(other)
        self.assertEqual((reopened['roots'],reopened['count'],reopened['indexedAt']),(state['roots'],state['count'],state['indexedAt']))
        self.assertNotEqual(reopened['libraryRevision'],state['libraryRevision'])
        self.client.call('sample.library.roots.set',dict(roots=[],expectedLibraryRevision=state['libraryRevision']));self.assertEqual(self.ready()['count'],0)
        self.assertTrue(all(path.is_file() for path in self.samples));self.assertEqual(self.ready(other)['count'],5)

    def test_library_work_preserves_running_song_transport(self):
        self.pid,self.client=self.launch(audio=True);before=self.client.call('document.get');transport=self.read('transport.get');self.assertTrue(transport['audioActive'])
        self.configure();self.read('sample.library.search',query='piano');self.read('sample.library.preview',path=str(self.samples[0]));self.read('sample.library.preview.stop')
        self.assertTrue(self.read('transport.get')['audioActive']);self.assertFalse(self.read('transport.get')['fault']);self.assertEqual(before,self.client.call('document.get'))

    def test_indexed_files_import_atomically_with_undo_and_persistence(self):
        self.configure();before=self.client.call('document.get');paths=[x['path'] for x in self.read('sample.library.search',query='kick')['items']]
        imported=self.client.call('sample.importMany',dict(paths=paths,createInstruments=True,expectedRevision=before['revision']))
        self.assertTrue(imported['changed']);self.assertEqual(imported['data']['count'],2);self.assertFalse(imported['revision'].startswith('library:'))
        after=self.client.call('document.get');self.assertEqual(len(after['data']['samples']),len(before['data']['samples'])+2)
        self.client.call('history.undo',dict(domain='document',expectedRevision=after['revision']));undone=self.client.call('document.get');self.assertEqual(undone['data']['samples'],before['data']['samples'])
        self.client.call('history.redo',dict(domain='document',expectedRevision=undone['revision']));redone=self.client.call('document.get');self.assertEqual(redone['data']['samples'],after['data']['samples'])
        path=self.folder/'library-import.screamseq';self.client.call('document.save',dict(path=str(path),expectedRevision=redone['revision']))
        current=self.client.call('document.get');self.client.call('document.open',dict(path=str(path),discard=True,expectedRevision=current['revision']));self.assertEqual(self.client.call('document.get')['data']['samples'],after['data']['samples'])

    def browser(self):return self.read('workspace.get')['sampleLibrary']['browser']
    def tool(self,family=False):
        name='ScreamSeq.MultisampleImport' if family else 'ScreamSeq.SampleLibrary';found=[]
        @private_desktop.callback
        def visit(hwnd,_):
            pid=wintypes.DWORD();private_desktop.user.GetWindowThreadProcessId(hwnd,ctypes.byref(pid));text=ctypes.create_unicode_buffer(100);private_desktop.user.GetClassNameW(hwnd,text,100)
            if pid.value==self.pid and text.value==name:found.append(hwnd)
            return True
        private_desktop.check(private_desktop.user.EnumDesktopWindows(self.desktop.desktop,visit,0));self.assertEqual(len(found),1);return found[0]
    def idle_browser(self):
        end=time.monotonic()+8
        while time.monotonic()<end:
            state=self.browser()
            if not state.get('pending') and not state.get('queued') and not state.get('multisample',{}).get('pending'):return state
            time.sleep(.01)
        self.fail(str(self.browser()))
    def open_browser(self):
        self.configure();self.desktop.send(self.desktop.hwnd(self.pid),0x111,511)
        end=time.monotonic()+8
        while time.monotonic()<end:
            workspace=self.read('workspace.get');state=workspace['sampleLibrary']['browser']
            if state['visible'] and not workspace['pendingViewCommands']:return self.idle_browser()
            time.sleep(.01)
        self.fail(str(workspace))
    def control(self,identifier,family=False):
        hwnd=private_desktop.user.GetDlgItem(self.tool(family),identifier);self.assertTrue(hwnd);return hwnd
    def press(self,identifier,family=False):
        self.desktop.send(self.tool(family),0x111,identifier,self.control(identifier,family));return self.idle_browser()
    def field(self,identifier,value,family=False):
        text=ctypes.create_unicode_buffer(str(value));self.desktop.send(self.control(identifier,family),0xC,0,ctypes.addressof(text));return self.idle_browser()
    def select_files(self,indices):
        control=self.control(5404);self.desktop.send(control,0x185,0,-1)
        for index in indices:self.desktop.send(control,0x185,1,index)
        if indices:self.desktop.send(control,0x19E,indices[0])
        self.desktop.send(self.tool(),0x111,5404|(1<<16),control);return self.idle_browser()

    def test_native_browser_search_waveform_preview_and_retained_selection(self):
        self.open_browser();before=self.client.call('document.get');state=self.field(5401,'electric piano');self.assertEqual(state['total'],3)
        state=self.select_files([0]);self.assertEqual(len(state['inspection']['peaks']),512);self.assertEqual(state['family']['count'],3)
        path=state['selected'];self.press(5410);self.assertFalse(self.browser()['inspection']['audible']);self.press(5411)
        self.press(5418);self.assertFalse(self.browser()['visible']);self.desktop.send(self.desktop.hwnd(self.pid),0x111,511);state=self.idle_browser()
        self.assertEqual(state['search'],'electric piano');self.assertEqual(state['selected'],path);self.assertEqual(before,self.client.call('document.get'))

    def test_native_browser_multi_selection_import_uses_shared_transaction(self):
        self.open_browser();self.field(5401,'kick');self.select_files([0,1]);before=self.client.call('document.get')
        if not self.browser()['createInstruments']:self.press(5409)
        self.press(5414);after=self.client.call('document.get');self.assertEqual(len(after['data']['samples']),len(before['data']['samples'])+2)
        self.assertIn('2 samples imported',self.browser()['status']);self.assertEqual(len(self.browser()['selectedPaths']),2)
        self.client.call('history.undo',dict(domain='document',expectedRevision=after['revision']));self.assertEqual(self.client.call('document.get')['data']['samples'],before['data']['samples'])

    def test_native_multisample_roots_stale_guard_retained_draft_and_rebase(self):
        self.open_browser();self.field(5401,'piano');self.select_files([0]);self.press(5415);state=self.browser()['multisample'];self.assertTrue(state['visible']);self.assertEqual(state['octaveShift'],'1')
        self.field(5601,'Glass keys',True);self.field(5602,'0',True);self.press(5603,True);state=self.browser()['multisample'];self.assertEqual([z['rootNote'] for z in state['zones']],[37,41,44])
        before=self.client.call('document.get');self.client.call('document.patch',dict(title='Changed while reviewing',expectedRevision=before['revision']));changed=self.client.call('document.get')
        self.press(5604,True);state=self.browser()['multisample'];self.assertTrue(state['stale']);self.assertIn('Song changed',state['status']);self.assertEqual(changed,self.client.call('document.get'))
        self.press(5608,True);self.press(5415);state=self.browser()['multisample'];self.assertEqual(state['name'],'Glass keys');self.assertEqual(state['octaveShift'],'0');self.assertEqual(state['revision'],before['revision'])
        self.press(5609,True);self.press(5603,True);self.press(5604,True);self.assertFalse(self.browser()['multisample']['visible'])
        after=self.client.call('document.get');self.assertEqual(len(after['data']['samples']),len(changed['data']['samples'])+3)
        instrument=after['data']['instruments'][-1];self.assertEqual(instrument['name'],'Glass keys')
        self.assertEqual(self.read('workspace.get')['musicalTyping']['id'],instrument['id'])
        detail=self.read('instrument.get',instrument=instrument['index']);self.assertEqual(len(detail['mapping']),128);self.assertEqual(detail['mapping'][35],0);self.assertEqual(detail['mapping'][44],0)
        self.client.call('history.undo',dict(domain='document',expectedRevision=after['revision']));self.assertEqual(self.client.call('document.get')['data']['samples'],changed['data']['samples'])

    def test_native_browser_controls_fit_minimum_size_and_folder_removal_preserves_files(self):
        self.open_browser();user=private_desktop.user;hwnd=self.tool()
        user.GetDpiForWindow.argtypes=[wintypes.HWND];user.SetWindowPos.argtypes=[wintypes.HWND,wintypes.HWND,ctypes.c_int,ctypes.c_int,ctypes.c_int,ctypes.c_int,wintypes.UINT]
        user.GetClientRect.argtypes=[wintypes.HWND,ctypes.POINTER(wintypes.RECT)];user.GetWindowRect.argtypes=[wintypes.HWND,ctypes.POINTER(wintypes.RECT)];user.ClientToScreen.argtypes=[wintypes.HWND,ctypes.POINTER(wintypes.POINT)]
        scale=user.GetDpiForWindow(hwnd)/96;user.SetWindowPos(hwnd,None,0,0,int(920*scale),int(600*scale),0x16)
        rect=wintypes.RECT();user.GetClientRect(hwnd,ctypes.byref(rect));origin=wintypes.POINT();user.ClientToScreen(hwnd,ctypes.byref(origin))
        for identifier in range(5401,5420):
            control=self.control(identifier);bounds=wintypes.RECT();user.GetWindowRect(control,ctypes.byref(bounds));self.assertGreaterEqual(bounds.left-origin.x,0);self.assertLessEqual(bounds.right-origin.x,rect.right);self.assertGreaterEqual(bounds.top-origin.y,0)
            if identifier!=5402:self.assertLessEqual(bounds.bottom-origin.y,rect.bottom)
        control=self.control(5402);self.desktop.send(control,0x14E,1);self.desktop.send(hwnd,0x111,5402|(1<<16),control);self.idle_browser();self.press(5406);self.assertEqual(self.ready()['count'],0);self.assertTrue(all(path.is_file() for path in self.samples))


if __name__=='__main__':unittest.main()
