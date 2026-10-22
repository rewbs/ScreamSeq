"""Cheap inventory drift checks before the native conformance checkpoint."""
import copy
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import api_vectors
from project_tree import EvidenceError, read_bytes, sha256


class APIInventoryTests(unittest.TestCase):
    def test_historical_contract_is_not_an_open_difference_when_expectations_agree(self):
        vector = {'difference': 'reviewed-history', 'expected': {'windows': 'success', 'macos': 'success'}}
        self.assertEqual(api_vectors.contract_labels(vector),
                         {'knownDifference': None, 'regressionFor': 'reviewed-history'})
        self.assertEqual(api_vectors.contract_labels({'expected': vector['expected']}),
                         {'knownDifference': None, 'regressionFor': None})

    def test_preservation_drift_remains_open_even_when_outcomes_agree(self):
        for key, windows, macos in (
                ('expected', 'success', -32601),
                ('documentUnchanged', True, False),
                ('changedPaths', [], ['$.revision']),
                ('persistedDifference', None, {'equal': False})):
            vector = {'difference': 'reviewed-drift', 'expected': {'windows': 'success', 'macos': 'success'}}
            vector[key] = {'windows': windows, 'macos': macos}
            with self.subTest(key=key):
                self.assertEqual(api_vectors.contract_labels(vector),
                                 {'knownDifference': 'reviewed-drift', 'regressionFor': None})

    def test_current_corpus_keeps_take_gap_and_historical_regressions_distinct(self):
        corpus = json.loads(read_bytes(api_vectors.VECTORS))
        labels = [api_vectors.contract_labels(vector) for vector in corpus['cases']]
        labels.append(api_vectors.contract_labels({'difference': 'API-REPLAY-ERROR',
                                                   'expected': corpus['rejectedRequestIDReuse']}))
        self.assertEqual({row['knownDifference'] for row in labels if row['knownDifference']},
                         {'API-NATIVE-PARAMETER-TAKE'})
        self.assertEqual({row['regressionFor'] for row in labels if row['regressionFor']},
                         {'API-INTEGER-TYPES', 'API-PLUGIN-DRY-RUN', 'API-REPLAY-ERROR'})

    def test_reviewed_schema_and_platform_deltas_are_consistent(self):
        baseline = json.loads(read_bytes(api_vectors.BASELINE))
        schema_path = api_vectors.ROOT / baseline['schema']['path']
        methods = api_vectors.schema_methods(schema_path)
        self.assertEqual(methods, baseline['schema']['methods'])
        # The reviewed schema hash names its Git LF text. Windows checkout may
        # expand CRLF; native evidence still records the exact on-disk hash.
        self.assertEqual(sha256(read_bytes(schema_path).replace(b'\r\n', b'\n')), baseline['schema']['sha256'])
        for platform, inventory in baseline['platforms'].items():
            with self.subTest(platform=platform):
                reads, writes = inventory['reads'], inventory['writes']
                self.assertEqual(reads, sorted(set(reads)))
                self.assertEqual(writes, sorted(set(writes)))
                self.assertFalse(set(reads) & set(writes))
                advertised = set(reads) | set(writes)
                self.assertEqual(inventory['missingSharedMethods'], sorted(set(methods) - advertised))
                self.assertEqual(inventory['additionalMethods'], sorted(advertised - set(methods)))

    def test_unreviewed_runtime_inventory_stops_before_document_access(self):
        baseline = json.loads(read_bytes(api_vectors.BASELINE))
        platform = 'windows' if api_vectors.sys.platform == 'win32' else 'macos'
        for change in ('added-read', 'removed-write', 'duplicate', 'read-write-overlap'):
            inventory = copy.deepcopy(baseline['platforms'][platform])
            if change == 'added-read':
                inventory['reads'].append('unreviewed.read')
            elif change == 'removed-write':
                inventory['writes'].pop()
            elif change == 'duplicate':
                inventory['reads'].append(inventory['reads'][0])
            else:
                inventory['writes'].append(inventory['reads'][0])

            class Client:
                calls = []

                def call(self, method, params=None, request_id=None):
                    self.calls.append(method)
                    if method != 'api.describe':
                        raise AssertionError('Inventory failure must precede document access')
                    return {'data': inventory}

            with self.subTest(change=change), tempfile.TemporaryDirectory() as folder:
                client = Client()
                output = Path(folder) / 'evidence'
                with self.assertRaises(EvidenceError):
                    api_vectors.run(client, output)
                self.assertEqual(client.calls, ['api.describe'])
                report = json.loads((output / 'report.json').read_text())
                self.assertFalse(report['baselineMatched'])
                self.assertFalse(report['parityComplete'])
                self.assertEqual(report['fixtureLoads'], [])
                self.assertIn('error', report)

    def test_unreviewed_shared_method_stops_before_document_access(self):
        baseline = json.loads(read_bytes(api_vectors.BASELINE))
        platform = 'windows' if api_vectors.sys.platform == 'win32' else 'macos'

        class Client:
            def call(self, method, params=None, request_id=None):
                if method != 'api.describe':
                    raise AssertionError('Schema drift must precede document access')
                return {'data': baseline['platforms'][platform]}

        with tempfile.TemporaryDirectory() as folder, patch.object(
                api_vectors, 'schema_methods', return_value=baseline['schema']['methods'] + ['unreviewed.shared']):
            with self.assertRaisesRegex(EvidenceError, 'Shared schema inventory changed'):
                api_vectors.run(Client(), Path(folder) / 'evidence')
