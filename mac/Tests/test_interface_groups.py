"""Portable checks for complete, bounded native interface qualification."""
import contextlib
import io
import json
import subprocess
import unittest

from run_interface_groups import run


class InterfaceGroupRunnerTests(unittest.TestCase):
    def execute(self, groups, outcomes=None, arguments=()):
        calls = []
        outcomes = outcomes or {}

        def invoke(command, **options):
            calls.append((command, options))
            if command[1:] == ["--list-groups"]:
                return subprocess.CompletedProcess(command, 0, json.dumps(groups))
            outcome = outcomes.get(command[2], 0)
            if isinstance(outcome, Exception):
                raise outcome
            return subprocess.CompletedProcess(command, outcome)

        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            result = run("interface-tests", list(arguments), invoke)
        return result, calls, output.getvalue()

    def test_failure_does_not_hide_later_groups_or_retry(self):
        result, calls, output = self.execute(
            ["first", "failed", "last", "core-layout"], {"failed": -6})
        self.assertEqual(result, 1)
        self.assertEqual([call[0][2] for call in calls[1:]],
                         ["first", "failed", "last", "core-layout"])
        self.assertIn("FAIL interface group failed: exit -6", output)
        self.assertIn("3/4 passed", output)

    def test_timeout_is_failure_and_remaining_groups_run(self):
        result, calls, output = self.execute(
            ["slow", "core-layout"],
            {"slow": subprocess.TimeoutExpired("interface-tests", 120)})
        self.assertEqual(result, 1)
        self.assertEqual([call[0][2] for call in calls[1:]], ["slow", "core-layout"])
        self.assertTrue(all(call[1]["timeout"] == 120 for call in calls[1:]))
        self.assertIn("exceeded 120-second bound", output)

    def test_success_forwards_snapshot_arguments_and_bounds_inventory(self):
        arguments = ["--snapshots", "output with spaces"]
        result, calls, output = self.execute(["first", "core-layout"], arguments=arguments)
        self.assertEqual(result, 0)
        self.assertEqual(calls[0][1], dict(capture_output=True, text=True, check=True, timeout=15))
        self.assertTrue(all(call[0][3:] == arguments for call in calls[1:]))
        self.assertIn("2/2 passed", output)

    def test_invalid_inventory_never_launches_a_group(self):
        invalid = [[], ["first"], ["core-layout", "core-layout"],
                   ["core-layout", ""], ["core-layout", "--group"],
                   ["core-layout", {}], {"core-layout": True}]
        for inventory in invalid:
            with self.subTest(inventory=inventory):
                calls = []

                def invoke(command, **options):
                    calls.append(command)
                    return subprocess.CompletedProcess(command, 0, json.dumps(inventory))

                with self.assertRaises(ValueError):
                    run("interface-tests", [], invoke)
                self.assertEqual(calls, [["interface-tests", "--list-groups"]])

    def test_subset_cannot_be_reported_as_complete_run(self):
        for flag in ["--group", "--list-groups"]:
            with self.subTest(flag=flag):
                with self.assertRaises(ValueError):
                    run("interface-tests", [flag],
                        lambda *args, **kwargs: self.fail("Must reject before launch"))


if __name__ == "__main__":
    unittest.main()
