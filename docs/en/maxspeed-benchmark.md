# MAX SPEED benchmark

Measuring emulation efficiency in MAX SPEED mode.

## Purpose

In normal mode the emulator runs at the speed of the real machine (100 %). In
MAX SPEED mode it runs as fast as the host computer can manage. The benchmark
measures how efficient the emulation is, i.e. how much "real MZ-800 time" gets
executed per one real second.

100 % corresponds to the speed of the real hardware (50 screens per second for
the MZ-800). A value above 100 % tells you how many times faster than the real
machine the emulation runs (for example 5000 % = fifty times faster). The result
changes depending on what the emulation is currently doing (MZ-700 / MZ-800 mode,
whether the PSG is playing, how often the screen contents change). On real
hardware these things do not affect the speed, the machine always runs at 100 %.

Measurement is active only in MAX SPEED. In normal mode, while paused, or during
blocking operations (for example loading via the CMT hack) nothing is measured,
so these intervals do not distort the result.

## Benchmark window

Open the window from the **Emulator -> MAX SPEED Benchmark...** menu. It shows
live values updated at runtime.

| Value | Meaning |
|-------|---------|
| Measured time | Real time spent in MAX SPEED since the last reset. |
| Emulated pxCLK | Number of emulated pixel clock ticks (pxCLK). |
| Throughput | Emulated pxCLK per one real second. |
| Efficiency | Efficiency in % (100 % = real hardware speed). |
| FB-FPS | Number of rendered video frames per real second. |
| Distribution | Spread of the instantaneous efficiency (min / max / mean / median / standard deviation), sampled once per second. Shows how stable or variable the speed was. |

Buttons:

- **Reset** - clears the measurement and starts a new one from the current moment.
- **Print to console** - prints the current report to the console.

## Keyboard shortcuts

| Key | Action |
|-----|--------|
| `Alt + T` | Print the benchmark report to the console |
| `Alt + Shift + T` | Reset the benchmark |

## Command-line measurement (headless)

The `--maxspeed-bench` option starts the emulator directly in MAX SPEED and keeps
printing the report to the console periodically (every 5 seconds). It is intended
for automated efficiency measurement without a GUI. Combine it with `--headless`
and `--run-mzf`:

```
mz800emu --headless --maxspeed-bench --run-mzf program.mzf
```

## How to read the results

- The cumulative values (Efficiency, Throughput) are an average across the whole
  run since the reset. They integrate over time, so they are stable.
- The distribution shows how the efficiency varied over time. A narrow range
  (small standard deviation) means a stable load, a wide range a variable one.
- To compare two versions or configurations, measure on an idle computer and
  average several runs. The result varies between runs depending on the host
  state (other running processes, CPU frequency scaling), which can be larger
  than the difference being measured.
