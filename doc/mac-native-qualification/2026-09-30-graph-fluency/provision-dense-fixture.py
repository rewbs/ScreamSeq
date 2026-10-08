import sys,json,pathlib
sys.path.insert(0,'mac/Tools')
from resonance_api import Client
c=Client(pid=85720);log=[]
def read(method,**p):return c.call(method,p)['data']
def write(method,**p):
 p['expectedRevision']=c.call('document.get')['revision'];r=c.call(method,p)
 log.append({'method':method,'params':p,'result':r});return r['data']
doc=read('document.get');assert doc['channels']==16
if not doc['instruments']:write('instrument.create',sample=1)
tracks=sorted(doc['tracks'],key=lambda x:x['index']);ids=[x['id'] for x in tracks]
cells=[]
for row in range(64):
 for ch in range(16):
  on=row%4==0
  cells.append(dict(pattern=0,row=row,channel=ch,note=(37+ch%24) if on else 0,instrument=(1+ch%2) if on else 0,volumeCommand=1 if on else 0,volume=10 if on else 0,effect=0,parameter=0))
write('pattern.apply',cells=cells)
g=read('graph.get');master=next(b['id'] for b in g['mixer']['buses'] if b['kind']=='master')
group=next(b['id'] for b in g['mixer']['buses'] if b['kind']=='group');ret=next(b['id'] for b in g['mixer']['buses'] if b['kind']=='return')
write('mixer.bus.set',bus=group,name='Rhythm group')
for ch in range(4,8):write('mixer.bus.set',bus=ids[ch],output=group)
write('mixer.bus.set',bus=ids[0],name='Layer with a deliberately long duplicate name')
write('mixer.bus.set',bus=ids[1],name='Layer with a deliberately long duplicate name')
reverb=next(p['id'] for p in g['plugins'] if p['name']=='Apple: AUReverb2')
write('mixer.inserts.move',plugins=[reverb],target=ret)
created=write('mixer.bus.add',kind='return',name='Comb return',output=master)
ret2=created.get('bus') or created.get('id');assert isinstance(ret2,str),created
write('plugin.add',descriptor=dict(format='Built-in',classID='resonance.comb-filter.v1',type=0,subtype=0,manufacturer=0),target=ret2)
for ch in [0,3]:
 bus=next(b for b in read('mixer.get')['buses'] if b['id']==ids[ch]);sends=bus['sends']
 sends.append(dict(target=ret2,gainDB=-18,preFader=False,enabled=True));write('mixer.sends.set',bus=ids[ch],sends=sends)
g=read('graph.get');recipe=g['library'][0];graph=recipe['id']
lfo=write('graph.node.add',graph=graph,kind='lfo',name='Slow secondary motion')['node']
recipe=next(x for x in read('graph.get')['library'] if x['id']==graph)
recipe['modulation'].append(dict(source=lfo,target='n29',parameter=2,minimum=0.0,maximum=0.03,base=recipe['modulation'][0]['base'],enabled=True))
write('graph.update',definition=recipe)
for ch in [3,7,11,15]:write('graph.assign',target=ids[ch],graph=graph,amount=.3,wet=.5)
write('graph.instrument.assign',instrument=1,graph=graph,amount=.2,wet=.3)
write('graph.commands.set',pattern=0,lanes=[dict(target=ids[3],count=2)],commands=[dict(target=ids[3],graph=graph,position=0,column=0,kind='start',amount=.4,wet=.3),dict(target=ids[3],graph=graph,position=16*65536,column=1,kind='row',amount=.2,wet=.4)])
path=str(pathlib.Path('bin/mac-graph-fluency/dense-graph-fixture.screamseq').resolve())
write('document.save',path=path)
pathlib.Path('doc/mac-native-qualification/2026-09-30-graph-fluency/logs/dense-fixture-preparation.json').write_text(json.dumps(log,indent=2)+'\n')
print('Saved',path,'with',len(log),'API writes')
