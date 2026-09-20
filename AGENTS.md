# ScreamSeq native application work

ScreamSeq is an independent OpenMPT derivative. Preserve upstream code and attribution. The native application lives in `editor/` and `mac/`; a Windows-native sibling belongs in `windows/`. `mptrack/` is upstream OpenMPT's Windows UI, not ScreamSeq's new frontend.

Relevant project skills are in `.agents/skills/`: `screamseq-development`, `screamseq-realtime-audio`, `screamseq-agent-api` and `screamseq-qualification`. Read the ones needed for the task. Start with `doc/SCREAMSEQ_ARCHITECTURE.md` and the user's `doc/RESONANCE_UI_PHILOSOPHY.md` for native-app work.

Keep musical behavior, project data and agent API semantics shared across platforms; keep native UI/device hosting platform-specific. All new musical editing features need API, Undo and persistence. Legacy Resonance storage identifiers are compatibility contracts; see `assets/branding/BRANDING.md` before renaming them.

When a musician has a running app, develop and test in a separate bundle/process without replacing their session or changing system audio defaults. Dated reports are historical evidence; verify current source and tests before relying on a completion claim.
