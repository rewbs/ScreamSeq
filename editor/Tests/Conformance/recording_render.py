"""Bounded actual-API PCM evidence for the deterministic F04/VST3 journey.

This is a pattern-local 48 kHz render, not a device, latency or vendor capacity
benchmark. Optional layer comparisons use an explicitly bypassed effect-rack
derivative; native callback allocation audits remain separate.
"""
import array
import base64
import sys
from pathlib import Path

from native_roundtrip import write_json
from project_tree import EvidenceError, differences, read_bytes, read_project, sha256


def run(client, baseline, recorded, output, *, layered_instrument=None, layered_plugin=None):
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
        added = next(sample for sample in read('document.get')['samples'] if sample['index'] == result['sample'])
        next_id = source['native']['nextID']
        require(added['id'] == 'n' + str(next_id), 'Rendered sample did not consume the expected single stable identity')
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
        # Rendering appends one sample identity. Document::undo deliberately
        # keeps the allocator high-water mark so that ID cannot be reused.
        # Assert that exact allocation; this is not a general ignore rule.
        write('history.undo')
        restored = output / (name + '-undo.screamseq')
        write('document.save', path=str(restored), overwrite=False)
        # The strict plist reader returns dictionaries which reject replacing
        # an existing key (to catch duplicates on disk). Build new expectation
        # dictionaries rather than mutating that read-only parsed reference.
        expected = dict(source, native=dict(source['native'], nextID=next_id + 1))
        row['allocatedSampleID'] = added['id']
        row['expectedNextIDAfterUndo'] = next_id + 1
        row['undoPreservation'] = differences(expected, read_project(restored))
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
        if layered_instrument is not None or layered_plugin is not None:
            require(isinstance(layered_instrument, int) and not isinstance(layered_instrument, bool) and
                    layered_instrument > 0 and isinstance(layered_plugin, str) and layered_plugin,
                    'Layer evidence needs an explicit instrument slot and stable plugin identity')
            write('document.open', path=str(recorded), discard=True)
            instrument = read('instrument.get', instrument=layered_instrument)
            assignment = read('plugin.instruments.get', plugin=layered_plugin)
            opaque = read('plugin.state.get', plugin=layered_plugin)
            require(any(instrument['mapping']), 'Layer evidence requires an existing sample keymap')
            require(len(assignment['assignments']) == 1 and
                    assignment['assignments'][0]['instrument'] == layered_instrument,
                    'Layer evidence requires exactly one explicit plugin assignment')
            # This deterministic VST3 fixture emits DC while notes are held.
            # F04 deliberately contains DC removal and a gate. Their suppression
            # of an isolated DC stem is not proof of missing instrument input.
            # Keep the original processed recording/reopen check above; isolate
            # source contributions in a separately saved, explicitly bypassed
            # rack derivative. Exact typed comparison permits only those flags.
            bypassed = []
            for entry in recorded_tree['plugins']:
                if not entry['isInstrument'] and not entry['bypass']:
                    write('plugin.bypass', plugin=entry['instanceID'], bypass=True)
                    bypassed.append(entry['instanceID'])
            layer_source = output / 'layer-source.screamseq'
            write('document.save', path=str(layer_source), overwrite=False)
            expected = dict(recorded_tree, plugins=[dict(p, bypass=True) if p['instanceID'] in bypassed else p
                                                   for p in recorded_tree['plugins']])
            report['layerSource'] = dict(file=layer_source.name, bypassedPlugins=bypassed,
                reason='Constant-DC fixture source isolation before downstream DC removal and gating',
                preservation=differences(expected, read_project(layer_source)))
            require(report['layerSource']['preservation']['equal'],
                    'Layer-source preparation changed more than the declared effect bypass flags')
            immutable[layer_source] = sha256(read_bytes(layer_source))
            report['sourceSHA256'][layer_source.name] = immutable[layer_source]
            both_values, _ = render(layer_source, 'both-layers')
            layers = {'both-layers': both_values}
            # Compare four versions of the same song. Other tracks, routing and
            # recorded automation stay intact. Do not assume a linear sum through
            # the common graph or count saturated PCM as audible contribution.
            for name, sample, plugin in (('sample-layer', True, False),
                                         ('plugin-layer', False, True),
                                         ('neither-layer', False, False)):
                write('document.open', path=str(layer_source), discard=True)
                if not sample:
                    write('instrument.patch', instrument=layered_instrument,
                          values=dict(mapping=[0] * len(instrument['mapping'])))
                if not plugin:
                    write('instrument.plugin.set', instrument=layered_instrument, plugin='')
                expected_instrument = dict(instrument, mapping=instrument['mapping'] if sample else
                                           [0] * len(instrument['mapping']))
                require(read('instrument.get', instrument=layered_instrument) == expected_instrument,
                        'Layer isolation changed unrelated instrument settings')
                require(read('plugin.state.get', plugin=layered_plugin) == opaque,
                        'Layer isolation changed opaque plugin state')
                path = output / (name + '.screamseq')
                write('document.save', path=str(path), overwrite=False)
                immutable[path] = sha256(read_bytes(path))
                report['sourceSHA256'][path.name] = immutable[path]
                layers[name], _ = render(path, name)
            comparisons = []
            report['layerContributions'] = comparisons
            for present, absent in (('both-layers', 'sample-layer'), ('both-layers', 'plugin-layer'),
                                    ('sample-layer', 'neither-layer'), ('plugin-layer', 'neither-layer')):
                a, b = layers[present], layers[absent]
                require(len(a) == len(b), 'Removing one layer changed render duration')
                delta = [abs(x - y) for x, y in zip(a, b) if abs(x) < 32767 and abs(y) < 32767]
                result = dict(present=present, absent=absent, peak=max(delta, default=0),
                              changedUnsaturatedSamples=sum(value > 2 for value in delta))
                comparisons.append(result)
                require(result['changedUnsaturatedSamples'] >= 64,
                        present + ' versus ' + absent + ': missing audible layer contribution')
            report['independentLayersQualified'] = True
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
