# Local automation API qualification — 19 September 2026

The version 1 API and Python client were tested without controlling the user's desktop or sending audio to speakers. Test applications used private discovery directories and disposable demo songs. The existing interactive app was not restarted or edited.

| Check | Evidence | Result |
|---|---|---|
| Existing and new native tests | [ctest.log](ctest.log) | 8/8 suites pass, including core, plugins, session, MIDI, export, stress, native plugins and automation |
| External client over an actual Unix socket | [socket-and-hidden-app.log](socket-and-hidden-app.log) | Private endpoint discovery, permissions, cursor-driven exponential drum roll, dry run, one undo step, neighboring-cell/effect preservation, duplicate-request handling and revision conflicts pass |
| Actual packaged app, hidden | [socket-and-hidden-app.log](socket-and-hidden-app.log) | Same client test through AppController and its serial worker, including marking the document unsaved; no visible windows or audio output |
| API memory/undefined behavior | [api-sanitizers.log](api-sanitizers.log) | Final API boundary suite passes AddressSanitizer and UndefinedBehaviorSanitizer, including AU/VST3 state, automation lanes and PCM replacement |
| Other memory/undefined behavior checks | [full-sanitizers.log](full-sanitizers.log) | Core, session, export, MIDI and native-plugin suites also passed during the feature implementation |

The API tests also reject malformed numeric types, invalid coordinates, partial batches, nonexistent parameters and stale revisions without changing the document. They verify revisions change after native GUI-facing editing calls and document replacement. State/automation, plugin instrument assignments and pattern changes survive project saving/reopening. Recent mutation retries are deduplicated; competing writers must reread after a conflict.

These are correctness and integration checks, not visible UI inspection, sustained 60 fps qualification or a physical-audio benchmark. The outstanding earlier performance gates remain open. The hidden test mode does not alter the normal app's visibility requirements or waive those gates.

`BuildInfo.json` identifies the packaged source snapshot. `artifacts.json` records executable/ZIP hashes and the archive verification result; `package.log` and `signature.log` record the final build and local signature checks. The application remains ad-hoc signed for local development.

See [the guide](../../../mac/AUTOMATION.md) for protocol, method documentation, examples and supported limits. Reproduce the feature checks with:

```sh
bash mac/build.sh
ctest --test-dir bin/mac-native --output-on-failure
python3 mac/Tests/test_automation.py
python3 mac/Tests/test_automation.py --app
bash mac/sanitize.sh
```
