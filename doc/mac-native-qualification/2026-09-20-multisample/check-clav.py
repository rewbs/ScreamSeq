"""Import the user's actual Clav recordings into a private silent disposable song."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[3]
APP = ROOT / 'bin/mac-background/Resonance.app/Contents/MacOS/Resonance'
sys.path.insert(0, str(ROOT / 'mac/Tools'))
from resonance_api import Client, endpoints

with tempfile.TemporaryDirectory(prefix='resonance-clav-') as directory:
    root = Path(directory)
    with (root/'app.log').open('w+') as log:
        process = None
        def stop():
            if process is not None and process.poll() is None:
                process.terminate(); process.wait(timeout=10)
        def launch(file=None):
            global process
            process = subprocess.Popen([str(APP), '--automation-test'] + ([str(file)] if file else []), stdout=log, stderr=log,
                env={**os.environ, 'RESONANCE_AUTOMATION_TEST_DIRECTORY': directory})
            deadline = time.monotonic()+20
            while time.monotonic()<deadline:
                assert process.poll() is None
                found = [item for item in endpoints(root) if item['pid']==process.pid]
                if found: return Client(found[0]['socket'])
                time.sleep(.05)
            raise AssertionError('No endpoint')
        def ready():
            deadline=time.monotonic()+20
            while time.monotonic()<deadline:
                status=client.call('sample.library.get')['data']
                if status['ready'] and not status['indexing']:
                    assert status['error'] is None
                    return status
                time.sleep(.05)
            raise AssertionError('Library not ready')
        def write(method, **params):
            return client.call(method, {'expectedRevision':client.call('document.get')['revision'], **params})
        try:
            client=launch()
            folder=Path.home()/'samples/Junos From Mars/WAV/01. Keys/Clav'
            client.call('sample.library.roots.set', {'roots':[str(folder)], 'expectedLibraryRevision':ready()['libraryRevision']})
            assert ready()['count']==109
            group=client.call('sample.library.multisample.get',{'path':str(folder/'077 Clav Junos F4.wav')})['data']['group']
            assert group['count']==109 and group['suggestedOctaveShift']==2
            sources=[{'path':s['path'],'rootNote':s['semitone']+25} for s in group['samples']]
            before=client.call('document.get')
            started=time.monotonic()
            check=write('instrument.importMultisample', name=group['name'], samples=sources, dryRun=True)
            assert not check['changed'] and client.call('document.get')==before
            checked=time.monotonic()-started
            started=time.monotonic()
            applied=write('instrument.importMultisample', name=group['name'], samples=sources)['data']
            elapsed=time.monotonic()-started
            assert applied['count']==109 and applied['zones']==check['data']['zones']
            instrument=applied['instrument']
            info=client.call('instrument.get',{'instrument':instrument})['data']
            assert info['noteMapping'][:109]==[61]*109 and info['mapping'][:109]==[zone['sample'] for zone in applied['zones']]
            assert info['mapping'][109:]==[0]*19
            project=root/'Clav.resonance'
            write('document.save',path=str(project))
            size=project.stat().st_size
            write('history.undo',domain='document')
            undone=client.call('document.get')['data']
            assert undone['samples']==before['data']['samples'] and undone['instruments']==before['data']['instruments']
            write('history.redo',domain='document')
            assert client.call('instrument.get',{'instrument':instrument})['data']==info
            stop(); client=launch(project)
            assert client.call('instrument.get',{'instrument':instrument})['data']==info
            print(json.dumps({'family':group['name'],'files':109,'octaveShift':2,'trackerRootRange':[1,109],
                'dryRunSeconds':checked,'importSeconds':elapsed,'projectBytes':size,'instrument':instrument,
                'undoRedo':True,'nativeRelaunch':True,'sourceFilesModified':False,'physicalAudio':False},indent=2))
        except Exception:
            log.flush();log.seek(0);print(log.read(),file=sys.stderr);raise
        finally:
            stop()
