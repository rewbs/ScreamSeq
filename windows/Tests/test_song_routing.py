"""Native song routing, including cross-view targets and shared history."""
import ctypes
from ctypes import wintypes
import json
import hashlib
import os
from pathlib import Path
import plistlib
import tempfile
import time
import unittest
import private_desktop
import test_graph_mixer_app as support
from client import Client, ApiError, TransportError


class SongRoutingTests(unittest.TestCase):
    def setUp(self):
        self.folder=Path(self.enterContext(tempfile.TemporaryDirectory(prefix='song-routing-',dir=os.environ['TMPDIR'])))
        self.desktop=self.enterContext(private_desktop.PrivateDesktop());self.cache=self.folder/'plugins.json';modules={}
        for key in ('SCREAMSEQ_TEST_PROVIDER_CACHE','SCREAMSEQ_TEST_PLUGIN_CACHE','SCREAMSEQ_TEST_INSTRUMENT_CACHE'):
            if os.environ.get(key):
                for item in json.loads(Path(os.environ[key]).read_text(encoding='utf-8-sig')):modules[item['path']]=item
        self.cache.write_text(json.dumps(list(modules.values())))
        self.launch()

    def launch(self,project=None):
        args=[os.environ['SCREAMSEQ_TEST_EXE'],'--audio-test-silent' if project else '--inspection','--automation','--seconds','120','--vst3-test-cache',str(self.cache)]
        if project:args+=['--project',str(project),'--audio-test-allow-stop']
        self.pid=self.desktop.launch(args);self.client=Client(r'\\.\pipe\ScreamSeq.Api.'+str(self.pid),timeout=20)
        for _ in range(100):
            try:
                self.doc()
                if not project or self.read('transport.get')['audioActive']:return
            except TransportError:pass
            time.sleep(.1)
        self.fail('routing app did not start')

    doc=support.GraphMixerAppTests.doc
    def read(self,method,**fields):
        deadline=time.monotonic()+8
        while True:
            try:return support.GraphMixerAppTests.read(self,method,**fields)
            except ApiError as error:
                # Reads can overlap the retained graph/parameter refresh. Never
                # replay writes or uncertain deliveries through this helper.
                if error.code!=-32002 or not method.endswith('.get') or time.monotonic()>=deadline:raise
                time.sleep(.02)
    def write(self,method,**fields):
        self.idle()
        return support.GraphMixerAppTests.write(self,method,**fields)
    add_gain=support.GraphMixerAppTests.add_gain
    def main_command(self,identifier):
        hwnd=self.desktop.hwnd(self.pid);self.desktop.send(hwnd,0x111,identifier,private_desktop.user.GetDlgItem(hwnd,identifier))
    def local(self):return self.read('workspace.get')['songRouting']
    def window(self):
        found=[]
        @private_desktop.callback
        def visit(hwnd,_):
            pid=wintypes.DWORD();private_desktop.user.GetWindowThreadProcessId(hwnd,ctypes.byref(pid));name=ctypes.create_unicode_buffer(100);private_desktop.user.GetClassNameW(hwnd,name,100)
            if pid.value==self.pid and name.value=='ScreamSeq.SongRouting':found.append(hwnd)
            return True
        private_desktop.check(private_desktop.user.EnumDesktopWindows(self.desktop.desktop,visit,0));self.assertEqual(len(found),1);return found[0]
    def control(self,identifier):
        hwnd=private_desktop.user.GetDlgItem(self.window(),identifier);self.assertTrue(hwnd);return hwnd
    def idle(self):
        end=time.monotonic()+8;quiet=None;revision=self.doc()['revision']
        while time.monotonic()<end:
            ws=self.read('workspace.get')
            routing=ws['songRouting']
            refresh_due=routing.get('visible') and routing.get('stale') and not any(routing.get(k) for k in ('draft','layoutDraft','completion','readbackNeedsReload'))
            parameter_due=any(p.get('visible') and not any(p.get(k) for k in ('dirty','completion','needsReload')) and (p.get('pending') or p.get('revision')!=revision) for p in ws.get('graphPluginParameters',[]))
            if ws['documentBusy'] or routing.get('pending') or refresh_due or parameter_due:quiet=None
            elif quiet is None:quiet=time.monotonic()
            elif time.monotonic()-quiet>=.1:return
            time.sleep(.02)
        self.fail(str(self.local()))
    def press(self,identifier):self.idle();self.desktop.send(self.window(),0x111,identifier,self.control(identifier));self.idle()
    def select(self,identifier,index):
        self.desktop.send(self.control(identifier),0x14E,index);self.desktop.send(self.window(),0x111,identifier|(1<<16),self.control(identifier));self.idle()
    def field(self,identifier,value):
        text=ctypes.create_unicode_buffer(str(value));self.desktop.send(self.control(identifier),0xC,0,ctypes.addressof(text))
    def nodes(self):return self.local()['canvas']['nodes']
    def edges(self):return self.local()['canvas']['edges']
    def node(self,identifier):return next(n for n in self.nodes() if n['id']==identifier)
    def choose_node(self,control,identifier):self.select(control,next(i for i,n in enumerate(self.nodes()) if n['id']==identifier))
    def choose_wire(self,predicate):self.select(3803,next(i+1 for i,e in enumerate(self.edges()) if predicate(e['action'])))
    def buses(self):return self.read('mixer.get')['buses']
    def record(self,name,**data):
        root=os.environ.get('SCREAMSEQ_ROUTING_EVIDENCE_DIR')
        if root:
            folder=Path(root);self.assertTrue(folder.is_absolute());folder.mkdir(parents=True,exist_ok=True)
            with open(os.environ['SCREAMSEQ_TEST_EXE'],'rb') as executable:data['executableSHA256']=hashlib.file_digest(executable,'sha256').hexdigest()
            data.update(privateDesktop=True,foregroundVisualQualified=False)
            (folder/(name+'.json')).write_text(json.dumps(data,indent=2),encoding='utf-8')
    def bus(self,identifier):return next(b for b in self.buses() if b['id']==identifier)
    def start(self):
        self.idle();self.main_command(417);self.idle();state=self.read('workspace.get')
        self.assertTrue(state['songRouting']['visible'],json.dumps(state,ensure_ascii=False))
    def setup_mixer(self):
        self.write('mixer.enable');b=self.buses();return b[0]['id'],b[1]['id'],b[-1]['id']
    def route(self,kind,source,target,gain=None,input=None,output=None):
        self.select(3804,kind);self.choose_node(3805,source);self.choose_node(3806,target)
        for identifier,value in [(3807,gain),(3808,input),(3809,output)]:
            if value is None:continue
            if identifier==3808 and kind in (4,6):
                self.select(3837,self.local()['inputPorts']['indices'].index(value)+1)
            elif identifier==3809 and kind in (5,6):
                self.select(3838,self.local()['outputPorts']['indices'].index(value)+1)
            else:self.field(identifier,value)

    def test_graph_click_jitter_and_completed_moves_do_not_lock_selection(self):
        first,second,_=self.setup_mixer();self.start();scale=self.read('workspace.get')['dpi']/96
        def mouse(message,x,y):self.desktop.send(self.window(),message,1 if message!=0x202 else 0,(round(x*scale)&65535)|((round(y*scale)&65535)<<16))
        self.choose_node(3802,first);before=self.doc();r=self.node(first)['rect'];x,y=r[0]+r[2]/2,r[1]+r[3]/2
        mouse(0x201,x,y);mouse(0x200,x+1,y+1);mouse(0x202,x+1,y+1);self.idle()
        self.assertFalse(self.local()['layoutDraft']);self.assertEqual(self.doc(),before)
        self.choose_node(3802,second);self.assertEqual(self.local()['selected'],second)
        layouts=[]
        for node in (first,second):
            self.choose_node(3802,node);r=self.node(node)['rect'];x,y=r[0]+r[2]/2,r[1]+r[3]/2
            mouse(0x201,x,y);mouse(0x200,x+24,y+14);mouse(0x202,x+24,y+14);self.idle()
            self.assertFalse(self.local()['layoutDraft']);layouts.append(self.read('graph.get',includeState=False)['layout'])
        self.assertNotEqual(layouts[0],layouts[1]);self.write('history.undo',domain='all');self.assertEqual(self.read('graph.get',includeState=False)['layout'],layouts[0])
        self.write('history.undo',domain='all');self.assertEqual(self.read('graph.get',includeState=False)['layout'],[])
        self.write('history.redo',domain='all');self.write('history.redo',domain='all');self.assertEqual(self.read('graph.get',includeState=False)['layout'],layouts[1])

    def test_graph_wheel_navigation_is_anchored_and_new_actions_fit(self):
        first,_,_=self.setup_mixer();self.start();before=self.doc();saved=self.read('graph.get',includeState=False)
        user=private_desktop.user;owner=self.window();scale=self.read('workspace.get')['dpi']/96
        user.ClientToScreen.argtypes=[wintypes.HWND,ctypes.POINTER(wintypes.POINT)]
        v=self.local()['canvas']['viewport'];point=(v[0]+v[2]/2,v[1]+v[3]/2)
        screen=wintypes.POINT(round(point[0]*scale),round(point[1]*scale));self.assertTrue(user.ClientToScreen(owner,ctypes.byref(screen)))
        packed=(screen.x&65535)|((screen.y&65535)<<16)
        old=self.node(first)['rect'];zoom=self.local()['canvas']['zoom']
        self.desktop.send(owner,0x20A,(120<<16)|8,packed)  # Ctrl+wheel, cursor-anchored zoom.
        fresh=self.node(first)['rect'];self.assertGreater(self.local()['canvas']['zoom'],zoom)
        # Compare a world point in normalized node coordinates (DPI rounding bounded).
        for axis in (0,1):self.assertAlmostEqual((point[axis]-old[axis])/old[2+axis],(point[axis]-fresh[axis])/fresh[2+axis],delta=.02)
        self.desktop.send(owner,0x20A,((-120&65535)<<16),packed)
        panned=self.node(first)['rect'];self.assertAlmostEqual(panned[1]-fresh[1],-48,delta=.1)
        self.desktop.send(owner,0x20A,(120<<16)|4,packed)  # Shift+wheel pans horizontally.
        horizontal=self.node(first)['rect'];self.assertAlmostEqual(horizontal[0]-panned[0],48,delta=.1)
        self.assertEqual(self.doc(),before);self.assertEqual(self.read('graph.get',includeState=False),saved)
        user.SetWindowPos.argtypes=[wintypes.HWND,wintypes.HWND,ctypes.c_int,ctypes.c_int,ctypes.c_int,ctypes.c_int,wintypes.UINT]
        user.GetWindowRect.argtypes=[wintypes.HWND,ctypes.POINTER(wintypes.RECT)]
        self.assertTrue(user.SetWindowPos(owner,None,0,0,round(1040*scale),round(680*scale),0x16))
        frame=wintypes.RECT();user.GetWindowRect(owner,ctypes.byref(frame));previous=None
        for identifier in (3822,3839,3840,3841,3815,3820):
            rect=wintypes.RECT();user.GetWindowRect(self.control(identifier),ctypes.byref(rect))
            self.assertGreater(rect.right,rect.left);self.assertGreaterEqual(rect.left,frame.left);self.assertLessEqual(rect.right,frame.right)
            if previous is not None:self.assertGreaterEqual(rect.left,previous)
            previous=rect.right

    def test_new_bus_socket_cable_preserves_main_and_selected_handle_rewires_only_send(self):
        first,second,master=self.setup_mixer();third=self.buses()[2]['id']
        group=self.write('mixer.bus.add',kind='return',name='Branch')['bus']
        old=dict(target=third,gainDB=-12,preFader=True,enabled=False)
        self.write('mixer.sends.set',bus=first,sends=[old]);self.start()
        scale=self.read('workspace.get')['dpi']/96
        def mouse(message,p):
            self.desktop.send(self.window(),message,1 if message!=0x202 else 0,(round(p[0]*scale)&65535)|((round(p[1]*scale)&65535)<<16))
        def socket(node,output):
            x,y,w,h=self.node(node)['rect'];return (x+w-2 if output else x+2,y+h/2)
        # Start at an already connected input: this still adds a branch.
        mouse(0x201,socket(second,False));mouse(0x200,socket(first,True));mouse(0x202,socket(first,True));self.idle()
        self.assertEqual(self.bus(first)['output'],master)
        self.assertEqual(self.bus(first)['sends'],[old,dict(target=second,gainDB=0,preFader=False,enabled=True)])
        self.choose_wire(lambda a:a.get('kind')=='send' and a.get('source')==first and a.get('index')==1)
        selected=next(e for e in self.edges() if e['action'].get('kind')=='send' and e['action'].get('source')==first and e['action'].get('index')==1)
        mouse(0x201,selected['targetHandle']);mouse(0x200,socket(group,False));mouse(0x202,socket(group,False));self.idle()
        self.assertEqual(self.bus(first)['sends'],[old,dict(target=group,gainDB=0,preFader=False,enabled=True)])
        self.assertEqual(self.bus(first)['output'],master)
        self.write('history.undo',domain='document');self.assertEqual(self.bus(first)['sends'][1]['target'],second)

    def test_plugin_output_add_and_cut_preserve_other_destinations(self):
        first,_,master=self.setup_mixer();plugin=self.add_gain()
        self.write('mixer.bus.set',bus=first,inserts=[plugin])
        one=self.write('mixer.bus.add',kind='return',name='Branch one')['bus'];two=self.write('mixer.bus.add',kind='return',name='Branch two')['bus'];self.start()
        for target in (one,two):
            self.select(3803,0);self.route(5,'plugin:'+plugin,target,output=0);self.press(3812)
        routes=[r for r in self.read('mixer.get')['instruments'] if r['plugin']==plugin and r['output']==0]
        self.assertEqual({r['target'] for r in routes},{one,two})
        self.select(3803,next(i+1 for i,e in enumerate(self.edges()) if e['action'].get('kind')=='plugin-output' and e['action'].get('plugin')==plugin and e['target']==one))
        self.press(3814)
        self.assertEqual([r['target'] for r in self.read('mixer.get')['instruments'] if r['plugin']==plugin and r['output']==0],[two])
        self.write('history.undo',domain='document')
        self.assertEqual([r for r in self.read('mixer.get')['instruments'] if r['plugin']==plugin and r['output']==0],routes)
        path=self.folder/'parallel-outputs.screamseq';self.write('document.save',path=str(path));self.write('document.open',path=str(path),discard=True)
        self.assertEqual([r for r in self.read('mixer.get')['instruments'] if r['plugin']==plugin and r['output']==0],routes)

    def test_overview_stages_default_inserts_filter_and_connections(self):
        first,second,master=self.setup_mixer();a=self.add_gain();b=self.add_gain();group=self.write('mixer.bus.add',kind='group',name='Drum group')['bus']
        self.write('mixer.bus.set',bus=first,output=group,inserts=[a]);g=self.write('graph.create',name='Shared chain')['graph'];self.write('graph.assign',target=first,graph=g)
        self.write('graph.commands.set',pattern=0,lanes=[dict(target=first,count=2)],commands=[dict(target=first,graph=g,position=0,column=0,kind='row'),dict(target=first,graph=g,position=0,column=1,kind='start')])
        self.start();self.assertEqual(self.node('plugin:'+a)['bus'],first);self.assertEqual(self.node('plugin:'+b)['bus'],master)
        self.assertEqual({n['id'] for n in self.nodes() if n['graph']==g},{f'graph:{first}:{role}:{g}' for role in ('Row','Persistent','Ordinary')})
        self.select(3801,next(i+1 for i,bus in enumerate(self.buses()) if bus['id']==first))
        ids={n['id'] for n in self.nodes()};self.assertTrue({first,group,master}<=ids);self.assertNotIn(second,ids)
        self.assertTrue(any(e['action']==dict(kind='output',source=first) for e in self.edges()))
        self.select(3801,0);self.choose_node(3802,'plugin:'+b);self.press(3821)
        inspector=next(p for p in self.read('workspace.get')['graphPluginParameters'] if p['plugin']==b)
        self.assertTrue(inspector['visible']);self.assertEqual(inspector['selected'],0)
        self.assertEqual(inspector['parameters'],self.read('plugin.parameters.get',plugin=b));self.assertGreater(len(inspector['parameters']),0)
        self.record('song-routing-scene',canvas=self.local()['canvas'],graph=self.read('graph.get',includeState=False))

    def test_command_stages_cannot_change_ordinary_assignment_or_regular_inserts(self):
        first, _, _ = self.setup_mixer()
        effect = self.add_gain()
        self.write('mixer.bus.set', bus=first, inserts=[effect])
        ordinary = self.write('graph.create', name='Ordinary recipe')['graph']
        row = self.write('graph.create', name='Row recipe')['graph']
        persistent = self.write('graph.create', name='Persistent recipe')['graph']
        self.write('graph.assign', target=first, graph=ordinary, amount=.25, wet=.75)
        self.write('graph.commands.set', pattern=0, lanes=[dict(target=first, count=2)], commands=[
            dict(target=first, graph=row, position=0, column=0, kind='row'),
            dict(target=first, graph=persistent, position=0, column=1, kind='start')])
        self.start()
        before = self.doc()
        original = self.read('graph.get', includeState=False)
        for role, graph, choice in [('Row', row, 2), ('Persistent', persistent, 3)]:
            with self.subTest(role=role):
                key = f'graph:{first}:{role}:{graph}'
                self.choose_node(3802, key)
                node = self.node(key)
                self.assertEqual(node['stageRole'], role.lower())
                self.assertFalse(node['canAssignGraph'])
                self.assertFalse(node['canEditInserts'])
                self.select(3823, 2)
                self.assertEqual(self.desktop.send(self.control(3830), 0x147), choice)
                for control in (3830, 3831, 3832, 3833, 3834):
                    self.assertFalse(private_desktop.user.IsWindowEnabled(self.control(control)))
                # Posted commands bypass disabled HWNDs, so the handler must
                # independently refuse them before issuing any document write.
                for command in (3833, 3834):
                    self.press(command)
                    self.assertEqual(self.doc(), before)
                    self.assertIn('pattern commands', self.local()['status'])
                self.select(3823, 1)
                for control in (3824, 3825, 3826, 3827, 3828, 3829):
                    self.assertFalse(private_desktop.user.IsWindowEnabled(self.control(control)))
                for command in (3826, 3827, 3828, 3829):
                    self.press(command)
                    self.assertEqual(self.doc(), before)
                self.press(3821)
                self.assertEqual(self.read('workspace.get')['graphEditor']['graph'], graph)
                self.assertEqual(self.read('graph.get', includeState=False), original)
        self.choose_node(3802, f'graph:{first}:Ordinary:{ordinary}')
        self.select(3823, 2)
        self.assertTrue(private_desktop.user.IsWindowEnabled(self.control(3834)))
        self.press(3834)
        self.assertEqual(self.read('graph.get', includeState=False)['assignments'], [])
        self.assertEqual(self.bus(first)['inserts'], [effect])
        self.write('history.undo', domain='document')
        self.assertEqual(self.read('graph.get', includeState=False), original)

    def test_native_send_update_preserves_other_routes_dry_stale_and_retained_draft(self):
        first,second,master=self.setup_mixer();g1=self.write('mixer.bus.add',kind='group',name='A')['bus'];g2=self.write('mixer.bus.add',kind='return',name='B')['bus'];self.start()
        before=self.doc();self.route(1,first,g1,gain=-9);self.press(3810);self.press(3836);self.assertEqual(self.doc(),before)
        self.press(3812);self.assertEqual(self.bus(first)['sends'],[dict(target=g1,gainDB=-9.0,preFader=True,enabled=True)])
        self.write('mixer.sends.set',bus=first,sends=self.bus(first)['sends']+[dict(target=g2,gainDB=-18,enabled=False)]);self.press(3815)
        untouched=self.bus(first)['sends'][1];self.choose_wire(lambda a:a.get('kind')=='send' and a.get('source')==first and a['index']==0)
        self.field(3807,-12);self.press(3813);self.assertEqual(self.bus(first)['sends'][1],untouched);self.assertEqual(self.bus(first)['sends'][0]['gainDB'],-12)
        self.write('history.undo',domain='document');self.assertEqual(self.bus(first)['sends'][0]['gainDB'],-9);self.press(3815)
        self.choose_wire(lambda a:a.get('kind')=='send' and a.get('source')==first and a['index']==0);self.choose_node(3806,first);before=self.doc();self.press(3813)
        self.assertEqual(self.doc(),before);self.assertTrue(self.local()['draft'])
        self.choose_node(3802,second);self.assertEqual(self.local()['selected'],'')
        self.press(3820);self.start();self.assertTrue(self.local()['draft'])
        # The failed cycle has no attributed return: inspect once, then reload.
        # Neither Review nor a refused repeat may mutate the song or history.
        retained=self.local()['completion'];self.assertIsNotNone(retained)
        self.press(3813);self.assertEqual(self.local()['completion'],retained);self.assertEqual(self.doc(),before)
        self.press(3815);self.assertIsNone(self.local()['completion']);self.assertTrue(self.local()['readbackNeedsReload']);self.assertTrue(self.local()['draft']);self.assertEqual(self.doc(),before)
        self.press(3815);self.assertFalse(self.local()['readbackNeedsReload'])
        self.choose_wire(lambda a:a.get('kind')=='send' and a.get('source')==first and a['index']==0);self.field(3807,-13)
        self.write('document.patch',title='External edit');before=self.doc();self.press(3813);self.assertEqual(self.doc(),before);self.assertTrue(self.local()['stale']);self.assertIn('Song changed',self.local()['status'])
        self.press(3815);self.choose_wire(lambda a:a.get('kind')=='send' and a.get('source')==first and a['index']==0);self.press(3814);self.assertEqual(self.bus(first)['sends'],[untouched])

    def test_insert_order_layout_keyboard_mouse_history_and_reopen(self):
        first,_,master=self.setup_mixer();a=self.add_gain();b=self.add_gain();self.start();self.choose_node(3802,first);self.select(3823,1)
        self.press(3826);self.press(3826);self.assertEqual(self.bus(first)['inserts'],[a,b]);self.select(3824,1);self.press(3828);self.assertEqual(self.bus(first)['inserts'],[b,a])
        self.press(3827);self.assertEqual(self.bus(first)['inserts'],[a]);self.assertEqual(self.node('plugin:'+b)['bus'],master)
        self.write('history.undo',domain='document');self.press(3815);self.assertEqual(self.bus(first)['inserts'],[b,a]);self.select(3823,0)
        self.choose_node(3802,first);before=self.doc();x=self.node(first)['x'];self.desktop.send(self.window(),0x100,0x27);self.assertTrue(self.local()['layoutDraft']);self.assertEqual(self.doc(),before);self.assertEqual(self.node(first)['x'],x+4)
        self.assertFalse(private_desktop.user.IsWindowEnabled(self.control(3812)))
        self.desktop.send(self.window(),0x101,0x27);self.idle();self.assertFalse(self.local()['layoutDraft']);self.press(3820);self.start();self.assertFalse(self.local()['layoutDraft'])
        positions=self.read('graph.get',includeState=False)['layout'];self.assertEqual(next(p for p in positions if p['node']==first)['x'],x+4)
        self.write('history.undo',domain='document');self.assertEqual(self.read('graph.get',includeState=False)['layout'],[]);self.write('history.redo',domain='document');self.press(3815)
        self.choose_node(3802,first);rect=self.node(first)['rect'];scale=self.read('workspace.get')['dpi']/96
        def mouse(message,x,y):self.desktop.send(self.window(),message,1 if message!=0x202 else 0,(int(x*scale)&65535)|((int(y*scale)&65535)<<16))
        mx,my=rect[0]+12,rect[1]+10;mouse(0x201,mx,my);mouse(0x200,mx+20,my+12);mouse(0x202,mx+20,my+12);self.idle();self.assertFalse(self.local()['layoutDraft'])
        path=self.folder/'routing.screamseq';self.write('document.save',path=str(path));layout=self.read('graph.get',includeState=False)['layout'];self.write('graph.layout.set',reset=True);self.write('document.open',path=str(path),discard=True);self.start();self.press(3815)
        self.assertEqual(self.read('graph.get',includeState=False)['layout'],layout);self.assertEqual(self.bus(first)['inserts'],[b,a])

    def test_bus_and_sample_instrument_graph_assignment_expand_and_inspect(self):
        first,_,_=self.setup_mixer();number=self.write('instrument.create',sample=1)['instrument'];g=self.write('graph.create',name='Voice space')['graph'];data=self.read('graph.get',includeState=False);instrument=next(i['id'] for i in data['instruments'] if i['index']==number)
        self.start();self.choose_node(3802,'instrument:'+instrument);self.select(3823,2);self.select(3830,1);self.field(3831,.4);self.field(3832,.75);self.press(3833)
        assignment=self.read('graph.get',includeState=False)['instrumentAssignments'][0];self.assertEqual(assignment,dict(target=instrument,graph=g,amount=.4,wet=.75))
        copies=[n for n in self.nodes() if n['instrument']==instrument and n['graph']==g];self.assertEqual(len(copies),sum(b['kind']=='track' for b in self.buses()))
        self.choose_node(3802,first);self.assertTrue(any(n['id']=='instrument-graph:'+instrument+':all' for n in self.nodes()));self.select(3830,1);self.press(3833)
        stage=f'graph:{first}:Ordinary:{g}';self.choose_node(3802,stage);self.press(3821);self.assertEqual(self.read('workspace.get')['graphEditor']['graph'],g)
        self.press(3834);self.assertEqual(self.read('graph.get',includeState=False)['assignments'],[]);self.assertEqual(self.read('graph.get',includeState=False)['instrumentAssignments'],[assignment])

    @unittest.skipUnless(os.environ.get('SCREAMSEQ_TEST_PROVIDER_CACHE'),'native provider fixture')
    def test_native_plugin_sidechains_auxiliary_output_and_disconnection(self):
        first,second,master=self.setup_mixer();descriptor=next(p for p in self.read('plugin.discover',format='VST3') if p['classID']=='5245534F4E414E4350524F4752410001')
        self.write('plugin.add',descriptor=descriptor);plugin=self.doc()['data']['nativePlugins'][0]['instanceID'];self.write('plugin.buses.set',slot=0,inputs=[1]);self.write('mixer.bus.set',bus=master,inserts=[plugin])
        instrument=next(p for p in self.read('plugin.discover',format='VST3') if p['classID']=='5245534F4E414E43494E535452550001');self.write('plugin.add',descriptor=instrument);synth=self.doc()['data']['nativePlugins'][1]['instanceID'];self.write('plugin.buses.set',slot=1,outputs=[1])
        target=self.write('mixer.bus.add',kind='return',name='Auxiliary')['bus'];self.write('mixer.bus.set',bus=target,output=None);self.start()
        self.route(4,first,'plugin:'+plugin,input=1,gain=-6);self.press(3812);side=self.read('mixer.get')['sidechains'];self.assertEqual(side[0]['source'],first)
        self.write('mixer.sidechains.set',plugin=plugin,input=1,sources=[dict(source=first,gainDB=-6),dict(source=second,gainDB=-12,enabled=False)]);self.press(3815)
        self.choose_wire(lambda a:a.get('kind')=='plugin-input' and a['index']==0);self.field(3807,-18);self.press(3813);sides=self.read('mixer.get')['sidechains'];self.assertEqual(sides[0]['gainDB'],-18);self.assertEqual(sides[1]['gainDB'],-12);self.assertFalse(sides[1]['enabled'])
        self.choose_wire(lambda a:a.get('kind')=='plugin-input' and a['index']==0);self.press(3814);self.assertEqual(len(self.read('mixer.get')['sidechains']),1)
        self.select(3803,0);self.route(5,'plugin:'+synth,target,output=1);self.press(3812)
        route=next(r for r in self.read('mixer.get')['instruments'] if r['plugin']==synth);self.assertEqual((route['output'],route['target']),(1,target))
        self.choose_wire(lambda a:a.get('kind')=='plugin-output' and a.get('plugin')==synth and a['output']==1);self.press(3814);route=next(r for r in self.read('mixer.get')['instruments'] if r['plugin']==synth);self.assertEqual(route['target'],'')
        self.write('history.undo',domain='document');self.assertEqual(next(r for r in self.read('mixer.get')['instruments'] if r['plugin']==synth)['target'],target)

    def test_graph_ports_preserve_other_routes_and_socket_drag_uses_selected_kind(self):
        first,second,master=self.setup_mixer();third=self.buses()[2]['id'];g=self.write('graph.create',name='Ports')['graph']
        definition=self.read('graph.get',includeState=False)['library'][0];a,b=definition['nodes'][0]['id'],definition['nodes'][1]['id'];definition['audio'].append(dict(source=a,target=b,output=1,input=1,gain=1));self.write('graph.update',definition=definition);self.write('graph.assign',target=first,graph=g)
        group=self.write('mixer.bus.add',kind='return',name='Graph output')['bus'];self.start();self.route(2,second,first,input=1,gain=-6);self.press(3810);self.press(3812)
        initial=self.read('graph.get',includeState=False)['inputs'];self.assertEqual(initial,[dict(source=second,target=first,input=1,gainDB=-6.0,preFader=True)])
        self.write('graph.routes.set',inputs=initial+[dict(source=third,target=first,input=1,gainDB=-12,preFader=False)]);self.press(3815)
        self.choose_wire(lambda action:action.get('kind')=='graph-input' and action['index']==0);self.field(3807,-9);self.press(3813);inputs=self.read('graph.get',includeState=False)['inputs'];self.assertEqual(inputs[0]['gainDB'],-9);self.assertEqual(inputs[1]['source'],third)
        self.select(3803,0);self.route(3,first,master,output=1);self.press(3812);self.choose_wire(lambda action:action.get('kind')=='graph-output');self.choose_node(3806,group);self.press(3813)
        self.assertEqual(self.read('graph.get',includeState=False)['outputs'],[dict(source=first,target=group,output=1)]);self.assertEqual(self.read('graph.get',includeState=False)['inputs'],inputs)
        self.choose_wire(lambda action:action.get('kind')=='graph-input' and action['index']==0);self.press(3814);self.assertEqual(self.read('graph.get',includeState=False)['inputs'],[inputs[1]])
        # Real output-handle drag obeys Send mode and the pending gain field.
        self.select(3803,0);self.select(3804,1);self.field(3807,-15);rect=self.node(second)['rect'];to=self.node(group)['rect'];scale=self.read('workspace.get')['dpi']/96
        def mouse(message,x,y):self.desktop.send(self.window(),message,1 if message!=0x202 else 0,(round(x*scale)&65535)|((round(y*scale)&65535)<<16))
        mouse(0x201,rect[0]+rect[2]-2,rect[1]+rect[3]/2);mouse(0x200,to[0]+3,to[1]+to[3]/2);mouse(0x202,to[0]+3,to[1]+to[3]/2);self.idle()
        self.assertTrue(any(s['target']==group and s['gainDB']==-15 for s in self.bus(second)['sends']))

    @unittest.skipUnless(os.environ.get('SCREAMSEQ_TEST_INSTRUMENT_CACHE'),'installed Surge XT')
    def test_installed_instrument_output_preserves_sound_aliases_and_saved_project(self):
        _,_,master=self.setup_mixer();descriptor=next(p for p in self.read('plugin.discover',format='VST3') if p['name']=='Surge XT');self.write('plugin.add',descriptor=descriptor);plugin=self.doc()['data']['nativePlugins'][0]['instanceID']
        number=self.write('instrument.create',empty=True)['instrument'];self.write('plugin.instruments.set',plugin=plugin,assignments=[dict(instrument=number,channel=3)]);baseline=self.read('plugin.state.get',slot=0)['data'];group=self.write('mixer.bus.add',kind='group',name='Synth bus')['bus'];self.start()
        self.assertTrue(any(e['action'].get('kind')=='plugin-output' and e['target']==master for e in self.edges()));self.route(5,'plugin:'+plugin,group,output=0);self.press(3812)
        self.assertEqual(self.read('plugin.state.get',slot=0)['data'],baseline);self.assertEqual(self.doc()['data']['nativePlugins'][0]['instrumentAssignments'],[dict(instrument=number,channel=3)])
        path=self.folder/'surge-routing.screamseq';self.write('document.save',path=str(path));self.write('history.undo',domain='document');self.write('document.open',path=str(path),discard=True);self.press(3815)
        self.assertTrue(any(e['action'].get('plugin')==plugin and e['target']==group for e in self.edges()));self.assertEqual(self.read('plugin.state.get',slot=0)['data'],baseline)

    @unittest.skipUnless(os.environ.get('SCREAMSEQ_TEST_LIVE_AUDIO')=='1','owned silent WASAPI')
    def test_layout_and_validated_routing_keep_audio_running_through_history(self):
        first,_,_=self.setup_mixer();self.add_gain();path=self.folder/'live.screamseq';self.write('document.save',path=str(path));self.launch(path);self.start();self.choose_node(3802,first)
        before=self.read('transport.get');self.desktop.send(self.window(),0x100,0x27);self.desktop.send(self.window(),0x101,0x27);self.idle();time.sleep(.25);after=self.read('transport.get');self.assertTrue(after['audioActive']);self.assertGreater(after['frames'],before['frames']);self.assertFalse(after['fault']);self.assertEqual(after['overruns'],0)
        def adopted(previous, previous_plan):
            deadline=time.monotonic()+8
            while time.monotonic()<deadline:
                current=self.read('transport.get');routing=self.read('graph.signal.get')['routing']
                self.assertTrue(current['audioActive']);self.assertFalse(current['fault']);self.assertEqual(current['overruns'],0)
                self.assertEqual(current['playbackEpoch'],previous['playbackEpoch'])
                self.assertEqual(routing['failedPlan'],0)
                if (current['frames']>previous['frames'] and current['callbacks']>previous['callbacks']
                        and routing['requestedPlan']>previous_plan and routing['renderedPlan']==routing['requestedPlan']
                        and routing['state']=='stable' and not routing['latencyPending']):
                    return current,routing
                time.sleep(.02)
            self.fail(f'Live routing was not adopted: {current}, {routing}')
        original=self.bus(first);layout=self.read('graph.get',includeState=False)['layout']
        plan=self.read('graph.signal.get')['routing']['renderedPlan']
        self.choose_wire(lambda a:a.get('kind')=='output' and a.get('source')==first);self.press(3814)
        disconnected=dict(original,output='');self.assertEqual(self.bus(first),disconnected)
        disconnected_audio,disconnected_plan=adopted(after,plan)
        self.write('history.undo',domain='document');self.assertEqual(self.bus(first),original)
        restored_audio,restored_plan=adopted(disconnected_audio,disconnected_plan['renderedPlan'])
        self.write('history.redo',domain='document');self.assertEqual(self.bus(first),disconnected)
        redone_audio,redone_plan=adopted(restored_audio,restored_plan['renderedPlan'])
        self.assertEqual(self.read('graph.get',includeState=False)['layout'],layout)
        self.record('song-routing-live',beforeLayout=before,afterLayout=after,afterRouting=disconnected_audio,
                    disconnectedPlan=disconnected_plan,afterUndo=restored_audio,undoPlan=restored_plan,
                    afterRedo=redone_audio,redoPlan=redone_plan)

    def test_minimum_native_control_bounds_focus_and_layout_history(self):
        first,_,_=self.setup_mixer();self.start();user=private_desktop.user
        user.SetWindowPos.argtypes=[wintypes.HWND,wintypes.HWND,ctypes.c_int,ctypes.c_int,ctypes.c_int,ctypes.c_int,wintypes.UINT];user.GetWindowRect.argtypes=[wintypes.HWND,ctypes.POINTER(wintypes.RECT)];user.IsWindowVisible.argtypes=[wintypes.HWND];user.IsWindowEnabled.argtypes=[wintypes.HWND]
        scale=self.read('workspace.get')['dpi']/96;self.assertTrue(user.SetWindowPos(self.window(),None,0,0,int(1040*scale),int(680*scale),0x16));frame=wintypes.RECT();user.GetWindowRect(self.window(),ctypes.byref(frame))
        for page in range(3):
            self.select(3823,page)
            for identifier in list(range(3801,3837))+list(range(3900,3914)):
                control=self.control(identifier)
                if not user.IsWindowVisible(control):continue
                rect=wintypes.RECT();user.GetWindowRect(control,ctypes.byref(rect));self.assertGreater(rect.right,rect.left);self.assertGreaterEqual(rect.left,frame.left);self.assertLessEqual(rect.right,frame.right);self.assertLessEqual(rect.bottom,frame.bottom)
        self.choose_node(3802,first);self.assertEqual(self.desktop.focus(self.window()),self.window());self.desktop.send(self.window(),0x100,0x75);self.assertEqual(self.desktop.focus(self.window()),self.control(3802));self.desktop.send(self.control(3802),0x100,0x75);self.assertEqual(self.desktop.focus(self.window()),self.window())
        before=self.doc();self.press(3819);self.assertFalse(self.local()['layoutDraft']);self.assertNotEqual(self.doc(),before);self.assertTrue(self.read('graph.get',includeState=False)['layout']);self.write('history.undo',domain='document');self.assertEqual(self.read('graph.get',includeState=False)['layout'],[])

    def test_unavailable_insert_retains_routing_and_open_does_not_select_another_plugin(self):
        _,_,master=self.setup_mixer();a=self.add_gain();b=self.add_gain();self.write('mixer.bus.set',bus=master,inserts=[b])
        # Explicit plugin.remove now intentionally cleans its routes. Model an
        # imported missing reference instead, preserving the saved insert ID.
        path=self.folder/'unavailable-insert.screamseq';self.write('document.save',path=str(path))
        tree=plistlib.loads(path.read_bytes());native=tree['native']
        self.assertEqual([p['instanceID'] for p in tree['plugins']],[a,b])
        tree['plugins']=[p for p in tree['plugins'] if p['instanceID']!=b]
        path.write_bytes(plistlib.dumps(tree,fmt=plistlib.FMT_BINARY))
        self.assertEqual(plistlib.loads(path.read_bytes())['native'],native)
        self.write('document.open',path=str(path),discard=True);self.start()
        self.assertEqual([p['instanceID'] for p in self.doc()['data']['nativePlugins']],[a]);self.assertEqual(self.bus(master)['inserts'],[b])
        self.assertEqual(self.node('plugin:'+b)['bus'],master);self.choose_node(3802,'plugin:'+b);before=self.doc();self.press(3821)
        self.assertEqual(self.doc(),before);self.assertIn('unavailable',self.local()['status']);self.assertEqual(self.local()['selected'],'plugin:'+b)
        self.select(3823,1);self.press(3827);self.assertNotIn(b,self.bus(master)['inserts']);self.write('history.undo',domain='document');self.assertEqual(self.bus(master)['inserts'],[b]);self.assertEqual(self.doc()['data']['nativePlugins'][0]['instanceID'],a)
