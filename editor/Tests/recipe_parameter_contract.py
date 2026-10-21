"""Shared recipe baseline contract through an owned native socket/pipe."""
import copy
from pathlib import Path
import plistlib
from Conformance.project_tree import typed


def recipe_parameter_contract(client, directory, expect_error):
    def doc():
        return client.call('document.get')

    def read(method, **params):
        return client.call(method, params)['data']

    def write(method, **params):
        return client.call(method, dict(expectedRevision=doc()['revision'], **params))

    gain = next(p for p in read('plugin.discover', format='Built-in')
                if p['classID'] == 'resonance.gainer.v1')
    write('plugin.add', descriptor=gain)
    slot = len(doc()['data']['nativePlugins']) - 1
    rack = read('plugin.state.get', slot=slot)
    graph = write('graph.create', name='Exact recipe baseline')['data']['graph']
    node = write('graph.node.add', graph=graph, kind='plugin', slot=slot,
                 insertEdge=0)['data']['node']
    sources = [write('graph.node.add', graph=graph, kind='lfo')['data']['node']
               for _ in range(2)]

    def definition():
        return next(g for g in read('graph.get')['library'] if g['id'] == graph)

    def parameter():
        return next(p for p in read('graph.plugin.get', graph=graph, node=node)['parameters']
                    if p['id'] == 1)

    configured = definition()
    configured['modulation'] = [dict(source=source, target=node, parameter=1,
        minimum=0, maximum=0, base=base, enabled=enabled, quantized=False)
        for source, base, enabled in zip(sources, [.123456789, .123456789], [True, False])]
    write('graph.update', definition=configured)
    baseline = definition()
    path = Path(directory) / 'recipe-baseline-before.screamseq'
    write('document.save', path=str(path))
    saved = typed(plistlib.loads(path.read_bytes()))
    before = doc()
    value = parameter()['manualValue']
    for dry in (True, False):
        result = write('graph.plugin.set', graph=graph, node=node,
                       parameters=[dict(id=1, value=value)], dryRun=dry)
        assert not result['changed'], 'Same recipe manual value created a document edit'
        assert doc() == before and definition() == baseline
    after_path = Path(directory) / 'recipe-baseline-after.screamseq'
    write('document.save', path=str(after_path))
    assert typed(plistlib.loads(after_path.read_bytes())) == saved, 'No-op rewrote saved recipe or modulation bases'
    before = doc()
    for values in ([dict(id=1, value=-6), dict(id=1, value=-3)],
                   [dict(id=1, value=-6), dict(id=4294967295, value=0)],
                   [dict(id=1, value=-6), dict(id=True, value=0)],
                   [dict(id=1, value=True)]):
        expect_error(-32602, lambda values=values: write('graph.plugin.set',
            graph=graph, node=node, parameters=values))
        assert doc() == before and definition() == baseline
    metadata = parameter()
    write('graph.plugin.set', graph=graph, node=node, parameters=[dict(id=1, value=-6)])
    edited = definition()
    expected = copy.deepcopy(baseline)
    target = next(n for n in expected['nodes'] if n['id'] == node)
    target['plugin']['parameters'] = [dict(id=1, value=-6)]
    for edge in expected['modulation']:
        edge['base'] = (-6 - metadata['min']) / (metadata['max'] - metadata['min'])
    assert edited == expected, 'Recipe edit changed fields beyond its override and contribution bases'
    assert read('plugin.state.get', slot=slot) == rack
    write('history.undo')
    assert definition() == baseline
    # Repeating the original value must retain the exact Redo branch.
    before = doc()
    write('graph.plugin.set', graph=graph, node=node, parameters=[dict(id=1, value=value)])
    assert doc() == before and definition() == baseline
    write('history.redo')
    assert definition() == edited
    final_path = Path(directory) / 'recipe-baseline-edited.screamseq'
    write('document.save', path=str(final_path))
    print('PASS recipe baseline: exact no-op, disabled contributions, atomic rejection, Undo/Redo and saved state')
    return final_path, graph, edited
