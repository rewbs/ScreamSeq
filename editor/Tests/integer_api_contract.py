"""Numeric representation, atomic edits and history through an owned socket/pipe."""
from pathlib import Path
import plistlib
from Conformance.project_tree import typed


def integer_api_contract(client, directory, expect_error):
    def doc():
        return client.call('document.get')

    def read(method, **params):
        return client.call(method, params)['data']

    def write(method, **params):
        return client.call(method, dict(expectedRevision=doc()['revision'], **params))

    baseline = doc()
    region = read('pattern.get', pattern=0, rowCount=1, channelCount=1)
    for value in (0.0, -0.0):
        assert read('pattern.get', pattern=value, startRow=0.0, startChannel=0.0,
                    rowCount=1.0, channelCount=1.0) == region
    for value in (True, False, None, '0', -1, .5, 65536, 2**64-1, float(2**64)):
        expect_error(-32602, lambda value=value: read('pattern.get', pattern=value))
        assert doc() == baseline, 'Invalid numeric read changed the document'

    gain = next(p for p in read('plugin.discover', format='Built-in')
                if p['classID'] == 'resonance.gainer.v1')
    # Descriptor IDs take the same integral-number path as mutable parameters.
    gain = dict(gain, type=float(gain['type']), subtype=float(gain['subtype']),
                manufacturer=float(gain['manufacturer']))
    write('plugin.add', descriptor=gain)
    slot = len(doc()['data']['nativePlugins']) - 1
    plugin = doc()['data']['nativePlugins'][slot]['instanceID']

    def state():
        return read('plugin.state.get', slot=slot)

    def set_value(value):
        return write('plugin.parameters.set', slot=float(slot), values=[dict(id=1.0, value=value)])

    assert read('plugin.parameters.get', slot=float(slot)) == read('plugin.parameters.get', slot=slot)
    original = state()
    set_value(-6)
    accepted = state()
    assert accepted != original, 'Integral-real parameter edit did not apply'
    write('history.undo')
    assert state() == original
    before = doc()
    set_value(0)
    assert doc() == before and state() == original, 'Numeric no-op changed history or state'

    for value in (True, False, None, '1', -1, .5, 2**32, 2**64-1, float(2**64)):
        expect_error(-32602, lambda value=value: write('plugin.parameters.set', plugin=plugin,
            values=[dict(id=1, value=-9), dict(id=value, value=-12)]))
        assert doc() == before and state() == original, 'Rejected numeric batch partially applied'
    expect_error(-32602, lambda: write('plugin.parameters.set', plugin=plugin,
        values=[dict(id=1, value=-9), dict(id=1.0, value=-12)]))
    assert doc() == before and state() == original, 'Equivalent integer IDs escaped duplicate validation'

    key = dict(plugin=plugin, parameter=1.0, gesture='integer-api-preview')
    write('plugin.parameters.preview.set', **key, value=-18)
    assert read('plugin.parameters.preview.get')['parameter'] == 1
    write('plugin.parameters.preview.cancel', **key)
    assert doc() == before and state() == original
    write('history.redo')
    assert state() == accepted, 'No-op, rejection or preview erased Redo'
    stale = before['revision']
    expect_error(-32001, lambda: client.call('plugin.parameters.set',
        dict(expectedRevision=stale, plugin=plugin, values=[dict(id=1.0, value=-9)])))
    assert state() == accepted

    path = Path(directory) / 'integer-api.screamseq'
    write('document.save', path=str(path))
    tree = typed(plistlib.loads(path.read_bytes()))
    write('history.undo')
    assert state() == original
    write('history.redo')
    assert state() == accepted
    again = Path(directory) / 'integer-api-redone.screamseq'
    write('document.save', path=str(again))
    assert typed(plistlib.loads(again.read_bytes())) == tree, 'Numeric edit history changed persisted data'
    print('PASS integer API: integral representations, strict rejection, atomic batches, preview, history and saved tree')
    return path, plugin, accepted
