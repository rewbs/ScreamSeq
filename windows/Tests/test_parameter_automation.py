"""Mac-compatible pattern parameter automation through the real Windows pipe."""
import unittest
import test_graph_mixer_app as support
from client import ApiError


class ParameterAutomationTests(unittest.TestCase):
    setUp=support.GraphMixerAppTests.setUp
    doc=support.GraphMixerAppTests.doc
    read=support.GraphMixerAppTests.read
    write=support.GraphMixerAppTests.write
    rejected=support.GraphMixerAppTests.rejected
    add_gain=support.GraphMixerAppTests.add_gain
    def lane(self,pattern=0):return self.read('automation.pattern.get',pattern=pattern)['lanes'][0]
    def create(self,plugin=None,pattern=0,**kwargs):
        plugin=plugin or self.add_gain();points=[dict(position=0,value=.2,curve='linear'),dict(position=1024,value=.8,curve='smooth'),dict(position=16383,value=.4,curve='step')]
        return plugin,self.write('automation.pattern.set',pattern=pattern,plugin=plugin,parameter=1,points=kwargs.pop('points',points),**kwargs)['lane']

    def test_full_lane_contract_id_noop_dry_history_and_native_reopen(self):
        plugin=self.add_gain();before=self.doc();points=[dict(position=0,value=.1),dict(position=8193,value=.9,curve='scripted',formula='mix(start,end,t)')]
        preview=self.write('automation.pattern.set',pattern=0,plugin=plugin,parameter=1,points=points,dryRun=True);self.assertEqual(self.doc(),before)
        _,identifier=self.create(plugin,points=points);self.assertEqual(identifier,preview['lane']);lane=self.lane();catalog=next(p for p in self.read('plugin.parameters.get',slot=0) if p['id']==1);self.assertEqual(lane['id'],identifier);self.assertTrue(lane['resolved']);self.assertEqual(lane['minimum'],catalog['min']);self.assertEqual(lane['maximum'],catalog['max'])
        before=self.doc();self.write('automation.pattern.set',pattern=0,plugin=plugin,parameter=1,points=lane['points']);self.assertEqual(self.doc(),before)
        pattern=self.write('pattern.create',rows=64)['pattern'];self.create(plugin,pattern=pattern);other=self.lane(pattern)
        self.write('automation.pattern.remove',lane=identifier);self.assertEqual(self.read('automation.pattern.get',pattern=0)['lanes'],[]);self.assertEqual(self.lane(pattern),other)
        self.write('history.undo',domain='document');self.assertEqual(self.lane(),lane);self.write('history.redo',domain='document');self.assertEqual(self.read('automation.pattern.get',pattern=0)['lanes'],[])
        self.write('history.undo',domain='document');path=self.folder/'parameter-curves.screamseq';self.write('document.save',path=str(path));self.write('automation.pattern.remove',lane=identifier);self.write('document.open',path=str(path),discard=True);self.assertEqual(self.lane(),lane);self.assertEqual(self.lane(pattern),other)
        self.assertEqual(self.read('automation.pattern.get',pattern=0)['rowsPerBeat'],4);self.write('document.timing.set',rowsPerBeat=6,rowsPerMeasure=24);self.assertEqual(self.read('automation.pattern.get',pattern=0)['rowsPerBeat'],6);self.write('history.undo',domain='document');self.assertEqual(self.read('automation.pattern.get',pattern=0)['rowsPerBeat'],4)

    def test_atomic_validation_stale_and_linked_master_protection(self):
        plugin,identifier=self.create();before=self.doc()
        for points in [[dict(position=True,value=.3)],[dict(position=16384,value=.3)],[dict(position=0,value=2)],[dict(position=0,value=.3),dict(position=0,value=.7)],[dict(position=8,value=.3),dict(position=0,value=.7)],[dict(position=0,value=.3,curve='scripted')],[dict(position=0,value=.3,curve='scripted',formula='unknown(t)')],[dict(position=0,value=.3,other=1)]]:
            self.rejected('automation.pattern.set',pattern=0,plugin=plugin,parameter=1,points=points)
        self.rejected('automation.pattern.set',pattern=0,plugin=plugin,parameter=True,points=[]);self.rejected('automation.pattern.set',pattern=0,plugin='missing',parameter=1,points=[])
        self.rejected('automation.pattern.set',pattern=0,plugin=plugin,parameter=4294967295,points=[]);self.assertEqual(self.doc(),before)
        self.write('document.patch',title='Another edit')
        with self.assertRaises(ApiError):self.client.call('automation.pattern.remove',dict(expectedRevision=before['revision'],lane=identifier))
        target=dict(kind='parameter',pattern=0,plugin=plugin,parameter=1);template=self.write('envelope.bank.save',name='Linked parameter',shape=dict(span=16384,points=self.lane()['points']))['id'];self.write('envelope.bank.apply',template=template,target=target,linked=True)
        self.rejected('automation.pattern.set',pattern=0,plugin=plugin,parameter=1,points=[dict(position=0,value=.5)])
        self.rejected('automation.pattern.transform',lane=identifier,operation='flip-values')
        self.write('envelope.bank.unlink',target=target);self.write('automation.pattern.transform',lane=identifier,operation='flip-values');self.assertAlmostEqual(self.lane()['points'][0]['value'],.8)

    def test_every_shared_tool_copy_clipping_seed_and_preservation(self):
        plugin,identifier=self.create(points=[dict(position=0,value=.2),dict(position=1024,value=.8),dict(position=8192,value=.4)]);original=self.lane();points=original['points'];other=self.write('pattern.create',rows=64)['pattern'];self.create(plugin,pattern=other);other_lane=self.lane(other)
        clip=self.read('automation.pattern.copy',lane=identifier,start=0,end=2048);self.assertEqual(clip['span'],2048);self.assertEqual(len(clip['points']),2);self.assertEqual(clip['unitsPerRow'],256)
        for operation,options,fields in [('flip-time',{},dict(start=0,end=2048)),('flip-values',{},{}),('shift',dict(amount=256),dict(start=0,end=2048)),('scale',dict(amount=2,offset=.2),{}),('ramp',dict(from_=0,to=1),{}),('sine',dict(cycles=2,spacing=128),{}),('humanize',dict(amount=.1,jitter=4,seed=19),{}),('paste',dict(clip=clip,repeats=2),dict(start=4096)),('insert',dict(clip=clip,repeats=1),dict(start=0))]:
            if 'from_' in options:options['from']=options.pop('from_')
            before=self.doc();preview=self.write('automation.pattern.transform',lane=identifier,operation=operation,options=options,dryRun=True,**fields);self.assertEqual(self.doc(),before);self.assertEqual(preview['before'],points)
            again=self.write('automation.pattern.transform',lane=identifier,operation=operation,options=options,dryRun=True,**fields);self.assertEqual(again,preview)
            if operation=='scale':self.assertGreater(preview['clippedValues'],0)
            result=self.write('automation.pattern.transform',lane=identifier,operation=operation,options=options,**fields);self.assertEqual(result['after'],preview['after']);self.assertEqual(self.lane()['points'],preview['after']);self.assertEqual(self.lane(other),other_lane)
            if result['wouldChange']:self.write('history.undo',domain='document')
            self.assertEqual(self.lane(),original)
        for operation,options,fields in [('shift',dict(amount=.5),{}),('humanize',dict(seed=True),{}),('paste',dict(clip=clip),dict(end=8192)),('sine',dict(spacing=0),{}),('ramp',dict(curve='invalid'),{}),('flip-time',dict(extra=True),{})]:self.rejected('automation.pattern.transform',lane=identifier,operation=operation,options=options,**fields)

    def test_disabled_lane_conflicts_and_unavailable_target_reads(self):
        plugin=self.add_gain();self.write('pattern.effects.set',pattern=0,columns=[dict(channel=0,count=2)],bindings=[dict(id=1,plugin=plugin,parameter=1)],commands=[dict(channel=0,column=1,position=0,kind='parameter-set',binding=1,value=.4)])
        self.rejected('automation.pattern.set',pattern=0,plugin=plugin,parameter=1,points=[dict(position=0,value=.5)])
        _,identifier=self.create(plugin,enabled=False);self.assertFalse(self.lane()['enabled']);points=self.lane()['points'];self.rejected('automation.pattern.set',pattern=0,plugin=plugin,parameter=1,enabled=True,points=points)
        self.write('pattern.effects.set',pattern=0,commands=[]);self.create(plugin,enabled=True);self.write('plugin.remove',slot=0);self.assertFalse(self.lane()['resolved']);self.assertEqual(self.lane()['points'],points)
        self.rejected('automation.pattern.set',pattern=0,plugin=plugin,parameter=1,enabled=False,points=points);self.write('automation.pattern.remove',lane=identifier);self.assertEqual(self.read('automation.pattern.get',pattern=0)['lanes'],[])


if __name__=='__main__':unittest.main()
