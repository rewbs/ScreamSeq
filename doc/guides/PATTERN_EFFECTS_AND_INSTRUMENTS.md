# Pattern effects and instrument sound sources

## Find an effect

Press **?** in a volume or FX cell. The floating finder starts in
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

## Unified FX columns

Every channel has **1–8 equal FX columns**. Click **+FX** in its header to add
one, or right-click and choose **FX columns**. Every column accepts every effect
in the current format's finder, including PS/PL, BS/BL and NC. Use arrow keys
between each command and its value; **?** finds effects, and Return edits a cell.
The value editor retains full precision even when the grid shows a rounded value.

Effects run left to right. Independent effects combine; repeated set commands
leave the rightmost value. Tracker effect memory belongs to the channel, so
moving a command between columns preserves its memory. Notes trigger once even
when several effects apply. Source-format timing and conflict rules (including
pattern delays) still apply. Reducing the count refuses to hide populated cells.

Copy/paste and row insertion, deletion, clear, reverse and rotation include all
FX columns. Paste brings referenced parameter bindings and grows the destination
column count as needed. Module export cannot represent extra or precise effects;
save these songs as `.screamseq`.

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
`pattern.effects.get/set`, `pattern.effect.set`, `pattern.commands`, `instrument.patch`, and the
new atomic `instrument.plugin.set`. Normal revision checks, validation, Undo and
project persistence apply.

## Direct command entry and playback displays

Type **PS**, **PL**, **BS** or **BL** in any FX command cell to open that
command's target editor. The first character stays as a visible pending prefix;
Escape cancels it. Ordinary two-character aliases such as **0H** and **SD** also
work. Moving away after one letter keeps its legacy effect meaning. The chosen command replaces the contents of that FX cell.

The inspector menu is now a scrollable row of tabs, with Control-Option shortcuts
shown on each tab. The duplicate inspector switcher above the pattern is removed.
Click **ROW** to cycle rows, beats, pattern time and song time. Time values include
tempo changes and refer to the first visit to each row in the selected order
occurrence; `--:--.---` means that position is unreachable or not arranged.

Waveforms and instrument envelopes show separate light cursors for every active
native sample voice, including overlapping auditions. Their positions come from
the audio engine and follow sample loops and envelope progress. Third-party
plugins do not expose their internal waveform/envelope positions.

Double-click a recovery entry to restore it through the same protected recovery
workflow as the button. Double-click a graph plugin to open its custom interface
(built-ins open their parameter controls). Reusable subgraph interfaces still
edit a draft: **Apply plugin settings** saves changes to the recipe. Select an
audio/modulation wire and press Delete to remove its route; deleting a bus's main
output leaves that bus disconnected, with its sends preserved.

## Creating a VSTi/AU trigger instrument

1. In **Plugins**, add the instrument plugin (for example Serum), select it, and
   click **New trigger instrument…**. Alternatively choose **Instruments →
   New plugin instrument…**.
2. Choose the synth instance and MIDI channel, name the instrument, and click
   **Create & assign instrument**. No sample is needed.
3. Use the returned instrument number beside a note in the pattern. It also
   becomes the current instrument for note entry and keyboard preview.

**Assign instrument plugin…** changes the sound source of an existing instrument.
**Assigned instruments…** manages existing parts sharing a synth. These actions
share the instance's preset, automation and outputs; separate synth instances are
needed for independent presets. Creating a trigger and assigning it use document
and plugin Undo respectively.

## SC and precise NC cuts

Type **SC**, then the hexadecimal tick digit, in any FX command cell.
Typing the digit now preserves SC's subcommand; it cannot accidentally turn SC2
into S32. The timing remains the original format's tick behavior.

For finer placement, type **NC**, or find **Precise note cut / plugin note-off**
with **?**. Choose an offset in **Rows** or **Beats** and Apply. NC has 1/65,536-row
resolution and runs at the corresponding audio sample boundary. At four rows per
beat, 0.125 beats is halfway through the row. The offset must stay within the row.
Native samples cut with the usual tiny anticlick ramp; plugin instruments receive
note-offs and retain their own release tails. Other tracker channels sharing the
plugin remain active. A cut at an exact retrigger position runs after the trigger.

## Native project format

This development build writes project container version 6 with native metadata
17. Historical `.screamseq` and `.resonance` project versions are deliberately
rejected. Import of original OpenMPT-compatible modules remains available.
