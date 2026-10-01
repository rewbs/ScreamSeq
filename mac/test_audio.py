#!/usr/bin/env python3
"""Compare the editable core against a separate stock libopenmpt process.

The real-time audit library self-tests its allocation/lock interception before
measuring each callback. Results include cold first-use callbacks.
"""
import array, json, pathlib, subprocess, tempfile, math, sys, os
root=pathlib.Path(__file__).resolve().parents[1]
binary=pathlib.Path(os.environ.get('RESONANCE_BUILD_DIR',str(root/'bin/mac-native'))).resolve()
fixtures=[root/'test'/n for n in ['test.mod','test.xm','test.s3m']]
fixtures += [pathlib.Path(tempfile.gettempdir())/'resonance-tests'/n for n in ['demo.mptm','demo.it','loops.mptm','test.mptm']]
results=[]
with tempfile.TemporaryDirectory(prefix='resonance-audio-') as directory:
    sequence_fixture = pathlib.Path(tempfile.gettempdir())/'resonance-tests/sequences.mptm'
    cases = [(source, 0, 0) for source in fixtures] + [(fixtures[3], 1, 0)]
    cases += [(sequence_fixture, 0, sequence) for sequence in (0, 1)]
    for source, order, sequence in cases:
        if not source.exists():raise SystemExit(f'Missing fixture {source}; run tracker-core-tests first.')
        for rate in (44100,48000,96000):
            actual=pathlib.Path(directory)/'actual.f32'
            reference=pathlib.Path(directory)/'reference.f32'
            audited=subprocess.run([binary/'render-audit',source,actual,str(rate),str(order),str(sequence)],capture_output=True,text=True)
            if audited.returncode:raise SystemExit(f'Audit failed: {source} @ {rate}\n{audited.stdout}\n{audited.stderr}')
            metrics=json.loads(audited.stdout.strip().splitlines()[-1])
            subprocess.run([binary/'reference-renderer',source,reference,str(rate),str(order),str(sequence)],check=True,capture_output=True)
            a=array.array('f');a.frombytes(actual.read_bytes());b=array.array('f');b.frombytes(reference.read_bytes())
            if len(a)!=len(b):raise SystemExit(f'Frame count differs: {source} @ {rate}: {len(a)//2} versus {len(b)//2}')
            if not all(math.isfinite(x) for x in a) or not all(math.isfinite(x) for x in b):
                raise SystemExit(f'Non-finite output: {source} @ {rate}')
            error=max((abs(x-y) for x,y in zip(a,b)),default=0.0)
            rms=math.sqrt(sum((x-y)**2 for x,y in zip(a,b))/max(1,len(a)))
            result=dict(file=source.name,order=order,sequence=sequence,rate=rate,maxError=error,rmsError=rms,**metrics);results.append(result)
            print(json.dumps(result),flush=True)
            if error != 0:raise SystemExit(f'Audio fidelity regression: {source} @ {rate}, peak difference {error}')
output=binary/'audio-test-results.json';output.write_text(json.dumps(results,indent=2)+'\n')
print(f'PASS {len(results)} render comparisons; zero intercepted allocation, release or mutex calls. {output}')
