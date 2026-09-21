"""Independent browser preferences and native library controls on owned desktops."""
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import tempfile
import time
import unittest
import private_desktop
import test_plugins_app as support
from client import Client, ApiError, TransportError


class PluginLibraryTests(unittest.TestCase):
    def setUp(self):
        self.folder = Path(self.enterContext(tempfile.TemporaryDirectory(prefix='library-ui-', dir=os.environ['TMPDIR'])))
        self.desktop = self.enterContext(private_desktop.PrivateDesktop())
        self.preferences = self.folder / 'preferences' / 'plugin-library.json'
        self.cache = self.folder / 'plugins.json'
        modules = []
        for key in ('SCREAMSEQ_TEST_PLUGIN_CACHE', 'SCREAMSEQ_TEST_INSTRUMENT_CACHE'):
            if os.environ.get(key): modules.extend(json.loads(Path(os.environ[key]).read_text(encoding='utf-8-sig')))
        self.cache.write_text(json.dumps(modules), encoding='utf-8')
        self.pid, self.client = self.launch()
        self.hwnd = self.desktop.hwnd(self.pid)

    def launch(self, audio=False):
        args = [os.environ['SCREAMSEQ_TEST_EXE'], '--audio-test-silent' if audio else '--inspection', '--automation',
                '--seconds', '90', '--report', str(self.folder / ('audio.json' if audio else 'inspection.json')),
                '--vst3-test-cache', str(self.cache), '--plugin-test-library', str(self.preferences)]
        pid = self.desktop.launch(args)
        client = Client(r'\\.\pipe\ScreamSeq.Api.' + str(pid), timeout=20)
        for _ in range(100):
            try:
                if not audio or client.call('transport.get')['data']['audioActive']:
                    client.call('document.get'); return pid, client
            except TransportError: pass
            time.sleep(.1)
        self.fail('owned library test app did not start')

    doc = support.PluginAppTests.doc
    write = support.PluginAppTests.write
    rack = support.PluginAppTests.rack
    tick = support.PluginAppTests.tick
    command = support.PluginAppTests.command

    def get(self, client=None, **filters):
        return (client or self.client).call('plugin.library.get', filters)['data']

    def set(self, entry, client=None, revision=None, **patch):
        client = client or self.client
        return client.call('plugin.library.set', dict(catalogID=entry['catalogID'],
                expectedLibraryRevision=revision or self.get(client)['libraryRevision'], **patch))

    def reject(self, code, action):
        with self.assertRaises(ApiError) as error: action()
        self.assertEqual(error.exception.code, code, str(error.exception))

    def test_mac_contract_filters_defaults_dry_noop_replay_and_independent_history(self):
        description = self.client.call('api.describe')['data']
        self.assertEqual(description['revisionGuards']['plugin.library.set'], ['expectedLibraryRevision'])
        first = self.get(); self.assertTrue(first['preferencesAvailable']); self.assertEqual(first['libraryRevision'], 'library:0')
        self.assertFalse(self.preferences.parent.exists())
        entry = self.get(format='Built-in')['plugins'][0]
        expected_id = 'p'+hashlib.sha256(json.dumps(['Built-in', entry['classID']], separators=(',', ':')).encode()).hexdigest()
        self.assertEqual(entry['catalogID'], expected_id)
        descriptor = entry['descriptor']
        self.assertEqual(set(descriptor), {'type','subtype','manufacturer','name','format','path','classID','isInstrument'})
        before = self.doc()
        dry = self.set(entry, favorite=True, dryRun=True)
        self.assertEqual(dry['data'], dict(libraryRevision='library:0', wouldChange=True, written=False,
                                          catalogID=expected_id, preferences=dict(favorite=True,hidden=False,category='')))
        self.assertFalse(dry['changed']); self.assertFalse(self.preferences.parent.exists())
        params = dict(catalogID=expected_id, expectedLibraryRevision='library:0', favorite=True, category=' \u00a0Échos\u00a0 ')
        result = self.client.call('plugin.library.set', params, request_id='library-preference')
        self.assertEqual(result['data']['preferences']['category'], 'Échos')
        self.assertFalse(result['changed']); self.assertFalse(result['playbackStopped'])
        content = self.preferences.read_bytes()
        self.assertEqual(self.client.call('plugin.library.set', params, request_id='library-preference'), result)
        self.assertFalse(self.set(entry, favorite=True)['data']['wouldChange'])
        self.assertEqual(self.preferences.read_bytes(), content)
        self.assertEqual(self.doc(), before)
        self.assertEqual([p['catalogID'] for p in self.get(search='ECHOS')['plugins']], [expected_id])
        self.assertEqual([p['catalogID'] for p in self.get(favoritesOnly=True)['plugins']], [expected_id])
        self.assertEqual([p['catalogID'] for p in self.get(category='Échos')['plugins']], [expected_id])
        self.set(entry, hidden=True)
        self.assertFalse(self.get(favoritesOnly=True)['plugins'])
        self.assertEqual(len(self.get(favoritesOnly=True,includeHidden=True)['plugins']),1)
        self.assertIn('Échos', self.get()['categories'])
        self.set(entry, favorite=False, hidden=False, category='')
        self.assertEqual(json.loads(self.preferences.read_text())['entries'], {})
        self.assertEqual(self.doc(), before)
        for fields in (dict(format=''),dict(kind='other'),dict(search='x'*201),dict(includeHidden=1),dict(unknown=True)):
            self.reject(-32602, lambda:self.get(**fields))
        for fields in ({},dict(favorite=1),dict(hidden='yes'),dict(category=9),dict(category='x'*81),dict(expectedRevision=before['revision'])):
            self.reject(-32602, lambda:self.set(entry, **fields))
        self.assertEqual(self.doc(), before)

    @unittest.skipUnless(os.environ.get('SCREAMSEQ_TEST_INSTRUMENT_CACHE'), 'installed Windows plugins')
    def test_installed_catalog_filters_identity_and_add_undo(self):
        library = self.get(format='VST3')
        names = {p['name'] for p in library['plugins']}
        self.assertTrue({'Contourtonist','OrbitCab','Surge XT'} <= names)
        surge = next(p for p in self.get(kind='instrument')['plugins'] if p['name']=='Surge XT')
        self.assertFalse(self.get(format='AU')['plugins'])
        self.assertTrue(all(p['format']=='Built-in' for p in self.get(format='Built-in')['plugins']))
        self.assertTrue(all(not p['isInstrument'] for p in self.get(kind='effect')['plugins']))
        self.set(surge, favorite=True,category='Synths')
        self.write('plugin.add', descriptor=surge['descriptor'])
        self.assertEqual(self.rack()[0]['name'], 'Surge XT')
        self.write('history.undo',domain='plugins'); self.assertEqual(self.rack(), [])
        self.assertEqual(self.get(search='synths')['plugins'][0]['catalogID'], surge['catalogID'])
        # Library IDs depend on the normalized bundle path/class, not its label.
        modules = json.loads(self.cache.read_text())
        module = next(m for m in modules if any(c['name']=='Surge XT' for c in m['classes']))
        for c in module['classes']:
            if c['name']=='Surge XT': c['name']='Renamed synthesizer'
        self.cache.write_text(json.dumps(modules))
        _, other = self.launch()
        renamed = next(p for p in self.get(other,format='VST3')['plugins'] if p['name']=='Renamed synthesizer')
        self.assertEqual(renamed['catalogID'], surge['catalogID'])
        self.assertTrue(renamed['favorite'])

    def test_shared_library_stale_writers_locking_atomic_failure_and_corruption(self):
        _, other = self.launch(); entry = self.get()['plugins'][0]; before = self.doc()
        token = self.get(other)['libraryRevision']; committed = self.set(entry, favorite=True)['data']
        saved = self.preferences.read_bytes()
        self.assertEqual(self.get(other)['libraryRevision'], committed['libraryRevision'])
        self.reject(-32001, lambda:self.set(entry,client=other,revision=token,hidden=True))
        self.assertEqual(self.preferences.read_bytes(),saved)
        with self.preferences.open('rb'):
            self.reject(-32003,lambda:self.set(entry,hidden=True))
        self.assertEqual(self.preferences.read_bytes(),saved)
        self.assertFalse(list(self.preferences.parent.glob('*.staged.*')))
        # Hold the exact nonblocking lock from a separate process/thread owner.
        kernel = private_desktop.kernel
        kernel.CreateMutexW.argtypes=[ctypes.c_void_p,wintypes.BOOL,wintypes.LPCWSTR]; kernel.CreateMutexW.restype=wintypes.HANDLE
        kernel.ReleaseMutex.argtypes=[wintypes.HANDLE]
        digest = hashlib.sha256(str(self.preferences.resolve()).lower().encode('utf-16-le')).hexdigest()
        lock = kernel.CreateMutexW(None,True,'Global\\org.resonance.tracker.plugin-library-v1.'+digest)
        self.assertTrue(lock)
        try:
            busy = self.get(); self.assertFalse(busy['preferencesAvailable']); self.assertTrue(busy['plugins']); self.assertIn('busy',busy['preferenceError'])
            self.reject(-32002,lambda:self.set(entry,revision=committed['libraryRevision'],hidden=True))
        finally: kernel.ReleaseMutex(lock); kernel.CloseHandle(lock)
        self.assertEqual(self.preferences.read_bytes(),saved)
        self.preferences.write_bytes(b'not json')
        corrupt = self.get(); self.assertFalse(corrupt['preferencesAvailable']); self.assertEqual(corrupt['libraryRevision'],''); self.assertTrue(corrupt['warning'])
        self.reject(-32602,lambda:self.set(entry,revision=committed['libraryRevision'],favorite=True))
        self.assertEqual(self.preferences.read_bytes(),b'not json')
        self.assertEqual(self.doc(),before)
        self.write('plugin.add',descriptor=entry['descriptor']); self.assertEqual(len(self.rack()),1)
        self.assertEqual(self.preferences.read_bytes(),b'not json')

    def test_bounded_preferences_and_duplicate_decoded_keys(self):
        entry=self.get()['plugins'][0]; self.preferences.parent.mkdir()
        valid=dict(version=1,revision='library:fixture',entries={entry['catalogID']:dict(favorite=True,hidden=False,category='Effects')})
        invalid=[b'x'*(2*1024*1024+1),b'{}',b'[]',b'{"version":1,"version":1}',
                 json.dumps(valid).replace('"favorite": true','"favorite": true,"favor\\u0069te": false').encode()]
        for key,value in [('version',True),('revision',''),('entries',[]),('extra',1)]:
            item=dict(valid);item[key]=value;invalid.append(json.dumps(item).encode())
        item=dict(valid);item['entries']={'p'+format(i,'064x'):dict(favorite=False,hidden=True,category='') for i in range(4097)}
        invalid.append(json.dumps(item).encode())
        for data in invalid:
            self.preferences.write_bytes(data); result=self.get()
            self.assertFalse(result['preferencesAvailable']);self.assertTrue(result['plugins']);self.assertTrue(result['warning'])
            self.assertEqual(self.preferences.read_bytes(),data)
        item['entries'].pop('p'+format(4096,'064x'));self.preferences.write_text(json.dumps(item))
        self.assertTrue(self.get()['preferencesAvailable'])
        self.reject(-32602,lambda:self.set(entry,hidden=True))

    def local(self):
        return self.client.call('workspace.get')['data']['pluginLibrary']

    def idle(self):
        end=time.monotonic()+6; quiet=None
        while time.monotonic()<end:
            state=self.client.call('workspace.get')['data']; browser=state['pluginLibrary']
            if state['documentBusy'] or browser.get('pending') or browser.get('refreshQueued'): quiet=None
            elif quiet is None: quiet=time.monotonic()
            elif time.monotonic()-quiet>=.18: return
            time.sleep(.02)
        self.fail(str(self.local()))

    def window(self):
        found=[]
        @private_desktop.callback
        def visit(hwnd,_):
            pid=wintypes.DWORD();private_desktop.user.GetWindowThreadProcessId(hwnd,ctypes.byref(pid));name=ctypes.create_unicode_buffer(100)
            private_desktop.user.GetClassNameW(hwnd,name,100)
            if pid.value==self.pid and name.value=='ScreamSeq.PluginLibrary':found.append(hwnd)
            return True
        private_desktop.check(private_desktop.user.EnumDesktopWindows(self.desktop.desktop,visit,0));self.assertEqual(len(found),1);return found[0]

    def control(self,identifier):
        hwnd=private_desktop.user.GetDlgItem(self.window(),identifier);self.assertTrue(hwnd);return hwnd

    def press(self,identifier,notification=0):
        self.idle();self.desktop.send(self.window(),0x111,identifier|(notification<<16),self.control(identifier));self.idle()

    def field(self,identifier,value):
        self.idle();text=ctypes.create_unicode_buffer(value);self.desktop.send(self.control(identifier),0xC,0,ctypes.addressof(text));self.idle()

    def choose(self,index=0):
        self.idle();self.desktop.send(self.control(3207),0x186,index);self.press(3207,1)

    def test_native_search_favorites_hide_categories_add_and_retained_stale_draft(self):
        self.command(326);self.idle();self.assertTrue(self.local()['visible']);self.choose()
        before=self.doc();first=self.local()['plugins'][0];identifier=first['catalogID']
        self.press(3208);self.assertTrue(self.local()['plugins'][0]['favorite']);self.assertEqual(self.doc(),before)
        self.press(3205);self.assertEqual([p['catalogID'] for p in self.local()['plugins']],[identifier])
        count_before=self.desktop.send(private_desktop.user.GetDlgItem(self.hwnd,301),0x146)
        self.press(3209);self.assertFalse(self.local()['plugins']);self.assertFalse(self.local()['selected'])
        self.tick()
        self.assertEqual(self.desktop.send(private_desktop.user.GetDlgItem(self.hwnd,301),0x146),count_before-1)
        self.press(3206);self.assertTrue(self.local()['plugins'][0]['hidden']);self.choose()
        self.press(3209);self.assertFalse(self.local()['plugins'][0]['hidden'])
        self.field(3210,'  Textures  ');self.assertTrue(self.local()['categoryDraft']);self.press(3211)
        self.assertEqual(self.local()['plugins'][0]['category'],'Textures');self.assertFalse(self.local()['categoryDraft'])
        self.field(3201,'TEXTURES');self.assertEqual(len(self.local()['plugins']),1)
        self.press(3212);self.assertEqual(self.rack()[0]['classID'],first['classID'])
        self.write('history.undo',domain='plugins');self.assertFalse(self.rack());self.assertTrue(self.local()['plugins'][0]['favorite'])
        self.field(3210,'Unsaved draft');self.set(first,category='Changed elsewhere')
        self.press(3211);self.assertTrue(self.local()['categoryDraft']);self.assertIn('changed',self.local()['status'].lower())
        self.press(3215);self.assertFalse(self.local()['visible']);self.command(326);self.idle();self.assertTrue(self.local()['categoryDraft'])
        self.press(3213);self.assertFalse(self.local()['categoryDraft'])
        self.field(3201,'');self.assertEqual(self.local()['plugins'][0]['category'],'Changed elsewhere')

    def test_native_keyboard_minimum_bounds_and_unavailable_preferences(self):
        self.preferences.parent.mkdir();self.preferences.write_bytes(b'corrupt')
        self.command(326);self.idle();self.assertTrue(self.local()['plugins']);self.assertFalse(self.local()['libraryRevision']);self.choose()
        user=private_desktop.user;user.IsWindowEnabled.argtypes=[wintypes.HWND]
        for identifier in (3208,3209,3210,3211):self.assertFalse(user.IsWindowEnabled(self.control(identifier)))
        self.assertTrue(user.IsWindowEnabled(self.control(3212)))
        self.desktop.send(self.control(3201),0x100,0x75);self.assertEqual(self.desktop.focus(self.window()),self.control(3207))
        self.desktop.send(self.control(3207),0x100,0x75);self.assertEqual(self.desktop.focus(self.window()),self.control(3201))
        user.SetWindowPos.argtypes=[wintypes.HWND,wintypes.HWND,ctypes.c_int,ctypes.c_int,ctypes.c_int,ctypes.c_int,wintypes.UINT]
        user.GetWindowRect.argtypes=[wintypes.HWND,ctypes.POINTER(wintypes.RECT)]
        scale=self.client.call('workspace.get')['data']['dpi']/96
        self.assertTrue(user.SetWindowPos(self.window(),None,0,0,int(640*scale),int(540*scale),0x16));frame=wintypes.RECT();user.GetWindowRect(self.window(),ctypes.byref(frame))
        for identifier in list(range(3201,3216))+list(range(3300,3308)):
            rect=wintypes.RECT();user.GetWindowRect(self.control(identifier),ctypes.byref(rect))
            self.assertGreater(rect.right,rect.left);self.assertGreaterEqual(rect.left,frame.left);self.assertLessEqual(rect.right,frame.right);self.assertLessEqual(rect.bottom,frame.bottom)
        self.press(3212);self.assertEqual(len(self.rack()),1);self.assertEqual(self.preferences.read_bytes(),b'corrupt')

    @unittest.skipUnless(os.environ.get('SCREAMSEQ_TEST_LIVE_AUDIO')=='1','opt-in owned silent WASAPI')
    def test_browser_writes_leave_live_audio_and_document_unchanged(self):
        _,client=self.launch(audio=True);before=client.call('document.get');entry=self.get(client)['plugins'][0]
        start=client.call('transport.get')['data'];self.assertTrue(start['audioActive'])
        for category in ('Dynamics','Textures',''):
            result=self.set(entry,client=client,category=category,favorite=True)
            self.assertFalse(result['changed']);self.assertFalse(result['playbackStopped'])
            self.assertEqual(client.call('document.get'),before)
        time.sleep(.25);after=client.call('transport.get')['data']
        self.assertTrue(after['audioActive']);self.assertFalse(after['fault']);self.assertGreater(after['frames'],start['frames']);self.assertEqual(after['overruns'],0)
