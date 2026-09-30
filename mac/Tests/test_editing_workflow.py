#!/usr/bin/env python3
"""Fast-input guards and precise clipboard through a disposable app's real socket."""
import os, sys, subprocess, tempfile, time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
BUILD=Path(os.environ.get('RESONANCE_BUILD_DIR',ROOT/'bin/mac-background')).resolve()
sys.path.insert(0,str(ROOT/'mac/Tools'))
from resonance_api import Client,endpoints,APIError
with tempfile.TemporaryDirectory(prefix='screamseq-editing-') as temporary:
    with open(Path(temporary)/'app.log','w+') as log:
        app=subprocess.Popen([str(BUILD/'ScreamSeq.app/Contents/MacOS/ScreamSeq'),'--automation-test'],stdout=log,stderr=log,env={**os.environ,'RESONANCE_AUTOMATION_TEST_DIRECTORY':temporary})
        try:
            found=[];deadline=time.monotonic()+30
            while not found and time.monotonic()<deadline:
                assert app.poll() is None
                found=endpoints(Path(temporary));time.sleep(.05)
            assert found
            c=Client(found[0]['socket'])
            def write(method,**params):return c.call(method,{'expectedRevision':c.call('document.get')['revision'],**params})
            def reject(code,fn):
                try:fn()
                except APIError as error:assert error.code==code,(error.code,str(error))
                else:raise AssertionError('Invalid operation accepted')
            before=c.call('document.get');context=c.call('context.get')
            guards={'expectedRevision':context['revision'],'expectedContext':context['data']['contextRevision']}
            reject(-32602,lambda:c.call('workspace.input',{**guards,'instrument':2,'octave':True}))
            assert c.call('context.get')==context
            updated=c.call('workspace.input',{**guards,'instrument':2,'octave':6})
            assert updated['data']['instrument']==2 and updated['data']['octave']==6
            reject(-32001,lambda:c.call('workspace.input',{**guards,'instrument':3}))
            assert c.call('document.get')==before
            assert 'workspace.input' in c.call('api.describe')['data']['writes']
            original=[{'channel':0,'position':8192,'note':49,'instrument':1,'velocity':87},
                      {'channel':1,'position':14000,'note':61,'instrument':2,'velocity':99}]
            write('pattern.notes.set',pattern=0,events=original)
            notes=lambda:c.call('pattern.notes.get',{'pattern':0})['data']['events']
            clear={'operation':'clear','pattern':0,'startRow':0,'rowCount':1,'startChannel':0,'channelCount':1}
            assert not write('pattern.transform',**clear,dryRun=True)['changed'] and notes()==original
            write('pattern.transform',**clear);assert notes()==[original[1]]
            write('history.undo',domain='document');assert notes()==original
            write('history.redo',domain='document');assert notes()==[original[1]]
            paste={'pattern':0,'startRow':2,'startChannel':2,'rows':1,'channels':1,'cells':[[0]*6],'notes':[original[0]]}
            assert not write('pattern.paste',**paste,dryRun=True)['changed']
            write('pattern.paste',**paste)
            pasted={**original[0],'channel':2,'position':2*65536+8192}
            assert notes()==[original[1],pasted],notes()
            assert not write('pattern.paste',**paste)['changed'],'Identical paste must be revision-neutral'
            reject(-32602,lambda:write('pattern.paste',**{**paste,'notes':[original[0],original[0]]}))
            assert notes()==[original[1],pasted]
            write('pattern.paste',**{**paste,'notes':[]});assert notes()==[original[1]]
            write('history.undo',domain='document');assert notes()==[original[1],pasted]
            tempo=c.call('document.timing.get')['data']
            write('document.timing.set',tempo=137.25)
            assert c.call('document.timing.get')['data']['tempo']==137.25
            write('history.undo',domain='document');assert c.call('document.timing.get')['data']==tempo
            saved=Path(temporary)/'clipboard.screamseq';write('document.save',path=str(saved))
            app.terminate();app.wait(timeout=10)
            app=subprocess.Popen([str(BUILD/'ScreamSeq.app/Contents/MacOS/ScreamSeq'),'--automation-test',str(saved)],stdout=log,stderr=log,env={**os.environ,'RESONANCE_AUTOMATION_TEST_DIRECTORY':temporary})
            found=[];deadline=time.monotonic()+30
            while not found and time.monotonic()<deadline:
                assert app.poll() is None
                found=[e for e in endpoints(Path(temporary)) if e['pid']==app.pid];time.sleep(.05)
            assert found;c=Client(found[0]['socket'])
            assert notes()==[original[1],pasted],notes()
            print('PASS real socket: guarded input, malformed/stale atomicity, precise cut/paste, dry run, no-op, Undo/Redo, native save/reopen and fractional BPM')
        except Exception:
            log.flush();log.seek(0);print(log.read(),file=sys.stderr);raise
        finally:
            app.terminate();app.wait(timeout=10)
