"""Native output preferences, draft guards and silent real-device lifecycle."""
import ctypes
from ctypes import wintypes
import os
from pathlib import Path
import sys
import time
import unittest

import private_desktop
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'Api'))
from client import Client,ApiError,TransportError


class AudioSettingsTests(unittest.TestCase):
    def setUp(self):
        self.desktop=self.enterContext(private_desktop.PrivateDesktop())
        self.pid,self.client=self.launch()

    def launch(self,audio=False):
        args=[os.environ['SCREAMSEQ_TEST_EXE'],'--audio-test-silent' if audio else '--inspection','--automation','--seconds','120']
        if audio:args+=['--audio-test-allow-stop']
        pid=self.desktop.launch(args);client=Client(r'\\.\pipe\ScreamSeq.Api.'+str(pid),timeout=20)
        for _ in range(150):
            try:
                client.call('document.get')
                if not audio or client.call('transport.get')['data']['audioActive']:return pid,client
            except TransportError:pass
            time.sleep(.03)
        self.fail('audio settings app did not start')

    def read(self,method,**params):return self.client.call(method,params)['data']
    def settings(self):return self.read('audio.settings.get')
    def configure(self,period=128,endpoint='',**extra):
        return self.client.call('audio.settings.set',dict(endpoint=endpoint,periodFrames=period,expectedAudioRevision=self.settings()['audioRevision'],**extra))
    def reject(self,code,**params):
        with self.assertRaises(ApiError) as caught:self.client.call('audio.settings.set',params)
        self.assertEqual(caught.exception.code,code,str(caught.exception))

    def test_discovery_enumeration_independent_revision_replay_and_noop(self):
        before=self.client.call('document.get');catalog=self.read('api.describe')
        self.assertIn('audio.devices.get',catalog['reads']);self.assertIn('audio.settings.get',catalog['reads'])
        self.assertEqual(catalog['revisionGuards']['audio.settings.set'],['expectedAudioRevision'])
        devices=self.read('audio.devices.get')['devices'];self.assertEqual(len({x['id'] for x in devices}),len(devices))
        self.assertTrue(all(x['id'] and x['name'] and isinstance(x['default'],bool) for x in devices))
        original=self.settings();self.assertEqual(original['periodFrames'],0)
        params=dict(endpoint='',periodFrames=128,expectedAudioRevision=original['audioRevision'])
        first=self.client.call('audio.settings.set',params,request_id='owned-audio-change')
        self.assertFalse(first['changed']);self.assertFalse(first['playbackStopped']);self.assertNotIn('documentId',first)
        self.assertEqual(first,self.client.call('audio.settings.set',params,request_id='owned-audio-change'))
        self.assertNotEqual(first['revision'],original['audioRevision']);self.assertIsNone(first['data']['checked'])
        noop=self.configure();self.assertEqual(noop['revision'],first['revision']);self.assertEqual(before,self.client.call('document.get'))

    def test_stale_invalid_and_dry_run_preserve_settings_and_song(self):
        before=self.client.call('document.get');original=self.settings();base=dict(endpoint='',periodFrames=64,expectedAudioRevision=original['audioRevision'])
        for patch in ({'periodFrames':True},{'periodFrames':128.0},{'periodFrames':-1},{'periodFrames':65},{'periodFrames':2**64-1},{'endpoint':False},{'endpoint':'x\0y'},{'endpoint':'x'*4097},{'expectedAudioRevision':''},{'expectedAudioRevision':'x'*201},{'dryRun':1},{'surprise':1}):self.reject(-32602,**dict(base,**patch))
        self.reject(-32602,**dict(base,endpoint='ScreamSeq-owned-absent-output'))
        self.reject(-32001,**dict(base,expectedAudioRevision='old'))
        check=self.client.call('audio.settings.set',dict(base,dryRun=True));self.assertEqual(check['revision'],original['audioRevision']);self.assertEqual(check['data']['proposed']['periodFrames'],64)
        self.assertEqual(original,self.settings());self.assertEqual(before,self.client.call('document.get'))
        self.configure();self.reject(-32001,**base)

    def test_settings_revision_is_independent_of_document_edits(self):
        state=self.settings();before=self.client.call('document.get')
        self.client.call('document.patch',dict(title='Device-independent title',expectedRevision=before['revision']))
        after=self.client.call('document.get');self.assertEqual(self.settings()['audioRevision'],state['audioRevision'])
        self.client.call('audio.settings.set',dict(endpoint='',periodFrames=256,expectedAudioRevision=state['audioRevision']))
        self.assertEqual(after,self.client.call('document.get'))

    def tool(self):
        found=[]
        @private_desktop.callback
        def visit(hwnd,_):
            pid=wintypes.DWORD();private_desktop.user.GetWindowThreadProcessId(hwnd,ctypes.byref(pid));name=ctypes.create_unicode_buffer(100);private_desktop.user.GetClassNameW(hwnd,name,100)
            if pid.value==self.pid and name.value=='ScreamSeq.AudioSettings':found.append(hwnd)
            return True
        private_desktop.check(private_desktop.user.EnumDesktopWindows(self.desktop.desktop,visit,0));self.assertEqual(len(found),1);return found[0]
    def control(self,id):
        result=private_desktop.user.GetDlgItem(self.tool(),id);self.assertTrue(result);return result
    def window(self):return self.settings()['window']
    def open(self):
        self.desktop.send(self.desktop.hwnd(self.pid),0x111,512)
        end=time.monotonic()+8
        while time.monotonic()<end:
            state=self.window()
            if state['visible'] and not state.get('pending'):return state
            time.sleep(.01)
        self.fail(str(state))
    def press(self,id):
        self.desktop.send(self.tool(),0x111,id,self.control(id))
        # Device enumeration pumps the UI while pending. A sent command can
        # be observed before that outer operation has finished; wait for the
        # same readiness that enables the real button before the next action.
        end=time.monotonic()+8
        while time.monotonic()<end:
            state=self.window()
            if not state['pending']:return state
            time.sleep(.01)
        self.fail(str(state))
    def select(self,id,index):
        control=self.control(id);self.desktop.send(control,0x14E,index);self.desktop.send(self.tool(),0x111,id|(1<<16),control);return self.window()

    def test_native_output_draft_close_refresh_stale_rebase_and_apply(self):
        state=self.open();self.select(5702,3);self.assertEqual(self.window()['periodFrames'],256)
        self.press(5706);self.assertFalse(self.window()['visible']);self.assertEqual(self.open()['periodFrames'],256)
        self.press(5703);self.assertEqual(self.window()['periodFrames'],256)
        self.configure(64);self.press(5704);self.assertIn('Audio settings changed',self.window()['status']);self.assertEqual(self.settings()['periodFrames'],64)
        self.press(5705);self.assertEqual(self.window()['periodFrames'],64);self.select(5702,4)
        if state['devices']:
            self.select(5701,1);self.assertEqual(self.window()['endpoint'],state['devices'][0]['id'])
        self.press(5704);self.assertEqual(self.settings()['periodFrames'],512);self.assertEqual(self.settings()['endpoint'],self.window()['endpoint'])
        self.assertFalse(self.settings()['active']);self.assertIsNone(self.settings()['checked'])

    def test_silent_running_stream_survives_validation_then_stops_and_reopens_selected_output(self):
        self.pid,self.client=self.launch(audio=True);before=self.client.call('document.get')
        devices=self.read('audio.devices.get')['devices'];chosen=next(x['id'] for x in devices if x['default'])
        initial=self.settings();self.assertTrue(initial['active']);self.assertEqual(initial['activeEndpoint'],chosen)
        checked=self.configure(128,chosen,dryRun=True);self.assertFalse(checked['playbackStopped']);self.assertTrue(self.settings()['active'])
        self.assertGreater(checked['data']['proposed']['checked']['sampleRate'],0)
        self.reject(-32602,endpoint='ScreamSeq-owned-absent-output',periodFrames=128,expectedAudioRevision=initial['audioRevision']);self.assertTrue(self.settings()['active'])
        applied=self.configure(128,chosen);self.assertTrue(applied['playbackStopped']);self.assertFalse(applied['data']['active'])
        self.assertEqual(applied['data']['checked']['endpoint'],chosen)
        self.client.call('transport.play',dict(expectedRevision=before['revision']))
        running=self.settings();self.assertTrue(running['active']);self.assertEqual(running['activeEndpoint'],chosen);self.assertEqual(running['periodFrames'],128)
        self.assertGreater(running['actualPeriodFrames'],0);self.assertGreaterEqual(running['bufferFrames'],running['actualPeriodFrames'])
        noop=self.configure(128,chosen);self.assertFalse(noop['playbackStopped']);self.assertTrue(self.settings()['active'])
        self.assertFalse(self.read('transport.get')['fault']);self.assertEqual(before,self.client.call('document.get'))
        self.assertEqual(chosen,next(x['id'] for x in self.read('audio.devices.get')['devices'] if x['default']))

    def test_native_settings_controls_fit_minimum_window(self):
        self.open();user=private_desktop.user;hwnd=self.tool()
        user.GetDpiForWindow.argtypes=[wintypes.HWND];user.SetWindowPos.argtypes=[wintypes.HWND,wintypes.HWND,ctypes.c_int,ctypes.c_int,ctypes.c_int,ctypes.c_int,wintypes.UINT]
        user.GetClientRect.argtypes=[wintypes.HWND,ctypes.POINTER(wintypes.RECT)];user.GetWindowRect.argtypes=[wintypes.HWND,ctypes.POINTER(wintypes.RECT)];user.ClientToScreen.argtypes=[wintypes.HWND,ctypes.POINTER(wintypes.POINT)]
        scale=user.GetDpiForWindow(hwnd)/96;user.SetWindowPos(hwnd,None,0,0,int(560*scale),int(380*scale),0x16)
        rect=wintypes.RECT();user.GetClientRect(hwnd,ctypes.byref(rect));origin=wintypes.POINT();user.ClientToScreen(hwnd,ctypes.byref(origin))
        for identifier in list(range(5701,5707))+list(range(5800,5805)):
            bounds=wintypes.RECT();user.GetWindowRect(self.control(identifier),ctypes.byref(bounds))
            self.assertGreaterEqual(bounds.left-origin.x,0);self.assertLessEqual(bounds.right-origin.x,rect.right);self.assertGreaterEqual(bounds.top-origin.y,0)
            if identifier not in (5701,5702):self.assertLessEqual(bounds.bottom-origin.y,rect.bottom)


if __name__=='__main__':unittest.main()
