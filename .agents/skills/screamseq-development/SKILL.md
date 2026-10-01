---
name: screamseq-development
description: Develop ScreamSeq's native tracker UI and shared song model, preserve OpenMPT compatibility, and coordinate macOS and Windows implementations in this repository.
---

Locate the ScreamSeq checkout from the task's working directory or project context. Read `doc/SCREAMSEQ_ARCHITECTURE.md` for boundaries and `doc/RESONANCE_UI_PHILOSOPHY.md` for the musician's design requirements. Historical Resonance names in documents describe this same application; dated reports are evidence at that checkpoint, not current feature truth.

Keep shared musical data, editing semantics and DSP in `editor/` or narrowly guarded `soundlib/` extensions. macOS uses AppKit/Swift with a Metal pattern grid and an Objective-C++ session bridge. Windows belongs alongside it in `windows/`; don't replace either native UI with a common web shell. Choose render/audio frameworks using measurements and native integration, rather than assuming the newest abstraction is fastest.

User-visible musical edits also need an agent API path, schema, Undo and persistence. Stable document IDs must survive insertion/reordering; a rack index, pattern index or instrument slot is not persistent identity. Native metadata is versioned: inspect the current encoder/decoder before adding fields, accept only the current native project format. The user explicitly does not require historical `.screamseq` / `.resonance` files to remain readable before release. Preserve OpenMPT module import/playback. Use the API and qualification skills when those parts change.

Preserve panel drafts, their captured target/revision, selection, scroll, focus and pin state. Refreshes must not steal focus or redirect an edit. Prefer connected dockable inspectors and compact controls over blocking dialogs. Playback position, editing cursor and keyboard focus are separate. Global transport shortcuts yield to intentional local overrides; text fields and live musical typing have different responsibilities.

Graph curve drafts capture the graph, source, pattern and revision. An Apply completion must not discard edits made while that request was in flight; compare a draft generation as well as the target. Keep socket geometry and wire hit-testing in the same coordinate system, cache curves outside drawing, and preserve untouched routes when updating a selected wire. Floating the graph is useful for dense patching, but narrow dock layouts still need readable controls.

Before converting integer types, validate the source range or widen first. Past Swift crashes came from narrowing sample/velocity fields, and playback bugs came from modifying a different channel while handling a precise event. Validate whole batches before committing. Keep module-format limits and special note values explicit.

ScreamSeq branding intentionally retains legacy `org.resonance` identifiers, built-in class IDs, discovery/cache/recovery locations and wire magic. See `assets/branding/BRANDING.md`; do not globally replace persisted identifiers. New projects use `.screamseq`; historical native project versions are rejected.

Build into a separate directory while another app is running. Never overwrite its bundle or save destination. Before committing, inspect untracked sources: much of this application's initial history was uncommitted. Exclude build products, user songs, private dumps, caches and credentials, but include every source dependency and required license. Preserve upstream OpenMPT attribution and history.

Formula workbenches capture the original point and draft generation, validate the current text asynchronously, and preserve text if a newer draft makes Use stale. Control-Space in FormulaCodeView is a local completion binding and must yield from global transport. Reopening a visible workbench/bank should raise its existing draft. Bank operations also recheck the parent draft on completion. Wrap explanatory native labels with a preferred width and low horizontal compression resistance; otherwise a long help sentence can force a nominal 940-point window wider than the display.
