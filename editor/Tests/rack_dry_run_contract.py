"""Rack dry-run invariants through an owned native socket or PID pipe."""
from pathlib import Path
import plistlib
from Conformance.project_tree import typed


def rack_dry_run_contract(client, directory, expect_error):
    def doc():
        return client.call('document.get')

    def read(method, **params):
        return client.call(method, params)['data']

    def write(method, **params):
        return client.call(method, dict(expectedRevision=doc()['revision'], **params))

    gain = next(p for p in read('plugin.discover', format='Built-in')
                if p['classID'] == 'resonance.gainer.v1')
    # Two processors make both a real permutation and either edge no-op
    # reproducible even when the caller begins with an empty rack.
    write('plugin.add', descriptor=gain)
    write('plugin.add', descriptor=gain)
    plugin = doc()['data']['nativePlugins'][-1]['instanceID']
    slot = len(doc()['data']['nativePlugins']) - 1

    def state():
        return read('plugin.state.get', slot=slot)

    def parameters(value, **extra):
        return write('plugin.parameters.set', plugin=plugin,
                     values=[dict(id=1, value=value)], **extra)

    def saved(name):
        path = Path(directory) / (name + '.screamseq')
        write('document.save', path=str(path))
        return typed(plistlib.loads(path.read_bytes()))

    parameters(-6)
    accepted = state()
    write('history.undo')
    original_tree = saved('rack-dry-run-before')
    baseline, original = doc(), state()

    def unchanged():
        assert doc() == baseline, 'Dry run changed document, revision or history'
        assert state() == original, 'Dry run changed saved opaque plugin state'

    for value, changed in ((-12, True), (0, False)):
        reply = parameters(value, dryRun=True)
        assert not reply['changed'] and not reply['playbackStopped']
        assert reply['data'] == dict(wouldChange=changed, dryRun=True)
        unchanged()
    for bypass, changed in ((True, True), (False, False)):
        reply = write('plugin.bypass', plugin=plugin, bypass=bypass, dryRun=True)
        assert not reply['changed'] and not reply['playbackStopped']
        assert reply['data'] == dict(wouldChange=changed, dryRun=True)
        unchanged()
    for fields, changed in ((dict(position=0), True), (dict(direction=-1), True),
                            (dict(position=slot), False), (dict(direction=1), False)):
        reply = write('plugin.move', plugin=plugin, dryRun=True, **fields)
        assert not reply['changed'] and not reply['playbackStopped']
        assert reply['data'] == dict(wouldChange=changed, dryRun=True)
        unchanged()
    for fields in (dict(position=True), dict(position=-1), dict(position=slot+1),
                   dict(position=.5), dict(direction=0), dict(direction=True),
                   dict(direction=-1, position=0), dict(), dict(position=0, unexpected=True)):
        expect_error(-32602, lambda fields=fields: write('plugin.move', plugin=plugin, dryRun=True, **fields))
        unchanged()
    expect_error(-32602, lambda: write('plugin.move', plugin=plugin, position=0, dryRun=1))
    expect_error(-32602, lambda: write('plugin.move', plugin='missing', position=0, dryRun=True))
    expect_error(-32001, lambda: client.call('plugin.move',
        dict(plugin=plugin, position=0, dryRun=True, expectedRevision='stale')))
    unchanged()
    for fields in (
            dict(values=[]), dict(values=[dict(id=1, value=True)]),
            dict(values=[dict(id=1, value=-9), dict(id=1, value=-3)]),
            dict(values=[dict(id=1, value=-9), dict(id=4294967295, value=0)]),
            dict(values=[dict(id=1, value=1000)]), dict(unexpected=True)):
        request = dict(plugin=plugin, values=[dict(id=1, value=-12)], dryRun=True) | fields
        expect_error(-32602, lambda request=request: write('plugin.parameters.set', **request))
        unchanged()
    expect_error(-32602, lambda: write('plugin.bypass', plugin=plugin, bypass=1, dryRun=True))
    expect_error(-32602, lambda: write('plugin.bypass', plugin='missing', bypass=True, dryRun=True))
    expect_error(-32001, lambda: client.call('plugin.parameters.set',
        dict(plugin=plugin, values=[dict(id=1, value=-9)], dryRun=True, expectedRevision='stale')))
    unchanged()
    assert saved('rack-dry-run-after') == original_tree, 'Dry runs changed the saved property tree'
    baseline, original = doc(), state()

    key = dict(plugin=plugin, parameter=1, gesture='rack-dry-run-owner')
    write('plugin.parameters.preview.set', **key, value=-18)
    owner = read('plugin.parameters.preview.get')
    assert owner['active'] and not owner['audible']
    parameters(-9, dryRun=True)
    write('plugin.bypass', plugin=plugin, bypass=True, dryRun=True)
    write('plugin.move', plugin=plugin, position=0, dryRun=True)
    write('plugin.move', plugin=plugin, direction=1, dryRun=True)
    expect_error(-32602, lambda: write('plugin.move', plugin=plugin, position=slot+1, dryRun=True))
    expect_error(-32602, lambda: parameters(1000, dryRun=True))
    assert read('plugin.parameters.preview.get') == owner, 'Dry run cancelled or replaced preview ownership'
    unchanged()
    write('plugin.parameters.preview.cancel', **key)
    write('history.redo')
    assert state() == accepted, 'Dry run or preview destroyed the existing Redo branch'
    parameters(-9)
    changed = state()
    write('history.undo')
    assert state() == accepted, 'Real parameter edit no longer undoes in one step'
    write('history.redo')
    assert state() == changed
    write('plugin.bypass', plugin=plugin, bypass=True)
    write('history.undo')
    assert state() == changed, 'Real bypass edit no longer undoes independently'
    before_move = saved('rack-dry-run-move-before')
    before_ids = [p['instanceID'] for p in doc()['data']['nativePlugins']]
    write('plugin.move', plugin=plugin, position=0)
    assert [p['instanceID'] for p in doc()['data']['nativePlugins']] == [plugin] + before_ids[:-1]
    after_move = saved('rack-dry-run-move-after')
    write('history.undo')
    assert saved('rack-dry-run-move-undone') == before_move, 'Real move did not Undo exactly in one step'
    write('history.redo')
    assert saved('rack-dry-run-move-redone') == after_move, 'Real move did not Redo exactly in one step'
    print('PASS rack dry runs: parameters/bypass/move, atomic validation, exact state/save, preview isolation, Redo and real-edit history')
