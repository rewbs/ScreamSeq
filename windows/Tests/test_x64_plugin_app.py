"""Opt-in installed x64 VST3 qualification in a private ARM64EC app process."""
import math
import os
from pathlib import Path
import unittest
from test_parameter_automation_ui import ParameterAutomationUITests
from test_pattern_activity_app import PatternActivityAppTests


@unittest.skipUnless(os.environ.get('SCREAMSEQ_TEST_X64_PLUGIN'), 'opt-in installed x64 plugin')
class X64PluginAppTests(unittest.TestCase):
    setUp=ParameterAutomationUITests.setUp
    doc=PatternActivityAppTests.doc
    read=PatternActivityAppTests.read
    write=PatternActivityAppTests.write
    tone=PatternActivityAppTests.tone
    render=PatternActivityAppTests.render

    def test_x64_effect_editor_audio_and_reopen(self):
        self.tone()
        self.write('mixer.enable')
        dry=self.render('x64-dry')
        descriptor=next(p for p in self.read('plugin.discover',format='VST3') if p['name']==os.environ['SCREAMSEQ_TEST_X64_PLUGIN'])
        self.write('plugin.add',descriptor=descriptor)
        plugin=self.doc()['data']['nativePlugins'][0]['instanceID']
        master=self.read('mixer.get')['buses'][-1]['id']
        self.write('mixer.bus.set',bus=master,inserts=[plugin])
        parameters=self.read('plugin.parameters.get',slot=0)
        self.assertGreater(len(parameters),0)
        master_volume=next(p for p in parameters if p['name']=='Master Volume')
        self.write('plugin.parameters.set',slot=0,values=[dict(id=master_volume['id'],value=.3)])
        self.write('plugin.editor.open',slot=0)
        self.assertEqual(self.doc()['data']['openPluginEditors'],1)
        # Vendors can initialize opaque state when their editor opens. Capture
        # it before taking the revision guard for the separate close operation.
        self.read('plugin.parameters.get',slot=0)
        self.write('plugin.editor.close',slot=0)
        self.assertEqual(self.doc()['data']['openPluginEditors'],0)
        if os.environ.get('SCREAMSEQ_GATE_EVIDENCE_DIR'):
            self.write('document.save',path=str(Path(os.environ['SCREAMSEQ_GATE_EVIDENCE_DIR'])/'ohmicide.screamseq'),overwrite=True)
        wet=self.render('x64-wet')
        self.assertTrue(all(math.isfinite(v) for v in wet))
        self.assertGreater(max(abs(v) for v in wet),1e-6)
        delta=max(abs(a-b) for a,b in zip(dry,wet))
        print('x64 plugin:',descriptor['name'],'48 kHz stereo; peak',max(abs(v) for v in wet),'dry/wet maximum delta',delta)
        identity=self.doc()['data']['nativePlugins'][0]['instanceID']
        path=self.folder/'x64-plugin.screamseq'
        self.write('document.save',path=str(path))
        self.write('document.open',path=str(path),discard=True)
        self.assertEqual(self.doc()['data']['nativePlugins'][0]['instanceID'],identity)
        self.write('plugin.editor.open',slot=0)
        self.assertEqual(self.doc()['data']['openPluginEditors'],1)
        self.read('plugin.parameters.get',slot=0)
        self.write('plugin.editor.close',slot=0)
        if os.environ.get('SCREAMSEQ_TEST_X64_REQUIRE_WET')=='1':
            self.assertGreater(delta,1e-6,'The qualified effect must audibly change its input')
        else:
            print('Audio effect NOT qualified: installed Ohmicide demo UI reports that audio bounces are disabled. Set SCREAMSEQ_TEST_X64_REQUIRE_WET=1 for a licensed export check.')
