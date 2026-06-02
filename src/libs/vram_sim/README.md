# vram_sim - stateless simulátor MZ-800 VRAM přístupů

Modulární C knihovna která zduplikuje chování emu modulů `mz800_vramctrl`
+ `mz800_hwscroll` **bez závislosti na globálním emu state**. Slouží
debugger UI komponentám (Memory Browser CPU view, VRAM Viewer) které
potřebují simulovat "co by CPU dostalo" bez nutnosti reálně spustit
M-cykl na emu CPU.

## Použití

```c
#include "libs/vram_sim/vram_sim.h"

/* Live view: capture aktuálního emu state. */
st_VRAM_SIM_STATE state;
vram_sim_capture_from_emu(&state);

/* Co by CPU teď přečetlo z 0x8100? */
uint8_t b = vram_sim_cpu_read(&state, 0x8100);

/* Override view: vlastní DMD/RF/WF + emu plane data. */
state.dmd = 0x02;        /* 320x200@16 */
state.rf_plane = 0x0F;   /* all 4 planes */
state.rf_search = 1;     /* SEARCH mode */
uint8_t search_result = vram_sim_cpu_read(&state, 0x8100);

/* Pixel render pro canvas. */
en_VRAM_SIM_MODE mode = vram_sim_decode_mode(state.dmd);
int w, h;
vram_sim_canvas_dimensions(mode, &w, &h);
for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
        int pal_idx = vram_sim_get_pixel_palette_index(&state, x, y);
        /* ... resolve palette + render ... */
    }
}
```

## API přehled

| Funkce | Účel |
|--------|------|
| `vram_sim_capture_from_emu()` | Naplnit state z aktuálního emu (MZ-800 only) |
| `vram_sim_attach_emu_planes()` | Připojit jen plane pointery (pro override views) |
| `vram_sim_recompute_hwscroll_enabled()` | Po manuální změně SOF/SW/SSA/SEA |
| `vram_sim_cpu_read()` | Replika `vramctrl_mz800_memop_read_byte()` |
| `vram_sim_cpu_write()` | Replika `vramctrl_mz800_memop_write_byte()` |
| `vram_sim_get_pixel_palette_index()` | Pixel → palette idx (0..15) |
| `vram_sim_locate_pixel()` | Pixel → seznam plane lokací |
| `vram_sim_decode_mode()` | DMD bajt → `en_VRAM_SIM_MODE` |
| `vram_sim_hwscroll_shift()` | Replika `hwscroll_shift_addr()` |

## Disciplina

- **Arch-independent** v core - lib se kompiluje pro mz800/mz700/mz1500.
- **Capture adapter** je v `src/emulator/mzarch/mz800/vram_sim_emu_capture.c`
  - per-arch sběr, linkuje jen do mz800emu.
- **Hot path discipline**: NESMÍ být volaná z emu CPU/render hot path.
  Je to off-cycle helper pro debuggery/browsery.
- **Bit-for-bit identita** s emu reference - ověřeno smoke testem
  (matrix DMD x WF x HW scroll na celém 16 KB rozsahu).

## Detaily

Viz `devdoc/libs/vram-sim-library.md`.
