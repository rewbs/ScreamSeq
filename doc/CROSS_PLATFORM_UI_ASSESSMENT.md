**ScreamSeq cross-platform UI assessment — 2026-09-22**

**Recommendation: a unified UI is technically credible, and Qt 6 is the strongest first choice for this codebase.** Start the evaluation with a C++ Qt Widgets workspace and shared custom GPU views. Keep JUCE as the alternative. Adoption should depend on a representative prototype running on both platforms, rather than a framework demonstration or an assumption that native code is always faster.

This is an architectural source review at Windows checkout `49365f6d1`, including the Mac implementation present in this checkout. It is not a line-by-line correctness audit or a benchmark of a Qt/JUCE port. No application was launched, no audio configuration was changed, and no Mac build was run. The existing architecture and qualification reports were read as context and checked against representative current source. Online framework documentation was checked on the assessment date.

Concurrent Windows native-control edits appeared near the end of the review. Their diff concerns control updates, painting and layout; this assessment does not qualify that work. Only this report was added by this review.

**What the existing implementations establish**

| Concern | Source evidence | Consequence for unification |
| --- | --- | --- |
| Document, edits, history and native musical state | [TrackerDocument.hpp](../editor/TrackerDocument.hpp), especially `Document::transaction`, snapshots and `Renderer`; [NativeSong.hpp](../editor/NativeSong.hpp) | Preserve the existing C++ engine and transaction system. A new UI does not require a new song model or audio engine. |
| Shared hosted playback | [hosted/CMakeLists.txt](../editor/hosted/CMakeLists.txt), [PluginBackend.hpp](../editor/hosted/PluginBackend.hpp) | Sharing already extends to plugin scheduling, graph processing and instrument orchestration. Several portable sources still live under `mac/Audio`, but are compiled into the shared target. Their directory name does not make them Mac-only. |
| Mac tracker rendering | [PatternView.swift](../mac/App/PatternView.swift), `prepareGeometry`, `createAtlas`, `renderFrame` | One virtualized Metal view, visible-row geometry, a glyph atlas, three bounded vertex buffers and display-link pacing. Preserve these performance properties in a shared renderer. |
| Windows tracker rendering | [RenderSurface.hpp](../windows/App/RenderSurface.hpp), [WorkspaceDraw.inc](../windows/App/WorkspaceDraw.inc) | Direct2D/DirectWrite over a D3D11 flip swapchain, bounded retained text layouts and visible-cell drawing. The useful abstraction is shared geometry, text and input behavior, not a separate widget for every tracker cell. |
| Workspace | [DockWorkspace.swift](../mac/App/DockWorkspace.swift), [WorkspaceState.hpp](../windows/App/WorkspaceState.hpp) | Mac retains panels through docking/floating and stores pins/layout state. Windows has a more restricted main-workspace placement model alongside separate modeless editors. A unified implementation could remove this recurring parity work. |
| Graphs | [SignalCanvas.swift](../mac/App/SignalCanvas.swift), [GraphCanvas.hpp](../windows/App/GraphCanvas.hpp) | Both calculate sockets, curves and hit-test geometry. This is application presentation logic that can be shared; it does not intrinsically need AppKit or Win32. |
| Draft editors | [PatternAutomation.swift](../mac/App/PatternAutomation.swift), [ParameterAutomationWindow.hpp](../windows/App/ParameterAutomationWindow.hpp), both formula workbench implementations | Captured targets, revisions, draft generations, asynchronous previews and focus rules are substantial duplicated behavior. A common toolkit alone will not consolidate them automatically. |
| Session boundary | [TrackerSessionAPI.inc](../mac/Bridge/TrackerSessionAPI.inc), [main.swift](../mac/App/main.swift), [DocumentController.hpp](../windows/Session/DocumentController.hpp), [DocumentController.cpp](../windows/Session/DocumentController.cpp) | There is no single drop-in portable application session yet. Mac uses an Objective-C++ bridge and a Swift document queue; Windows owns a C++ document worker and immutable views. Both perform revision validation in their adapters. |
| Native plugin editors | [VST3Host.mm](../mac/Audio/VST3Host.mm), [WindowsVST3.cpp](../windows/Plugins/WindowsVST3.cpp), [UiOwner.hpp](../windows/Plugins/UiOwner.hpp) | Mac attaches plugin views to NSView; Windows attaches to HWND on a private COM STA. These lifecycle and threading requirements remain even with a common frontend. |

The high-frequency visuals are mostly text, rectangles, curves, waveforms, selection and playhead overlays. I found no rendering requirement that inherently demands separate application UI implementations. The harder requirements are the musician's connected workspace: independent pins, retained drafts, complete keyboard control, editable graphs and automation, detached editing during playback, and multiple windows/monitors. Those requirements favor a desktop framework with good input, text and window support.

The source inventory contains about 12,800 physical lines in `mac/App` and 6,200 in `windows/App`, excluding the Windows render probe and including source headers/inc files. Bridge/session code adds about 8,500 lines. These are size indicators, not duplication percentages or effort estimates: many lines are densely packed, and the directories contain integration as well as UI code. Migration is a substantial frontend rewrite.

**Framework comparison**

| Candidate | Assessment for ScreamSeq |
| --- | --- |
| **Qt 6** | Best overall fit. C++ integration, desktop controls and docking combine with portable custom GPU rendering. My preferred starting architecture is Widgets for the workspace and GPU surfaces for the tracker and demanding canvases. Qt Quick is a viable alternative within Qt if its rendering/layout benefits outweigh the extra desktop integration work. |
| **JUCE** | Credible second choice, particularly for an all-C++ custom audio interface. Its audio/plugin ecosystem is useful, but ScreamSeq already has those foundations. The decisive comparison is therefore UI behavior, docking, text, accessibility and measured drawing cost. |
| **Slint** | Technically promising: C++ bindings and GPU renderer options. Higher integration uncertainty for this particular application. Require an early demonstration of the full docking, text/focus, accessibility and native plugin-window workflow on the actual Windows ARM64 and Mac targets. |
| **Avalonia** | Technically viable with custom GPU drawing and native-view hosting. It introduces C#/.NET and a native interop boundary into a C++ application. More attractive if the team specifically wants C#/XAML than as the default migration here. |
| **Flutter** | Flexible custom UI with desktop support and native interop. It introduces Dart and another runtime boundary; floating workspaces and plugin integration must be demonstrated on both targets. No evidence here establishes that it would be too slow, but it offers a less direct fit than Qt or JUCE. |
| **wxWidgets** | A reasonable native-control C++ shell, but still requires selecting/integrating a shared renderer for the demanding custom canvases. It is less compelling than Qt's combined desktop and GPU facilities for this migration. |
| **Dear ImGui** | Good rendering and tool-building characteristics, but its documented accessibility and full international text limitations make it a poor default for the complete musician-facing application. |

The secondary-candidate conclusions are engineering judgments, not measured rankings. Supporting primary documentation: [Slint renderers](https://docs.slint.dev/latest/docs/slint/guide/backends-and-renderers/backends_and_renderers/), [Avalonia native interop and custom drawing](https://docs.avaloniaui.net/docs/app-development/native-interop), [Flutter platform integration](https://docs.flutter.dev/platform-integration), [wxWidgets overview](https://wxwidgets.org/about/), and [Dear ImGui's stated scope and limitations](https://github.com/ocornut/imgui).

A browser shell could also draw custom canvases, but this review found no reason to introduce its additional integration boundary. Rendering capability alone does not decide suitability. The repository's current native-work guidance also explicitly favors preserving native integration.

**How I would use Qt**

Use Qt Widgets for the main window, compact inspectors, lists, menus, settings, formula text editing and docking. These controls have configurable styling and layout; a unified ScreamSeq visual design does not require reproducing the current platform-specific controls pixel for pixel. Qt provides desktop model/view facilities, and `QDockWidget` supports docking and floating. Start with the built-in docking capabilities; evaluate a larger docking dependency only against a demonstrated requirement. [Qt Widgets](https://doc.qt.io/qt-6/qtwidgets-index.html), [QDockWidget](https://doc.qt.io/qt-6/qdockwidget.html).

Use a custom shared tracker renderer with cached glyphs/geometry and visible-cell traversal. `QRhiWidget` can integrate that renderer into Widgets, with Metal and Direct3D backends. It renders into a backing texture that Qt then composites, so this incurs work compared with drawing straight into a swapchain. QRhi also has limited source/binary compatibility guarantees: isolate its use and pin/test Qt upgrades. This path must earn its place through measurement. [QRhiWidget](https://doc.qt.io/qt-6/qrhiwidget.html).

Ordinary infrequently changing controls do not all need GPU-specific implementations. Graph and waveform views should retain geometry and use bounded detail appropriate to the visible area. Move graph hit-testing, tracker navigation and draft behavior into shared C++ presentation code, independent of both Qt and the audio model.

Qt Quick is the other serious Qt architecture: custom C++ scene-graph items for the tracker/canvases and QML for the surrounding interface. Its scene graph supports batching, portable graphics APIs and a threaded render loop. That is a plausible closer match to the Mac grid's GPU approach. Do not represent the entire song as QML objects per cell, and do not let rendering read the mutable document. [Qt Quick scene graph](https://doc.qt.io/qt-6/qtquick-visualcanvas-scenegraph.html).

Do not assume that putting Qt Quick inside a Widgets shell retains all those threading benefits. `QQuickWidget` introduces an offscreen pass and disables the threaded render loop. A Widgets/QRhi prototype and a Quick/custom-scene-graph prototype are materially different designs. Compare the latter if profiling shows the former is constrained by its composition/event-thread path. [QQuickWidget performance considerations](https://doc.qt.io/qt-6/qquickwidget.html).

Advanced docking is available through KDDockWidgets for Widgets and Quick. Its Quick documentation lists cross-window caveats, and its GPL/commercial license is separate from Qt's. It is an option, not a dependency to assume away. [KDDockWidgets](https://github.com/KDAB/KDDockWidgets), [Quick integration notes](https://github.com/KDAB/KDDockWidgets/blob/main/README-QtQuick.md).

Qt's newer Canvas Painter could simplify custom imperative drawing, but in 6.11 it is a technology preview and is GPLv3/commercial, rather than LGPL. I would not make the migration depend on that module before a deliberate version/license decision. [Qt Canvas Painter](https://doc.qt.io/qt-6/qtcanvaspainter-index.html).

**Why JUCE remains an alternative**

JUCE is a C++ application and audio framework that can be integrated through CMake. Its component model and custom painting can implement this tracker; it also exposes native-view embedding and explicit accessibility interfaces. Using its GUI does not require replacing ScreamSeq's audio engine. [JUCE repository](https://github.com/juce-framework/JUCE), [NSView embedding](https://docs.juce.com/master/classjuce_1_1NSViewComponent.html), [accessibility interfaces](https://docs.juce.com/master/classjuce_1_1AccessibilityHandler.html).

The current website describes JUCE 9. Direct2D became the default Windows renderer in JUCE 8; current Mac source still includes a CoreGraphics/Metal-layer rendering path. That is not evidence that ordinary JUCE painting is equivalent to ScreamSeq's explicit Metal glyph batching. Prototype the dense grid on both systems before accepting or rejecting it. [JUCE 9 features](https://juce.com/whats-new/), [Direct2D rendering](https://juce.com/blog/juce-8-feature-overview-direct-2d/), [Mac rendering source](https://raw.githubusercontent.com/juce-framework/JUCE/master/modules/juce_gui_basics/native/juce_NSViewComponentPeer_mac.mm).

I prefer Qt because this application needs an extensive retained desktop workspace, and much of JUCE's audio-host advantage is already implemented here. That preference could change if a JUCE prototype demonstrates simpler maintenance and equal or better interaction under load. Neither framework supplies ScreamSeq's pin/draft semantics automatically.

**What should become shared, and what should remain behind platform adapters**

| Shared application code | Platform responsibilities |
| --- | --- |
| Tracker, graphs, automation, envelopes, samples and mixer presentation | Core Audio/WASAPI device services and timestamped MIDI integration |
| Commands, key-binding policy, cursor/selection, focus intent, pins and draft state | Native key/text event translation and platform menu/window conventions where required |
| Document/session operations, revision checks and UI/API command dispatch | NSView/HWND plugin windows, COM/main-thread ownership and vendor lifecycle |
| Immutable view models, cached geometry and playback display sampling | Native file dialogs, application lifecycle, packaging/signing and OS integration |
| Existing musical transactions, history, native persistence and DSP | AU availability and plugin binary architecture compatibility |

There should be one in-process application service behind the UI and external API. Consolidate the portable portions of the Windows session operations and Mac bridge incrementally. Keep existing revision/context checks and distinct history domains. Do not route the new UI through the external JSON pipe/socket as a permanent rendering data path, or create a second musical model in Qt objects. During an initial prototype, a narrow adapter around the current session is sufficient; extracting every operation first would delay the actual framework evaluation.

Preserve document-worker ownership, immutable UI snapshots, prepared audio state and bounded telemetry publication. Qt signals, QML, managed runtimes, document locks, serialization and plugin discovery must stay off the audio callback. UI frame timing must never become the musical clock. Slow UI can still compete for CPU/GPU resources and delay live input, so separation is necessary but does not remove the need for loaded measurements.

Existing plugin editors can initially stay in their own platform-native top-level windows through `PluginBackend::showEditor/closeEditor`. Docking third-party editor views is a separate optional integration project. In particular, Windows currently gives plugins a private STA; casually reparenting those views into Qt's UI-thread hierarchy changes ownership assumptions. Keeping native plugin windows does not mean maintaining two ScreamSeq frontends.

Qt's current Windows matrix supports native ARM64 with MSVC, but explicitly excludes ARM64EC. JUCE lists both ARM64 and ARM64EC. Neither statement proves cross-architecture plugin hosting in ScreamSeq: running x64 plugin binaries inside an ARM64 application needs a separately designed compatibility/hosting solution. AU remains Mac-only regardless of frontend choice. [Qt Windows support](https://doc.qt.io/qt-6/windows.html), [JUCE deployment targets](https://juce.com/whats-new/).

License/deployment scope is part of the choice. Qt offers LGPLv3 and commercial routes, with some modules under GPL instead. JUCE offers AGPLv3 and commercial licensing. The repository's BSD attribution obligations remain. Decide the intended distribution model and exact dependencies before selecting a licensing route; the renderer does not require buying into an entirely new audio stack. [Qt licensing](https://doc.qt.io/qt-6/licensing.html), [JUCE licensing](https://juce.com/get-juce/).

**A concrete adoption gate**

Build a separate executable against the existing model/renderer with one demanding slice: editable tracker with 1–8 FX columns, graph, waveform/automation canvas, pinned draft inspector, formula text input, floating panel and a real native VST3 editor. Use genuine document commands, Undo and save/reopen so the evaluation exercises application ownership as well as drawing.

| Gate | Evidence needed |
| --- | --- |
| Presentation | Same visible song/workload, physical pixel dimensions, DPI and refresh rate as the native baseline. Measure CPU preparation, GPU time, actual presented-frame intervals and missed refreshes. At 60 Hz the whole-frame interval is 16.67 ms; at 120 Hz it is 8.33 ms. These are budgets, not claimed results. |
| Interaction | Rapid musical typing while a worker edit completes; tracker selection and horizontal scrolling with eight FX columns; graph dragging; waveform zoom; text composition/completion; focus changes into plugin windows; key releases on deactivation. Record input-to-visible-response latency and stalls. |
| Workspace | Dock/float/redock, mixed-DPI monitors and different refresh rates; retained target, pin, scroll, selection and text drafts; no stale completion overwriting a newer draft. |
| Audio | Same sample rate, device period, song and plugins as baseline while exercising UI. Compare callback durations, deadline overruns, faults and available glitch/loopback evidence. Keep audio callback audit separate from UI frame-rate success. |
| Idle/resources | Stopped, unchanged, hidden and minimized states; idle CPU/GPU activity, memory growth and wakeups. Test multiple detached windows rather than only one main canvas. |
| Accessibility | Actual keyboard workflows and screen-reader semantics for the custom tracker/graph, not just accessible stock buttons. Custom surfaces still need application-provided semantic interfaces. [Qt accessibility](https://doc.qt.io/qt-6/accessible.html). |
| Compatibility | Current native project round-trip and relevant shared/API regressions on both platforms. Plugin state, missing AU preservation, latency changes and shutdown must keep their existing contracts. |

Use at least a 60-second steady-state presentation capture and longer loaded interaction runs, reporting resize/occlusion separately. The repository's historical Windows render probe is useful evidence that its starting renderer was viable; it is not an equivalent-workload comparison with any candidate, nor proof that native rendering is fastest. Set acceptable regression thresholds against freshly measured baselines before deciding to migrate.

If the prototype passes, migrate complete connected editor families into one shared frontend. Retain each old implementation as a reference until the corresponding workflow is verified, then retire it so the prototype does not become a permanent third frontend. Bring the Mac build into the prototype immediately; a Windows-only success would repeat the current parity problem. Existing headless/model/API tests remain valuable; platform UI tests will require replacement or adaptation.

**Decision:** pursue Qt 6 as the preferred unification experiment. A common UI should reduce repeated feature and interaction work substantially, but the size of that saving is not yet measured. The architecture supports keeping the current audio and project foundations while sharing nearly all application UI behavior. Final adoption depends on the representative cross-platform gate above; there is no evidence from this review that two independently implemented frontends are required for performance.
