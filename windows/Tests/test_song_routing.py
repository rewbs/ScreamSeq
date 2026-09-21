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
from client import Client, TransportError


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
    read=support.GraphMixerAppTests.read
    write=support.GraphMixerAppTests.write
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
        end=time.monotonic()+8;quiet=None
        while time.monotonic()<end:
            ws=self.read('workspace.get')
            if ws['documentBusy'] or ws['songRouting'].get('pending'):quiet=None
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
    def start(self):self.idle();self.main_command(417);self.idle();self.assertTrue(self.local()['visible'])
    def setup_mixer(self):
        self.write('mixer.enable');b=self.buses();return b[0]['id'],b[1]['id'],b[-1]['id']
    def route(self,kind,source,target,gain=None,input=None,output=None):
        self.select(3804,kind);self.choose_node(3805,source);self.choose_node(3806,target)
        for identifier,value in [(3807,gain),(3808,input),(3809,output)]:
            if value is not None:self.field(identifier,value)

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
        control=private_desktop.user.GetDlgItem(self.desktop.hwnd(self.pid),300)
        self.assertEqual(self.desktop.send(control,0x188),1)
        self.record('song-routing-scene',canvas=self.local()['canvas'],graph=self.read('graph.get',includeState=False))

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
        self.write('document.patch',title='External edit');before=self.doc();self.press(3813);self.assertEqual(self.doc(),before);self.assertTrue(self.local()['stale']);self.assertIn('Song changed',self.local()['status'])
        self.press(3815);self.choose_wire(lambda a:a.get('kind')=='send' and a.get('source')==first and a['index']==0);self.press(3814);self.assertEqual(self.bus(first)['sends'],[untouched])

    def test_insert_order_layout_keyboard_mouse_history_and_reopen(self):
        first,_,master=self.setup_mixer();a=self.add_gain();b=self.add_gain();self.start();self.choose_node(3802,first);self.select(3823,1)
        self.press(3826);self.press(3826);self.assertEqual(self.bus(first)['inserts'],[a,b]);self.select(3824,1);self.press(3828);self.assertEqual(self.bus(first)['inserts'],[b,a])
        self.press(3827);self.assertEqual(self.bus(first)['inserts'],[a]);self.assertEqual(self.node('plugin:'+b)['bus'],master)
        self.write('history.undo',domain='document');self.press(3815);self.assertEqual(self.bus(first)['inserts'],[b,a]);self.select(3823,0)
        self.choose_node(3802,first);before=self.doc();x=self.node(first)['x'];self.desktop.send(self.window(),0x100,0x27);self.assertTrue(self.local()['layoutDraft']);self.assertEqual(self.doc(),before);self.assertEqual(self.node(first)['x'],x+4)
        self.assertFalse(private_desktop.user.IsWindowEnabled(self.control(3812)))
        self.press(3820);self.start();self.assertTrue(self.local()['layoutDraft']);self.press(3835)
        positions=self.read('graph.get',includeState=False)['layout'];self.assertEqual(next(p for p in positions if p['node']==first)['x'],x+4)
        self.write('history.undo',domain='document');self.assertEqual(self.read('graph.get',includeState=False)['layout'],[]);self.write('history.redo',domain='document');self.press(3815)
        self.choose_node(3802,first);rect=self.node(first)['rect'];scale=self.read('workspace.get')['dpi']/96
        def mouse(message,x,y):self.desktop.send(self.window(),message,1 if message!=0x202 else 0,(int(x*scale)&65535)|((int(y*scale)&65535)<<16))
        mx,my=rect[0]+12,rect[1]+10;mouse(0x201,mx,my);mouse(0x200,mx+20,my+12);mouse(0x202,mx+20,my+12);self.assertTrue(self.local()['layoutDraft']);self.press(3835)
        path=self.folder/'routing.screamseq';self.write('document.save',path=str(path));layout=self.read('graph.get',includeState=False)['layout'];self.write('graph.layout.set',reset=True);self.write('document.open',path=str(path),discard=True);self.press(3815)
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
    def test_layout_keeps_audio_running_and_routing_stops_after_validation(self):
        first,_,_=self.setup_mixer();self.add_gain();path=self.folder/'live.screamseq';self.write('document.save',path=str(path));self.launch(path);self.start();self.choose_node(3802,first)
        before=self.read('transport.get');self.desktop.send(self.window(),0x100,0x27);self.press(3835);time.sleep(.25);after=self.read('transport.get');self.assertTrue(after['audioActive']);self.assertGreater(after['frames'],before['frames']);self.assertFalse(after['fault']);self.assertEqual(after['overruns'],0)
        self.choose_wire(lambda a:a.get('kind')=='output' and a.get('source')==first);self.press(3814);self.assertFalse(self.read('transport.get')['audioActive']);self.assertEqual(self.bus(first)['output'],'')
        self.record('song-routing-live',beforeLayout=before,afterLayout=after,afterRouting=self.read('transport.get'))

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
        before=self.doc();self.press(3819);self.assertTrue(self.local()['layoutDraft']);self.assertEqual(self.doc(),before);self.press(3835);self.assertTrue(self.read('graph.get',includeState=False)['layout']);self.write('history.undo',domain='document');self.assertEqual(self.read('graph.get',includeState=False)['layout'],[])

    def test_unavailable_insert_retains_routing_and_open_does_not_select_another_plugin(self):
        _,_,master=self.setup_mixer();a=self.add_gain();b=self.add_gain();self.write('mixer.bus.set',bus=master,inserts=[b]);self.write('plugin.remove',slot=1);self.start()
        self.assertEqual(self.node('plugin:'+b)['bus'],master);self.choose_node(3802,'plugin:'+b);before=self.doc();self.press(3821)
        self.assertEqual(self.doc(),before);self.assertIn('unavailable',self.local()['status']);self.assertEqual(self.local()['selected'],'plugin:'+b)
        self.select(3823,1);self.press(3827);self.assertNotIn(b,self.bus(master)['inserts']);self.write('history.undo',domain='document');self.assertEqual(self.bus(master)['inserts'],[b]);self.assertEqual(self.doc()['data']['nativePlugins'][0]['instanceID'],a)
