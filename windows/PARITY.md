# Windows parity / acceptance matrix

**Workspace continuation:** `WORKSPACE_CHECKPOINT.md` is the latest scope and
evidence; `HANDOFF.md` retains the earlier bootstrap history. The Mac visual pack
has been reviewed, the native read-only workspace now has real navigation,
selection, inspectors and commands, and the actual Mac fixture has passed Python
container qualification. This remains a demo player, not an editing release.

This is an incremental port, **not Windows feature parity or a usable editing
release**. A successful core test does not implement its corresponding Windows
UI/API. No unchecked row below is an implicit capability. Mac behavior is defined
by current source and tests, not historical completion labels.

| Requirement | Windows implementation / verification status |
| --- | --- |
| Independent branch/worktree; preserve upstream attribution | `codex/windows-native`, sibling worktree; no commits/pushes; original checkout unchanged |
| Native ARM64 toolchain | MSVC 19.44.35229, VS2022 17.14.41 and Windows SDK 10.0.26100 installed and renderer compiled |
| Win32 + D2D/DirectWrite/D3D11/DXGI | Real GPU probe built, run, captured and inspected; measured loaded run in ARCHITECTURE.md |
| Sustained 60fps integrated workload | **Not qualified**; prototype has refresh gaps and incomplete present statistics |
| Shared OpenMPT document and renderer | Windows CMake builds shared `editor/`/`soundlib/`; 24 portable functional tests pass |
| Event-driven WASAPI shared low periods | Integrated shared-mode demo; latest 120-second real-engine/workspace run passes reported callback/device counters, not acoustic latency |
| Explicit exclusive mode / optional ASIO | Not implemented; no endpoint-exclusive access or ASIO startup dependency |
| Callback allocation/free/lock audit | Limited C++ new/delete diagnostic only; full host audit pending |
| Device loss / format changes / restart | Backend exits on faults and rejects changed rate/period on restart; controlled fault tests pending; no automatic reconfiguration UI |
| Timestamps / latency / underruns | Callback/padding telemetry implemented; no acoustic latency, comprehensive xrun or physical loopback qualification |
| Bounded shutdown | Explicit wake/join; unreturning driver call/callback can still block; no hard boundedness guarantee |
| Demo playback / independent cursor and playhead | ARM64 development host built and inspected; independent cursor/selection/playhead, Follow and real WASAPI demo playback |
| Module project open / full pattern editing | Pending; app must not suggest demo navigation is editing |
| Native `.screamseq` and `.resonance` load/save | Portable Python container foundation only; C++ integration and Mac-generated fixtures pending |
| Exact samples / unknown data / unavailable AU state | Generated trees plus one actual Mac fixture preserve exact opaque sections and typed plist tree; not an application/musical compatibility result |
| Metadata 14 / envelope banks | Version recognized and opaque data retained; typed restoration/UI pending |
| Pattern edits / Undo / Redo / stale guards / dry run | Shared editing layer exists; Windows musical mutation methods not exposed yet |
| Named pipe / local-only ACL / portable Python client | Private pipe integrated with control-thread host; actual app, DACL/collision, client and navigation/workspace tests pass |
| Full validated API method/schema parity | Pending; advertise only methods actually implemented; never successful stubs |
| Windows VST3 effects/instruments / HWND editors | Not implemented |
| Stable plugin class/instance/parameter identity | Preservation tooling does not rewrite identities; hosting/resolution pending |
| Cached isolated plugin scanning / explicit rescan | Not implemented |
| Plugin automation sample offsets / buses / sidechains | Shared/native Mac source exists; Windows host extraction/integration pending |
| Runtime plugin process isolation | Not implemented; no Windows plugin host presently loaded |
| Connected pattern/graph/automation workspace | Reference-guided Compose layout, retained note/sample inspectors; graph/automation areas explicitly unavailable, not functional editors |
| Dock/resizing/floating / Follow/Pin/Return | Resizable right/lower splits, three session-local presets, independent inspector Pin/Cursor/Return; arbitrary docking/floating and saved layouts pending |
| Full keyboard control / command palette / accessibility | Native buttons, grid field navigation/selection, F6 focus and searchable Ctrl+K palette; configurable keys/sequences and virtual-grid UIA pending |
| Sample/instrument inspectors and audition | Real sample selection, min/max waveform, frame/loop metadata and independent retained target; audition/editing pending |
| Selection/pattern/song playback and looping | Shared renderer capabilities; initial demo song loop only, broader controls pending |
| Timestamped MIDI / recording / compensation | Not implemented; reuse shared RecordingClock, no separate musical timing model |
| Shared graphs / modulation / scripts / assignments / commands | Core regression targets only; no Windows hosted musical workflow yet |
| Stock OpenMPT comparison / real rendered PCM | Actual-app offline 44.1/48/96 kHz and 17/128/4096 partitions pass finite/nonzero and <1e-6 PCM bound; not stock OpenMPT comparison |
| ASan / lifetime/index diagnostics | Not run on Windows |
| System audio route and musician session safety | No default routing/master-volume changes; disposable demo only; no running musician app found at initial inspection |
| ScreamSeq icon | Existing branding ICO embedded in native application |
| Separate usable checkpoint / notices / fingerprint | Bootstrap preserved; separate workspace QA executable/evidence in bin/windows-workspace, not an editing release |

## Shared change coordination

The initial shared-source changes include a `_WIN32`-guarded atomic module-save
path in `editor/TrackerDocument.cpp`: UTF-8 to UTF-16 paths, same-directory
CREATE_NEW temporary file, complete writes, flush and replace, cleanup on failure.
The existing POSIX branch remains in place. Its prior unconditional `unistd.h`
include was observed failing with the installed compiler, then the guarded source
compiled successfully. Native container persistence is a separate responsibility.
Windows Unicode save/open/replacement/failure tests now pass. Mac regression
review is still required. Also changed: guarded Windows path/filesystem helper
visibility in common/mptPathString.{h,cpp} and explicit ::mpt lookup in
src/mpt/format/default_formatter.hpp.
No direct message exchange with the concurrent Mac agent is claimed; this note
identifies the isolated patch that must be reconciled, without changing the Mac
agent's checkout or platform sources.
