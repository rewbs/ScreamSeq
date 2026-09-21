"""Acceptance checks for measured output, not a substitute for measurement."""
import importlib.util
from pathlib import Path
import unittest


class QualificationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        path = Path(__file__).resolve().parents[1] / "Tools" / "qualification.py"
        cls.module_path = path
        cls.gate = None
        if path.exists():
            spec = importlib.util.spec_from_file_location("qualification", path)
            cls.gate = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(cls.gate)

    def test_missing_measurement_never_passes(self):
        self.assertIsNotNone(self.gate, "qualification gate has not been implemented")
        failures = self.gate.validate({})
        self.assertTrue(failures)
        self.assertTrue(any("presentation" in error for error in failures))
        self.assertTrue(any("audio" in error for error in failures))

    def test_incomplete_and_submit_only_reports_fail(self):
        for report in [
            {"presentation": {}, "audio": {}},
            {"presentation": {"source": "cpu-submit", "intervalsMs": [16.0]}, "audio": {}},
        ]:
            with self.subTest(report=report):
                self.assertTrue(self.gate.validate(report))

    def test_synthetic_gate_fixture_is_accepted_but_not_evidence(self):
        # These values test the validator only. They are never application evidence.
        self.assertEqual(self.gate.validate(self.synthetic_report()), [])

    @staticmethod
    def synthetic_report():
        return {
            "sourceCommit": "a" * 40, "executableSha256": "b" * 64,
            "workload": "synthetic validator unit test, not a benchmark",
            "presentation": {
                "source": "dxgi-frame-statistics", "measurementStarted": True,
                "visibleThroughout": True, "occludedFrames": 0,
                "intervalsMs": [1000.0 / 60] * 3600,
            },
            "audio": {
                "source": "wasapi-event", "sampleRate": 48000,
                "periodFrames": 480, "durationSeconds": 60,
                "callbackCount": 6000, "renderedFrames": 2880000,
                "maxCallbackMicros": 1000, "deadlineOverruns": 0,
                "starvations": 0, "deviceErrors": 0,
                "realtimeAuditPassed": True,
            },
        }

    def test_bad_measurements_and_hidden_frames_fail(self):
        import copy
        import math
        changes = [
            ("presentation", "source", "cpu-submit"),
            ("presentation", "visibleThroughout", False),
            ("presentation", "measurementStarted", False),
            ("presentation", "occludedFrames", 1),
            ("presentation", "intervalsMs", [1000 / 60] * 100),
            ("presentation", "intervalsMs", [1000 / 60] * 3600 + [100.0]),
            ("presentation", "intervalsMs", [math.nan] * 3600),
            ("audio", "source", "offline-render"),
            ("audio", "durationSeconds", 59),
            ("audio", "renderedFrames", 100),
            ("audio", "callbackCount", 0),
            ("audio", "sampleRate", True),
            ("audio", "deadlineOverruns", 1),
            ("audio", "starvations", 1),
            ("audio", "deviceErrors", 1),
            ("audio", "maxCallbackMicros", 10001),
            ("audio", "maxCallbackMicros", math.inf),
            ("audio", "realtimeAuditPassed", False),
        ]
        for section, key, value in changes:
            report = copy.deepcopy(self.synthetic_report())
            report[section][key] = value
            with self.subTest(section=section, key=key):
                self.assertTrue(self.gate.validate(report))


if __name__ == "__main__":
    unittest.main()
