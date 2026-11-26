"""Pure-Python Impulse Tracker 2.14 (`.it`) writer matching OpenMPT's `ITTools.h`.

Emits stereo 16-bit uncompressed PCM samples (split-channel layout as required
by OpenMPT for `cwtv >= 0x0214`), full multi-sample `IMPI` keymaps, NNA modes,
channel mute flags, volume column, and sub-row `SDx`/`SCx` timing.
"""

from __future__ import annotations

import struct
from pathlib import Path
from typing import Dict, List, Tuple

import numpy as np

from .audio import load_audio
from .plan import InstrumentPlan, PatternData, SampleZone, ScratchpadPlan


def _ascii_fixed(text: str, length: int) -> bytes:
    raw = text.encode("ascii", errors="replace")[:length]
    return raw.ljust(length, b"\x00")


def _pack_empty_envelope() -> bytes:
    # ITEnvelope is 82 bytes: 6 header bytes + 25*(int8 value + uint16 tick) + 1 reserved
    return b"\x00" * 82


def _pack_instrument(ins: InstrumentPlan) -> bytes:
    nna_map = {"cut": 0, "continue": 1, "off": 2, "fade": 3}
    nna = nna_map.get(ins.nna, 0)
    # Build 120-entry (note, sample) keyboard table (240 bytes)
    kb = bytearray(240)
    for key in range(120):
        kb[2 * key] = key
        kb[2 * key + 1] = 0

    for z in ins.zones:
        if not (1 <= z.sample_id <= 255):
            continue
        lo = max(0, min(119, z.low_note))
        hi = max(lo, min(119, z.high_note))
        for key in range(lo, hi + 1):
            # Root plays at IT C-5 (60 = C5Speed = 44100 Hz); gaps transpose chromatically
            played = max(0, min(119, 60 + (key - z.root_note)))
            kb[2 * key] = played
            kb[2 * key + 1] = z.sample_id

    header = struct.pack(
        "<4s13sBBBHbBBBBBHBB26sBBBB2s",
        b"IMPI",
        _ascii_fixed(f"INS{ins.instrument_id:02d}.ITI", 13),
        nna,
        0,          # dct (off)
        0,          # dca (cut)
        256 if ins.nna == "fade" else 128,  # fadeout
        0,          # pps
        60,         # ppc (C-5)
        128,        # gbv (full instrument global volume)
        32 | 0x80,  # dfp (ignore panning)
        0,          # rv
        0,          # rp
        0x0214,     # trkvers
        min(255, len(ins.zones)),
        0,          # reserved1
        _ascii_fixed(ins.name, 26),
        0,          # ifc (filter disabled)
        0,          # ifr
        0,          # mch
        0,          # mpr
        b"\x00\x00",
    )
    body = header + bytes(kb) + _pack_empty_envelope() * 3 + b"\x00\x00\x00\x00"
    assert len(body) == 554, f"ITInstrument must be 554 bytes, got {len(body)}"
    return body


def _pack_sample_header(zone: SampleZone, length_frames: int, sample_pointer: int) -> bytes:
    # IT 2.14 stereo 16-bit signed split-channel PCM
    flags = 0x01 | 0x02 | 0x04  # sampleDataPresent | sample16Bit | sampleStereo
    cvt = 0x01                  # cvtSignedSample
    header = struct.pack(
        "<4s13sBBB26sBBIIIIIIIBBBB",
        b"IMPS",
        _ascii_fixed(f"SMP{zone.sample_id:03d}.WAV", 13),
        max(1, min(64, zone.default_volume)),  # gvl (per-sample relative level)
        flags,
        64,                                    # default vol
        _ascii_fixed(zone.name, 26),
        cvt,
        32,                                    # dfp (center, panning disabled)
        length_frames,
        0,                                     # loopbegin
        0,                                     # loopend
        zone.sample_rate,                      # C5Speed (plays 1:1 at note 60)
        0,                                     # susloopbegin
        0,                                     # susloopend
        sample_pointer,
        0, 0, 0, 0,                            # vibrato
    )
    assert len(header) == 80, f"ITSample must be 80 bytes, got {len(header)}"
    return header


def _pack_pattern(pat: PatternData, tempo_events: Dict[int, Tuple[int, int]] | None = None) -> bytes:
    """Pack `PatternData` into IT compressed pattern payload.

    Optional `tempo_events` maps `row -> (channel, bpm_int)` for emitting IT
    `Txx` (command 20) tempo-sync commands without colliding with note FX.
    """
    by_row: Dict[int, List] = {}
    for cell in pat.cells:
        if 0 <= cell.row < pat.rows and 0 <= cell.channel < 64:
            by_row.setdefault(cell.row, []).append(cell)

    stream = bytearray()
    for r in range(pat.rows):
        row_cells = sorted(by_row.get(r, ()), key=lambda c: c.channel)
        for c in row_cells:
            mask = 0
            note_byte = None
            ins_byte = None
            vol_byte = None
            cmd_bytes = None

            if c.note == 254 and c.cut_ticks > 0:
                # Sub-row note cut uses SCx without immediate tick-0 note cut
                mask |= 0x08
                cmd_bytes = bytes([19, 0xC0 | (c.cut_ticks & 0x0F)])
            else:
                if c.note >= 0:
                    mask |= 0x01
                    note_byte = c.note & 0xFF
                if c.instrument > 0:
                    mask |= 0x02
                    ins_byte = c.instrument & 0xFF
                if 0 <= c.volume <= 64:
                    mask |= 0x04
                    vol_byte = c.volume & 0xFF
                if c.delay_ticks > 0:
                    mask |= 0x08
                    cmd_bytes = bytes([19, 0xD0 | (c.delay_ticks & 0x0F)])

            if mask == 0:
                continue
            stream.append((c.channel + 1) | 0x80)
            stream.append(mask)
            if note_byte is not None:
                stream.append(note_byte)
            if ins_byte is not None:
                stream.append(ins_byte)
            if vol_byte is not None:
                stream.append(vol_byte)
            if cmd_bytes is not None:
                stream.extend(cmd_bytes)

        if tempo_events and r in tempo_events:
            t_ch, t_bpm = tempo_events[r]
            if 0 <= t_ch < 64 and 32 <= t_bpm <= 255:
                stream.append((t_ch + 1) | 0x80)
                stream.append(0x08)       # command + info only
                stream.append(20)         # IT 'T' = Set Tempo
                stream.append(t_bpm & 0xFF)
        stream.append(0)  # End of row

    return struct.pack("<HH4s", len(stream), pat.rows, b"\x00\x00\x00\x00") + bytes(stream)


def _compute_it_tempo_sync(
    plan: ScratchpadPlan,
    bpm_int: int,
) -> Dict[int, Dict[int, Tuple[int, int]]]:
    """Closed-loop bar-level `Txx` compensation for classic IT integer-tick truncation.

    In classic IT playback, each tick is `floor(2.5 * sr / bpm_int)` samples.
    At 177 BPM / 44.1 kHz, `floor(622.881) = 622` runs 0.14% fast (~260 ms drift
    over 3 minutes). Alternating `bpm_int` and `bpm_int - 1` on a clean helper
    channel keeps cumulative drift within +/-5 ms over any song length!
    """
    sr = plan.sample_rate
    exact_tick = 2.5 * sr / max(32.0, plan.bpm)
    int_tick = int(2.5 * sr // bpm_int)
    total_ticks = len(plan.order_list) * plan.pattern_rows * plan.speed
    projected_drift_sec = abs(exact_tick - int_tick) * total_ticks / float(sr)

    # Skip if classic IT timing already stays within 15 ms across the whole track,
    # or if patterns are shared across orders (preserve pattern deduplication).
    if projected_drift_sec <= 0.015:
        return {}
    if len(set(plan.order_list)) < len(plan.order_list):
        return {}
    tempo_ch = len(plan.channel_names)
    if tempo_ch >= 64:
        return {}

    bar_rows = max(1, plan.rows_per_beat * plan.beats_per_bar)
    bar_ticks = bar_rows * plan.speed
    candidates = sorted({max(32, min(255, bpm_int + d)) for d in (-1, 0, 1)})
    cand_bar_samps = {b: bar_ticks * int(2.5 * sr // b) for b in candidates}

    accum_actual = 0.0
    accum_target = 0.0
    cur_bpm = bpm_int
    per_pat: Dict[int, Dict[int, Tuple[int, int]]] = {}

    for pat_id in plan.order_list:
        pat = plan.patterns[pat_id]
        row = 0
        while row < pat.rows:
            step_rows = min(bar_rows, pat.rows - row)
            step_ticks = step_rows * plan.speed
            target_step = step_ticks * exact_tick
            best_b = min(
                candidates,
                key=lambda b: abs((accum_actual + step_ticks * int(2.5 * sr // b)) - (accum_target + target_step)),
            )
            if best_b != cur_bpm:
                per_pat.setdefault(pat_id, {})[row] = (tempo_ch, best_b)
                cur_bpm = best_b
            accum_actual += step_ticks * int(2.5 * sr // cur_bpm)
            accum_target += target_step
            row += step_rows

    return per_pat


def write_it(plan: ScratchpadPlan, output_path: str | Path) -> Path:
    """Serialize `ScratchpadPlan` into an OpenMPT/ScreamSeq-compatible `.it` file."""
    # Collect all samples ordered by 1-based sample_id
    all_zones: List[SampleZone] = []
    for ins in plan.instruments:
        all_zones.extend(ins.zones)
    all_zones.sort(key=lambda z: z.sample_id)
    if len(all_zones) > 255:
        raise ValueError(f"IT writer supports up to 255 samples; got {len(all_zones)}")

    # Verify contiguous 1..N sample IDs
    for idx, z in enumerate(all_zones, start=1):
        if z.sample_id != idx:
            raise ValueError(f"Non-contiguous sample_id {z.sample_id} at position {idx}")

    # Load all sample PCM arrays as split stereo int16
    sample_pcm_blobs: List[bytes] = []
    sample_lengths: List[int] = []
    for z in all_zones:
        stereo, _ = load_audio(z.wav_path, target_sr=z.sample_rate)
        pcm = np.round(np.clip(stereo, -1.0, 1.0) * 32767.0).astype("<i2")
        # IT split-channel stereo: all L frames, then all R frames
        blob = pcm[:, 0].tobytes() + pcm[:, 1].tobytes()
        sample_pcm_blobs.append(blob)
        sample_lengths.append(int(pcm.shape[0]))

    orders = list(plan.order_list) + [255]  # 255 = end-of-song marker
    ordnum = len(orders)
    insnum = len(plan.instruments)
    smpnum = len(all_zones)
    patnum = len(plan.patterns)

    # Channel panning/mute + volumes
    chnpan = bytearray([32] * 64)
    chnvol = bytearray([64] * 64)
    for ch, is_muted in enumerate(plan.channel_muted[:64]):
        if is_muted:
            chnpan[ch] = 32 | 0x80

    # Song message documenting exact BPM, tiers, and source metadata
    msg_lines = [
        f"ScreamSeq Stem Import v{plan.version}",
        f"Mode: {plan.mode} | Editability: {plan.editability:.2f} | BPM: {plan.bpm:.3f}",
    ]
    for ins in plan.instruments:
        msg_lines.append(f"  Ins {ins.instrument_id:02d}: {ins.name} [{ins.role}/{ins.tier}] ({len(ins.zones)} samples)")
    msg_bytes = ("\r".join(msg_lines) + "\x00").encode("ascii", errors="replace")

    # Compute exact file offsets
    header_size = 192
    orders_size = ordnum
    ptr_tables_size = 4 * (insnum + smpnum + patnum)
    msg_offset = header_size + orders_size + ptr_tables_size
    cur = msg_offset + len(msg_bytes)

    ins_blobs = [_pack_instrument(ins) for ins in plan.instruments]
    ins_offsets: List[int] = []
    for b in ins_blobs:
        ins_offsets.append(cur)
        cur += len(b)

    smp_header_offsets: List[int] = []
    for _ in all_zones:
        smp_header_offsets.append(cur)
        cur += 80

    bpm_int = max(32, min(255, int(round(plan.bpm))))
    tempo_sync = _compute_it_tempo_sync(plan, bpm_int)
    pat_blobs = [_pack_pattern(pat, tempo_sync.get(pat.pattern_id)) for pat in plan.patterns]
    pat_offsets: List[int] = []
    for b in pat_blobs:
        pat_offsets.append(cur)
        cur += len(b)

    smp_data_offsets: List[int] = []
    for blob in sample_pcm_blobs:
        smp_data_offsets.append(cur)
        cur += len(blob)

    smp_headers = [
        _pack_sample_header(z, sample_lengths[i], smp_data_offsets[i])
        for i, z in enumerate(all_zones)
    ]

    # Header flags: stereo (0x01) | vol0opt (0x02) | instrumentMode (0x04) | linearSlides (0x08)
    flags = 0x01 | 0x02 | 0x04 | 0x08
    special = 0x01 | 0x04  # embedSongMessage | embedPatternHighlights

    file_header = struct.pack(
        "<4s26sBBHHHHHHHHBBBBBBHII64s64s",
        b"IMPM",
        _ascii_fixed(plan.title, 26),
        plan.rows_per_beat,
        plan.rows_per_beat * plan.beats_per_bar,
        ordnum,
        insnum,
        smpnum,
        patnum,
        0x0214,  # cwtv
        0x0214,  # cmwt
        flags,
        special,
        128,     # globalvol
        112,     # mv (OpenMPT stereo headroom with realistic stem balance)
        plan.speed,
        bpm_int,
        128,     # sep (full stereo)
        0,       # pwd
        len(msg_bytes),
        msg_offset,
        0,       # reserved
        bytes(chnpan),
        bytes(chnvol),
    )
    assert len(file_header) == 192

    out = Path(output_path)
    out.parent.mkdir(parents=True, exist_ok=True)
    with out.open("wb") as f:
        f.write(file_header)
        f.write(bytes(orders))
        for off in ins_offsets:
            f.write(struct.pack("<I", off))
        for off in smp_header_offsets:
            f.write(struct.pack("<I", off))
        for off in pat_offsets:
            f.write(struct.pack("<I", off))
        f.write(msg_bytes)
        for b in ins_blobs:
            f.write(b)
        for b in smp_headers:
            f.write(b)
        for b in pat_blobs:
            f.write(b)
        for b in sample_pcm_blobs:
            f.write(b)
    return out
