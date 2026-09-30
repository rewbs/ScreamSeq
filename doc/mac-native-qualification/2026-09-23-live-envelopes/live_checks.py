import sys,json,time
from pathlib import Path
sys.path.insert(0,str(Path.cwd()/'mac/Tools'))
from screamseq_api import Client
c=Client(pid=int(sys.argv[1]))
checks=[]
def read(method,**p):return c.call(method,p)['data']
def write(method,**p):
 r=c.call(method,{'expectedRevision':c.call('document.get')['revision'],**p})
 checks.append({'method':method,'stopped':r['playbackStopped'],'changed':r['changed']})
 return r
mode=sys.argv[2]
if mode=='setup':
 desc={'format':'VST3','type':0,'subtype':0,'manufacturer':0,'name':'Resonance Test Gain','path':str(Path.cwd()/'bin/mac-live-envelopes/test-plugins/ResonanceFixture.vst3'),'classID':'5245534F4E414E434546464543540001','isInstrument':False}
 write('plugin.add',descriptor=desc)
 write('plugin.add',descriptor={'format':'Built-in','type':0,'subtype':0,'manufacturer':0,'classID':'resonance.eq10.v1','name':'EQ10'})
 write('plugin.parameters.set',slot=0,values=[{'id':7,'value':.6}])
 print(json.dumps({'plugins':read('document.get')['nativePlugins'],'samples':read('document.get')['samples'],'parameters':read('plugin.parameters.get',slot=1)[:5]},indent=2))
elif mode=='audio':
 plugins=read('document.get')['nativePlugins'];plugin=plugins[0]['instanceID']
 write('transport.play',pattern=0,startRow=0,endRow=64,loop=True)
 time.sleep(.2)
 before=read('transport.get')
 lane=write('automation.pattern.set',pattern=0,plugin=plugin,parameter=7,points=[{'position':0,'value':0,'curve':'step'}])['data']['lane']
 time.sleep(.1);muted=read('transport.get')
 assert muted['playing'] and muted['frames']>before['frames'],muted
 assert max(muted['left'],muted['right'])<1e-6,muted
 write('automation.pattern.set',pattern=0,plugin=plugin,parameter=7,points=[{'position':0,'value':1,'curve':'step'}])
 time.sleep(.1);audible=read('transport.get')
 assert audible['playing'] and audible['frames']>muted['frames'] and max(audible['left'],audible['right'])>1e-4,audible
 write('history.undo',domain='document');time.sleep(.1);undo=read('transport.get')
 assert undo['playing'] and max(undo['left'],undo['right'])<1e-6,undo
 write('history.redo',domain='document');time.sleep(.1);redo=read('transport.get')
 assert redo['playing'] and max(redo['left'],redo['right'])>1e-4,redo
 # Return a visible ramp for the UI drag/cursor checks.
 write('automation.pattern.set',pattern=0,plugin=plugin,parameter=7,points=[{'position':0,'value':.2,'curve':'smooth'},{'position':8192,'value':.8,'curve':'linear'},{'position':16383,'value':.2,'curve':'linear'}])
 loop=write('sample.loops.set',sample=1,normal={'start':32,'end':224,'enabled':True},sustain={'start':64,'end':192,'enabled':True})
 time.sleep(.1);after=read('transport.get')
 assert after['playing'] and after['frames']>redo['frames'] and not loop['playbackStopped'],after
 assert all(not x['stopped'] for x in checks),checks
 Path('bin/mac-live-envelopes/qualification/live-audio.json').write_text(json.dumps({'before':before,'muted':muted,'audible':audible,'undo':undo,'redo':redo,'after':after,'operations':checks},indent=2))
 print(json.dumps({'muted':muted,'audible':audible,'after':after},indent=2))
