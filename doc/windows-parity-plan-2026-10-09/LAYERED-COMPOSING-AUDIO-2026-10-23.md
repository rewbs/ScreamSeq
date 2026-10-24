# Layered composing audio evidence

The complete sample/VST3 composing and recording journey previously checked
assignment, routing, history, recording and replay, but explicitly left the
independent layer contribution unqualified. The shared Python helper
`editor/Tests/Conformance/recording_render.py` now accepts the captured instrument
slot and stable plugin identity. The existing Windows journey requests this check.
No product/audio behavior changed in this batch.

The first additional diagnostic failed: the plugin-only derivative differed
from the no-layer derivative by at most two signed16 units. This was initially
suspected to be missing note delivery. Source and fixture inspection showed that
the deterministic VST3 instrument emits constant DC while a note is held, whereas
F04 deliberately includes DC removal, filters and a gate. That processed output
is unsuitable as an independent stem oracle. Preserve the failed result at
`bin/mac-gesture-20261022/layer-contribution/render/report.json`; it is not evidence
of a confirmed product defect.

The corrected test retains the original fully processed before/recorded/reopened
comparison unchanged. For independent sources it saves a separate derivative
with the 15 non-instrument rack effects bypassed. An exact typed project comparison
allows only those declared bypass flags. From that derivative it renders both
layers, sample only, plugin only and neither. All four pairwise contribution
assertions still require at least 64 unsaturated samples differing by more than
two signed16 units. Routing, recorded automation and other tracks remain intact;
instrument fields and opaque plugin state are checked during isolation. Each of
the seven renders retains the existing exact Undo/persistence assertion, including
the single consumed sample identity. Source files are hash-protected.

The corrected isolated actual-app diagnostic passed in 29.013 seconds using
source `dfe9300ea1b6729499fd810cf3d560d52c76e34a`, ARM64 executable SHA256
`82db328ccb00fda1027f4edd8d1a461e19f47d23450af0ea03edfe711d56967f`.
At 48 kHz stereo, each 370120-frame render covers about 7.71 seconds. The four
comparisons changed 678641, 643127, 643145 and 740240 unsaturated samples;
the plugin-only contribution peaked at 3277 signed16 units. The original processed
recording changed 11611 unsaturated samples and reopened bit-exactly. Every input
hash and typed preservation check passed. Receipt and PCM:
`bin/mac-gesture-20261022/layer-contribution/isolated-render/report.json`;
test log: `layer-contribution/isolated-check.log`.

This closes that bounded Windows offline-audio question, not native device,
commercial synth, Mac execution, sustained performance or overall parity.
Reuse this evidence while the rendering/provider/fixture inputs remain unchanged.
The pending precise Hit-list keyboard correction changes none of those inputs.
The original native Save fixture failure still blocks the next production package.
