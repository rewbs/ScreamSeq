#!/usr/bin/env python3
"""Compare the editable core against a separate stock libopenmpt process.

The real-time audit library self-tests its allocation/lock interception before
measuring each callback. Results include cold first-use callbacks.
"""
import array, hashlib, json, pathlib, subprocess, tempfile, math, sys, os
root=pathlib.Path(__file__).resolve().parents[1]
binary=pathlib.Path(os.environ.get('SCREAMSEQ_BUILD_DIR',os.environ.get('RESONANCE_BUILD_DIR',str(root/'bin/mac-native')))).resolve()
blocks = tuple(int(value) for value in os.environ.get('SCREAMSEQ_AUDIO_BLOCKS', '512').split(','))
if not blocks or len(set(blocks)) != len(blocks) or any(block < 1 or block > 4096 for block in blocks):
    raise SystemExit('SCREAMSEQ_AUDIO_BLOCKS must contain distinct integers in 1..4096')
oracle = json.loads((binary/'reference-source.json').read_text())
if oracle['upstreamRevision'] != 'f83cedb0cd5446e4dfaa83ac97e3087107e26767' or oracle['stockTests'] != 'passed':
    raise SystemExit('Require the pinned stock oracle and its successful upstream tests')
for path, expected in ((binary/'reference-renderer', oracle['rendererSHA256']),
                       (pathlib.Path(oracle['sourceDirectory'])/'bin/screamseq-reference/libopenmpt.a', oracle['librarySHA256'])):
    if hashlib.sha256(path.read_bytes()).hexdigest() != expected:
        raise SystemExit(f'Stock oracle differs from its source receipt: {path}')
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
            baseline = None
            for block in blocks:
                actual=pathlib.Path(directory)/'actual.f32'
                reference=pathlib.Path(directory)/'reference.f32'
                arguments=[str(rate),str(order),str(sequence),str(block)]
                audited=subprocess.run([binary/'render-audit',source,actual,*arguments],capture_output=True,text=True)
                if audited.returncode:raise SystemExit(f'Audit failed: {source} @ {rate}/{block}\n{audited.stdout}\n{audited.stderr}')
                metrics=json.loads(audited.stdout.strip().splitlines()[-1])
                if metrics.get('blockFrames') != block:
                    raise SystemExit('Render audit did not confirm the requested block size; rebuild the harness at the next checkpoint.')
                subprocess.run([binary/'reference-renderer',source,reference,*arguments],check=True,capture_output=True)
                a=array.array('f');a.frombytes(actual.read_bytes());b=array.array('f');b.frombytes(reference.read_bytes())
                if len(a)!=len(b):raise SystemExit(f'Frame count differs: {source} @ {rate}/{block}: {len(a)//2} versus {len(b)//2}')
                if not all(math.isfinite(x) for x in a) or not all(math.isfinite(x) for x in b):
                    raise SystemExit(f'Non-finite output: {source} @ {rate}/{block}')
                error=max((abs(x-y) for x,y in zip(a,b)),default=0.0)
                rms=math.sqrt(sum((x-y)**2 for x,y in zip(a,b))/max(1,len(a)))
                result=dict(file=source.name,order=order,sequence=sequence,rate=rate,maxError=error,rmsError=rms,**metrics)
                # Characterize upstream partition dependence separately from
                # the mandatory exact editable-core/stock comparison above.
                # The 30-second harness cap may also give different lengths.
                if baseline is not None:
                    result['stockPartitionComparison'] = dict(baselineBlock=blocks[0], baselineFrames=len(baseline)//2,
                        frames=len(b)//2, changedSamples=sum(x!=y for x,y in zip(b,baseline)),
                        maxError=max((abs(x-y) for x,y in zip(b,baseline)),default=0.0))
                else:
                    baseline = b
                results.append(result)
                print(json.dumps(result),flush=True)
                if error != 0:raise SystemExit(f'Audio fidelity regression: {source} @ {rate}/{block}, peak difference {error}')
output=binary/'audio-test-results.json';output.write_text(json.dumps(results,indent=2)+'\n')
print(f'PASS {len(results)} render comparisons; zero intercepted allocation, release or mutex calls. {output}')
