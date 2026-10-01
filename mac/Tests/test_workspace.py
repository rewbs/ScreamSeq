#!/usr/bin/env python3
"""Exercise panel navigation through the actual application's private API."""
import json,os,subprocess,sys,tempfile,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
BUILD=Path(os.environ.get('RESONANCE_BUILD_DIR',str(ROOT/'bin/mac-background'))).resolve()
sys.path.insert(0,str(ROOT/'mac/Tools'))
from resonance_api import Client,endpoints,APIError
with tempfile.TemporaryDirectory(prefix='resonance-workspace-') as temporary:
    root=Path(temporary)
    with (root/'app.log').open('w+') as log:
        app=subprocess.Popen([str(BUILD/'ScreamSeq.app/Contents/MacOS/ScreamSeq'),'--automation-test'],stdout=log,stderr=log,env={**os.environ,'RESONANCE_AUTOMATION_TEST_DIRECTORY':temporary})
        try:
            deadline=time.monotonic()+30
            while time.monotonic()<deadline:
                assert app.poll() is None
                found=endpoints(root)
                if found:break
                time.sleep(.05)
            assert found
            client=Client(found[0]['socket'])
            before=client.call('document.get')
            read=lambda:client.call('workspace.get')['data']
            state=read();assert {'notes','samples','instruments','plugins','automation','mixer'}<=set(state['panels'])
            client.call('workspace.panel',{'panel':'notes','pinned':True})
            assert read()['pins']['notes']
            client.call('workspace.layout',{'name':'Pattern focus'})
            assert read()['focusLayout'] and not read()['visible']
            client.call('workspace.layout',{'name':'Compose'})
            assert not read()['focusLayout'] and read()['pins']['notes']
            client.call('workspace.panel',{'panel':'notes','placement':'bottom'})
            state=read();assert state['locations']['notes']=='bottom'
            try:client.call('workspace.panel',{'panel':'notes','placement':'right','pinned':1})
            except APIError as error:assert error.code==-32602
            else:raise AssertionError('Boolean accepted integer')
            assert read()['locations']['notes']=='bottom','Rejected changes are atomic'
            client.call('workspace.panel',{'panel':'notes','follow':True})
            assert not read()['pins']['notes']
            context=client.call('context.get');value=context['data']
            client.call('context.set',{'expectedRevision':context['revision'],'expectedContext':value['contextRevision'],'row':4,'following':False})
            client.call('workspace.panel',{'panel':'notes','follow':True})
            assert 'R4' in read()['targets']['notes']
            client.call('workspace.panel',{'panel':'notes','return':True})
            assert client.call('context.get')['data']['row']==0
            shortcuts=client.call('workspace.commands.get')['data']['commands']
            command=next(c for c in shortcuts if 'Graph' in c['name'])
            original=command['keys']
            try:
                client.call('workspace.shortcut.set',{'command':command['id'],'keys':['ctrl+opt+shift+g','m']})
                configured=client.call('workspace.commands.get')['data']['commands']
                assert next(c for c in configured if c['id']==command['id'])['keys']==['ctrl+opt+shift+g','m']
                try:client.call('workspace.shortcut.set',{'command':command['id'],'keys':['g','m']})
                except APIError as error:assert error.code==-32602
                else:raise AssertionError('Plain note entry prefix accepted')
                assert client.call('workspace.commands.get')['data']['commands']==configured
            finally:
                client.call('workspace.shortcut.set',{'command':command['id'],'keys':original})
            assert client.call('document.get')==before
            advertised=client.call('api.describe')['data']
            assert {'workspace.get','workspace.panel','workspace.layout','workspace.commands.get','workspace.shortcut.set'}<=set(advertised['reads']+advertised['writes'])
            print('PASS actual app workspace: independent pins, pattern focus/compose, moves, rejected-edit atomicity, cursor follow/return, unchanged song and API discovery')
        except Exception:
            log.flush();log.seek(0);print(log.read(),file=sys.stderr);raise
        finally:
            app.terminate();app.wait(timeout=10)
