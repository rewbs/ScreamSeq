#!/usr/bin/env python3
"""Create a disposable graph tutorial through the same API available to agents.

Starts its own headless demo session; never connects to a musician's open song.
Usage: SCREAMSEQ_BUILD_DIR=bin/mac-screamseq python3 mac/Tools/create_graph_demo.py /absolute/output.screamseq
"""
import json, os, subprocess, sys, tempfile, time
from pathlib import Path
from resonance_api import Client, endpoints

ROOT = Path(__file__).resolve().parents[2]
BUILD = Path(os.environ.get('SCREAMSEQ_BUILD_DIR', os.environ.get('RESONANCE_BUILD_DIR', ROOT / 'bin/mac-screamseq'))).resolve()
output = Path(sys.argv[1]).resolve()
if output.exists():
    raise SystemExit('Choose a new output file; this tutorial never overwrites songs.')
output.parent.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='resonance-graph-demo-') as temporary:
    root = Path(temporary)
    with (root / 'host.log').open('w+') as log:
        host = subprocess.Popen([str(BUILD / 'automation-test-host'), temporary], stdout=log, stderr=log)
        try:
            deadline = time.monotonic() + 30
            found = []
            while time.monotonic() < deadline:
                if host.poll() is not None:
                    raise RuntimeError('Tutorial host exited')
                found = endpoints(root)
                if found: break
                time.sleep(.05)
            if not found: raise RuntimeError('Tutorial API did not start')
            client = Client(found[0]['socket'])
            revision = client.call('document.get')['revision']
            def write(method, **params):
                global revision
                result = client.call(method, {'expectedRevision': revision, **params})
                revision = result['revision']
                return result['data']
            def graph_data():
                return client.call('graph.get', {'includeState': False})['data']
            def definition(identity):
                return next(d for d in graph_data()['library'] if d['id'] == identity)
            def chain(name, effects):
                identity = write('graph.create', name=name)['graph']
                current = next(n['id'] for n in definition(identity)['nodes'] if n['kind'] == 'input')
                processors = []
                for effect, title, parameters in effects:
                    current = write('graph.node.add', graph=identity, kind='plugin', name=title,
                                    plugin={'format':'Built-in','classID':effect,'name':title}, insertAfter=current)['node']
                    write('graph.plugin.set', graph=identity, node=current,
                          parameters=[{'id': key, 'value': value} for key, value in parameters.items()])
                    processors.append(current)
                d = definition(identity)
                ordered = [next(n for n in d['nodes'] if n['kind']=='input')]
                ordered += [next(n for n in d['nodes'] if n['id']==p) for p in processors]
                ordered += [next(n for n in d['nodes'] if n['kind']=='output')]
                for column, node in enumerate(ordered): node.update(x=40+240*column,y=100)
                write('graph.update', definition=d)
                return identity, processors
            write('document.patch', title='Connected Circuit — graph tutorial')
            for sample in range(1,5): write('instrument.create', sample=sample)
            write('mixer.enable')
            buses = graph_data()['mixer']['buses']
            tracks = [b for b in buses if b['kind']=='track']
            master = next(b['id'] for b in buses if b['kind']=='master')
            for bus, name in zip(tracks, ['Pulse','Sub','Kick','Hats']):
                write('mixer.bus.set', bus=bus['id'], name=name)
            liquid, liquid_nodes = chain('Liquid comb', [
                ('resonance.comb-filter.v1','Comb',{3:35,4:35,7:-3}),
                ('resonance.digital-filter.v1','Low-pass',{2:4000,3:1.2})])
            lfo = write('graph.node.add', graph=liquid, kind='lfo', name='Slow filter motion', x=520,y=270)['node']
            amount = write('graph.node.add', graph=liquid, kind='amount', name='Amount → resonance',x=280,y=270)['node']
            d = definition(liquid)
            next(n for n in d['nodes'] if n['id']==lfo)['rate']=.125
            d['modulation']=[{'source':lfo,'target':liquid_nodes[1],'parameter':2,'minimum':.06,'maximum':.3},
                             {'source':amount,'target':liquid_nodes[0],'parameter':3,'minimum':.55,'maximum':.8}]
            write('graph.update', definition=d)
            curve=write('graph.node.add',graph=liquid,kind='automation',name='Drawn cutoff',x=760,y=270)['node']
            write('graph.automation.set',graph=liquid,node=curve,pattern=0,points=[
                {'position':0,'value':0,'curve':'smooth'},
                {'position':4096,'value':.4,'curve':'exponential'},
                {'position':8192,'value':.12,'curve':'scripted','formula':'L+0.08*sin(tau*beats)'},
                {'position':16383,'value':.1,'curve':'linear'}])
            d=definition(liquid)
            d['modulation'].append({'source':curve,'target':liquid_nodes[1],'parameter':2,'minimum':0,'maximum':.15})
            write('graph.update',definition=d)
            crunch, crunch_nodes = chain('Crunch', [('resonance.distortion.v1','Soft drive',{1:10,6:-12})])
            amount = write('graph.node.add', graph=crunch, kind='amount',name='Amount → drive',x=280,y=250)['node']
            d=definition(crunch)
            d['modulation']=[{'source':amount,'target':crunch_nodes[0],'parameter':1,'minimum':.1,'maximum':.45}]
            write('graph.update', definition=d)
            trim, _ = chain('Listening trim', [('resonance.gainer.v1','Output trim',{1:-9})])
            instrument_trim, _ = chain('Instrument character', [('resonance.gainer.v1','Instrument trim',{1:-3})])
            write('graph.instrument.assign',instrument=1,graph=instrument_trim)
            write('graph.assign',target=master,graph=trim)
            lead=tracks[0]['id'];drums=tracks[2]['id']
            write('graph.assign',target=lead,graph=liquid,amount=.25,wet=.35)
            events=[]
            def event(row, kind, graph=crunch, target=lead, column=0, amount=.5, wet=1, tails=False, offset=0):
                events.append(dict(target=target,graph=graph,position=row*65536+offset,column=column,
                                   kind=kind,amount=amount,wet=wet,tails=tails))
            event(0,'clear',graph='')
            for row in [4,12,20,28]: event(row,'row',amount=.3+(row/40)*.5)
            event(32,'start',amount=.4)
            event(40,'start',graph=liquid,column=1,amount=.6,wet=.45)
            event(44,'amount',amount=.85,offset=32768)
            event(48,'stop')
            event(56,'clear',graph='',tails=True)
            for row in [8,24,40,56]: event(row,'row',graph=liquid,target=drums,amount=.5,wet=.5)
            write('graph.commands.set',pattern=0,lanes=[{'target':lead,'count':2},{'target':drums,'count':1}],commands=events)
            light=graph_data()
            assert all('state' not in node.get('plugin',{}) for d in light['library'] for node in d['nodes'])
            write('document.save',path=str(output))
            output.with_suffix('.graph.json').write_text(json.dumps(light,indent=2)+'\n')
            print(output)
        except Exception:
            log.flush();log.seek(0);print(log.read(),file=sys.stderr);raise
        finally:
            host.terminate();host.wait(timeout=10)
