"""Terminal mutation replay through an owned native app; no transport auto-retry."""
from pathlib import Path
import plistlib
import uuid
from Conformance.project_tree import typed


def request_replay_contract(client, directory, api_error):
    def call(method, params=None, request_id=None):
        return client.call(method, params or {}, request_id=request_id)

    def doc():
        return call('document.get')

    def write(method, **params):
        return call(method, dict(expectedRevision=doc()['revision'], **params))

    def reject(code, method, params, request_id):
        try:
            call(method, params, request_id)
        except api_error as error:
            assert error.code == code, (method, error.code, str(error))
            return error.code, str(error), error.data
        raise AssertionError(f'{method} reused a retained request ID')

    prefix = uuid.uuid4().hex
    before_path = Path(directory) / 'replay-before.screamseq'
    write('document.save', path=str(before_path))
    before = doc()
    bad_id = prefix + '-bad'
    bad = dict(expectedRevision=before['revision'], speed=True)
    rejected = reject(-32602, 'document.patch', bad, bad_id)
    assert reject(-32602, 'document.patch', bad, bad_id) == rejected
    corrected = dict(expectedRevision=before['revision'], title=before['data']['title'])
    reject(-32600, 'document.patch', corrected, bad_id)
    reject(-32600, 'document.get', {}, bad_id)
    reject(-32600, 'unknown.replay.method', {}, bad_id)
    assert doc() == before, 'Rejected request reuse changed document/history'

    read_id = prefix + '-read'
    assert call('document.get', request_id=read_id) == before
    edit_id = prefix + '-edit'
    title = 'Request replay acceptance'
    if title == before['data']['title']:
        title += ' updated'
    params = dict(expectedRevision=before['revision'], title=title)
    accepted = call('document.patch', params, edit_id)
    assert accepted['changed'] and doc()['data']['title'] == title
    changed = doc()
    assert call('document.patch', params, edit_id) == accepted
    assert doc() == changed, 'Successful replay created another history entry'
    assert call('document.get', request_id=read_id) == changed, 'Read ID returned a stale cached snapshot'
    assert reject(-32602, 'document.patch', bad, bad_id) == rejected, 'Terminal error was revalidated after revision changed'
    reject(-32600, 'document.patch', dict(params, title=title + ' conflict'), edit_id)
    reject(-32600, 'document.get', {}, edit_id)
    assert doc() == changed

    write('history.undo')
    assert doc()['data']['title'] == before['data']['title'], 'Replay created more than one Undo step'
    undone_path = Path(directory) / 'replay-undone.screamseq'
    write('document.save', path=str(undone_path))
    assert typed(plistlib.loads(undone_path.read_bytes())) == typed(plistlib.loads(before_path.read_bytes())), \
        'Undo after replay changed unrelated persisted state'
    write('history.redo')
    assert doc()['data']['title'] == title
    after_path = Path(directory) / 'replay-after.screamseq'
    write('document.save', path=str(after_path))
    print('PASS request replay: terminal errors, ID conflicts, fresh reads, one-step history and exact saved baseline')
    return after_path, title
