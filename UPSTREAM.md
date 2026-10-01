# OpenMPT foundation and attribution

ScreamSeq is an independent tracker DAW, maintained in this repository. It has its own native macOS and Windows applications, shared editing model, project format, plugin hosts and agent API. It is not an official OpenMPT release and is not endorsed by the OpenMPT project.

Its module loading and playback engine comes from [OpenMPT](https://openmpt.org/) and the [OpenMPT source repository](https://github.com/OpenMPT/openmpt). We gratefully acknowledge the OpenMPT developers and Olivier Lapicque, the original ModPlug Tracker author. Their copyright notices and the [BSD 3-Clause license](LICENSE) remain intact. Source files and bundled dependencies retain their individual notices and licenses.

## History and retained code

The complete inherited Git ancestry is preserved. The upstream baseline used for ScreamSeq's stock-engine comparison tests is commit [`f83cedb0cd5446e4dfaa83ac97e3087107e26767`](https://github.com/OpenMPT/openmpt/commit/f83cedb0cd5446e4dfaa83ac97e3087107e26767) (upstream SVN revision 25686). The original source tree and documentation remain available through Git history; no history was rewritten during the transition to `main`.

ScreamSeq retains the engine and support code in `soundlib/`, `sounddsp/`, `common/`, `src/`, and the sample-editing subset of `tracklib/`. The module fixtures in `test/` continue to exercise compatibility. The native apps use the dependencies retained in `include/` and the VST3 SDK in `mac/ThirdParty/`; the latter is shared by both native plugin hosts.

OpenMPT's MFC frontend, legacy plugin bridge, command-line player, installers, standalone libopenmpt distribution, obsolete platform build systems and libraries unused by either ScreamSeq application have been removed from the current tree. Engine namespaces, format identifiers and the `OpenMPTCore` build target deliberately keep their accurate upstream names.

## Playback comparison tests

The independent playback oracle is built from the pinned upstream commit, extracted from local Git history into an ignored build directory. It does not compile against ScreamSeq's modified engine. A full clone contains this history; the script explains how to retrieve the commit if a shallow clone lacks it.

```sh
SCREAMSEQ_BUILD_DIR=bin/mac-background SCREAMSEQ_BUILD_JOBS=2 bash mac/build-reference.sh
```

See [macOS compatibility and qualification](mac/COMPATIBILITY.md) for the comparison procedure. Keeping the oracle outside the current source tree lets ScreamSeq stay focused without losing regression evidence against its foundation.
