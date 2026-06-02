# PSG Audio Scope

A separate debugger window for **dynamic analysis of the audio output**
from the PSG SN76489 sound chip. Complements the **PSG State** window,
which shows a static snapshot of internal registers and latch phases.
PSG Audio Scope, by contrast, focuses on the **audible output over
time** - what the channels are currently playing, what their amplitude
envelope looks like, and what notes are being produced.

The window is strictly **observational** - it only reads the current
PSG state through a non-invasive mirror API. It does not affect
emulation and can be kept open continuously.

Available **only on MZ-800 and MZ-1500**. The MZ-700 does not have a
PSG chip - in the MZ-700 build the window, menu entry, and keyboard
shortcut are not available.


## Activation and persistence

Open the window in one of these ways:

- Menu **Debugger -> PSG Audio Scope**.
- Keyboard shortcut **Alt+Shift+A**.
- From the DBG Workplace - in submenu **Workplace -> PSG Audio Scope**
  you can enable it so that the window opens automatically together
  with the main debugger window. Off by default.

Position, size, the collapsed/expanded state of sections, and the
Tempo BPM value are remembered between emulator runs.

PSG sampling for rendering runs **continuously even when the window
is closed**. As a result, once you open the window you immediately
see roughly the last 10 seconds of history and the piano roll already
contains the previously detected notes. The sampling overhead is
negligible and is side-effect free with respect to the emulator.


## Common UI elements

- The window is **resizable** and its sections are in collapsible
  **collapsing headers** - the collapsed state is remembered.
- Per-channel graphs (oscilloscope and envelope) automatically
  **respond to the window width**, so widening the window gives you
  higher time and amplitude resolution.
- Piano roll bars have a **hover tooltip** with detail of the
  individual note.


## Window layout

The window is divided top-to-bottom into these blocks:

1. **Toolbar** - MIDI / CSV export and the Tempo BPM setting.
2. **Info header (statusline)** - two lines:
   - First line: frame counter, total number of recorded notes and an
     indicator of currently active notes.
   - Second line: three diagnostic checkboxes **Log samples**,
     **Log events**, **Log writes** and an `(s:N e:N w:N)` indicator
     showing the row count written into each log file.
3. **Per-chip section PSG (chip 0)** in mono mode, or **PSG Left
   (chip 0)** + **PSG Right (chip 1)** in stereo mode. Each section
   contains 4 rows (channels 0-3); each row has an oscilloscope and
   an envelope graph.
4. **Piano roll** - timeline of detected notes.


## Per-channel oscilloscope

Below the label `Channel N (TONE)` or `Channel N (NOISE)` is a
miniature oscilloscope that draws the current output waveform of the
channel.

| State | What is drawn |
|-------|----------------|
| Active TONE | Synthetic square waveform at the current frequency |
| Active NOISE | Pseudo-random noise pattern (looks different each redraw) |
| Attenuation = 15 | Label `silent` |
| Tone divider < 2 | Label `DC` (no oscillation) |
| Before first tick | Label `no data` |

Limitations worth knowing:

- The oscilloscope always draws approximately **4 periods**, regardless
  of frequency. It is meant to let you tell that the channel is really
  oscillating and in what shape, not to precisely measure period length.
- The graph width **scales responsively** with the window width -
  widening it gives you a more detailed rendering.


## Per-channel envelope

Below the oscilloscope is a second graph that shows the **history of
the channel volume over time**. The horizontal axis is time (right =
now, left = up to 10 s ago), the vertical axis is volume (top =
`Attenuation = 0`, bottom = `Attenuation = 15`, silent).

The graph is drawn as a filled area under the curve, which makes the
**ADSR envelope shape** immediately recognizable - fast attack, decay
to a sustain level, optionally a release ramp. It helps identify:

- Tremolo / vibrato (= regular volume oscillations within a single
  note).
- "Polyphony via attenuation" (the channel periodically mutes itself
  to play interrupted notes).
- 1-bit PCM samples (= rapid alternation between full volume and
  silence on a cycle of a few milliseconds).


## Note event detector

The window continuously watches transitions between silence and playing
per channel and detects **note on / note off** events:

- **Note on** = `Attenuation` drops from 15 (silent) to a value
  smaller than 15.
- **Note off** = `Attenuation` returns to 15.

At the **note on** moment these values are recorded:

| Value | How it is obtained |
|-------|--------------------|
| Pitch | Nearest MIDI note matching the current Tone divider |
| Cents | Deviation of the actual frequency from the equal-tempered note (-50 to +50 cents) |
| Velocity | 0-127, derived from Attenuation (`0` -> 127, `15` -> 0) |
| Channel + chip | Source identification |

Important behavior:

- Pitch is captured **only at note on**. If the Tone divider changes
  while the note is sustained (= glissando), the note is not split;
  it keeps its initial pitch.
- If the channel changes type TONE -> NOISE (or vice versa) during a
  note, the current note is closed and a new one starts with the
  corresponding type.
- NOISE notes have no pitch (recorded as `Noise`).
- If a channel plays for longer than the internal buffer can hold
  (about 1000 notes per channel), the oldest notes are overwritten.
  For ordinary game audio this corresponds to roughly 10-20 minutes
  of continuous music.

### Volume changes within a note

Besides the initial Attenuation, all **subsequent volume changes**
within the note (= further writes to the attn register of the same
channel while the note is still on) are recorded. Each change is
stored as a point in an internal list (max 32 points per note). If
the note exceeds this limit, an "overflow" flag is set and further
changes are silently dropped; the note itself keeps running.

These values are reflected in three places:

- **Piano roll** - short vertical tick marks are drawn inside the
  note bar at the times where the volume changed.
- **Note tooltip** - a `Volume changes: N (attn min..max)` line is
  added to the standard tooltip, optionally suffixed with
  `(overflowed)` if the note exceeded the 32-point limit.
- **CSV and MIDI export** - see the export section below.


## Piano roll

The bottom collapsible **Piano roll** section displays all captured
notes as horizontal colored bars on a time axis.

### Controls

- **Range** - time range selector:
  - **Last 10s** (default) - shows the last 10 seconds.
  - **Last 30s** - last 30 seconds.
  - **All** - everything in the buffer (can be 10+ minutes).
- **Channels** - color legend (CH0, CH1, CH2, CH3 NOISE). Orientation
  only, not a filter.

### Rendering

- **Main area** - pitches on the vertical axis (MIDI pitch). The range
  is **auto-fit** to the contents, so you do not see empty octaves
  above or below the actual playing range.
- **Left column** - octave labels (`C2`, `C3`, ...) as guide markers.
- **Noise lane** (`NS`) - a narrow horizontal strip at the bottom for
  NOISE notes. Split into 4 sub-lanes by channel index, so you can
  see overlaps.
- **Vertical grid** - adaptive time spacing (1 s, 5 s, or 10 s
  depending on density); the right edge marks "now".
- **Active note** (= currently playing) is drawn extended up to "now"
  with a thin outline.

### Tooltip and colors

Hovering over a bar shows a tooltip with details:

```
Channel 1 (chip 0)
Pitch: A4 (MIDI 69) +5 cents
Duration: 0.250 s
Velocity: 110
Volume changes: 4 (attn 2..9)
```

If the note has tick marks inside its bar, those are exactly these
volume changes. The `Volume changes` line only appears for notes
where a change actually occurred. If the note exceeded the 32-point
limit, the line is suffixed with `(overflowed)`.

Colors correspond to channels (CH0 blue, CH1 orange, CH2 yellow-green,
CH3 NOISE pink). In stereo mode the right chip uses a lighter shade
of the same palette.


## Exporting notes

The toolbar at the top contains:

- **Export MIDI...** - saves notes to a `.mid` file (Standard MIDI
  File, type 1).
- **Export CSV...** - saves notes to a tabular `.csv` file.
- **Tempo BPM** - tempo metadata written into the MIDI file (40-300,
  default 120). Affects the notation grid in a DAW, not the actual
  playback speed.

The buttons are disabled until at least one captured or currently
active note exists in the buffer.

### MIDI export

- Format: **Standard MIDI File, type 1**, resolution 480 PPQN.
- Each PSG channel gets its own **separate MIDI track**. In mono mode
  this is 4 tracks (CH0-CH3); in stereo mode 8 tracks (Left CH0-CH3,
  Right CH0-CH3) plus track 0 as the conductor (tempo + 4/4 time
  signature).
- **NOISE** notes are mapped to the MIDI drum channel (channel 10),
  pitch 38 (Acoustic Snare).
- The Tempo BPM value is only **metadata** written into the MIDI file
  (default 120). It affects the notation grid in a DAW (how the
  captured notes are laid out into bars), not the playback speed -
  the actual note durations in seconds are preserved absolutely.
- Volume changes within each note are emitted as MIDI **CC 7
  (Channel Volume)** events between `note_on` and `note_off`. The
  CC 7 value is computed from the current Attenuation using
  `round(127 * (15 - attn) / 15)`. Most MIDI players and DAWs honor
  these events, so per-note dynamics (tremolo, decay ramp, polyphony
  via attn) come through on external playback as well.
- The output is hardware-independent - it can be opened in any MIDI
  player or imported into a DAW (Reaper, Cakewalk, MuseScore, etc.)
  for further editing.

### CSV export

- The file is UTF-8 without BOM, LF line endings, decimal separator
  `.` (locale-safe).
- The first line is the header:
  ```
  time_s,channel,chip,pitch_midi,note_name,duration_s,velocity,cents,attn_changes
  ```
- Each subsequent line corresponds to one note:
  - `time_s` - note on time from the start of the recording (3 decimal
    places).
  - `channel` - 0..3 (3 = NOISE).
  - `chip` - 0 = mono / left, 1 = right.
  - `pitch_midi` - 0-127 for TONE, `-1` for NOISE.
  - `note_name` - e.g. `A4`, `C#5`, or `NOISE`.
  - `duration_s` - note length in seconds.
  - `velocity` - 0-127.
  - `cents` - deviation from the equal-tempered note (-50 to +50).
  - `attn_changes` - count of volume changes recorded within the note
    (0 = the note had constant volume). If the note exceeded the
    internal 32-point limit, the value matches the stored count, not
    the actual one.
- Can be opened directly in a spreadsheet for further analysis
  (filters, pivot tables, charts).


## Tip - recording a game soundtrack

1. Start the emulator and open the PSG Audio Scope window
   (Alt+Shift+A).
2. Start the game whose soundtrack you want to capture.
3. Play / let the music run. The piano roll grows as it plays.
4. Once you have enough material, click **Export MIDI...** - optionally
   tweak **Tempo BPM** beforehand if you have a guess; the value only
   serves as metadata for the notation grid in a DAW.
5. Pick the destination file and confirm.

You can then open the file in any MIDI player (e.g. `vlc`, `timidity`,
`aplaymidi`, `Synthesia`, `MuseScore`...) and compare with the
emulator's audio. The actual note durations in seconds are preserved
absolutely, so the playback matches the original regardless of tempo.


## Tip - identifying PCM samples

If a game uses 1-bit PCM by rapidly modulating Attenuation (e.g.
sampled speech or drums), the envelope graph shows very fast
oscillations between `Attenuation = 0` and `Attenuation = 15`. The
note event detector **interprets this as a series of very short
notes** - the piano roll then shows a dense "rain" of tiny bars. This
is correct behavior, just not useful for musical transcription.

In that case it is more useful to look directly at the envelope graph
and observe the amplitude shape rather than to interpret the resulting
piano roll.


## Diagnostic logs

The second statusline row contains three independent checkboxes. Each
of them enables a separate log file in the emulator's working
directory. The `(s:N e:N w:N)` indicator shows the current number of
rows written into each file.

| Checkbox | What it logs | Frequency |
|----------|--------------|-----------|
| **Log samples** | PSG state read via the mirror at each rendering tick - polling diagnostic | approx. 60 Hz |
| **Log events** | Detected note_on / note_off events | per note |
| **Log writes** | Every register write to the PSG with a timestamp - authoritative 1:1 trace of HW communication | per write |

Logs are plain text (TSV), generated as `psg_scope_samples_*.tsv`,
`psg_scope_events_*.tsv` and `psg_writes_*.tsv` with a date / time
stamp in the file name. They can be opened in a text editor,
spreadsheet, or processed by a script.

### Log writes format

The write log is best suited for **offline post-processing** - it
contains the full authoritative trace of PSG communication. It begins
with a header:

```
# PSG write log
# emulator_clock_hz=17734475
# stereo=1
# columns: pxclk_ticks	channel_mask	raw_byte_hex
```

Each subsequent line is tab-separated with three columns:

- `pxclk_ticks` - absolute time in emulator ticks from the start of
  emulation. Divide by `emulator_clock_hz` to get seconds.
- `channel_mask` - bitmask identifying which chip the write went to.
  In mono mode always `0x01`; in stereo mode a combination of bits
  for the left and right chip.
- `raw_byte_hex` - the 8-bit value written to the PSG in hex notation
  (e.g. `0x8F` for latch tone CH0 or `0x9A` for attn CH0).

From this trace the full PSG state machine can be reconstructed
offline (see the example tool below).


## Offline conversion of the PSG write log to MIDI

An example Python script is provided for offline post-processing of
the write log: `docs/tools/example_psg_write_log_to_midi.py`. Full
description and the list of switches is in `docs/tools/README.md`.

The script:

- Reads a `psg_writes_*.tsv` TSV file produced by the **Log writes**
  checkbox.
- Replicates the PSG state machine (CS latch, attn latch, per-channel
  divider / attn / noise) identically with the emulator.
- Detects notes and pitches from the sequence of register writes,
  outside of the running emulator.
- Produces the resulting `.mid` file.

Dependencies: Python 3 and the `mido` package (`pip install mido`).

Usage:

```bash
python docs/tools/example_psg_write_log_to_midi.py psg_writes_20260523_143000.tsv -o soundtrack.mid
```

The script is provided as an **example** - it demonstrates how to
build a MIDI file from the write log without modifying the emulator.
It is not a production tool, but a useful starting point for your own
analysis scripts.


## Relation to the PSG State window

PSG State and PSG Audio Scope complement each other:

| Window | What it shows |
|--------|---------------|
| **PSG State** | Current register contents, latch phase, decoded channel type and frequency |
| **PSG Audio Scope** | Time-domain output, volume history, detected notes, export |

Typical workflow: open PSG State when troubleshooting a specific PSG
write (wrong channel, wrong value). Open PSG Audio Scope when you want
to observe **what the PSG plays over a longer horizon** and possibly
extract something from it (a recording, identification of tempo,
analysis of a sound effect).
