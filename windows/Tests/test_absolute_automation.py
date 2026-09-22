"""Absolute lane API, plugin history, pagination, identity and conflicts."""
import unittest
import test_graph_mixer_app as support
from client import ApiError


class AbsoluteAutomationTests(unittest.TestCase):
    setUp=support.GraphMixerAppTests.setUp
    doc=support.GraphMixerAppTests.doc
    read=support.GraphMixerAppTests.read
    write=support.GraphMixerAppTests.write
    rejected=support.GraphMixerAppTests.rejected
    add_gain=support.GraphMixerAppTests.add_gain
    def lane(self,slot=0):return [p for p in self.read('automation.get',limit=4096)['points'] if p['slot']==slot and p['id']==1]

    def test_contract_sort_native_values_noop_replay_history_and_reopen(self):
        self.add_gain();self.add_gain();self.write('automation.replaceLane',slot=1,id=1,points=[dict(frame=0,value=-3)]);other=self.lane(1);original=self.doc();opaque=self.read('plugin.state.get',slot=0)
        points=[dict(frame=48000,value=0),dict(frame=0,value=-12),dict(frame=24000,value=-6)];params=dict(expectedRevision=original['revision'],slot=0,id=1,points=points)
        result=self.client.call('automation.replaceLane',params,request_id='absolute-once');self.assertEqual(result['data'],dict(points=4));self.assertEqual(self.client.call('automation.replaceLane',params,request_id='absolute-once'),result);self.assertEqual(self.lane(1),other);self.assertEqual(self.read('plugin.state.get',slot=0),opaque)
        saved=self.read('automation.get');self.assertEqual(saved['sampleRate'],48000);self.assertEqual([p['frame'] for p in self.lane()],[0,24000,48000]);before=self.doc();self.write('automation.replaceLane',slot=0,id=1,points=list(reversed(points)));self.assertEqual(self.doc(),before);self.assertEqual(self.read('automation.get'),saved)
        with self.assertRaises(ApiError):self.client.call('automation.replaceLane',params)
        self.write('history.undo',domain='plugins');self.assertEqual(self.lane(),[]);before=self.doc();self.write('automation.replaceLane',slot=0,id=1,points=[]);self.assertEqual(self.doc(),before);self.write('history.redo',domain='plugins');self.assertEqual(self.read('automation.get'),saved)
        path=self.folder/'absolute-lanes.screamseq';self.write('document.save',path=str(path));self.write('document.open',path=str(path),discard=True);self.assertEqual(self.read('automation.get'),saved);self.assertEqual(self.read('plugin.state.get',slot=0),opaque)
        self.write('automation.replaceLane',slot=0,id=1,points=[]);self.assertEqual(self.lane(),[]);self.assertEqual(self.lane(1),other)

    def test_atomic_bounds_types_missing_parameter_and_pagination(self):
        self.add_gain();points=[dict(frame=i*48000,value=-i) for i in range(5)];self.write('automation.replaceLane',slot=0,id=1,points=points)
        before=self.doc();saved=self.read('automation.get');self.assertEqual(self.read('automation.get',offset=2,limit=2)['points'],saved['points'][2:4]);self.assertEqual(self.read('automation.get',offset=5)['points'],[])
        for fields in [dict(offset=-1),dict(offset=6),dict(offset=True),dict(limit=0),dict(limit=4097),dict(extra=1)]:
            with self.assertRaises(ApiError):self.read('automation.get',**fields)
        for invalid in [[dict(frame=True,value=0)],[dict(frame=-1,value=0)],[dict(frame=29030400001,value=0)],[dict(frame=.5,value=0)],[dict(frame=0,value=True)],[dict(frame=0,value=25)],[dict(frame=0,value=-97)],[dict(frame=0,value=-6),dict(frame=0,value=0)],[dict(frame=0,value=-6,curve='linear')]]:self.rejected('automation.replaceLane',slot=0,id=1,points=invalid)
        for fields in [dict(slot=True,id=1),dict(slot=1,id=1),dict(slot=0,id=True),dict(slot=0,id=4294967295)]:self.rejected('automation.replaceLane',points=[],**fields)
        self.assertEqual(self.doc(),before);self.assertEqual(self.read('automation.get'),saved)
        self.write('automation.replaceLane',slot=0,id=1,points=[dict(frame=29030400000,value=24)]);self.assertEqual(self.lane()[0]['frame'],29030400000)

    def test_conflicts_both_directions_and_plugin_reorder_preserve_target(self):
        plugin=self.add_gain();other=self.add_gain();curve=[dict(position=0,value=.5)];self.write('automation.pattern.set',pattern=0,plugin=plugin,parameter=1,points=curve)
        self.rejected('automation.replaceLane',slot=0,id=1,points=[dict(frame=0,value=-6)]);self.write('automation.pattern.set',pattern=0,plugin=plugin,parameter=1,enabled=False,points=curve);self.write('automation.replaceLane',slot=0,id=1,points=[dict(frame=0,value=-6)])
        self.rejected('automation.pattern.set',pattern=0,plugin=plugin,parameter=1,points=curve);self.rejected('pattern.effects.set',pattern=0,columns=[dict(channel=0,count=2)],bindings=[dict(id=1,plugin=plugin,parameter=1)],commands=[dict(channel=0,column=1,position=0,kind='parameter-set',binding=1,value=.4)])
        self.write('plugin.move',slot=0,direction=1);self.assertEqual(self.doc()['data']['nativePlugins'][1]['instanceID'],plugin);self.assertEqual(self.lane(1),[dict(slot=1,id=1,value=-6,frame=0)]);self.write('plugin.remove',slot=0);self.assertEqual(self.doc()['data']['nativePlugins'][0]['instanceID'],plugin);self.assertEqual(self.lane()[0]['value'],-6)
        self.write('automation.replaceLane',slot=0,id=1,points=[]);self.write('pattern.effects.set',pattern=0,columns=[dict(channel=0,count=2)],bindings=[dict(id=1,plugin=plugin,parameter=1)],commands=[dict(channel=0,column=1,position=0,kind='parameter-set',binding=1,value=.4)]);self.rejected('automation.replaceLane',slot=0,id=1,points=[dict(frame=0,value=-6)])
        self.write('pattern.effects.set',pattern=0,commands=[]);self.write('automation.replaceLane',slot=0,id=1,points=[dict(frame=0,value=-6)]);self.write('plugin.remove',slot=0);self.assertEqual(self.read('automation.get')['points'],[]);self.write('history.undo',domain='plugins');self.assertEqual(self.lane()[0]['value'],-6)

    def test_global_capacity_and_all_pages_preserve_the_full_lane(self):
        self.add_gain();self.add_gain();points=[dict(frame=i*4800,value=-12) for i in range(100000)];self.write('automation.replaceLane',slot=0,id=1,points=points)
        self.rejected('automation.replaceLane',slot=1,id=1,points=[dict(frame=0,value=-3)]);seen=[]
        while len(seen)<100000:
            page=self.read('automation.get',offset=len(seen),limit=4096);self.assertEqual(page['offset'],len(seen));self.assertEqual(page['total'],100000);seen.extend(page['points'])
        self.assertEqual([dict(frame=p['frame'],value=p['value']) for p in seen],points);self.write('automation.replaceLane',slot=0,id=1,points=[]);self.assertEqual(self.read('automation.get')['total'],0);self.write('history.undo',domain='plugins');self.assertEqual(self.read('automation.get',offset=99999)['points'][0]['frame'],99999*4800)


if __name__=='__main__':unittest.main()
