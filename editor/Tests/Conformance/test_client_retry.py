"""Portable transport doubles: identical guarded busy policy on both clients.

No application, socket, pipe, audio device or build is started by these tests.
Native app checks separately exercise the real transport and replay cache.
"""
import contextlib
import importlib.util
import json
from pathlib import Path
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[3]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, ROOT / path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


CLIENTS = (
    ('windows', load('retry_windows_client', 'windows/Api/client.py')),
    ('macos', load('retry_mac_client', 'mac/Tools/resonance_api.py')),
)


@contextlib.contextmanager
def connection(platform, module, respond):
    wires, sleeps = [], []

    def exchange(wire):
        wires.append(wire)
        result = respond(json.loads(wire), len(wires))
        if isinstance(result, BaseException):
            raise result
        return result if isinstance(result, bytes) else json.dumps(result).encode() + b'\n'

    with contextlib.ExitStack() as stack:
        stack.enter_context(patch.object(module.time, 'sleep', side_effect=sleeps.append))
        if platform == 'windows':
            client = module.Client(r'\\.\pipe\ScreamSeq.Api.12345')
            client._exchange = exchange
        else:
            class Socket:
                def __enter__(self):
                    return self
                def __exit__(self, *args):
                    return False
                def settimeout(self, value):
                    pass
                def connect(self, path):
                    assert path == '/private/disposable-api.sock'
                def sendall(self, wire):
                    self.reply = exchange(wire)
                def recv(self, count):
                    result, self.reply = self.reply[:count], self.reply[count:]
                    return result
            stack.enter_context(patch.object(module.socket, 'socket', side_effect=lambda *args: Socket()))
            # Some Windows Python distributions omit AF_UNIX. This double
            # never creates an OS socket; supply only the symbolic argument.
            stack.enter_context(patch.object(module.socket, 'AF_UNIX', 1, create=True))
            client = module.Client(socket_path='/private/disposable-api.sock')
        yield client, wires, sleeps


def error_reply(request, code=-32002, data=None):
    error = dict(code=code, message='fixture refusal')
    if data is not None:
        error['data'] = data
    return dict(jsonrpc='2.0', id=request['id'], error=error)


class ClientBusyRetryTests(unittest.TestCase):
    def test_confirmed_busy_preserves_id_revision_timestamp_and_exact_bytes(self):
        for platform, module in CLIENTS:
            with self.subTest(platform=platform):
                params = dict(expectedRevision='captured:7', take='take-one', timestamp='123456', value=.25)
                def respond(request, attempt):
                    if attempt == 1:
                        params.update(expectedRevision='must-not-rebase:8', timestamp='999999', value=.9)
                    if attempt < 3:
                        return error_reply(request, data=dict(writeOutcome='notCommitted'))
                    return dict(jsonrpc='2.0', id=request['id'], result={'accepted': True})
                with connection(platform, module, respond) as (client, wires, sleeps):
                    self.assertEqual(client.call('automation.recording.capture', params, request_id='one-input'),
                                     {'accepted': True})
                    self.assertEqual(len(wires), 3)
                    self.assertEqual(len(set(wires)), 1)
                    self.assertEqual(json.loads(wires[0])['params'],
                                     dict(expectedRevision='captured:7', take='take-one', timestamp='123456', value=.25))
                    self.assertEqual(json.loads(wires[0])['id'], 'one-input')
                    self.assertEqual(sleeps, [.05, .1])

    def test_generated_id_is_also_stable_and_attempts_are_bounded(self):
        for platform, module in CLIENTS:
            with self.subTest(platform=platform), connection(platform, module,
                    lambda request, attempt: error_reply(request, data={'writeOutcome': 'notCommitted'})) as (client, wires, sleeps):
                with self.assertRaises(RuntimeError):
                    client.call('history.undo', {'expectedRevision': 'captured'})
                self.assertEqual(len(wires), 6)
                self.assertEqual(len(set(wires)), 1)
                self.assertTrue(json.loads(wires[0])['id'])
                self.assertEqual(sleeps, [.05, .1, .2, .4, .5])

    def test_ambiguous_terminal_and_nonbusy_errors_are_never_retried(self):
        cases = [(code, {'writeOutcome': 'notCommitted'}) for code in (-32001, -32003, -32602)]
        cases += [(-32002, data) for data in (None, {}, [], 'notCommitted',
                  {'writeOutcome': 'committed'}, {'writeOutcome': 'unknown'},
                  {'writeOutcome': 'noChange'}, {'writeOutcome': True})]
        for platform, module in CLIENTS:
            for code, data in cases:
                with self.subTest(platform=platform, code=code, data=data), connection(platform, module,
                        lambda request, attempt: error_reply(request, code, data)) as (client, wires, sleeps):
                    with self.assertRaises(RuntimeError) as failure:
                        client.call('history.undo', {'expectedRevision': 'captured'})
                    self.assertEqual(failure.exception.code, code)
                    self.assertEqual(len(wires), 1)
                    self.assertEqual(sleeps, [])

    def test_transport_loss_and_malformed_reply_never_repeat_a_write(self):
        for platform, module in CLIENTS:
            failures = [OSError('reply lost after send'), b'not JSON\n',
                        b'{"jsonrpc":"2.0","id":"another-request","result":{}}\n']
            for reply in failures:
                with self.subTest(platform=platform, reply=repr(reply)), connection(platform, module,
                        lambda request, attempt: reply) as (client, wires, sleeps):
                    with self.assertRaises((OSError, RuntimeError, ValueError)):
                        client.call('history.undo', {'expectedRevision': 'captured'})
                    self.assertEqual(len(wires), 1)
                    self.assertEqual(sleeps, [])

    def test_malformed_busy_envelope_cannot_authorize_retry(self):
        for platform, module in CLIENTS:
            for shape in ('both', 'neither', 'error-string', 'missing-message', 'boolean-code'):
                def respond(request, attempt):
                    reply = error_reply(request, data={'writeOutcome': 'notCommitted'})
                    if shape == 'both': reply['result'] = {}
                    if shape == 'neither': del reply['error']
                    if shape == 'error-string': reply['error'] = 'busy'
                    if shape == 'missing-message': del reply['error']['message']
                    if shape == 'boolean-code': reply['error']['code'] = True
                    return reply
                with self.subTest(platform=platform, shape=shape), connection(platform, module, respond) as (client, wires, sleeps):
                    with self.assertRaises(RuntimeError):
                        client.call('history.undo', {'expectedRevision': 'captured'})
                    self.assertEqual(len(wires), 1)
                    self.assertEqual(sleeps, [])


if __name__ == '__main__':
    unittest.main()
