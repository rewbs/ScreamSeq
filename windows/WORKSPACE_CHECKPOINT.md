# Windows workspace continuation

## Run this build

Worktree: `C:\Users\P14\code\ScreamSeq-windows`, branch `codex/windows-native`.
Development executable: `bin/windows-workspace/Release/ScreamSeq.exe`.
Preserved preview: `bin/windows-checkpoints/workspace-preview/ScreamSeq.exe`
(with source hashes, notices, screenshots and qualification evidence).
The original `bin/windows-arm64` build and `native-bootstrap` checkpoint are not
replaced. This is still a **read-only demo player/inspection host**, not an editing
release. Do not open or save a musician's project with it.

```powershell
.\bin\windows-workspace\Release\ScreamSeq.exe
# No audio device; explicit private local API:
.\bin\windows-workspace\Release\ScreamSeq.exe --inspection --automation
```

All work remains uncommitted. No push, branch reset, shared model changes, Mac
source changes, system audio-route changes or user-song writes were made in this
continuation. All **751 non-Windows source hashes** from the bootstrap manifest remain identical.
The four pre-existing shared portability patches still need Mac review before
merge; see HANDOFF.md.

## Real behavior added

- Dense project/sample sidebar, colored monospaced tracker fields and native
  Segoe UI controls, based on the supplied Mac references rather than preserving
  the provisional right-hand stack. Note/volume/effect glyphs come from the
  shared OpenMPT format helpers, not a Windows command-number dialect.
- **Compose**, **Pattern focus**, **Sound design** presets retain the same
  note/sample inspector states. Right and lower splitters resize by mouse;
  Ctrl+Alt+arrows provide a keyboard alternative. State is session-local.
- Independent read-only note and sample inspectors have **Follow target / Pin**,
  **Cursor** and **Return**. Return retains the panel's original opening row,
  even after an unpinned target follows elsewhere. Selecting a sidebar sample
  explicitly pins that asset. Waveforms use the shared min/max bins and display
  real sample length, channel/bit depth and normal-loop bounds.
- Mouse click/drag and Shift+arrows select real grid cells; arrows navigate five
  fields, Tab changes channel, Page/Home/End navigate rows, wheel detaches and
  scrolls. Purple cursor, blue selection, green playhead and panel-focus outline
  are separate. Playback-follow scrolls the viewport without moving edit targets.
- **Ctrl+K** opens a native modeless searchable command list. Enter runs, arrows
  choose, Escape closes. Text input owns Space and letter keys rather than
  accidentally starting playback. F6 switches pattern/active-inspector focus.
- `context.set` now uses both revision guards and strict validation. It returns
  `contextChanged` without changing the document, Undo or transport. `context.get`
  includes canonical `following`, field/selection and independent playback state.
- `workspace.get`, `.panel`, `.layout` read/change the real retained state. GUI
  commands use the same host operations. Unsupported panels, floating/custom
  layouts and unknown fields reject rather than succeeding as no-ops. See
  `Api/README.md` for the supported subset of the existing Mac schema.

The graph and automation areas are clearly unavailable. They have **no fake
nodes, wires or curves**. No musical edit, precise-note draft, Undo/save, sample
processing, audition, plugin hosting or graph/curve editing is claimed.

## Reference review and actual Mac fixture

The supplied pack's **21 application captures and five component fixtures** were
all inspected with vision; `bin/windows-workspace/reference-review.json` contains
the complete deduplicated inventory. Pack README, UI philosophy and current Mac
sources define interaction contracts; screenshots alone do not prove behavior.
Some captures have JPEG encoding despite `.png` filenames, and capture dimensions
are not logical layout specifications.

The actual `reference.screamseq` passed the Python container qualifier and its
opt-in hash-pinned test. Its source SHA-256 is
`96ec9f809ef95a113d16613c29e73841e1f4a64ecc129672e5a48e548e587cd7`.
The complete typed plist tree and opaque snapshot sections survive a disposable
on-disk repack/reopen, and the source stays unchanged. Outer version 4, metadata
14, snapshot 1, built-in effects, graph/automation/envelope-bank data were present.
Repacked physical plist bytes differ. **No Windows application restoration, Mac
reopen, audio fidelity or AU/VST3 compatibility is established by this test.**
`Project/README.md` records exact fixture scope and remaining codec/model gates.

## Verification reproduced here

- Native ARM64 build, verified with `IsWow64Process2`: native machine `0xaa64`.
- 24/24 portable C++ functional regressions passed. Not sanitizer/audit evidence.
- Actual-app offline audio, actual-app pipe and **16 workspace tests** passed.
  The workspace tests exercise real HWND controls/search, selection, divider
  resizing, stale/invalid rejection and retained Pin/Cursor/Return through the
  actual pipe. They are not a substitute for visual inspection.
- Standalone native pipe CTest and four Python pipe-client tests passed.
- 16 Python project tests passed with the actual Mac fixture opted in; one skips
  without that external input. Four qualification-validator tests passed.
- Unicode atomic module save/replacement/public reopen/failure cleanup passed in
  a disposable scratch directory. This is not native project saving.
- Computer-use invoked Pattern focus, Compose, sample selection and command
  search/Enter in the real QA process; API readbacks verified the resulting
  layout and sample/pin state. Actual screenshots were inspected. No foreground
  escalation, default-route change or musician process was needed.
- The mouse test reproduced a real capture bug: queued no-button hover messages
  could redirect a drag to the physical pointer. Motion now requires MK_LBUTTON;
  a DPI-aware regression asserts selection and context remain correct.
- Independent review found and drove regressions for immediate unpin/follow,
  Shift-click anchoring, non-selecting placement, and undrawn grid margins.
  Related tests cover independently hidden panels, precise API revision-guard
  discovery and partial/reversed wheel deltas. Final live computer-use unpin
  switched the inspector from row 4 to row 16 immediately; pipe reads also
  confirmed playback continued and the edit cursor remained independent.
- Independent re-review passed after the corrections (source review, no runtime
  execution by that reviewer). The parent separately reran all 16 workspace tests,
  app API/offline tests, 24 portable tests and native/Python pipe suites. Evidence:
  `bin/windows-workspace/tests-final.log` and `workspace-review.json`.
- No QA application remains running after the bounded final run.

### Real engine plus new workspace

Final corrected executable: `bin/windows-workspace/engine-workspace-final.json`,
**120.021 seconds**, 48,000 Hz, 480-frame negotiated period, **12,006 callbacks**,
5,762,880 rendered frames. Maximum callback **675.1 us**, service **686.3 us**;
zero reported deadline overruns, starvation indicators, device faults and MMCSS
errors. The UI and pipe were exercised during playback. Normal demo attenuation
remains -20 dB. The earlier 65-second run is retained separately in
`engine-workspace.json`; it predates the final interaction corrections.

7,152 UI frames drawn; CPU draw p99 **10,067 us**. No DXGI displayed-frame gate
was added to the integrated report: **sustained 60fps remains unqualified**.
Zero occlusion status reports are not proof of continuous visible presentation.
No physical loopback/latency measurement or complete allocation/free/lock audit
was performed. Do not substitute these counters for those gates.

## Reproduce

```powershell
.\windows\build.ps1 -BuildDirectory "$PWD\bin\windows-workspace" -Test
$env:SCREAMSEQ_TEST_EXE = "$PWD\bin\windows-workspace\Release\ScreamSeq.exe"
python -B windows/Tests/test_workspace.py -v
python -B windows/Tests/test_app.py
python -B windows/Tests/test_app_api.py
python -B windows/Tests/test_qualification.py
$env:SCREAMSEQ_REFERENCE_PROJECT = 'C:\path\to\reference.screamseq'
python -B -m unittest discover -s windows/Tests/Project -p 'test_*.py' -v
python -B windows/Project/qualify_project.py $env:SCREAMSEQ_REFERENCE_PROJECT
```

Set TMPDIR/TEMP/TMP to the approved scratch directory before tests. For native
pipe/client tests use `Api/README.md`. To reproduce the bounded real-audio run,
use `--audio-test --automation --seconds 120 --report <new-report-path>` in a
separate QA process; it intentionally plays the built-in demo, not a user song.

## Next vertical slice / limits

1. Integrate a loss-preserving native project codec with shared Document and
   NativeSong restoration, full semantic validation and real Mac reopen/audio
   comparisons. Do not expose a partial project loader or renamed module Save.
2. Then implement musical pattern editing through the shared edit layer with
   API, atomic validation, Undo/Redo and native persistence together.
3. Continue retained workspace work with virtual-grid UI Automation, deliberate
   small-dock scrolling, arbitrary docking/floating, saved layouts and configurable
   shortcuts. Current right/lower splitters are not a complete docking framework.
4. Extract shared hosted graph/plugin orchestration and add Windows VST3/MIDI,
   audio endpoint UI/device-fault qualification, and sustained presentation tests.

No file in the Mac platform tree was modified; no independent Windows musical
model or DSP path was introduced. The full original Windows-port task is still
open, not completed by this UI checkpoint.
