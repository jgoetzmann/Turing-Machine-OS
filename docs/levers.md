# Levers

A lever changes the *machine*, not the picture of it. Every lever has a C API (`tos_lever_set/get`), a CLI flag on `build/turingos`, a URL parameter on the playground (`#/playground?…`), a control in the Levers panel, and at least one test. Ids and ranges are frozen in `src/tos.h`.

**Machine levers** reset the machine when changed (the tape is re-created, the shell reboots). **View levers** take effect immediately and never touch tape or CPU state.

## Summary

| Id | Lever | Kind | Range | Default | CLI | URL | Meta byte |
|---|---|---|---|---|---|---|---|
| 0 | `TAPES` | machine | 1, 2, 4 | 1 | `--tapes=N` | `tapes=` | 0x07 |
| 1 | `TAPE_LEN` | machine | 32768, 49152, 65536 | 65536 | `--len=N` | `len=` | 0x08–0x09 (L/256) |
| 2 | `HZ` | view | 0 = unthrottled, else cycles/s | 0 | `--hz=N` | `hz=` | 0x31–0x34 |
| 3 | `SEED` | machine | 0–255 (0 acts as 1) | 1 | `--seed=N` | `seed=` | 0x30 |
| 4 | `INPUT_MODE` | view | 0 console, 1 keys | 0 | `--input=console\|keys` | `input=console\|keys` | 0x35 |
| 5 | `DISKS` | machine | 1, 2 | 1 | `--disks=N`, `--disk-b=` | `disks=` | 0x36 |
| 6 | `TRACE` | view | 0, 1 | 1 in the browser, 0 native | `--trace` | `trace=` | 0x37 |
| 7 | `SNAP_INTERVAL` | view | steps between snapshots, 0 = never | 1000 | `--snap=N` (`--snap-dir=` also dumps each one) | — | — |

`tos_lever_set(id, value)` returns 0 or −1 for a bad id or out-of-range value. `tos_lever_get(id)` reads the current value. The `Engine` class exposes them as `leverSet(id, value)` / `leverGet(id)` with `LEVER.TAPES … LEVER.SNAP_INTERVAL`. `kernel_config_default` (native) is 1 tape, 64K, hz 0, seed 1, console, 1 disk, trace off, snapshots every 1000 steps; `Engine.create` (browser) is the same with trace on.

## 0. Tape count (`TAPES`)

A k-tape Turing machine. Addresses `0x4000 … L − 0x2001` (the banked window) exist once per tape; every other address is common and always resolves to tape 0. Programs choose the tape for the window with `OUT 02H` (A = n) and read it back with `IN 02H`; `IN 04H` returns k. Selecting n ≥ k halts the machine with reason `BAD_TAPE` (6). Boot and `RUN` reset the selection to 0.

What it changes: where the Brainfuck cells live (tape 1 when k ≥ 2), how many machine tapes a `.tm` program can spread its TM tapes over, and the head-travel cost of the palindrome demo (`pal1.tm` vs `pal2.tm`). The visualizer stacks k strips and highlights the selected one.

Tests: WS4-01a, WS4-01b, WS6-05, WS6-08.

## 1. Tape length (`TAPE_LEN`)

L ∈ {32K, 48K, 64K}. Only `0x0000–0x3FFF` is fixed. The banked window ends at `L − 0x2001`, scratch at `L − 0x1001`, the stack at `L − 0x201` (the loader sets SP there), the display at `L − 0x101` and the metadata block at `L − 1`. `IN 05H` returns `(L / 256) & 0xFF` so a program can find the top of its own tape; the shell's `mem` command prints the map for the running length.

An access at or beyond L is a **tape fault**: reads return `0xFF`, writes are dropped, and the kernel halts with reason 4 after the current instruction. `demos/fault/fault.c` walks writes upward from `0x4000` until this happens — on a 32K tape it faults; on 64K it reaches the metadata block and returns to the shell.

Tests: WS4-02a, WS6-09; the shell and every demo run at 32K.

## 2. Clock (`HZ`)

The nominal 8080 clock in cycles per second, using real per-opcode cycle counts (`cpu_opcode_cycles`, Intel's table). `0` means unthrottled. The native `kernel_run` sleeps so that `cycles` advance at this rate — `--hz=2000000` runs at the speed of an original 2 MHz 8080. `kernel_step` itself never sleeps, so the browser is unaffected: there the run loop uses the separate `speed` setting below. The value is published at meta `0x31`.

Tests: WS4-03.

### Playground speed (`speed=`)

Not a machine lever — a run-loop budget. `speed=<steps per second>` or `speed=max`. At `max` the page runs the machine for up to 8 ms per animation frame; at 60 or below every head move is visible on the strip. `web/src/speed.ts` (`stepsForFrame(speed, dtMs)`) computes the per-frame budget.

## 3. PRNG seed (`SEED`)

Seeds the BIOS `RAND` (0x07) generator, an 8-bit xorshift (`x ^= x << 3; x ^= x >> 5; x ^= x << 1`). Seed 0 behaves as seed 1 so the generator never sticks at zero. With the same seed and the same input log, Pong serves in the same direction and Life's random preset is the same board every run — determinism (WS1-16) depends on it.

Tests: WS4-05.

## 4. Input mode (`INPUT_MODE`)

Both input paths always exist: parked console reads (`CONIN`, `READLINE`, `CONST`) and polled keys (`IN 03H`). The lever tells the UI where keystrokes go — to the console (typed into the shell) or to the key bitmask (games). It is recorded at meta `0x35` and changes nothing inside the machine. For reproducible runs the native binary takes `--stdin-script=<file>`, whose bytes are fed as console input followed by EOF.

## 5. Disks (`DISKS`)

One or two 512,512-byte images (A: and B:, 77 × 26 × 256). `SELDISK` (0x09) picks one; the shell's `disk a` / `disk b` switches and the prompt becomes `B> `. A file written on B: is not visible on A:. Natively, `--disk=<a.img>` and `--disk-b=<b.img>`; in the browser, `Engine.loadDiskImage(d, bytes)` and `diskPutFile/diskGetFile/diskList`.

Tests: WS4-07, WS1-11c.

## 6. Trace (`TRACE`)

Turns the 65,536-event trace ring on or off. Events are `{step, addr, kind, value}` with kinds FETCH, READ, WRITE, SYSCALL, STATE, TAPE. Off by default natively (fast), on by default in the browser (the timeline and detail panels read it). Toggling it does not change machine state.

Tests: WS1-09.

## 7. Snapshot interval (`SNAP_INTERVAL`)

Steps between automatic full-machine snapshots (32-slot ring). Time travel (`tos_seek`, the timeline scrubber) restores the nearest earlier snapshot and replays the input log forward, so a smaller interval means a shorter replay. `0` disables interval snapshots; the step-0 anchor (and the anchor taken when a program is loaded) still exist, so seeking replays from there (`decisions.md` B17). Natively, `--snap-dir=<dir>` additionally writes each snapshot's tape 0 and metadata to files for debugging.

Tests: WS1-10.

## Counters that are not levers

- **Head-travel odometer.** `mem_travel()` = Σ |addr<sub>i</sub> − addr<sub>i−1</sub>| over every read and write since `mem_init`; `mem_accesses()` counts them; `mem_cells_written()` counts distinct cells with a write age. Shown in the Stats panel; the 1-tape vs 2-tape palindrome demo compares the two odometers. Reset with the machine. Test: WS4-08.
- **Steps, cycles, frames, syscalls.** `tos_steps`, `tos_cycles_lo/hi`, `tos_frame`, `tos_last_syscall`.
- **Breakpoints.** `bp=<kind:lo:hi,…>` in the URL, `tos_bp_add(kind, lo, hi)` in C, where kind is 0 PC, 1 READ, 2 WRITE, 3 SYSCALL, 4 STATE. A hit pauses the run loop without halting the machine.

## Playground URL

```
#/playground?demo=<name>&tapes=1|2|4&len=32768|49152|65536&hz=0|N&seed=N
            &input=console|keys&disks=1|2&trace=0|1&speed=<steps/s|max>&bp=<kind:lo:hi,...>
```

`web/src/urlstate.ts` parses and formats it (`parseHash` / `formatHash`); the Levers panel rewrites the hash whenever a lever moves, so a URL is a complete description of a machine configuration.
