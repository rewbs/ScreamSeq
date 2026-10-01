# ScreamSeq native application work

ScreamSeq is an independent native tracker DAW built on OpenMPT's playback engine, with `main` as its integration branch. Preserve upstream history, copyright notices and attribution in the retained engine. Shared musical behavior lives in `editor/`; the native frontends live in `mac/` and `windows/`. Upstream OpenMPT's Windows UI is not ScreamSeq's frontend. See `CONTRIBUTING.md` and `UPSTREAM.md` for repository scope and provenance.

Relevant project skills are in `.agents/skills/`: `screamseq-development`, `screamseq-realtime-audio`, `screamseq-agent-api` and `screamseq-qualification`. Read the ones needed for the task. Start with `doc/SCREAMSEQ_ARCHITECTURE.md` and the user's `doc/RESONANCE_UI_PHILOSOPHY.md` for native-app work.

Keep musical behavior, project data and agent API semantics shared across platforms; keep native UI/device hosting platform-specific. All new musical editing features need API, Undo and persistence. Legacy Resonance storage identifiers are compatibility contracts; see `assets/branding/BRANDING.md` before renaming them.

When a musician has a running app, develop and test in a separate bundle/process without replacing their session or changing system audio defaults. Dated reports are historical evidence; verify current source and tests before relying on a completion claim.
