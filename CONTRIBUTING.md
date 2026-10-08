# Contributing to ScreamSeq

ScreamSeq development integrates on `main`. Open issues and pull requests in
this ScreamSeq repository. Include the observed behavior, the intended result,
the platform and the smallest useful reproducer. ScreamSeq's native applications
are maintained here independently of OpenMPT.

Start with [the architecture](doc/SCREAMSEQ_ARCHITECTURE.md),
[the UI philosophy](doc/RESONANCE_UI_PHILOSOPHY.md) and [AGENTS.md](AGENTS.md).
Build instructions are in the [macOS](mac/README.md) and
[Windows](windows/README.md) guides. Repository-specific agent instructions live
in [.agents/skills/](.agents/skills/).

## Keep musical behavior shared

The C++ song model, transactions, playback semantics and DSP belong in `editor/`
or narrowly guarded engine extensions. AppKit/Metal and Core Audio integration
belong in `mac/`; Win32/Direct2D and WASAPI integration belong in `windows/`.
Coordinate changes to shared APIs and project data across both frontends.

User-visible musical edits need an API operation, validation, Undo and
persistence. Use stable document identities and revision guards, validate whole
batches before committing, and preserve unfinished UI drafts and their captured
targets. Plugin preparation, allocation, file I/O and UI calls must stay outside
the audio callback.

Current native projects use container 6 / metadata 17; earlier native formats
are rejected. Preserve OpenMPT module import/playback and the legacy identifiers
listed in [the branding notes](assets/branding/BRANDING.md). A product rename is
not a reason to rewrite persisted plugin IDs, recovery paths or wire formats.

## Validate the change

Use a separate build directory and disposable projects. Preserve any running
musician session, its bundle, unsaved work and system audio defaults. Run the
relevant model, API, persistence and platform tests for the change. Audio changes
need rendered-audio evidence; UI and presentation claims need native observation.

Report the exact checks performed, failures and untested boundaries in the pull
request. Distinguish automated tests from live audio, visible UI and sustained
performance qualification. Dated reports describe their recorded executable;
they do not qualify a later source tree automatically.

Keep generated binaries, build caches, user songs, private sample packs and
credentials out of commits. Include source dependencies, reproducible fixtures
and their licenses. Keep changes focused and preserve upstream history and
copyright notices; see [upstream provenance](UPSTREAM.md) and [LICENSE](LICENSE).
