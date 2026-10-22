"""Bounded actual-API PCM evidence for the deterministic F04/VST3 journey.

This is a pattern-local 48 kHz render, not a device, latency or vendor capacity
benchmark. Native layers have separate independent-stem and callback audits.
"""
import array
import base64
import sys
from pathlib import Path

from native_roundtrip import write_json
from project_tree import EvidenceError, differences, read_bytes, read_project, sha256


def run(client, baseline, recorded, output):
    baseline, recorded, output = Path(baseline), Path(recorded), Path(output)
    output.mkdir(exist_ok=False)
    immutable = {p: sha256(read_bytes(p)) for p in (baseline, recorded)}
    report = dict(format='screamseq-recording-render-v1', passed=False,
                  sourceSHA256={p.name: h for p, h in immutable.items()},
                  toolSHA256=sha256(read_bytes(Path(__file__))), renders=[],
                  scope='Pattern 0, all channels, 48 kHz stereo signed16; no tail',
                  deviceQualified=False, independentLayersQualified=False)

    def require(condition, message):
        if not condition:
            raise EvidenceError(message)

    def read(method, **params):
        return client.call(method, params)['data']

    def write(method, **params):
        return client.call(method, dict(expectedRevision=client.call('document.get')['revision'], **params))['data']

    def render(path, name):
        write('document.open', path=str(path), discard=True)
        source = read_project(path)
        pattern = read('pattern.get', pattern=0, rowCount=1, channelCount=1)
        params = dict(pattern=0, firstRow=0, lastRow=pattern['rows'] - 1,
                      firstChannel=0, lastChannel=pattern['channels'] - 1,
                      name='Recording replay evidence', createInstrument=False, tailSeconds=0)
        before = client.call('document.get')
        preview = write('sample.renderSelection', **params, dryRun=True)
        require(preview['dryRun'] and preview['frames'] == 0 and client.call('document.get') == before,
                'Render validation changed song/history or performed rendering')
        result = write('sample.renderSelection', **params)
        require(result['rate'] == 48000 and result['channels'] == 2 and result['bits'] == 16,
                'Composing replay changed its canonical PCM format')
        frames = result['frames']
        require(0 < frames <= 48000 * 20, 'F04 replay exceeds its bounded 20-second evidence window')
        pcm = bytearray()
        for start in range(0, frames, 65536):
            count = min(65536, frames - start)
            page = read('sample.pcm.get', sample=result['sample'], start=start, frames=count)
            require(page['format'] == 's16le' and page['channels'] == 2 and page['rate'] == 48000 and
                    page['start'] == start and page['frames'] == count and page['totalFrames'] == frames,
                    'Rendered sample PCM page identity or format changed')
            data = base64.b64decode(page['data'], validate=True)
            require(len(data) == count * 4, 'Rendered sample page is incomplete')
            pcm.extend(data)
        values = array.array('h', pcm)
        if sys.byteorder != 'little':
            values.byteswap()
        peak = max(abs(value) for value in values)
        require(peak > 2, 'The composed render is silent')
        (output / (name + '.s16le')).write_bytes(pcm)
        row = dict(name=name, source=path.name, result=result, frames=frames,
                   pcmSHA256=sha256(pcm), peak=peak,
                   meanSquare=sum(value * value for value in values) / len(values))
        report['renders'].append(row)
        # Rendering appends a real sample. One Undo must remove that addition
        # without changing any other musical or retained project field.
        write('history.undo')
        restored = output / (name + '-undo.screamseq')
        write('document.save', path=str(restored), overwrite=False)
        row['undoPreservation'] = differences(source, read_project(restored))
        require(row['undoPreservation']['equal'], 'Render Undo changed unrelated saved project fields')
        return values, bytes(pcm)

    try:
        baseline_tree, recorded_tree = read_project(baseline), read_project(recorded)
        report['onlyAutomationChanged'] = differences(dict(baseline_tree, automation=recorded_tree['automation']), recorded_tree)
        require(report['onlyAutomationChanged']['equal'] and baseline_tree['automation'] != recorded_tree['automation'],
                'PCM comparison inputs must differ only by the committed recording')
        before_values, before_pcm = render(baseline, 'before-recording')
        after_values, after_pcm = render(recorded, 'recorded')
        reopened_values, reopened_pcm = render(recorded, 'reopened')
        require(len(before_values) == len(after_values) == len(reopened_values),
                'Parameter recording changed render duration')
        # Clipping is reported by the renderer. Do not count saturated samples
        # as evidence of useful parameter replay in this dense musical fixture.
        delta = [abs(a - b) for a, b in zip(before_values, after_values) if abs(a) < 32767 and abs(b) < 32767]
        report['recordedDifference'] = dict(peak=max(delta, default=0),
                                          changedUnsaturatedSamples=sum(value > 2 for value in delta))
        require(report['recordedDifference']['changedUnsaturatedSamples'] >= 64 and before_pcm != after_pcm,
                'Saved parameter recording did not change audible replay')
        require(after_pcm == reopened_pcm, 'Reopening changed the exact recorded replay PCM')
        report['reopenedPCMExact'] = True
        write('document.open', path=str(recorded), discard=True)
        report['passed'] = True
        return report
    except Exception as error:
        report['error'] = dict(type=type(error).__name__, message=str(error))
        raise
    finally:
        report['inputFilesUnchanged'] = all(sha256(read_bytes(p)) == value for p, value in immutable.items())
        if not report['inputFilesUnchanged']:
            report['passed'] = False
        write_json(output / 'report.json', report)
        if not report['inputFilesUnchanged']:
            raise EvidenceError('Recording render changed an original saved input')
