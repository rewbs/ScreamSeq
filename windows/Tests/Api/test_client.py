"""Run after building ApiTests; uses only disposable test-host processes."""
import importlib.util
import pathlib
import subprocess
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[3]
CLIENT = ROOT / 'windows/Api/client.py'
EXE = ROOT / 'bin/windows-api/Debug/ApiTests.exe'

class ClientTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not CLIENT.exists():
            raise AssertionError('Windows explicit-pipe client is not implemented')
        spec = importlib.util.spec_from_file_location('pipe_client', CLIENT)
        cls.client = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.client)
        cls.host = subprocess.Popen([str(EXE), '--serve'], stdin=subprocess.PIPE,
                                    stdout=subprocess.PIPE, text=True)
        cls.pipe = cls.host.stdout.readline().strip()
        if not cls.pipe.startswith('\\\\.\\pipe\\'):
            raise AssertionError('test host did not publish its explicit pipe path: ' + cls.pipe)

    @classmethod
    def tearDownClass(cls):
        cls.host.communicate('\n', timeout=5)
        if cls.host.returncode != 0:
            raise AssertionError('test host failed')

    def test_read_and_transport_same_contract(self):
        c = self.client.Client(self.pipe)
        doc = c.call('document.get')
        self.assertEqual(doc['data']['title'], 'fixture')
        p = {'expectedRevision': doc['revision']}
        played = c.call('transport.play', p, request_id='python-play')
        self.assertTrue(played['data']['playing'])
        self.assertEqual(c.call('transport.play', p, request_id='python-play'), played)
        self.assertTrue(c.call('transport.stop', p)['playbackStopped'])
        self.assertFalse(c.call('transport.get')['data']['playing'])

    def test_server_validation(self):
        c = self.client.Client(self.pipe)
        for method, params, code in [('pattern.apply', {}, -32601),
                                     ('pattern.get', {'pattern': True}, -32602),
                                     ('transport.stop', {'expectedRevision': 'stale'}, -32001)]:
            with self.assertRaises(self.client.ApiError) as caught:
                c.call(method, params)
            self.assertEqual(caught.exception.code, code)

    def test_explicit_local_pipe_only(self):
        for value in ['', 'ScreamSeq', '\\\\remote\\pipe\\ScreamSeq']:
            with self.assertRaises(ValueError):
                self.client.Client(value)

    def test_uncertain_send_is_never_retried(self):
        c = self.client.Client(self.pipe)
        calls = []
        def lost(*args):
            calls.append(args)
            raise self.client.TransportError('lost reply; outcome unknown', True)
        c._exchange = lost
        with self.assertRaises(self.client.TransportError) as caught:
            c.call('transport.play', {'expectedRevision': 'x'})
        self.assertTrue(caught.exception.may_have_been_sent)
        self.assertEqual(len(calls), 1)

if __name__ == '__main__':
    unittest.main()
