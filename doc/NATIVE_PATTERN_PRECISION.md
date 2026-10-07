# Native pattern precision map

This is the complete mapping of the **79 effect definitions and 14 volume-column definitions** in `editor/PatternCommandsData.inc` to the typed native catalog in `editor/NativePatternCommands.cpp`. The five editable source formats filter that list through their own engine specifications; some definitions are retained for imported formats. This table records musical families and explicit boundaries, not a promise of bit-identical conversion from legacy effect memory.

Native commands retain 65536 row units for onset and common slide duration. NF/NR use a separate floating-point `durationBeats` (default 1 beat), including push and recovery; their old row-unit duration is no longer accepted. Their independent named numeric fields are doubles, except selectors, switches and counts that remain discrete. The UI defaults to beats, with rows available; conversion uses the current pattern signature. Multiple FX columns combine independent controls, replacing packed nibbles and special combined aliases. Imported tracker commands are unchanged and remain available next to native commands.

Continuous GS/GL, PN/PA, PR/PT, VB/TM/PB, AR/TR, SO/DR and envelope controls apply to **native sample voices**. Channel/song gain scopes affect sample voices and do not pretend to change opaque plugin-instrument polyphony or plugin outputs. RT, ND and NO use normal sample/plugin note delivery where the destination supports those events. Existing PS/PL retain stable plugin-parameter bindings; BS/BL retain the supported pitch-bend protocol. For plugin gain/filter/pan modulation use an actual writable plugin or graph parameter. No fractional macro IDs, order IDs, waveform IDs, note-action IDs or switches are invented.

A native command has no hidden legacy byte memory. Zero means the documented field value, not reuse of a previous tracker nibble. RT's count is the number of subsequent repeats; the first occurs one interval after the command. Cue 0 means sample start. PT uses tracker note numbering (`C4 = 49`), including fractional notes.

The catalog, strict API validation, Undo and metadata-17 persistence are shared across platforms. Runtime and UI qualification are recorded separately by the current task report; this inventory alone does not certify rendering.

## Effect definitions

| Engine command / subcommand | Legacy name | Native family or retained behavior | Meaning / boundary |
| --- | --- | --- | --- |
| `CMD_DUMMY` | Empty effect slot | Discrete | Clear the cell; no numeric interpolation. |
| `CMD_ARPEGGIO` | Arpeggio | AR | Independent fractional semitone offsets, rate and phase. |
| `CMD_PORTAMENTOUP` | Portamento Up | PR | Positive semitone change and duration. |
| `CMD_PORTAMENTODOWN` | Portamento Down | PR | Negative semitone change and duration. |
| `CMD_TONEPORTAMENTO` | Tone Portamento | PT | Fractional tracker-note target and duration; no retrigger. |
| `CMD_VIBRATO` | Vibrato | VB | Independent depth, rate, waveform, phase and duration. |
| `CMD_TONEPORTAVOL` | Volslide+Toneporta | GL + PT | Two independent FX columns. |
| `CMD_VIBRATOVOL` | VolSlide+Vibrato | GL + VB | Two independent FX columns. |
| `CMD_TREMOLO` | Tremolo | TM | Independent depth, rate, waveform, phase and duration. |
| `CMD_PANNING8` | Set Panning | PN | Pan -1..1, with percentage display. |
| `CMD_OFFSET` | Set Offset | SO | Exact frame position; no byte-offset memory. |
| `CMD_VOLUMESLIDE` | Volume Slide | GL | Target sample gain and duration. |
| `CMD_POSITIONJUMP` | Position Jump | Discrete | Order index remains an exact integer tracker command. |
| `CMD_VOLUME` | Set Volume | GS | Voice sample gain. |
| `CMD_PATTERNBREAK` | Pattern Break | Discrete | Row index remains an exact integer tracker command. |
| `CMD_RETRIG` | Retrigger Note | RT | Beat interval, subsequent repeat count, volume factor and volume step. |
| `CMD_SPEED` | Set Speed | RL / discrete | RL controls the current row length in beats. The legacy ticks-per-row selector remains discrete. |
| `CMD_TEMPO` | Set Tempo | TS / TL | Fractional BPM set or continuous target/duration slide. |
| `CMD_TREMOR` | Tremor | TR | Independent on/off beat lengths, depth and phase. |
| `CMD_CHANNELVOLUME` | Set Channel Volume | GS | scope: channel; sample voices only. |
| `CMD_CHANNELVOLSLIDE` | Channel Volume Slide | GL | scope: channel; sample voices only. |
| `CMD_GLOBALVOLUME` | Set Global Volume | GS | scope: song; sample voices only, not a plugin-output master fader. |
| `CMD_GLOBALVOLSLIDE` | Global Volume Slide | GL | scope: song; sample voices only. |
| `CMD_KEYOFF` | Key Off | NO | Exact-position note release. |
| `CMD_FINEVIBRATO` | Fine Vibrato | VB | Fractional semitone depth; no fine/coarse nibble split. |
| `CMD_PANBRELLO` | Panbrello | PB | Independent depth, rate, waveform, phase and duration. |
| `CMD_PANNINGSLIDE` | Panning Slide | PA | Pan target and duration. |
| `CMD_SETENVPOSITION` | Envelope position | EP | Envelope selection and beat position. |
| `CMD_MIDI` | MIDI Macro | PS / discrete | Native plugin parameters use stable PS bindings; arbitrary imported MIDI macros retain their original discrete byte protocol. |
| `CMD_SMOOTHMIDI` | Smooth MIDI Macro | PL / discrete | Native plugin parameters use stable PL bindings; imported macro semantics remain unchanged. |
| `CMD_MODCMDEX / 0x00` | Set Filter | Discrete | Imported classic LED-filter switch; choose an explicit native filter processor and PS/PL for continuous filter automation. |
| `CMD_MODCMDEX / 0x10` | Fine Porta Up | PR | Positive fractional semitone change. |
| `CMD_MODCMDEX / 0x20` | Fine Porta Down | PR | Negative fractional semitone change. |
| `CMD_MODCMDEX / 0x30` | Glissando Control | Discrete | Imported pitch-quantization switch; native PT is continuous. |
| `CMD_MODCMDEX / 0x40` | Vibrato Waveform | VB | Named waveform and phase/reset fields, not fractional waveform IDs. |
| `CMD_MODCMDEX / 0x50` | Set Finetune | BS / PR | Absolute bend or relative semitone change; source-format tuning memory is not copied. |
| `CMD_MODCMDEX / 0x60` | Pattern Loop | Discrete | Integer repeat count and loop marker remain tracker flow commands. |
| `CMD_MODCMDEX / 0x70` | Tremolo Waveform | TM | Named waveform and phase/reset fields. |
| `CMD_MODCMDEX / 0x80` | Set Panning | PN | Pan -1..1, with percentage display. |
| `CMD_MODCMDEX / 0x90` | Retrigger Note | RT | Beat interval, subsequent repeat count, volume factor and volume step. |
| `CMD_MODCMDEX / 0xA0` | Fine Volslide Up | GL | Gain target and duration. |
| `CMD_MODCMDEX / 0xB0` | Fine Volslide Down | GL | Gain target and duration. |
| `CMD_MODCMDEX / 0xC0` | Note Cut | NC | Exact command offset in 65536 row units. |
| `CMD_MODCMDEX / 0xD0` | Note Delay | ND | Delay the actual row note to an exact offset; one onset. |
| `CMD_MODCMDEX / 0xE0` | Pattern Delay | RL | One exact beat length for the current row; not a fractional repetition count. |
| `CMD_MODCMDEX / 0xF0` | Set Active Macro | Discrete | Integer macro selection remains imported/tracker behavior; PS/PL use stable parameter bindings. |
| `CMD_MODCMDEX / 0xF0` | Invert Loop | Import behavior | Destructive MOD loop inversion is not sample reversal; original engine effect remains available. |
| `CMD_S3MCMDEX / 0x10` | Glissando Control | Discrete | Imported pitch-quantization switch; native PT is continuous. |
| `CMD_S3MCMDEX / 0x20` | Set Finetune | BS / PR | Absolute bend or relative semitone change; source-format tuning memory is not copied. |
| `CMD_S3MCMDEX / 0x30` | Vibrato Waveform | VB | Named waveform and phase/reset fields, not fractional waveform IDs. |
| `CMD_S3MCMDEX / 0x40` | Tremolo Waveform | TM | Named waveform and phase/reset fields. |
| `CMD_S3MCMDEX / 0x50` | Panbrello Waveform | PB | Named waveform and phase/reset fields. |
| `CMD_S3MCMDEX / 0x60` | Fine Pattern Delay | RL | One exact beat length for the current row. |
| `CMD_S3MCMDEX / 0x80` | Set Panning | PN | Pan -1..1, with percentage display. |
| `CMD_S3MCMDEX / 0xA0` | Set High Offset | SO | Exact frame position replaces high/low byte packing. |
| `CMD_S3MCMDEX / 0xB0` | Pattern Loop | Discrete | Integer repeat count and loop marker remain tracker flow commands. |
| `CMD_S3MCMDEX / 0xC0` | Note Cut | NC | Exact command offset in 65536 row units. |
| `CMD_S3MCMDEX / 0xD0` | Note Delay | ND | Delay the actual row note to an exact offset; one onset. |
| `CMD_S3MCMDEX / 0xE0` | Pattern Delay | RL | One exact beat length for the current row; not a fractional repetition count. |
| `CMD_S3MCMDEX / 0xF0` | Set Active Macro | Discrete | Integer macro selection remains imported/tracker behavior; PS/PL use stable parameter bindings. |
| `CMD_XFINEPORTAUPDOWN / 0x10` | Extra Fine Porta Up | PR | Positive fractional semitone change. |
| `CMD_XFINEPORTAUPDOWN / 0x20` | Extra Fine Porta Down | PR | Negative fractional semitone change. |
| `CMD_XFINEPORTAUPDOWN / 0x50` | Panbrello Waveform | PB | Named waveform and phase/reset fields. |
| `CMD_XFINEPORTAUPDOWN / 0x60` | Fine Pattern Delay | RL | One exact beat length for the current row. |
| `CMD_XFINEPORTAUPDOWN / 0x90` | Sound Control | DR / discrete | DR exposes sample direction. Surround, filter-mode and other switches retain source-format discrete semantics. |
| `CMD_XFINEPORTAUPDOWN / 0xA0` | Set High Offset | SO | Exact frame position replaces high/low byte packing. |
| `CMD_S3MCMDEX / 0x90` | Sound Control | DR / discrete | DR exposes sample direction. Surround, filter-mode and other switches retain source-format discrete semantics. |
| `CMD_S3MCMDEX / 0x70` | Instr. Control | EN / discrete | EN exposes envelope enable. NNA and past-note selectors remain exact discrete tracker controls. |
| `CMD_DELAYCUT` | Note Delay and Cut | ND + NC | Independent precise onset and cut columns. |
| `CMD_XPARAM` | Parameter Extension | Native fields | Full numeric values need no extension row; imported extension bytes retain their original behavior. |
| `CMD_NOTESLIDEUP` | Note Slide Up | PR / import behavior | Fractional pitch change is available; imported integer staircase timing remains source-format behavior. |
| `CMD_NOTESLIDEDOWN` | Note Slide Down | PR / import behavior | Fractional pitch change is available; imported integer staircase timing remains source-format behavior. |
| `CMD_NOTESLIDEUPRETRIG` | Note Slide Up + Retrigger Note | PR + RT / import behavior | Independent pitch and repeat controls; not an automatic PTM staircase conversion. |
| `CMD_NOTESLIDEDOWNRETRIG` | Note Slide Down + Retrigger Note | PR + RT / import behavior | Independent pitch and repeat controls; not an automatic PTM staircase conversion. |
| `CMD_REVERSEOFFSET` | Revert Sample + Offset | DR + SO | Independent direction and exact sample position. |
| `CMD_DBMECHO` | Echo Enable | Import behavior | DBM echo switch remains imported engine behavior; native effects have explicit processor state. |
| `CMD_OFFSETPERCENTAGE` | Offset (Percentage) | SO | mode: normalized and floating position 0..1. |
| `CMD_FINETUNE` | Finetune | BS / PR | Fractional absolute/relative semitone control. |
| `CMD_FINETUNE_SMOOTH` | Finetune (Smooth) | BL / PR | Absolute bend or relative semitone change with duration. |

## Volume-column definitions

| Engine command | Legacy name | Native family or retained behavior | Meaning / boundary |
| --- | --- | --- | --- |
| `VOLCMD_VOLUME` | Set Volume | GS | Voice sample gain. |
| `VOLCMD_PANNING` | Set Panning | PN | Pan -1..1, with percentage display. |
| `VOLCMD_VOLSLIDEUP` | Volume slide up | GL | Sample gain target and duration. |
| `VOLCMD_VOLSLIDEDOWN` | Volume slide down | GL | Sample gain target and duration. |
| `VOLCMD_FINEVOLUP` | Fine volume up | GL | Sample gain target and duration. |
| `VOLCMD_FINEVOLDOWN` | Fine volume down | GL | Sample gain target and duration. |
| `VOLCMD_VIBRATOSPEED` | Vibrato speed | VB | Independent rate with beats/Hz selector. |
| `VOLCMD_VIBRATODEPTH` | Vibrato depth | VB | Independent fractional semitone depth. |
| `VOLCMD_PANSLIDELEFT` | Pan slide left | PA | Negative pan target and duration. |
| `VOLCMD_PANSLIDERIGHT` | Pan slide right | PA | Positive pan target and duration. |
| `VOLCMD_TONEPORTAMENTO` | Tone portamento | PT | Fractional tracker-note target and duration. |
| `VOLCMD_PORTAUP` | Portamento up | PR | Positive fractional semitone change. |
| `VOLCMD_PORTADOWN` | Portamento down | PR | Negative fractional semitone change. |
| `VOLCMD_OFFSET` | Sample Cue | SO | mode: cue; 0 is sample start, 1..9 are saved cues. Cue IDs stay integral. |
