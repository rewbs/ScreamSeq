#!/usr/bin/env python3
"""Record scratch editing through a disposable app's actual socket."""
import os,sys,subprocess,tempfile,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
BUILD=Path(os.environ.get('RESONANCE_BUILD_DIR',ROOT/'bin/mac-background')).resolve()
sys.path.insert(0,str(ROOT/'mac/Tools'))
from resonance_api import Client,endpoints,APIError
with tempfile.TemporaryDirectory(prefix='screamseq-nudge-') as directory:
    with open(Path(directory)/'app.log','w+') as log:
        app=subprocess.Popen([str(BUILD/'ScreamSeq.app/Contents/MacOS/ScreamSeq'),'--automation-test'],stdout=log,stderr=log,env={**os.environ,'RESONANCE_AUTOMATION_TEST_DIRECTORY':directory})
        try:
            found=[];deadline=time.monotonic()+30
            while not found and time.monotonic()<deadline:
                assert app.poll() is None
                found=[e for e in endpoints(Path(directory)) if e['pid']==app.pid];time.sleep(.05)
            assert found;c=Client(found[0]['socket'])
            def write(method,**p):return c.call(method,{'expectedRevision':c.call('document.get')['revision'],**p})
            def read():return c.call('pattern.effects.get',{'pattern':0})['data']['commands']
            description=c.call('api.describe')['data']['patternPerformance']
            assert description['displayCodes']['nudge-reverse']=='NR'
            assert 'nudge-forward' in description['kinds']
            write('pattern.effect.set',pattern=0,row=1,channel=0,column=0,command={'kind':'note-cut','offset':32768})
            original=read();assert len(original)==1
            effect={'pattern':0,'row':2,'channel':0,'column':0,'command':{'kind':'nudge-reverse','value':.75,'offset':12345,'duration':70001}}
            assert not write('pattern.effect.set',**effect,dryRun=True)['changed'] and read()==original
            write('pattern.effect.set',**effect);saved=read()
            assert len(saved)==2 and saved[0]==original[0] and saved[1]['value']==.75 and saved[1]['duration']==70001
            assert not write('pattern.effect.set',**effect)['changed']
            write('history.undo',domain='document');assert read()==original
            write('history.redo',domain='document');assert read()==saved
            # All eight columns accept both commands. Direction survives copy/paste.
            write('pattern.effects.set',pattern=0,columns=[{'channel':0,'count':8}])
            write('pattern.effect.set',pattern=0,row=3,channel=0,column=7,command={'kind':'nudge-forward','value':.625,'duration':65536})
            effect=read()[-1];assert effect['column']==7 and effect['kind']=='nudge-forward'
            paste={k:v for k,v in effect.items() if k not in ('track',)};paste['position']=0
            write('pattern.paste',pattern=0,startRow=5,startChannel=0,rows=1,channels=1,cells=[[0]*6],effects=[paste],bindings=[])
            assert any(e['position']==5*65536 and e['column']==7 and e['kind']=='nudge-forward' for e in read())
            print('PASS real socket: NF/NR discovery, cell edit, unrelated preservation, dry-run/no-op, Undo/Redo, eight columns and clipboard')
        except Exception:
            log.flush();log.seek(0);print(log.read(),file=sys.stderr);raise
        finally:
            app.terminate();app.wait(timeout=10)
