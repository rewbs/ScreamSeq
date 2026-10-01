# Vendored VST3 interfaces

These are the official Steinberg `vst3_pluginterfaces` headers and support sources, plus `vstinitiids.cpp` from `vst3_public_sdk`. Exact source revisions are recorded in `REVISIONS.json`. Sources are unchanged. Both repositories were obtained from https://github.com/steinbergmedia and use the included MIT license.

Resonance compiles only the platform-neutral interface identifiers and FUnknown helpers. Its macOS bundle loader, event and parameter queues, state streams, controller connection, transport and NSView host are implemented in `mac/Audio/VST3Host.mm`. No VST2 or Windows runtime is included. The test bundle is built from `mac/Tests/FixtureVST3.mm`, not from a commercial plugin.
