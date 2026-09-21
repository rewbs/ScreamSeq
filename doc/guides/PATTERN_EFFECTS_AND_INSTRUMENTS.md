# Pattern effects and instrument sound sources

## Find an effect

Press **?** in a volume, effect or native FX cell. The floating finder starts in
its search box. Search words such as `vibrato`, `delay`, or `slide plugin`; use
Up/Down to choose. Return on an ordinary tracker effect moves to its value field;
Return there applies it. Return on a native effect opens its target/timing editor.
Escape or Close dismisses without changing the song. Double-clicking the ordinary
effect column and **Pattern → Command Picker** also open the finder.

The finder shows commands supported by the current song format. Ordinary source
commands have two-character display aliases (`0A`, for example); extended commands
show their prefix (`SD`, for example). Original one-letter keyboard entry and
source-format byte values remain supported. These aliases are not a Renoise
command translation table.

## Extra effect columns

Click **+FX** in the channel header to add a native FX column. Right-click the
pattern and choose **Native effect columns** to select a count from 0 to 8.
Deleting a column that contains commands is refused rather than losing music.
Use the arrow keys to enter the added cells, then **?**, Return or double-click.

These extra columns currently hold native plugin-parameter and pitch commands.
The original tracker-effect column continues to hold source-format effects.
Generic additional source-format effect columns and the remaining Renoise
command behaviors are separate engine work.

## Plugin parameters from the pattern

Use **PS · Set plugin parameter** for an immediate change, or **PL · Slide plugin
parameter** for a glide from the current value to the target. Choose the plugin
and named parameter, then enter a **Target value (%)** with as many decimal digits
as needed. For a slide, set **Duration (rows)**. **Row offset** can place the start
between row boundaries. Apply saves the target binding and command as one Undo.
The first command automatically enables its destination FX column.

Alternatively, right-click a parameter row in the plugin inspector and choose
**Set this parameter in pattern** or **Slide this parameter in pattern**. The
editor opens at the current pattern cursor with that stable target already chosen.

The binding number references a plugin instance UUID and its native parameter ID.
Reordering the plugin rack does not change the target. Choosing a different target
creates/reuses another binding; it does not silently retarget other cells.

Values are stored as double-precision numbers. The grid's four hex value digits
are a compact, rounded preview only. Timing uses 65,536 units per row; output
scheduling is bounded by the audio sample rate and the plugin's automation support.
Only continuous parameters support slides. VST3 receives sample-offset parameter
queues; a third-party plugin controls its own internal response to those queues.

**BS · Set pitch bend** and **BL · Slide pitch bend** use semitones. For plugin
instruments, match **Plugin wheel range** to the synth's pitch-bend setting. Notes
sharing its MIDI channel share the wheel. Native sample pitch is independent of
that MIDI limitation.

## Preview instruments and assign plugin instruments

The **Sample** inspector previews the raw sample. The **Instrument** inspector
previews its keymap and enabled instrument envelopes. Click **Play instrument with
keys**, then use Z–M / Q–U. No instrument mode switch is required. If there are no
instruments yet, choose **New** first; existing sample slots are preserved as
matching instruments when converting a sample-only song.

**Enable envelope** applies immediately. Other instrument fields still use their
visible Apply controls. An enabled envelope is required to hear its shape.

In the instrument inspector, choose **Assign instrument plugin…**, select an AU
or VST3 instrument and its MIDI channel, then **Apply assignment**. Select **No
plugin · use sample keymap** to detach. Moving an instrument preserves other
instruments sharing either plugin and uses one effect-history Undo step.

The plugin inspector's **Assign tracker instruments…** opens its full multitimbral
assignment list. Create a tracker instrument with **New** before adding it there.

## Context menus and agents

Right-click the pattern for effect discovery, FX column count, note tools,
selection actions, transport and pattern tools. Inspector and floating-editor
menus expose their existing buttons, including tools in collapsed sections.
Graph node right-click selects that node before offering actions. Existing
shortcuts appear beside corresponding commands; **All commands & shortcuts**
provides the complete application menu. Editable text keeps its standard menu.

Musical edits use the same API as the UI. See `mac/AUTOMATION.md`, especially
`pattern.performance.get/set`, `pattern.commands`, `instrument.patch`, and the
new atomic `instrument.plugin.set`. Normal revision checks, validation, Undo and
project persistence apply.
