"""Captured native graph command drafts and row-aligned retained lane views."""
import ctypes
from ctypes import wintypes
import os
import json
import shutil
from pathlib import Path
import subprocess
import tempfile
import time
import unittest
import private_desktop
import test_song_routing as routing


class GraphCommandsTests(unittest.TestCase):
    setUp=routing.SongRoutingTests.setUp
    launch=routing.SongRoutingTests.launch
    doc=routing.SongRoutingTests.doc
    read=routing.SongRoutingTests.read
    write=routing.SongRoutingTests.write
    def main_command(self,identifier):
        hwnd=self.desktop.hwnd(self.pid);self.desktop.send(hwnd,0x111,identifier,private_desktop.user.GetDlgItem(hwnd,identifier) or 0)
    control=routing.SongRoutingTests.control
    press=routing.SongRoutingTests.press
    select=routing.SongRoutingTests.select
    field=routing.SongRoutingTests.field
    add_gain=routing.SongRoutingTests.add_gain
    def local(self):return self.read('workspace.get')['graphCommands']
    def window(self):
        found=[]
        @private_desktop.callback
        def visit(hwnd,_):
            pid=wintypes.DWORD();private_desktop.user.GetWindowThreadProcessId(hwnd,ctypes.byref(pid));name=ctypes.create_unicode_buffer(100);private_desktop.user.GetClassNameW(hwnd,name,100)
            if pid.value==self.pid and name.value=='ScreamSeq.GraphCommands':found.append(hwnd)
            return True
        private_desktop.check(private_desktop.user.EnumDesktopWindows(self.desktop.desktop,visit,0));self.assertEqual(len(found),1);return found[0]
    def idle(self):
        end=time.monotonic()+8;quiet=None
        while time.monotonic()<end:
            ws=self.read('workspace.get')
            if ws['documentBusy'] or ws['graphCommands'].get('pending'):quiet=None
            elif quiet is None:quiet=time.monotonic()
            elif time.monotonic()-quiet>=.1:return
            time.sleep(.02)
        self.fail(str(self.local()))
    def start(self):self.idle();self.main_command(500);self.idle();self.assertTrue(self.local()['visible'])
    def data(self):return self.read('graph.get',includeState=False)
    def commands(self,pattern=None):return [c for c in self.data()['commands'] if pattern is None or c['pattern']==pattern]
    def setup_graph(self):
        self.write('mixer.enable');graph=self.write('graph.create',name='Row colour')['graph'];buses=self.data()['mixer']['buses'];return graph,buses[0]['id'],buses[1]['id']
    def text(self,identifier):
        text=ctypes.create_unicode_buffer(2048);self.desktop.send(self.control(identifier),0xD,len(text),ctypes.addressof(text));return text.value
    def mouse(self,x,y,double=False):
        scale=self.read('workspace.get')['dpi']/96;self.desktop.send(self.desktop.hwnd(self.pid),0x203 if double else 0x201,1,(int(x*scale)&65535)|((int(y*scale)&65535)<<16))

    def test_commands_preserve_cells_verify_noop_history_and_reopen(self):
        graph,first,second=self.setup_graph();other=dict(target=second,graph=graph,position=2*65536+31,column=0,kind='start',amount=.4,wet=.75,tails=True)
        self.write('graph.commands.set',pattern=0,lanes=[dict(target=second,count=1)],commands=[other]);self.start()
        self.select(4002,2);self.select(4003,1);self.field(4005,3);self.field(4006,12.5);self.field(4007,25);self.field(4008,80);self.press(4009)
        before=self.doc();self.press(4012);self.assertEqual(self.doc(),before);self.assertTrue(self.local()['draft']);self.press(4010)
        events=self.commands();self.assertEqual(len(events),2);retained=next(c for c in events if c['target']==second);self.assertEqual({k:v for k,v in retained.items() if k!='pattern'},other)
        added=next(c for c in events if c['target']==first);self.assertEqual((added['position'],added['column'],added['amount'],added['wet'],added['tails']),(3*65536+8192,2,.25,.8,True))
        self.assertFalse(self.local()['draft']);self.assertEqual(self.local()['row'],3);saved=self.doc();self.press(4010);self.assertEqual(self.doc(),saved)
        self.write('history.undo',domain='document');self.assertEqual(self.commands(),[retained]);self.write('history.redo',domain='document');self.assertEqual(self.commands(),events)
        path=self.folder/'commands.screamseq';self.write('document.save',path=str(path));self.write('graph.commands.set',pattern=0,commands=[]);self.write('document.open',path=str(path),discard=True);self.assertEqual(self.commands(),events)
        self.press(4014);self.select(4002,2);self.field(4005,3);self.press(4019);self.press(4011);self.assertEqual(self.commands(),[retained])

    def test_drafts_stale_identity_close_and_invalid_values(self):
        graph,first,second=self.setup_graph();self.start();self.select(4003,5);self.field(4008,33);self.select(4001,1)
        self.assertEqual(self.local()['target'],first);self.assertEqual(self.text(4008),'33');self.assertEqual(self.desktop.send(self.control(4003),0x147),5)
        self.press(4018);self.assertFalse(self.local()['visible']);self.start();self.assertTrue(self.local()['draft']);self.assertEqual(self.text(4008),'33')
        self.write('document.patch',title='Changed elsewhere');before=self.doc();self.press(4010);self.assertEqual(self.doc(),before);self.assertTrue(self.local()['stale']);self.assertEqual(self.text(4008),'33')
        self.press(4013);self.field(4006,100);before=self.doc();self.press(4010);self.assertEqual(self.doc(),before);self.assertIn('below 100',self.local()['status'])
        self.field(4006,'NaN');self.press(4010);self.assertEqual(self.doc(),before);self.field(4006,0);self.field(4005,999999);self.press(4010);self.assertEqual(self.doc(),before)
        self.press(4013);self.press(4010);self.assertEqual(len(self.commands()),1)
        other=self.folder/'other.screamseq';self.write('document.save',path=str(other));self.field(4007,37);self.write('document.open',path=str(other),discard=True);before=self.doc();self.press(4010);self.assertEqual(self.doc(),before);self.assertTrue(self.local()['draft']);self.assertEqual(self.text(4007),'37')
        self.press(4013);self.assertEqual(self.text(4007),'37');self.assertIn('From cursor',self.local()['status']);self.press(4014);self.assertFalse(self.local()['stale'])

    def test_all_actions_offsets_lane_projection_keyboard_and_delete(self):
        graph,first,second=self.setup_graph();self.start();self.press(4015)
        for index in range(6):
            self.field(4005,index);self.select(4003,index);self.field(4006,99.99999 if index==5 else 0);self.field(4007,20);self.field(4008,60);self.press(4010)
        events=self.commands();self.assertEqual([c['kind'] for c in events],['row','start','stop','clear','amount','wet']);self.assertEqual(events[-1]['position'],6*65536-1);self.assertEqual(events[3]['graph'],'')
        self.press(4018);ws=self.read('workspace.get');lane=ws['graphLanes'];self.assertEqual(lane['lanes'],[dict(target=first,column=0,name='Track 1')]);self.assertGreater(lane['rect'][2],0)
        texts={c['row']:c['text'] for c in lane['visibleCommands']};self.assertEqual(texts[3],'CLR');self.assertEqual(texts[5],'~W001 99')
        x,y,_,_=lane['rect'];self.mouse(x+16,y+50+18*2+4);self.assertEqual(self.read('workspace.get')['focus'],'graphLanes')
        self.desktop.send(self.desktop.hwnd(self.pid),0x100,0x28);self.desktop.send(self.desktop.hwnd(self.pid),0x100,0x0D);self.idle();self.assertEqual(self.local()['row'],3);self.assertEqual(self.desktop.send(self.control(4003),0x147),3)
        self.press(4018);self.mouse(x+16,y+50+18*2+4);self.desktop.send(self.desktop.hwnd(self.pid),0x100,0x2E);self.idle();self.assertEqual(len(self.commands()),5)
        self.write('history.undo',domain='document');self.assertEqual(self.commands(),events);self.desktop.send(self.desktop.hwnd(self.pid),0x100,0x75);self.assertEqual(self.read('workspace.get')['focus'],'pattern')

    def test_group_lanes_scroll_open_graph_and_minimum_geometry(self):
        graph,first,second=self.setup_graph();group=self.write('mixer.bus.add',kind='group',name='Echoes')['bus']
        self.write('graph.commands.set',pattern=0,lanes=[dict(target=first,count=8),dict(target=group,count=8)]);self.start()
        buses=self.data()['mixer']['buses'];self.select(4001,next(i for i,b in enumerate(buses) if b['id']==group));self.select(4002,7);self.field(4005,4);self.press(4010);self.assertEqual(self.commands()[0]['target'],group)
        self.press(4017);self.assertEqual(self.read('workspace.get')['graphEditor']['graph'],graph)
        user=private_desktop.user;user.SetWindowPos.argtypes=[wintypes.HWND,wintypes.HWND,ctypes.c_int,ctypes.c_int,ctypes.c_int,ctypes.c_int,wintypes.UINT];user.GetClientRect.argtypes=[wintypes.HWND,ctypes.POINTER(wintypes.RECT)];user.GetWindowRect.argtypes=user.GetClientRect.argtypes;user.MapWindowPoints.argtypes=[wintypes.HWND,wintypes.HWND,ctypes.POINTER(wintypes.POINT),wintypes.UINT]
        scale=self.read('workspace.get')['dpi']/96;private_desktop.check(user.SetWindowPos(self.window(),None,0,0,int(660*scale),int(660*scale),0x16));rect=wintypes.RECT();user.GetClientRect(self.window(),ctypes.byref(rect))
        for identifier in range(4001,4020):
            child=wintypes.RECT();user.GetWindowRect(self.control(identifier),ctypes.byref(child));points=(wintypes.POINT*2)(wintypes.POINT(child.left,child.top),wintypes.POINT(child.right,child.bottom));user.MapWindowPoints(None,self.window(),points,2)
            self.assertGreaterEqual(points[0].x,0,identifier);self.assertGreaterEqual(points[0].y,0,identifier);self.assertLessEqual(points[1].x,rect.right,identifier);self.assertLessEqual(points[1].y,rect.bottom,identifier)
        self.press(4018);self.main_command(501)
        for _ in range(15):self.desktop.send(self.desktop.hwnd(self.pid),0x100,0x27)
        lane=self.read('workspace.get')['graphLanes'];self.assertEqual(lane['selected'],15);self.assertGreater(lane['first'],0)
        self.write('mixer.bus.remove',bus=group);lane=self.read('workspace.get')['graphLanes'];self.assertEqual(len(lane['lanes']),8);self.assertLess(lane['selected'],8)

    def test_different_patterns_and_deleted_targets_cannot_redirect_draft(self):
        graph,first,second=self.setup_graph();group=self.write('mixer.bus.add',kind='group',name='Temporary group')['bus']
        pattern=self.write('pattern.create',rows=64)['pattern'];other=dict(target=second,graph=graph,position=65536,column=0,kind='start')
        self.write('graph.commands.set',pattern=pattern,lanes=[dict(target=second,count=1)],commands=[other]);retained=self.commands();self.start()
        buses=self.data()['mixer']['buses'];self.select(4001,next(i for i,b in enumerate(buses) if b['id']==group));self.field(4007,44)
        self.write('mixer.bus.remove',bus=group);before=self.doc();self.press(4010);self.assertEqual(self.doc(),before);self.press(4013);self.assertTrue(self.local()['draft']);self.assertEqual(self.text(4007),'44');self.assertIn('unavailable',self.local()['status'])
        self.press(4014);self.press(4010);self.assertEqual([c for c in self.commands() if c['pattern']==retained[0]['pattern']],retained)

    @unittest.skipUnless(os.environ.get('SCREAMSEQ_TEST_PLUGIN_CACHE'),'installed Contourtonist')
    def test_installed_plugin_commands_native_save_reopen_and_partition_render(self):
        graph,first,second=self.setup_graph();descriptor=next(p for p in self.read('plugin.discover',format='VST3') if p['classID']=='ABCDEF019182FAEB416C736743746E31')
        self.write('plugin.add',descriptor=descriptor);input=self.data()['library'][0]['nodes'][0]['id'];self.write('graph.node.add',graph=graph,kind='plugin',slot=0,insertAfter=input)
        opaque=self.read('graph.get',includeState=True)['library'][0]
        self.start();buses=self.data()['mixer']['buses'];self.select(4001,len(buses)-1);self.select(4003,1);self.press(4010)
        self.field(4005,2);self.select(4003,5);self.field(4008,45);self.field(4006,37.5);self.press(4010)
        self.field(4005,4);self.select(4003,2);self.press(4009);self.press(4010)
        events=self.commands();path=self.folder/'native-command-lifecycle.screamseq';self.write('document.save',path=str(path));self.write('document.open',path=str(path),discard=True)
        self.assertEqual(self.commands(),events);self.assertEqual(self.read('graph.get',includeState=True)['library'][0],opaque)
        report=self.folder/'graph-command-render.json';result=subprocess.run([os.environ['SCREAMSEQ_TEST_EXE'],'--offline-hosted-test','--project',str(path),'--vst3-test-cache',str(self.cache),'--report',str(report)],timeout=90)
        if os.environ.get('SCREAMSEQ_COMMAND_EVIDENCE_DIR'):
            directory=Path(os.environ['SCREAMSEQ_COMMAND_EVIDENCE_DIR']);directory.mkdir(parents=True,exist_ok=True)
            for file in [path,report]:
                if file.exists():shutil.copy2(file,directory/file.name)
        self.assertEqual(result.returncode,0,report.read_text() if report.exists() else 'no render report');evidence=json.loads(report.read_text());self.assertTrue(evidence['finite']);self.assertTrue(evidence['documentUnchanged']);self.assertGreater(evidence['energy'],1);self.assertLess(evidence['maxPartitionDelta'],1e-6)


if __name__=='__main__':unittest.main()
