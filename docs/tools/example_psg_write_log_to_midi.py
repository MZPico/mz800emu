#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
PSG write log -> MIDI converter (offline, example).

Converts a TSV write log produced by the emulator (toggle "Log writes" in
the PSG Audio Scope window) into a Standard MIDI File. Replicates 1:1 the
PSG state machine logic from `src/emulator/hw-generic/psg/psg.c`
(`psg_write_byte_core`) and the note-event detector from
`src/ui-imgui/debugger/psg_audio_scope_window.cpp`.

This script is an EXAMPLE - it demonstrates the conversion approach and
is not meant to be production-ready. Fork and adapt for project specific
post-processing (chord coalescing, arpeggio detection, expression CC).

Input TSV header:
    # PSG write log
    # emulator_clock_hz=<pxCLK Hz>
    # stereo=<0|1>
    # columns: pxclk_ticks  channel_mask  raw_byte_hex

Decoding logic (psg_write_byte_core):
    - bit 7 = LATCH (1) -> updates latch_cs (bits 6:5) and latch_attn (bit 4)
    - LATCH with attn=1 -> the following DATA byte writes the channel
      attenuator; the LATCH byte itself also writes its low 4 bits as the
      new_attn value
    - LATCH with attn=0 -> low 4 bits = tone.latch_divider (TONE) OR
      noise div_type+type (NOISE)
    - DATA byte (bit 7=0): per latch_attn either attn or divider/noise

Note event state machine (1:1 from psg_audio_scope_window.cpp):
    - is_playing = (attn < 15) AND (TONE -> divider >= 2)
    - silent -> playing  : NOTE_ON
    - playing -> silent  : NOTE_OFF
    - playing -> playing with pitch_change (different integer MIDI) ->
      note_off + note_on
    - type swap (TONE<->NOISE) during a note: note_off + start new

Input pxclk_ticks is monotonic; the offset (= ticks of the first write)
is subtracted so that the first note begins at MIDI tick 0.

Pitch detection (TONE):
    freq_hz   = clock_hz / (32 * divider * GDGCLK2CPU_DIVIDER)
    midi      = round(12 * log2(freq_hz / 440) + 69)
    velocity  = round(127 * (1 - attn/15))

MIDI channel mapping:
    chip 0 (mono or left)  -> PSG ch 0..3 -> MIDI ch 0..3
    chip 1 (right)         -> PSG ch 0..3 -> MIDI ch 4..7
NOISE (PSG ch 3) -> drum lane (Program Change 119, "Reverse Cymbal");
pitch 60 (C4) is used as a fixed surrogate.

GDGCLK2CPU_DIVIDER is derived from clock_hz:
    >= 16e6  -> 5  (MZ-800)
    <  16e6  -> 4  (MZ-1500 / MZ-700)
"""

from __future__ import annotations

import argparse
import math
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional

try:
    import mido
except ImportError:
    print("ERROR: mido package is missing. Install it via:", file=sys.stderr)
    print("  /c/msys64/mingw64/bin/python.exe -m pip install --user "
          "--break-system-packages mido", file=sys.stderr)
    sys.exit(2)


# ---------------------------------------------------------------------------
# PSG state machine (replica of psg_write_byte_core)
# ---------------------------------------------------------------------------

PSG_TONE = 0
PSG_NOISE = 1

# PSG_CHANNEL_3 is always NOISE; channels 0..2 are TONE.
CHANNEL_TYPE = (PSG_TONE, PSG_TONE, PSG_TONE, PSG_NOISE)


@dataclass
class PsgChannel:
    """State of a single PSG channel (TONE or NOISE).

    Attributes:
        attn: 4-bit attenuator (0 = max volume, 15 = OFF).
        tone_divider: 10-bit tone divider (TONE only).
        tone_latch_divider: low 4 bits of the tone divider (intermediate).
        noise_div_type: 2-bit divider type (NOISE only).
        noise_type: 1-bit periodic(0) / white(1) (NOISE only).
    """
    attn: int = 15
    tone_divider: int = 0
    tone_latch_divider: int = 0
    noise_div_type: int = 0
    noise_type: int = 0


@dataclass
class PsgInstance:
    """State of a single PSG chip (4 channels + latch)."""
    latch_cs: int = 0
    latch_attn: int = 0
    channels: list = field(default_factory=lambda: [PsgChannel() for _ in range(4)])


def psg_write_byte_core(psg: PsgInstance, value: int) -> None:
    """Apply a single write byte to a PSG instance (1:1 from psg.c).

    Mutates `psg.latch_cs`, `psg.latch_attn`, and the addressed channel in
    place.

    Args:
        psg: PSG instance.
        value: Raw byte (0..255) as written by the CPU.
    """
    latch = value & 0x80

    if latch:
        cs = (value >> 5) & 0x03
        attn_flag = value & 0x10
        psg.latch_cs = cs
        psg.latch_attn = attn_flag
    else:
        cs = psg.latch_cs
        attn_flag = psg.latch_attn

    ch = psg.channels[cs]
    ch_type = CHANNEL_TYPE[cs]

    if attn_flag:
        ch.attn = value & 0x0F
    elif latch and ch_type == PSG_TONE:
        ch.tone_latch_divider = value & 0x0F
    else:
        if ch_type == PSG_TONE:
            ch.tone_divider = ((value & 0x3F) << 4) | ch.tone_latch_divider
        else:
            ch.noise_div_type = value & 0x03
            ch.noise_type = (value >> 2) & 0x01


# ---------------------------------------------------------------------------
# Pitch + velocity helpers
# ---------------------------------------------------------------------------

def tone_freq_hz(clock_hz: float, divider: int, gdg_cpu_div: int) -> float:
    """Compute the square wave frequency of a PSG tone channel.

    Args:
        clock_hz: Emulator pxCLK in Hz.
        divider: 10-bit tone divider (>= 1).
        gdg_cpu_div: GDGCLK2CPU_DIVIDER (5 for MZ-800, 4 for MZ-1500).

    Returns:
        Frequency in Hz, or 0.0 when divider <= 1 (DC case).
    """
    if divider < 2:
        return 0.0
    return clock_hz / (32.0 * divider * gdg_cpu_div)


def freq_to_midi(freq_hz: float) -> int:
    """Convert a frequency to the nearest MIDI pitch in 0..127.

    Returns 0 when freq_hz <= 0.
    """
    if freq_hz <= 0.0:
        return 0
    midi = 12.0 * math.log2(freq_hz / 440.0) + 69.0
    nearest = int(round(midi))
    return max(0, min(127, nearest))


def attn_to_velocity(attn: int) -> int:
    """Map a 4-bit attn (0 = max, 15 = silent) to MIDI velocity 1..127."""
    v = int(round(127.0 * (1.0 - attn / 15.0)))
    return max(1, min(127, v))


def is_playing(ch: PsgChannel, ch_type: int) -> bool:
    """State machine predicate: is the channel currently audible?"""
    if ch.attn >= 15:
        return False
    if ch_type == PSG_TONE and ch.tone_divider < 2:
        return False
    return True


# ---------------------------------------------------------------------------
# TSV parser
# ---------------------------------------------------------------------------

@dataclass
class WriteLogHeader:
    """Metadata parsed from the TSV header."""
    clock_hz: int
    stereo: bool
    gdg_cpu_div: int


@dataclass
class WriteEvent:
    """One TSV data row - a single PSG write."""
    ticks: int           # pxCLK timestamp
    channel_mask: int    # 0x01 = PSG0, 0x02 = PSG1, 0x03 = both
    value: int           # raw byte 0..255


def parse_log(path: Path) -> tuple[WriteLogHeader, list[WriteEvent]]:
    """Load a TSV write log.

    Args:
        path: Path to the TSV file.

    Returns:
        (header, list_of_events) - events in file order.

    Raises:
        ValueError: when the header is missing mandatory fields.
    """
    clock_hz: Optional[int] = None
    stereo: Optional[bool] = None
    events: list[WriteEvent] = []

    with path.open("r", encoding="utf-8") as f:
        for lineno, raw in enumerate(f, 1):
            line = raw.strip()
            if not line:
                continue
            if line.startswith("#"):
                if "emulator_clock_hz=" in line:
                    clock_hz = int(line.split("=", 1)[1])
                elif "stereo=" in line:
                    stereo = line.split("=", 1)[1].strip() == "1"
                continue
            parts = line.split("\t")
            if len(parts) != 3:
                raise ValueError(
                    f"line {lineno}: expected 3 columns, got {len(parts)}: {line!r}")
            ticks = int(parts[0])
            mask = int(parts[1])
            value = int(parts[2], 16)
            events.append(WriteEvent(ticks=ticks, channel_mask=mask, value=value))

    if clock_hz is None or stereo is None:
        raise ValueError("TSV header is missing emulator_clock_hz or stereo")

    gdg_cpu_div = 5 if clock_hz >= 16_000_000 else 4
    return WriteLogHeader(clock_hz=clock_hz, stereo=stereo,
                          gdg_cpu_div=gdg_cpu_div), events


# ---------------------------------------------------------------------------
# Note event extractor + MIDI builder
# ---------------------------------------------------------------------------

@dataclass
class NoteEvent:
    """A detected note ready for MIDI export.

    Attributes:
        chip: 0 (mono / left) or 1 (right).
        channel: 0..3 (PSG channel).
        is_noise: True when this is the NOISE channel.
        t_on_ticks: pxCLK timestamp of note_on.
        t_off_ticks: pxCLK timestamp of note_off.
        midi_pitch: 0..127 (TONE) or placeholder 60 (NOISE).
        velocity: 1..127.
    """
    chip: int
    channel: int
    is_noise: bool
    t_on_ticks: int
    t_off_ticks: int
    midi_pitch: int
    velocity: int


def extract_notes(hdr: WriteLogHeader,
                  events: list[WriteEvent]) -> list[NoteEvent]:
    """Walk the write log, replicate PSG state, emit note on/off transitions.

    For each write, the per-chip PSG state is updated, then for each
    (chip, channel) pair `is_playing` / pitch is re-evaluated. A change
    relative to the previous sample maps to NOTE_ON / NOTE_OFF /
    pitch_change / type_change and emits a note.

    Args:
        hdr: TSV header (clock_hz, stereo, gdg_cpu_div).
        events: chronologically ordered write events.

    Returns:
        List of NoteEvent (closed notes + active notes flushed at the end
        of the log).
    """
    n_chips = 2 if hdr.stereo else 1
    chips = [PsgInstance() for _ in range(n_chips)]

    # Active note tracker per (chip, channel).
    active: dict[tuple[int, int], NoteEvent] = {}
    notes: list[NoteEvent] = []

    last_ticks: int = events[0].ticks if events else 0

    def emit_close(chip: int, ch: int, t_off: int) -> None:
        key = (chip, ch)
        n = active.pop(key, None)
        if n is not None:
            n.t_off_ticks = t_off
            if n.t_off_ticks > n.t_on_ticks:
                notes.append(n)

    def maybe_start(chip: int, ch: int, t_on: int) -> None:
        ch_type = CHANNEL_TYPE[ch]
        ch_state = chips[chip].channels[ch]
        if not is_playing(ch_state, ch_type):
            return
        is_noise = (ch_type == PSG_NOISE)
        if is_noise:
            pitch = 60  # surrogate
        else:
            pitch = freq_to_midi(tone_freq_hz(
                hdr.clock_hz, ch_state.tone_divider, hdr.gdg_cpu_div))
        active[(chip, ch)] = NoteEvent(
            chip=chip,
            channel=ch,
            is_noise=is_noise,
            t_on_ticks=t_on,
            t_off_ticks=0,
            midi_pitch=pitch,
            velocity=attn_to_velocity(ch_state.attn),
        )

    for ev in events:
        last_ticks = ev.ticks
        # Snapshot the previous state (only the channels of the chip
        # addressed by this write - for simplicity we re-check all of them).
        for chip in range(n_chips):
            psg = chips[chip]
            # Select the chip per channel_mask.
            chip_bit = 1 << chip
            if not (ev.channel_mask & chip_bit):
                continue
            # State before the write.
            pre_states = []
            for ch in range(4):
                ch_state = psg.channels[ch]
                pre_states.append((
                    ch_state.attn,
                    ch_state.tone_divider,
                    is_playing(ch_state, CHANNEL_TYPE[ch]),
                ))
            psg_write_byte_core(psg, ev.value)
            # State after the write.
            for ch in range(4):
                ch_state = psg.channels[ch]
                ch_type = CHANNEL_TYPE[ch]
                was_playing = pre_states[ch][2]
                now_playing = is_playing(ch_state, ch_type)
                key = (chip, ch)

                if was_playing and not now_playing:
                    emit_close(chip, ch, ev.ticks)
                elif not was_playing and now_playing:
                    maybe_start(chip, ch, ev.ticks)
                elif was_playing and now_playing and key in active:
                    # Pitch change?
                    if ch_type == PSG_TONE:
                        new_pitch = freq_to_midi(tone_freq_hz(
                            hdr.clock_hz, ch_state.tone_divider,
                            hdr.gdg_cpu_div))
                        cur = active[key]
                        if new_pitch != cur.midi_pitch:
                            emit_close(chip, ch, ev.ticks)
                            maybe_start(chip, ch, ev.ticks)

    # Close anything still playing at end-of-log.
    for key in list(active.keys()):
        emit_close(key[0], key[1], last_ticks)

    notes.sort(key=lambda n: (n.t_on_ticks, n.chip, n.channel))
    return notes


def midi_channel_for(chip: int, ch: int, is_noise: bool) -> int:
    """Return the MIDI channel (0..15) for a (chip, channel) pair.

    NOISE -> drum channel 9.
    TONE  -> chip*4 + ch (chip 0: 0..2; chip 1: 4..6).
    """
    if is_noise:
        return 9
    return chip * 4 + ch


def build_midi(notes: list[NoteEvent],
               clock_hz: float,
               stereo: bool,
               tempo_bpm: int = 120,
               ppqn: int = 480) -> mido.MidiFile:
    """Build a Standard MIDI File type 1 from the extracted notes.

    Args:
        notes: chronologically sorted notes (output of extract_notes).
        clock_hz: pxCLK in Hz - used to convert ticks to seconds.
        stereo: True = two PSG chips.
        tempo_bpm: tempo in BPM (default 120).
        ppqn: pulses per quarter note (default 480).

    Returns:
        mido.MidiFile with a conductor track + one track per (chip, ch).
    """
    mf = mido.MidiFile(type=1, ticks_per_beat=ppqn)

    # Conductor track - tempo + time signature.
    conductor = mido.MidiTrack()
    conductor.append(mido.MetaMessage(
        "set_tempo", tempo=mido.bpm2tempo(tempo_bpm), time=0))
    conductor.append(mido.MetaMessage(
        "time_signature", numerator=4, denominator=4, time=0))
    conductor.append(mido.MetaMessage("end_of_track", time=0))
    mf.tracks.append(conductor)

    if not notes:
        return mf

    # Offset = first note_on (= MIDI tick 0).
    t0 = notes[0].t_on_ticks

    def sec_of(t: int) -> float:
        return (t - t0) / clock_hz

    def ticks_of(t: int) -> int:
        sec = sec_of(t)
        return int(round(sec * (tempo_bpm / 60.0) * ppqn))

    # Per (chip, ch) collection of (absolute_tick, message); converted to
    # delta times below.
    n_chips = 2 if stereo else 1
    per_track: dict[tuple[int, int], list[tuple[int, mido.Message]]] = {}

    def add(key: tuple[int, int], tick: int, msg: mido.Message) -> None:
        per_track.setdefault(key, []).append((tick, msg))

    for n in notes:
        key = (n.chip, n.channel)
        midi_ch = midi_channel_for(n.chip, n.channel, n.is_noise)
        on_tick = ticks_of(n.t_on_ticks)
        off_tick = max(on_tick + 1, ticks_of(n.t_off_ticks))
        add(key, on_tick,
            mido.Message("note_on", channel=midi_ch,
                         note=n.midi_pitch, velocity=n.velocity))
        add(key, off_tick,
            mido.Message("note_off", channel=midi_ch,
                         note=n.midi_pitch, velocity=0))

    # Build the tracks.
    for chip in range(n_chips):
        for ch in range(4):
            key = (chip, ch)
            events = per_track.get(key, [])
            if not events:
                continue
            track = mido.MidiTrack()
            is_noise_track = (CHANNEL_TYPE[ch] == PSG_NOISE)
            midi_ch = midi_channel_for(chip, ch, is_noise_track)
            track_name = f"PSG{chip} ch{ch}" + (" noise" if is_noise_track else " tone")
            track.append(mido.MetaMessage("track_name", name=track_name, time=0))
            # Program change.
            if is_noise_track:
                program = 119  # Reverse Cymbal (drum channel ignores program)
            else:
                program = 80   # Lead 1 (Square wave)
            track.append(mido.Message(
                "program_change", channel=midi_ch, program=program, time=0))

            # Absolute ticks -> delta ticks.
            events.sort(key=lambda e: e[0])
            prev_tick = 0
            for abs_tick, msg in events:
                delta = abs_tick - prev_tick
                if delta < 0:
                    delta = 0
                msg = msg.copy(time=delta)
                track.append(msg)
                prev_tick = abs_tick

            track.append(mido.MetaMessage("end_of_track", time=0))
            mf.tracks.append(track)

    return mf


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def summarize(hdr: WriteLogHeader, events: list[WriteEvent],
              notes: list[NoteEvent]) -> str:
    """Return a short textual summary printed by the CLI."""
    lines = []
    lines.append(f"clock_hz       : {hdr.clock_hz}")
    lines.append(f"stereo         : {hdr.stereo}")
    lines.append(f"gdg_cpu_div    : {hdr.gdg_cpu_div}")
    lines.append(f"write events   : {len(events)}")
    if events:
        dur_ticks = events[-1].ticks - events[0].ticks
        dur_sec = dur_ticks / hdr.clock_hz
        lines.append(f"log duration   : {dur_sec:.3f} s "
                     f"({dur_ticks} pxCLK ticks)")
    lines.append(f"detected notes : {len(notes)}")
    if notes:
        # Per-channel note counts.
        per_ch: dict[tuple[int, int], int] = {}
        for n in notes:
            key = (n.chip, n.channel)
            per_ch[key] = per_ch.get(key, 0) + 1
        for (chip, ch), cnt in sorted(per_ch.items()):
            kind = "NOISE" if CHANNEL_TYPE[ch] == PSG_NOISE else "TONE"
            lines.append(f"  chip{chip} ch{ch} {kind:5s}: {cnt} notes")
    return "\n".join(lines)


def main() -> int:
    """CLI entry point.

    Returns the process exit code (0 = OK, 1 = error).
    """
    ap = argparse.ArgumentParser(
        description="Convert a PSG write log TSV into a Standard MIDI File.")
    ap.add_argument("input", type=Path,
                    help="Path to the TSV write log (psg_writes_*.tsv).")
    ap.add_argument("-o", "--output", type=Path, default=None,
                    help="Output .mid path (default: input with .mid suffix).")
    ap.add_argument("--tempo", type=int, default=120,
                    help="MIDI tempo in BPM (default 120).")
    ap.add_argument("--ppqn", type=int, default=480,
                    help="MIDI PPQN (default 480).")
    args = ap.parse_args()

    if not args.input.is_file():
        print(f"ERROR: input file does not exist: {args.input}", file=sys.stderr)
        return 1

    out = args.output if args.output else args.input.with_suffix(".mid")

    try:
        hdr, events = parse_log(args.input)
    except ValueError as e:
        print(f"ERROR: failed to parse TSV: {e}", file=sys.stderr)
        return 1

    notes = extract_notes(hdr, events)
    mf = build_midi(notes, hdr.clock_hz, hdr.stereo,
                    tempo_bpm=args.tempo, ppqn=args.ppqn)
    mf.save(str(out))

    print(summarize(hdr, events, notes))
    print(f"\nmidi written   : {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
