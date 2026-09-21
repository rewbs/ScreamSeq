"""Explicit Windows VST3 relinking, retained identity/state and native controls."""
import base64
import copy
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import plistlib
import shutil
import tempfile
import time
from types import SimpleNamespace
import unittest
import private_desktop
import test_plugins_app as support
import test_editor_app as dialogs
from client import Client, ApiError, TransportError


class PluginPathTests(unittest.TestCase):
    def setUp(self):
        self.folder=Path(self.enterContext(tempfile.TemporaryDirectory(prefix='plugin-path-',dir=os.environ['TMPDIR'])))
        self.desktop=self.enterContext(private_desktop.PrivateDesktop());self.cache=self.folder/'plugins.json'
        modules={}
        for key in ('SCREAMSEQ_TEST_PLUGIN_CACHE','SCREAMSEQ_TEST_INSTRUMENT_CACHE','SCREAMSEQ_TEST_PROVIDER_CACHE'):
            if os.environ.get(key):
                for entry in json.loads(Path(os.environ[key]).read_text(encoding='utf-8-sig')):modules[entry['path']]=entry
        self.cache.write_text(json.dumps(list(modules.values())))
        self.pid,self.client=self.launch();self.hwnd=self.desktop.hwnd(self.pid);self.process=SimpleNamespace(pid=self.pid)
        self.descriptors=self.client.call('plugin.discover',dict(format='VST3'))['data']

    def launch(self,project=None):
        args=[os.environ['SCREAMSEQ_TEST_EXE'],'--audio-test-silent' if project else '--inspection','--automation','--seconds','100','--vst3-test-cache',str(self.cache)]
        if project:args+=['--project',str(project),'--report',str(self.folder/'audio.json')]
        pid=self.desktop.launch(args);client=Client(r'\\.\pipe\ScreamSeq.Api.'+str(pid),timeout=20)
        for _ in range(100):
            try:
                client.call('document.get')
                if not project or client.call('transport.get')['data']['audioActive']:return pid,client
            except TransportError:pass
            time.sleep(.1)
        self.fail('owned relink app did not start')

    doc=support.PluginAppTests.doc
    write=support.PluginAppTests.write
    rack=support.PluginAppTests.rack
    state=support.PluginAppTests.state
    parameters=support.PluginAppTests.parameters
    tick=support.PluginAppTests.tick
    command=support.PluginAppTests.command
    select=support.PluginAppTests.select
    dialog=dialogs.EditorAppTests.dialog
    choose_dialog_path=dialogs.EditorAppTests.choose_dialog_path

    def read(self,method,**params):return self.client.call(method,params)['data']
    def entry(self,name):return next(d for d in self.descriptors if d['name']==name)
    def program(self):return next(d for d in self.descriptors if d['classID']=='5245534F4E414E4350524F4752410001')
    def save_tree(self,name):
        path=self.folder/name;self.write('document.save',path=str(path),overwrite=path.exists());return path,plistlib.loads(path.read_bytes())

    def broken(self,descriptor,aliases=False,ports=False):
        self.write('plugin.add',descriptor=descriptor);plugin=self.rack()[0]['instanceID']
        if aliases:
            numbers=[self.write('instrument.create',empty=True,name=name)['data']['instrument'] for name in ('First part','Second part')]
            self.write('plugin.instruments.set',plugin=plugin,assignments=[dict(instrument=numbers[0],channel=1),dict(instrument=numbers[1],channel=9)])
        if ports:self.write('plugin.buses.set',slot=0,inputs=[1])
        self.write('plugin.bypass',slot=0,bypass=True)
        parameter=next(p for p in self.parameters() if p['writable'])
        original=self.state();path,tree=self.save_tree('broken.screamseq')
        tree['plugins'][0]['path']='/Library/Audio/Plug-Ins/VST3/Moved.vst3'
        tree['plugins'][0]['classID']=tree['plugins'][0]['classID'].lower()
        tree['plugins'][0]['futureLocationField']=dict(retain='opaque')
        tree['automation']=[[0,parameter['id'],parameter['value'],24000]]
        path.write_bytes(plistlib.dumps(tree,fmt=plistlib.FMT_BINARY));self.write('document.open',path=str(path),discard=True)
        return plugin,original,tree

    def location(self,plugin):return self.read('plugin.path.get',plugin=plugin)
    def reconnect(self,plugin,candidate=None,**extra):
        candidate=candidate or self.location(plugin)['candidates'][0]
        fields=dict(plugin=plugin,path=candidate['descriptor']['path'],expectedModuleSHA256=candidate['moduleSHA256']);fields.update(extra)
        return self.write('plugin.path.set',**fields)

    @unittest.skipUnless(os.environ.get('SCREAMSEQ_TEST_PROVIDER_CACHE'),'native provider fixture')
    def test_stale_deleted_targets_and_unsupported_formats_leave_saved_recipes_unchanged(self):
        plugin,_,_=self.broken(self.program());info=self.location(plugin);candidate=info['candidates'][0]
        fields=dict(plugin=plugin,path=candidate['descriptor']['path'],expectedModuleSHA256=candidate['moduleSHA256'])
        before=self.doc();cache_before=self.cache.read_bytes()
        for method,params in [('plugin.path.set',fields),('plugin.path.scan',dict(plugin=plugin,path=self.program()['path']))]:
            with self.subTest(method=method),self.assertRaises(ApiError) as error:
                self.client.call(method,dict(expectedRevision='stale',**params))
            self.assertEqual(error.exception.code,-32001)
        self.assertEqual(self.doc(),before);self.assertEqual(self.cache.read_bytes(),cache_before)
        path,tree=self.save_tree('unsupported.screamseq')
        tree['plugins'][0].update(format='AU',type=int.from_bytes(b'aufx','big'),classID='',state=b'opaque Mac Audio Unit state')
        path.write_bytes(plistlib.dumps(tree,fmt=plistlib.FMT_BINARY));self.write('document.open',path=str(path),discard=True);before=self.doc()
        for action in [lambda:self.location(plugin),lambda:self.write('plugin.path.scan',plugin=plugin,path=self.program()['path']),lambda:self.write('plugin.path.set',**fields)]:
            with self.assertRaises(ApiError) as error:action()
            self.assertEqual(error.exception.code,-32602)
        self.assertEqual(self.doc(),before);self.assertEqual(self.cache.read_bytes(),cache_before)
        _,after=self.save_tree('unsupported-retained.screamseq');self.assertEqual(after['plugins'],tree['plugins'])
        self.write('plugin.remove',slot=0);before=self.doc()
        with self.assertRaises(ApiError):self.write('plugin.path.set',**fields)
        self.assertEqual(self.doc(),before)

    @unittest.skipUnless(os.environ.get('SCREAMSEQ_TEST_PLUGIN_CACHE'),'installed effects')
    def test_real_effects_reconnect_preserves_every_recipe_field_and_undo(self):
        for name in ('Contourtonist','OrbitCab'):
            with self.subTest(plugin=name):
                plugin,original,before_tree=self.broken(self.entry(name));before=self.doc();info=self.location(plugin)
                self.assertFalse(info['moduleVerified']);self.assertTrue(info['reason']);self.assertTrue(info['candidates'])
                dry=self.reconnect(plugin,dryRun=True);self.assertFalse(dry['changed']);self.assertTrue(dry['data']['wouldChange']);self.assertEqual(self.doc(),before)
                touch=self.read('automation.target.get');self.reconnect(plugin);self.assertEqual(self.state(),original);self.assertTrue(self.location(plugin)['moduleVerified'])
                self.assertEqual(self.read('automation.target.get'),touch)
                _,after=self.save_tree('reconnected.screamseq');expected=copy.deepcopy(before_tree['plugins']);expected[0]['path']=self.entry(name)['path']
                self.assertEqual(after['plugins'],expected);self.assertEqual(after['native'],before_tree['native']);self.assertEqual(after['automation'],before_tree['automation'])
                current=self.doc();self.assertFalse(self.reconnect(plugin)['changed']);self.assertEqual(self.doc(),current)
                self.write('history.undo',domain='plugins');self.assertFalse(self.location(plugin)['moduleVerified']);self.assertEqual(self.state(),original)
                self.write('history.redo',domain='plugins');self.assertTrue(self.location(plugin)['moduleVerified'])
                self.write('document.open',path=str(self.folder/'reconnected.screamseq'),discard=True);self.assertEqual(self.state(),original)
                self.write('plugin.remove',slot=0)

    @unittest.skipUnless(os.environ.get('SCREAMSEQ_TEST_INSTRUMENT_CACHE'),'installed Surge XT')
    def test_surge_aliases_retained_and_stable_target_after_rack_move(self):
        plugin,original,tree=self.broken(self.entry('Surge XT'),aliases=True)
        builtins=self.read('plugin.discover',format='Built-in');self.write('plugin.add',descriptor=builtins[0]);self.write('plugin.move',slot=0,direction=1)
        self.reconnect(plugin);self.assertEqual(self.state(1),original)
        self.assertEqual(self.rack()[1]['instanceID'],plugin)
        self.assertEqual(self.rack()[1]['instrumentAssignments'],tree['plugins'][0]['instrumentAssignments'])
        self.write('history.undo',domain='plugins');self.assertEqual(self.location(plugin)['descriptor']['path'],tree['plugins'][0]['path'])

    @unittest.skipUnless(os.environ.get('SCREAMSEQ_TEST_PROVIDER_CACHE'),'native provider fixture')
    def test_scan_path_hash_rejection_failed_vendor_state_and_ports(self):
        plugin,original,tree=self.broken(self.program(),ports=True);before=self.doc()
        self.assertEqual(self.reconnect(plugin,dryRun=True)['data']['dryRun'],True)
        with self.assertRaises(ApiError):self.reconnect(plugin,expectedModuleSHA256='0'*64)
        different=next(d for d in self.descriptors if d['classID']!=self.program()['classID'])
        with self.assertRaises(ApiError):self.reconnect(plugin,path=different['path'])
        self.assertEqual(self.doc(),before)
        copied=self.folder/'moved.vst3';shutil.copy2(self.program()['path'],copied)
        scanned=self.write('plugin.path.scan',plugin=plugin,path=str(copied))['data'];self.assertEqual(self.doc(),before)
        candidate=next(c for c in scanned['candidates'] if Path(c['descriptor']['path'])==copied)
        self.assertEqual(candidate['moduleSHA256'],hashlib.sha256(copied.read_bytes()).hexdigest())
        copied.write_bytes(copied.read_bytes()+b'changed after scan')
        with self.assertRaises(ApiError):self.reconnect(plugin,candidate)
        self.assertEqual(self.doc(),before)
        scanned=self.write('plugin.path.scan',plugin=plugin,path=str(copied))['data']
        changed=next(c for c in scanned['candidates'] if Path(c['descriptor']['path'])==copied)
        self.assertNotEqual(candidate['moduleSHA256'],changed['moduleSHA256'])
        with self.assertRaises(ApiError):self.reconnect(plugin,candidate)
        self.reconnect(plugin,changed);self.assertEqual(self.rack()[0]['auxiliaryInputs'],[1]);self.assertEqual(self.state(),original)
        # Corrupt opaque state can be inspected and verified, but cannot commit.
        path,bad=self.save_tree('bad-state.screamseq');bad['plugins'][0]['path']='/foreign/again.vst3';bad['plugins'][0]['state']=b'invalid vendor state'
        path.write_bytes(plistlib.dumps(bad,fmt=plistlib.FMT_BINARY));self.write('document.open',path=str(path),discard=True);before=self.doc()
        self.reconnect(plugin,changed,dryRun=True)
        with self.assertRaises(ApiError):self.reconnect(plugin,changed)
        self.assertEqual(self.doc(),before);self.assertEqual(base64.b64decode(self.state()),b'invalid vendor state')

    @unittest.skipUnless(os.environ.get('SCREAMSEQ_TEST_PROVIDER_CACHE'),'native provider fixture')
    def test_graph_recipe_location_uses_document_history_and_preserves_connections(self):
        descriptor=self.program();self.write('plugin.add',descriptor=descriptor)
        graph=self.write('graph.create')['data']['graph'];node=self.write('graph.node.add',graph=graph,kind='plugin',slot=0)['data']['node']
        self.write('graph.plugin.set',graph=graph,node=node,inputs=[1]);rack_before=self.rack();opaque=self.state()
        definition=self.read('graph.get',includeState=True)['library'][0]
        recipe=next(n for n in definition['nodes'] if n['id']==node)['plugin'];recipe['path']='/Library/Audio/Plug-Ins/VST3/Graph.vst3'
        self.write('graph.update',definition=definition);before=self.doc()
        info=self.read('graph.plugin.path.get',graph=graph,node=node);self.assertFalse(info['moduleVerified']);candidate=info['candidates'][0]
        params=dict(graph=graph,node=node,path=candidate['descriptor']['path'],expectedModuleSHA256=candidate['moduleSHA256'])
        self.write('graph.plugin.path.set',**params,dryRun=True);self.assertEqual(self.doc(),before)
        self.write('graph.plugin.path.set',**params)
        after=self.read('graph.get',includeState=True)['library'][0];expected=copy.deepcopy(definition);next(n for n in expected['nodes'] if n['id']==node)['plugin']['path']=descriptor['path']
        self.assertEqual(after,expected);self.assertEqual(self.rack(),rack_before);self.assertEqual(self.state(),opaque)
        self.write('history.undo',domain='document');self.assertEqual(self.read('graph.get',includeState=True)['library'][0],definition)
        self.write('history.redo',domain='document');self.assertEqual(self.read('graph.get',includeState=True)['library'][0],expected)
        path,_=self.save_tree('graph-path.screamseq');self.write('document.open',path=str(path));self.assertEqual(self.read('graph.get',includeState=True)['library'][0],expected)
        self.assertTrue(self.read('graph.plugin.get',graph=graph,node=node)['parameters'])
        # Actual graph inspector entry point uses the same captured node.
        self.command(430);self.select(443,3);self.select(442,2);self.command(471);self.idle()
        self.assertEqual(self.local()['target'],dict(graph=graph,node=node))

    def local(self):return self.read('workspace.get')['pluginPath']
    def idle(self):
        end=time.monotonic()+7;quiet=None
        while time.monotonic()<end:
            state=self.read('workspace.get')
            if state['documentBusy'] or state['pluginPath'].get('pending'):quiet=None
            elif quiet is None:quiet=time.monotonic()
            elif time.monotonic()-quiet>=.18:return
            time.sleep(.02)
        self.fail(str(self.local()))
    def window(self):
        found=[]
        @private_desktop.callback
        def visit(hwnd,_):
            pid=wintypes.DWORD();private_desktop.user.GetWindowThreadProcessId(hwnd,ctypes.byref(pid));name=ctypes.create_unicode_buffer(100)
            private_desktop.user.GetClassNameW(hwnd,name,100)
            if pid.value==self.pid and name.value=='ScreamSeq.PluginPath':found.append(hwnd)
            return True
        private_desktop.check(private_desktop.user.EnumDesktopWindows(self.desktop.desktop,visit,0));self.assertEqual(len(found),1);return found[0]
    def control(self,identifier):return private_desktop.user.GetDlgItem(self.window(),identifier)
    def press(self,identifier):self.idle();self.desktop.send(self.window(),0x111,identifier,self.control(identifier));self.idle()

    @unittest.skipUnless(os.environ.get('SCREAMSEQ_TEST_PLUGIN_CACHE'),'installed effects')
    def test_native_reconnect_draft_guard_close_keyboard_minimum_and_dialog_cancel(self):
        plugin,original,tree=self.broken(self.entry('Contourtonist'));self.tick();self.select(318,5);self.tick();self.command(327);self.idle()
        self.assertEqual(self.local()['target'],dict(plugin=plugin));before=self.doc();self.press(3602);self.assertEqual(self.doc(),before)
        self.write('document.patch',title='Changed while choosing a module');before=self.doc();self.press(3603);self.assertEqual(self.doc(),before);self.assertTrue(self.local()['stale'])
        self.press(3608);self.assertFalse(self.local()['visible']);self.command(327);self.idle();self.assertEqual(self.local()['target'],dict(plugin=plugin));self.assertTrue(self.local()['stale'])
        self.press(3604);self.press(3603);self.assertTrue(self.location(plugin)['moduleVerified']);self.assertEqual(self.state(),original)
        self.write('history.undo',domain='plugins');self.assertFalse(self.location(plugin)['moduleVerified']);self.press(3604)
        user=private_desktop.user
        # Reload disables the controls while reading. Reopening restores the
        # candidate's real keyboard focus before exercising the F6 cycle.
        self.press(3608);self.command(327);self.idle()
        self.assertEqual(self.desktop.focus(self.window()),self.control(3601))
        self.desktop.send(self.control(3601),0x100,0x75);self.assertEqual(self.desktop.focus(self.window()),self.control(3611))
        self.desktop.send(self.control(3611),0x100,0x75);self.assertEqual(self.desktop.focus(self.window()),self.control(3601))
        user.SetWindowPos.argtypes=[wintypes.HWND,wintypes.HWND,ctypes.c_int,ctypes.c_int,ctypes.c_int,ctypes.c_int,wintypes.UINT];user.GetWindowRect.argtypes=[wintypes.HWND,ctypes.POINTER(wintypes.RECT)]
        scale=self.read('workspace.get')['dpi']/96;self.assertTrue(user.SetWindowPos(self.window(),None,0,0,int(700*scale),int(480*scale),0x16));frame=wintypes.RECT();user.GetWindowRect(self.window(),ctypes.byref(frame))
        for identifier in list(range(3601,3612))+list(range(3700,3705)):
            rect=wintypes.RECT();user.GetWindowRect(self.control(identifier),ctypes.byref(rect));self.assertGreater(rect.right,rect.left);self.assertGreaterEqual(rect.left,frame.left);self.assertLessEqual(rect.right,frame.right);self.assertLessEqual(rect.bottom,frame.bottom)
        user.PostMessageW.argtypes=[wintypes.HWND,wintypes.UINT,wintypes.WPARAM,wintypes.LPARAM]
        before=self.doc();user.PostMessageW(self.window(),0x111,3606,self.control(3606));dlg=self.dialog(self.process,'Choose Windows VST3 module');user.PostMessageW(dlg,0x111,2,0);self.idle();self.assertEqual(self.doc(),before)
        user.PostMessageW(self.window(),0x111,3606,self.control(3606));self.dialog(self.process,'Choose Windows VST3 module')
        self.write('document.patch',title='Changed inside the file chooser');before=self.doc();self.choose_dialog_path(self.process,Path(self.entry('Contourtonist')['path']));self.idle()
        self.assertEqual(self.doc(),before);self.assertIn('changed',self.local()['status'].lower());self.assertEqual(self.local()['manualPath'],'')

    @unittest.skipUnless(os.environ.get('SCREAMSEQ_TEST_PLUGIN_CACHE') and os.environ.get('SCREAMSEQ_TEST_LIVE_AUDIO')=='1','installed effect and owned silent WASAPI')
    def test_reconnected_project_restarts_real_plugin_audio(self):
        plugin,original,tree=self.broken(self.entry('Contourtonist'));self.reconnect(plugin)
        self.write('plugin.bypass',slot=0,bypass=False);path,_=self.save_tree('audio-reconnected.screamseq')
        _,client=self.launch(project=path);start=client.call('transport.get')['data'];self.assertTrue(start['audioActive'])
        time.sleep(.4);after=client.call('transport.get')['data'];self.assertTrue(after['audioActive']);self.assertFalse(after['fault']);self.assertGreater(after['frames'],start['frames']);self.assertEqual(after['overruns'],0)
        self.assertEqual(client.call('plugin.state.get',dict(slot=0))['data']['data'],original)
        self.assertEqual(client.call('document.get')['data']['nativePlugins'][0]['instanceID'],plugin)
