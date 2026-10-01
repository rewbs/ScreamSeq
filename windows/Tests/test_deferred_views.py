"""View requests during real worker activity, without replaying musical writes."""
import concurrent.futures
import ctypes
from ctypes import wintypes
import time
import unittest

import private_desktop
import test_song_routing as support


class DeferredViewTests(unittest.TestCase):
    setUp=support.SongRoutingTests.setUp
    launch=support.SongRoutingTests.launch
    doc=support.SongRoutingTests.doc
    read=support.SongRoutingTests.read
    write=support.SongRoutingTests.write

    def command(self, identifier):
        self.desktop.send(self.desktop.hwnd(self.pid),0x111,identifier)

    def during_worker(self, method, action, **fields):
        user=private_desktop.user
        user.GetWindowTextW.argtypes=[wintypes.HWND,wintypes.LPWSTR,ctypes.c_int]
        hwnd=self.desktop.hwnd(self.pid)
        with concurrent.futures.ThreadPoolExecutor(1) as pool:
            job=pool.submit(self.write,method,**fields)
            observed=False
            for _ in range(2000):
                title=ctypes.create_unicode_buffer(512);user.GetWindowTextW(hwnd,title,512)
                if 'Document worker busy' in title.value:
                    observed=True;break
                if job.done():break
                time.sleep(.001)
            self.assertTrue(observed,'Must exercise actual outstanding work')
            start=time.monotonic();action();self.assertLess(time.monotonic()-start,.5)
            result=job.result(timeout=15)
        deadline=time.monotonic()+8
        while time.monotonic()<deadline:
            state=self.read('workspace.get')
            if not state['documentBusy'] and not state['pendingViewCommands']:return result,state
            time.sleep(.01)
        self.fail('View queue did not drain')

    def large_pattern(self):
        self.write('pattern.create',rows=1024)

    def test_open_views_survive_busy_work_and_never_replay_mixer_mutation(self):
        self.large_pattern()
        def commands():
            self.command(417);self.command(417)  # Coalesced routing intent.
            self.command(402)  # Enable mixer is a write: it must remain rejected.
            self.command(499)  # Independent formula reference.
            self.command(102)  # Stop remains immediate.
        _,state=self.during_worker('document.patch',commands,channels=96)
        self.assertTrue(state['songRouting']['visible'],state)
        self.assertTrue(state['formulaReference']['visible'],state)
        self.assertEqual(self.doc()['data']['channels'],96)
        self.assertFalse(self.read('mixer.get')['active'])
        self.assertFalse(self.read('transport.get')['playing'])

    def test_focus_pattern_cancels_pending_view_request(self):
        self.large_pattern()
        _,state=self.during_worker('document.patch',lambda:(self.command(417),self.command(113)),channels=96)
        self.assertFalse(state['songRouting']['visible'],state)
        self.assertEqual(state['focus'],'pattern')

    def test_cursor_change_cannot_retarget_pending_note_inspector(self):
        self.large_pattern()
        def move():
            self.command(107)
            self.desktop.send(self.desktop.hwnd(self.pid),0x100,0x28)
        _,state=self.during_worker('document.patch',move,channels=96)
        self.assertFalse(state['noteEditor']['visible'],state)
        self.assertEqual(self.read('context.get')['row'],1)
        self.assertIn('View target changed',state['status'])

    def test_document_replacement_cannot_open_old_queued_view(self):
        self.large_pattern();self.write('document.patch',channels=96)
        path=self.folder/'large-view.screamseq';self.write('document.save',path=str(path))
        before=self.doc()['documentId']
        _,state=self.during_worker('document.open',lambda:self.command(417),path=str(path),discard=True)
        self.assertNotEqual(self.doc()['documentId'],before)
        self.assertFalse(state['songRouting']['visible'],state)
        self.assertIn('View target changed',state['status'])
