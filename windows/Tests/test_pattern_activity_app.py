"""Row-context replay and persistent atomic channel controls on a private desktop."""
import base64
import json
import math
import os
from pathlib import Path
import struct
import time
import unittest
import test_pattern_performance as support
from client import ApiError
from test_gate_app import GateAppTests
from test_parameter_automation_ui import ParameterAutomationUITests


class PatternActivityAppTests(unittest.TestCase):
    setUp = ParameterAutomationUITests.setUp
    doc = support.PatternPerformanceTests.doc
    read = support.PatternPerformanceTests.read
    write = support.PatternPerformanceTests.write
    navigate = support.PatternPerformanceTests.navigate
    control = support.PatternPerformanceTests.control
    render = GateAppTests.render

    def activity(self, row, channel=0, pattern=0, order=0):
        return self.read('pattern.activity.get', pattern=pattern, order=order, row=row, channel=channel)

    def tone(self, loop=True):
        self.write('document.new', discard=True)
        pcm = struct.pack('<4096h', *(round(6000*math.sin(2*math.pi*i/32)) for i in range(4096)))
        sample = self.write('sample.pcm.set', format='s16le', channels=1, rate=48000,
                            data=base64.b64encode(pcm).decode(), name='Context tone')['sample']
        if loop:
            self.write('sample.loops.set', sample=sample, normal=dict(start=0, end=4096, enabled=True, pingpong=False))
        self.write('pattern.apply', cells=[dict(pattern=0, row=0, channel=0, note=61, instrument=sample)])
        return sample

    def test_activity_input_guards_and_inventory(self):
        inventory=self.read('api.describe')
        self.assertIn('pattern.activity.get',inventory['reads'])
        self.assertIn('track.columns.set',inventory['writes'])
        if os.environ.get('SCREAMSEQ_ACTIVITY_INVENTORY'):
            Path(os.environ['SCREAMSEQ_ACTIVITY_INVENTORY']).write_text(json.dumps(inventory,indent=2),encoding='utf-8')
        self.tone()
        before=self.doc()
        for extra in (dict(row=-1),dict(row=1.5),dict(row=True),dict(channel=127),dict(order=999),dict(pattern=999),dict(unknown=True)):
            arguments=dict(pattern=0,order=0,row=1,channel=0)
            arguments.update(extra)
            with self.assertRaises(ApiError):self.read('pattern.activity.get',**arguments)
            self.assertEqual(before,self.doc())

    def test_held_sample_empty_row_and_completed_one_shot(self):
        sample = self.tone()
        before = self.doc()
        data = self.activity(2)
        self.assertTrue(data['complete'] and data['reachable'])
        self.assertIn(sample, [i['slot'] for i in data['items'] if i['kind']=='sample'])
        self.assertEqual(before, self.doc(), 'A context read must not edit or seek the live song')
        self.assertFalse(any(i['kind']=='sample' for i in self.activity(2, channel=1)['items']))
        self.write('sample.loops.set', sample=sample, normal=dict(start=0, end=4096, enabled=False, pingpong=False))
        self.assertFalse(any(i['kind']=='sample' for i in self.activity(8)['items']), 'Finished samples must not remain active because an old cell referenced them')

    def test_only_current_or_continuing_effects(self):
        self.tone()
        self.write('pattern.effects.set',pattern=0,commands=[
            dict(kind='native',native='gate',position=0,channel=0,column=0,parameters={}),
            dict(kind='native',native='sample-offset',position=65536,channel=0,column=0,parameters={}),
            dict(kind='native',native='gate',position=4*65536,channel=0,column=0,parameters=dict(period=.5)),
            dict(kind='native',native='gate-stop',position=8*65536,channel=0,column=0,parameters=dict(target='all'))])
        effects=lambda row:[i for i in self.activity(row)['items'] if i['kind']=='effect']
        self.assertEqual([i['row'] for i in effects(2)],[0], 'Only the running gate should remain; no earlier one-shot or future command')
        self.assertEqual([i['row'] for i in effects(5)],[4], 'Replacement gate must hide its predecessor')
        self.assertEqual(effects(9),[], 'A stopped gate must disappear')
        self.write('pattern.effects.set',pattern=0,commands=[
            dict(kind='native',native='vibrato',position=0,duration=3*65536,channel=0,column=0,parameters={})])
        self.assertTrue(any(i['label'].startswith('VB') for i in effects(1)), 'Continuing vibrato missing')
        self.assertEqual(effects(5),[], 'Expired vibrato still shown')

    def test_effect_links_and_async_panel_follow(self):
        self.tone()
        self.navigate(pattern=0,row=1,channel=0,column=3,following=False)
        self.write('pattern.effects.set',pattern=0,commands=[dict(kind='native',native='gate',position=0,channel=0,column=0,parameters={})])
        data=self.activity(1)
        effects=[i for i in data['items'] if i['kind']=='effect']
        self.assertTrue(any('GT' in i['label'] and i['row']==0 and i['channel']==0 for i in effects))
        deadline=time.monotonic()+12
        while time.monotonic()<deadline:
            panel=self.read('workspace.get')['contextPanel']['activity']
            if panel['ready']:
                break
            time.sleep(.05)
        self.assertTrue(panel['ready'],panel)
        self.assertEqual(panel['data']['items'],data['items'])
        self.navigate(row=8,channel=1,following=False)
        deadline=time.monotonic()+12
        while time.monotonic()<deadline:
            panel=self.read('workspace.get')['contextPanel']['activity']
            if panel['ready'] and not any(i['kind']=='sample' for i in panel['data']['items']):
                break
            time.sleep(.05)
        self.assertTrue(panel['ready'])
        self.assertFalse(any(i['kind']=='sample' for i in panel['data']['items']))

    def test_batch_mutes_validation_history_and_save(self):
        layout=self.read('track.get')
        old=[dict(column=c['id'],mute=c['mute']) for c in layout['columns']]
        solo=[dict(column=c['id'],mute=c['channel']!=0) for c in layout['columns']]
        before=self.doc()
        self.assertTrue(self.write('track.columns.set',columns=solo,dryRun=True)['wouldChange'])
        self.assertEqual(self.doc(),before)
        for columns in ([],[solo[0],solo[0]],[dict(column='n999999999',mute=True)],[dict(column=solo[0]['column'],mute=1)]):
            with self.assertRaises(ApiError):
                self.write('track.columns.set',columns=columns)
            self.assertEqual(self.doc(),before)
        self.write('track.columns.set',columns=solo)
        self.assertEqual([dict(column=c['id'],mute=c['mute']) for c in self.read('track.get')['columns']],solo)
        revision=self.doc()['revision']
        self.assertFalse(self.write('track.columns.set',columns=solo)['wouldChange'])
        self.assertEqual(self.doc()['revision'],revision)
        with self.assertRaises(ApiError):
            self.client.call('track.columns.set',dict(expectedRevision=before['revision'],columns=old))
        self.write('history.undo',domain='all')
        self.assertEqual([dict(column=c['id'],mute=c['mute']) for c in self.read('track.get')['columns']],old)
        self.write('history.redo',domain='all')
        path=self.folder/'solo.screamseq'
        self.write('document.save',path=str(path))
        self.write('document.open',path=str(path),discard=True)
        self.assertEqual([dict(column=c['id'],mute=c['mute']) for c in self.read('track.get')['columns']],solo)

    def test_routed_plugin_and_instrument_envelope_links(self):
        sample=self.tone()
        instrument=self.write('instrument.create',sample=sample)['instrument']
        self.write('pattern.apply',cells=[dict(pattern=0,row=0,channel=0,note=61,instrument=instrument)])
        self.write('instrument.patch',instrument=instrument,values=dict(envelope=0,enabled=True,points=[[0,64],[40,64]]))
        gain=next(p for p in self.read('plugin.discover',format='Built-in') if 'Gain' in p['name'])
        self.write('plugin.add',descriptor=gain)
        plugin=self.doc()['data']['nativePlugins'][-1]['instanceID']
        self.write('automation.pattern.set',pattern=0,plugin=plugin,parameter=1,enabled=True,points=[dict(position=0,value=.5)])
        links=self.activity(1)['items']
        self.assertTrue(any(i['kind']=='plugin' and i['plugin']==plugin for i in links))
        self.assertTrue(any(i['kind']=='instrument' and i['category']=='Envelopes' and i['slot']==instrument and i['envelope']=='volume' for i in links))
        self.assertTrue(any(i['kind']=='automation' and i['plugin']==plugin and i['parameter']==1 for i in links))

    def test_solo_mutes_reach_sample_audio(self):
        sample=self.tone()
        self.write('pattern.apply',cells=[dict(pattern=0,row=0,channel=1,note=61,instrument=sample)])
        self.write('mixer.enable')
        both=self.render('activity-both')
        columns=self.read('track.get')['columns']
        self.write('track.columns.set',columns=[dict(column=c['id'],mute=c['channel']!=0) for c in columns])
        solo=self.render('activity-solo')
        self.assertGreater(max(abs(v) for v in both),.001)
        # Identical centered voices sum linearly; ignore their initial ramp.
        self.assertLess(max(abs(a-.5*b) for a,b in zip(solo[400:48000],both[400:48000])),2e-6)

    @unittest.skipUnless(os.environ.get('SCREAMSEQ_TEST_PROVIDER_CACHE'), 'private VST3 instrument fixture')
    def test_plugin_instrument_held_note_and_release(self):
        self.write('document.new',discard=True)
        descriptor=next(d for d in self.read('plugin.discover',format='VST3') if d['classID']=='5245534F4E414E43494E535452550001')
        self.write('plugin.add',descriptor=descriptor)
        instrument=self.write('instrument.create',empty=True)['instrument']
        self.write('plugin.assign',slot=0,instrument=instrument)
        plugin=self.doc()['data']['nativePlugins'][0]['instanceID']
        self.write('pattern.apply',cells=[dict(pattern=0,row=0,channel=0,note=61,instrument=instrument),dict(pattern=0,row=4,channel=0,note=255)])
        self.assertTrue(any(i['kind']=='plugin' and i['plugin']==plugin for i in self.activity(1)['items']), 'Held plugin instrument missing on empty row')
        self.assertFalse(any(i['kind']=='plugin' and i['plugin']==plugin for i in self.activity(5)['items']), 'Released plugin note still reported as held')


if __name__=='__main__':
    unittest.main()
