"""Shared silent socket/pipe contract. Live PCM/queue tests are native tests.

The caller owns a disposable session and supplies its backend's error matcher.
No hardware, UI automation, builds, or process selection occurs in this helper.
"""
from pathlib import Path
import plistlib


def parameter_preview_contract(client, directory, expect_error):
    def doc():
        return client.call('document.get')

    def read(method, **params):
        return client.call(method, params)['data']

    def write(method, **params):
        return client.call(method, dict(expectedRevision=doc()['revision'], **params))

    gain = next(p for p in read('plugin.discover', format='Built-in')
                if p['classID'] == 'resonance.gainer.v1')
    write('plugin.add', descriptor=gain)
    plugin = doc()['data']['nativePlugins'][-1]['instanceID']
    key = dict(plugin=plugin, parameter=1, gesture='socket-preview')

    def state():
        return read('plugin.state.get', slot=len(doc()['data']['nativePlugins']) - 1)

    def preview(value):
        return write('plugin.parameters.preview.set', **key, value=value)

    def commit(value):
        return write('plugin.parameters.preview.commit', **key, value=value)

    before, baseline = doc(), state()
    first_request = dict(expectedRevision=before['revision'], **key, value=-12)
    first = client.call('plugin.parameters.preview.set', first_request, request_id='preview-contract-first')
    assert client.call('plugin.parameters.preview.set', first_request, request_id='preview-contract-first') == first
    assert not first['changed'] and first['revision'] == before['revision']
    assert first['data']['active'] and not first['data']['audible']
    preview(-24)
    assert doc() == before and state() == baseline
    retained = read('plugin.parameters.preview.get')
    for fields in (dict(value=1000), dict(value=True), dict(value='-6'),
                   dict(value=-6, parameter=True), dict(value=-6, gesture='other'),
                   dict(value=-6, gesture='bad\0token'), dict(value=-6, gesture='é' * 41),
                   dict(value=-6, plugin='missing'), dict(value=-6, typo=1)):
        expect_error(-32602, lambda fields=fields:
                     write('plugin.parameters.preview.set', **(key | fields)))
        assert read('plugin.parameters.preview.get') == retained
        assert doc() == before and state() == baseline
    expect_error(-32602, lambda: write('plugin.parameters.preview.cancel',
                                      **(key | dict(gesture='other'))))
    write('plugin.parameters.preview.cancel', **key)
    write('plugin.parameters.preview.cancel', **key)
    assert client.call('plugin.parameters.preview.set', first_request, request_id='preview-contract-first') == first
    assert not read('plugin.parameters.preview.get')['active']
    assert doc() == before and state() == baseline
    preview(-18)
    final_request = dict(expectedRevision=doc()['revision'], **key, value=-9)
    committed = client.call('plugin.parameters.preview.commit', final_request, request_id='preview-contract-final')
    assert client.call('plugin.parameters.preview.commit', final_request, request_id='preview-contract-final') == committed
    assert committed['changed'] and not committed['data']['active']
    assert not read('plugin.parameters.preview.get')['active']
    accepted = state()
    write('history.undo')
    assert state() == baseline
    write('history.redo')
    assert state() == accepted
    snapshot = doc()
    preview(-21)
    assert not commit(-9)['changed'] and doc() == snapshot
    # A subsequent accepted edit cancels the drag; a stale release must not
    # overwrite it, even if it retains the same stable plugin/parameter key.
    preview(-18)
    stale = doc()['revision']
    write('plugin.parameters.set', plugin=plugin, values=[dict(id=1, value=-3)])
    assert not read('plugin.parameters.preview.get')['active']
    expect_error(-32001, lambda: client.call('plugin.parameters.preview.commit',
        dict(expectedRevision=stale, **key, value=-18)))
    accepted = state()
    preview(-24)
    path = Path(directory) / 'parameter-preview-contract.screamseq'
    write('document.save', path=str(path))
    assert state() == accepted and not read('plugin.parameters.preview.get')['active']
    saved = plistlib.loads(path.read_bytes())
    assert saved['plugins'][-1]['instanceID'] == plugin
    # The serializer's opaque state must match a second save without a preview.
    second = Path(directory) / 'parameter-preview-contract-baseline.screamseq'
    write('document.save', path=str(second))
    assert plistlib.loads(second.read_bytes()) == saved
    print('PASS parameter preview socket/pipe: retained owner, strict validation, '
          'cancel, one-step history, no-op, stale release and exact saved baseline')
    return path, plugin, accepted
