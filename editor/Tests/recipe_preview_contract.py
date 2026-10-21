"""Shared stopped-host recipe preview contract for a disposable socket/pipe session.

No hardware or UI is started here. Live copy fanout, PCM and queue saturation
belong to the native host tests; this tests the externally observable contract.
"""
from pathlib import Path
import plistlib


def recipe_preview_contract(client, directory, expect_error):
    def doc():
        return client.call('document.get')

    def read(method, **fields):
        return client.call(method, fields)['data']

    def write(method, **fields):
        return client.call(method, dict(expectedRevision=doc()['revision'], **fields))

    descriptor = next(v for v in read('plugin.discover', format='Built-in')
                      if v['classID'] == 'resonance.gainer.v1')
    write('plugin.add', descriptor=descriptor)
    slot = len(doc()['data']['nativePlugins']) - 1
    plugin = doc()['data']['nativePlugins'][slot]['instanceID']
    graph = write('graph.create', name='Recipe preview contract')['data']['graph']
    node = write('graph.node.add', graph=graph, kind='plugin', slot=slot)['data']['node']
    key = dict(graph=graph, node=node, parameter=1, gesture='recipe-preview-contract')
    rack_key = dict(plugin=plugin, parameter=1, gesture='rack-preview-conflict')

    def state():
        return read('graph.get')

    def owner():
        return read('graph.plugin.preview.get')

    def preview(value):
        return write('graph.plugin.preview.set', **key, value=value)

    def commit(value):
        return write('graph.plugin.preview.commit', **key, value=value)

    def cancel():
        return write('graph.plugin.preview.cancel', **key)

    before, baseline = doc(), state()
    rack_before = read('plugin.state.get', plugin=plugin)
    request = dict(expectedRevision=before['revision'], **key, value=-12)
    first = client.call('graph.plugin.preview.set', request, request_id='recipe-preview-first')
    assert client.call('graph.plugin.preview.set', request, request_id='recipe-preview-first') == first
    assert not first['changed'] and first['data']['active'] and not first['data']['audible']
    preview(-24)
    retained = owner()
    assert retained['graph'] == graph and retained['node'] == node and retained['value'] == -24
    assert doc() == before and state() == baseline
    assert read('plugin.state.get', plugin=plugin) == rack_before
    assert next(v for v in read('graph.plugin.get', graph=graph, node=node)['parameters']
                if v['id'] == 1)['manualValue'] == 0
    for fields in (dict(value=1000), dict(value=True), dict(value='-6'),
                   dict(value=-6, parameter=True), dict(value=-6, gesture='other'),
                   dict(value=-6, gesture='bad\0token'), dict(value=-6, gesture='é' * 41),
                   dict(value=-6, graph='n01'), dict(value=-6, node='n0'),
                   dict(value=-6, typo=True)):
        expect_error(-32602, lambda fields=fields:
                     write('graph.plugin.preview.set', **(key | fields)))
        assert owner() == retained and state() == baseline and doc() == before
    expect_error(-32602, lambda: read('graph.plugin.preview.get', graph=graph))
    expect_error(-32602, lambda: write('graph.plugin.preview.cancel', **(key | dict(gesture='other'))))
    expect_error(-32602, lambda: write('plugin.parameters.preview.set', **rack_key, value=-3))
    # An inactive other-family cancel is harmless; it must not retire this owner.
    write('plugin.parameters.preview.cancel', **rack_key)
    assert owner() == retained
    cancel()
    cancel()
    assert client.call('graph.plugin.preview.set', request, request_id='recipe-preview-first') == first
    assert not owner()['active'] and state() == baseline and doc() == before
    write('plugin.parameters.preview.set', **rack_key, value=-6)
    rack_owner = read('plugin.parameters.preview.get')
    expect_error(-32602, lambda: preview(-12))
    cancel()
    assert read('plugin.parameters.preview.get') == rack_owner
    write('plugin.parameters.preview.cancel', **rack_key)
    expect_error(-32001, lambda: commit(-9))

    preview(-18)
    final_request = dict(expectedRevision=doc()['revision'], **key, value=-9)
    final = client.call('graph.plugin.preview.commit', final_request, request_id='recipe-preview-final')
    assert client.call('graph.plugin.preview.commit', final_request, request_id='recipe-preview-final') == final
    assert final['changed'] and not final['data']['active'] and not owner()['active']
    accepted = state()
    write('history.undo')
    assert state() == baseline
    write('history.redo')
    assert state() == accepted
    snapshot = doc()
    preview(-21)
    assert not commit(-9)['changed'] and state() == accepted and doc() == snapshot
    assert not owner()['active']
    preview(-18)
    stale = doc()['revision']
    dry = write('graph.plugin.set', graph=graph, node=node, parameters=[dict(id=1, value=-3)], dryRun=True)
    assert not dry['changed'] and owner()['active']
    write('graph.plugin.set', graph=graph, node=node, parameters=[dict(id=1, value=-3)])
    assert not owner()['active']
    expect_error(-32001, lambda: client.call('graph.plugin.preview.commit',
                                            dict(expectedRevision=stale, **key, value=-18)))
    accepted = state()
    preview(-24)
    path = Path(directory) / 'recipe-preview-contract.screamseq'
    write('document.save', path=str(path))
    assert state() == accepted and not owner()['active']
    second = Path(directory) / 'recipe-preview-baseline.screamseq'
    write('document.save', path=str(second))
    assert plistlib.loads(path.read_bytes()) == plistlib.loads(second.read_bytes())
    assert read('plugin.state.get', plugin=plugin) == rack_before
    print('PASS recipe preview contract: ownership, strict validation, cross-family refusal, '
          'cancel, one-step history, no-op release, request replay and exact saved baseline')
    return path, graph, accepted
