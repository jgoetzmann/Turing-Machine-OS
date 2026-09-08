# Architecture

TuringOS is a small operating system built so that the whole machine maps onto the parts of a Turing machine. The tape is a byte array, the head is an Intel 8080 program counter, the states are a six-state finite control, and everything outside the machine reaches it through one host abstraction layer. This page describes the system **as implemented** under `src/`. Every number here comes from `src/tos.h`; the table at the end is the output of `build/dump_constants --markdown` and `tests/docs/test_constants.sh` fails if the two disagree.

## 1. The Turing-machine mapping

| TM component | TuringOS | Where |
|---|---|---|
| Tape | k tapes of L bytes, k ∈ {1, 2, 4}, L ∈ {32768, 49152, 65536}. A cell is one byte; the alphabet is 0x00–0xFF. | `src/emu/mem.c` |
| Head | The 8080 program counter plus the address bus. The head moves only by executing an instruction. | `src/emu/cpu.c` |
| Finite control | `kernel_state_t`: BOOT, IDLE, SHELL, RUNNING, SYSCALL, HALT. Only `kernel.c` assigns `state`. | `src/kernel/kernel.c` |
| Transition function | `cpu_step()` (exactly one instruction) and `bios_dispatch()` (exactly one syscall). | `src/emu/cpu.c`, `src/bios/bios.c` |
| Environment | Console bytes, key bitmask, disk images, frames. All of it sits behind `hal.h`. | `src/hal/` |
| Halting | The HALT state, reachable only through the transitions in §5. | `src/kernel/kernel.c` |

Where it is *not* a pure Turing machine, on purpose:

- The tape is finite. An access at or beyond L is a **tape fault** (halt reason 4), not more tape.
- The head can jump (`JMP`, `CALL`, `LHLD`) instead of moving one cell at a time. The head-travel odometer (`mem_travel()`) charges every access the distance a single-head machine would have walked, so the cost is measured rather than hidden.
- A BIOS syscall does in one transition what a formal machine would spend thousands on (reading a directory, compiling a file). The machine sees each syscall as one visible `OUT 01H` instruction.
- The console, keys and disks are an environment. A formal TM has none; here it is the only thing the machine cannot compute by itself.

`decisions.md` A1 and B4-B6 record why each of these was chosen.

## 2. Components

| Path | Runs on | Role |
|---|---|---|
| `src/emu/cpu.c` | host (emulates the 8080) | All 256 opcodes, real cycle counts, undocumented aliases. |
| `src/emu/mem.c` | host | k tapes, banked window, faults, write/read ages, dirty pages, travel odometer. |
| `src/emu/disasm.c` | host | One-instruction disassembler used by the CLI and the visualizer. |
| `src/kernel/kernel.c` | host | The finite control: `kernel_step`, the 12 transitions, breakpoints, metadata block. |
| `src/kernel/trace.c`, `snapshot.c` | host | 65,536-event trace ring; 32-slot ring of full machine snapshots. |
| `src/bios/bios.c` | host ("ROM") | The syscall table behind `OUT 01H`; console ring, PRNG, disk sectors, file services. |
| `src/fs/fs.c` | host | CP/M-style flat filesystem over one or two 512,512-byte images. |
| `src/compiler/compiler.c`, `src/lang/*.c` | host ("ROM services") | tiny-C, 8080 assembler, TM language, Brainfuck. See `languages.md`. |
| `src/hal/hal_posix.c`, `hal_wasm.c` | host | The only files that touch a terminal, a file, or JavaScript. |
| `src/api/api.c` | host | `tos_*`: the one embedding API used by `main.c`, the C tests and the wasm build. |
| `src/shell/shell_tpa.c` | **the 8080** | The command shell, written in tiny-C, compiled to `build/bin/shell.com` and embedded as `tos_shell_blob`. |
| `demos/**` | **the 8080** | Every demo, including the Forth interpreter, runs inside the emulated machine. |

ROM services are the honest name for the compilers: from the machine's point of view `cc`, `asm`, `tm` and `bf` are fixed functions reached by one `OUT 01H`, like a firmware routine. Everything the user runs (the shell, Forth, every demo, every compiled TM or Brainfuck program) executes on the emulated 8080.

## 3. Memory map (v2)

Addresses are 16-bit. `0x0000–0x3FFF` is fixed; everything above it is anchored to the top of the tape so that one `.com` runs at every tape length.

| Region | 32K (L = 0x8000) | 48K (L = 0xC000) | 64K (L = 0x10000) | Size | Which tape |
|---|---|---|---|---|---|
| BIOS vectors (zeros) | 0x0000–0x00FF | 0x0000–0x00FF | 0x0000–0x00FF | 256 | tape 0 |
| TPA (programs load here) | 0x0100–0x3FFF | 0x0100–0x3FFF | 0x0100–0x3FFF | 16,128 | tape 0 |
| Banked window | 0x4000–0x5FFF | 0x4000–0x9FFF | 0x4000–0xDFFF | 8 KB / 24 KB / 40 KB | **selected tape** |
| Common scratch (default DMA buffer) | 0x6000–0x6FFF | 0xA000–0xAFFF | 0xE000–0xEFFF | 4,096 | tape 0 |
| Stack (initial SP = last byte) | 0x7000–0x7DFF | 0xB000–0xBDFF | 0xF000–0xFDFF | 3,584 | tape 0 |
| Display (64×32, 1 bpp) | 0x7E00–0x7EFF | 0xBE00–0xBEFF | 0xFE00–0xFEFF | 256 | tape 0 |
| Metadata block | 0x7F00–0x7FFF | 0xBF00–0xBFFF | 0xFF00–0xFFFF | 256 | tape 0 |
| Beyond L | 0x8000– | 0xC000– | — | — | **tape fault** |

Formulas: `TOS_BANK_END(L) = L − 0x2001`, `TOS_SCRATCH_BASE(L) = L − 0x2000`, `TOS_SCRATCH_END(L) = L − 0x1001`, `TOS_STACK_BASE(L) = L − 0x1000`, `TOS_STACK_TOP(L) = L − 0x201`, `TOS_DISPLAY_BASE(L) = L − 0x200`, `TOS_META_BASE(L) = L − 0x100`, `TOS_DMA_DEFAULT(L) = TOS_SCRATCH_BASE(L)`.

A program finds its own tape length with `IN 05H`, which returns `(L / 256) & 0xFF`: `0x80` for 32K, `0xC0` for 48K, `0x00` for 64K. The shell's `mem` command prints this table for the running length, one `XXXX-YYYY NAME` line per region.

Out-of-range accesses: reads return `0xFF`, writes are dropped, `mem_fault()` becomes `TOS_HALT_TAPE_FAULT`, and the kernel halts *after* the current instruction finishes.

### Tapes and the banked window

A k-tape machine has one control and k tapes. Only the banked window exists once per tape; the BIOS vectors, TPA, scratch, stack, display and metadata are common (they always resolve to tape 0). This is the CP/M 3 "common area" arrangement, and it means a program's code and stack never vanish under the PC when the selection changes.

- `OUT 02H` with A = n selects tape n for every access (fetch and data) inside the window. n ≥ k halts with `TOS_HALT_BAD_TAPE` (6).
- `IN 02H` reads the selection back; `IN 04H` reads k.
- Boot and `RUN` reset the selection to 0.
- `mem_peek`/`mem_poke` and the `tos_tape_ptr(i)` views address a specific tape directly; the visualizer shows k stacked strips.

## 4. Boot sequence

1. The embedder calls `hal_init()` (raw TTY on POSIX when stdin is a terminal; nothing on wasm). The kernel never calls it.
2. `tos_create(cfg)` → `kernel_init`: `mem_init(k, L)` zero-fills the tapes and resets ages and counters; `trace_reset` / `trace_enable(cfg.trace)`; `snapshot_reset`; `bios_init`, `bios_set_tape_len(L)`, `bios_set_seed(seed)`; `fs_init(disks)` loads each image through `hal_disk_load` (a missing image becomes a blank formatted one).
3. The shell blob (`build/bin/shell.com`, embedded by `tools/bin2c`) is copied to `0x0100`. `cpu_reset`; `PC = 0x0100`; `SP = TOS_STACK_TOP(L)`; tape 0 selected; `steps = 0`; `halt_reason = 0`; `bp_hit = −1`.
4. Transition 0 (BOOT → SHELL) is counted and the metadata block is written. No instruction has run yet.
5. The first `kernel_step` executes the shell's `CALL main`. The shell prints `A> ` through CONOUT and calls READLINE, which parks the machine in IDLE (transitions 1 then 6) until a console byte arrives.

## 5. The kernel finite state machine

### States

| Value | State | Meaning |
|---|---|---|
| 0 | BOOT | Before the shell is loaded. Left during `kernel_init`. |
| 1 | IDLE | Parked: a syscall is waiting for console input the environment has not provided. |
| 2 | SHELL | The shell program is executing. |
| 3 | RUNNING | A user program loaded by `RUN` / `tos_load_com` is executing. |
| 4 | SYSCALL | `bios_dispatch` is servicing an `OUT 01H`. |
| 5 | HALT | Terminal. `kernel_step` returns immediately. |

### The 12 frozen transitions

| # | From → To | Fires when |
|---|---|---|
| 0 | BOOT → SHELL | shell loaded |
| 1 | SHELL → SYSCALL | `OUT 01H` executed by the shell |
| 2 | SYSCALL → SHELL | syscall done |
| 3 | SYSCALL → RUNNING | program loaded by `RUN`, or a syscall issued by a running program is done |
| 4 | RUNNING → SYSCALL | `OUT 01H` executed by a program |
| 5 | RUNNING → SHELL | program executed `HLT` (the shell is reloaded into the TPA) |
| 6 | SYSCALL → IDLE | waiting for input |
| 7 | IDLE → SYSCALL | input available |
| 8 | IDLE → HALT | console EOF |
| 9 | SHELL → HALT | `halt` command (or a tape fault while in SHELL) |
| 10 | RUNNING → HALT | tape fault |
| 11 | SYSCALL → HALT | console EOF |

The table above is checked against the kernel's own (`tests/docs/test_fsm_table.c`): rewording the last column is free, renumbering or re-pointing a transition is not. Every transition increments `transition_counts[#]`, pushes a `TR_STATE` trace event, and is a candidate for a `KBP_STATE` breakpoint. `tos_transition_from/to/why/fired(i)` expose the table and its counters; the visualizer's FSM panel is drawn from them. A boot followed by the `halt` command fires 0, 1, 2 and 9 and never 3, 4 or 5.

### `kernel_step(k, max_steps, &steps_run)`

Runs at most `max_steps` instructions. One instruction is one `cpu_step` is one step; syscalls do not count as steps.

- **SHELL / RUNNING.** Refresh `io_in_ports[2..5]`; check `KBP_PC`; push `TR_FETCH`; `cpu_step`. If the CPU latched an `OUT`: port 1 → `resume_state = state`, enter SYSCALL (transition 1 or 4); port 2 → `mem_select_tape(A)`, push `TR_TAPE`; other ports are ignored. Then: `mem_fault()` set → HALT with that reason (9 or 10); CPU halted in SHELL → HALT with `TOS_HALT_COMMAND` (9); CPU halted in RUNNING → reload the shell blob, `cpu_reset`, `PC = 0x0100`, `SP = sp_init`, SHELL (5). Finally drain BIOS output to `hal_con_out`.
- **SYSCALL.** `r = bios_dispatch(&cpu)`; record `last_syscall`; push `TR_SYSCALL`; check `KBP_SYSCALL`. `BIOS_DONE` → RUNNING if a `RUN` just loaded a program (3, with `SP = sp_init`) else back to `resume_state` (2 or 3). `BIOS_WAIT` → IDLE (6), return `KSTOP_WAIT_INPUT`. `BIOS_VSYNC` → `frame++`, `bios_tick()`, `hal_display(fb)`, back to `resume_state`, return `KSTOP_VSYNC` (the host loop paces the frame; the kernel does not sleep). `BIOS_EOF` → HALT with `TOS_HALT_EOF` (11).
- **IDLE.** If `hal_con_in_ready()` → SYSCALL (7) and continue; otherwise return `KSTOP_WAIT_INPUT` having run 0 steps. EOF while idle → HALT (8).
- **HALT.** Return `KSTOP_HALT`, 0 steps.
- Every call ends with `tick++` and `kernel_write_meta`. When `cfg.snap_interval` is non-zero and `steps − last_snapshot_step ≥ snap_interval`, it also runs `snapshot_save` plus the `hal_snapshot` hook.

Stop reasons (`kernel_stop_t`, also `tos_stop_reason()`): `KSTOP_BUDGET` 0, `KSTOP_HALT` 1, `KSTOP_WAIT_INPUT` 2, `KSTOP_VSYNC` 3, `KSTOP_BREAKPOINT` 4.

### Breakpoints

Up to 16 (`kernel_bp_add`, `tos_bp_add`), each `{kind, lo, hi}`:

| Kind | Checked | Fires when |
|---|---|---|
| `KBP_PC` 0 | before an instruction | `lo ≤ pc ≤ hi` (does not re-fire until the PC leaves and re-enters the range) |
| `KBP_READ` 1 / `KBP_WRITE` 2 | after an instruction | it read / wrote an address in range |
| `KBP_SYSCALL` 3 | on dispatch | `lo ≤ fn ≤ hi` |
| `KBP_STATE` 4 | on any transition | `lo ≤ to ≤ hi` |

A hit sets `bp_hit` and returns `KSTOP_BREAKPOINT`. The machine is **not** halted: `halt_reason` is unchanged, `TOS_META_HALT_REASON` shows `TOS_HALT_BREAKPOINT` only while `last_stop == KSTOP_BREAKPOINT`, and the next `kernel_step` resumes past the breakpoint.

### `kernel_run`

Loops `kernel_step(k, budget, &n)` with a 4,096-step budget, or about one 60 Hz frame's worth when `cfg.hz > 0`. On `KSTOP_WAIT_INPUT` it waits for the HAL (the POSIX HAL blocks on stdin when stdin is a pipe or file, so scripted input never spins) and restarts its clock afterwards, because waiting for a person is not emulated time; on `KSTOP_VSYNC` it calls `hal_vsync()` to pace the frame; on `KSTOP_HALT` it returns. With `cfg.hz > 0` it sleeps through `hal_sleep_ms` so that `cycles` advance at `hz` per second. `kernel_step` itself never sleeps and never reads a clock: `src/main.c` runs the same loop for the CLI.

## 6. I/O ports

| Port | `IN` | `OUT` |
|---|---|---|
| 0x01 | — | BIOS call; A = function id (§7). Enters SYSCALL. |
| 0x02 | selected tape | select tape A; A ≥ k → halt `TOS_HALT_BAD_TAPE` |
| 0x03 | key bitmask (§9) | — |
| 0x04 | tape count k | — |
| 0x05 | `(L / 256) & 0xFF` | — |

The kernel refreshes ports 2-5 before every instruction. Other `OUT` ports are latched by the CPU and ignored by the kernel; other `IN` ports read the CPU's `io_in_ports` table, which the kernel leaves at 0. Port reads are ordinary instructions: they cost one step and appear in the trace, unlike a syscall (`decisions.md` B12).

## 7. BIOS syscalls (A on `OUT 01H`)

Arguments travel in `C` and `DE`; results come back in `A`. Console output goes to a 4,096-byte ring that the kernel drains to `hal_con_out`. `NAME` below means the 8.3 name accumulated by `NAMECH` calls; it is cleared after the operation that uses it, and a missing file prints `?` and a newline.

| Id | Name | In | Out | Notes |
|---|---|---|---|---|
| 0x01 | CONIN | — | A = byte | Parks the machine (`BIOS_WAIT`) until a byte exists; EOF → `BIOS_EOF`. |
| 0x02 | CONOUT | C = byte | — | |
| 0x03 | AUXOUT | C = byte | — | Stub. |
| 0x04 | AUXIN | — | A = 0 | Stub. |
| 0x05 | CONST | — | A = 0xFF if a console byte is ready else 0 | |
| 0x06 | VSYNC | — | — | `BIOS_VSYNC`: the kernel bumps `frame`, renders the display, returns `KSTOP_VSYNC`. |
| 0x07 | RAND | — | A = next PRNG byte | 8-bit xorshift (`x ^= x<<3; x ^= x>>5; x ^= x<<1`), seeded by the SEED lever; seed 0 acts as 1. The cycle is 17 values long. |
| 0x08 | TICKS | — | A = frame count & 0xFF | Counts VSYNC frames, never wall-clock time. |
| 0x09 | SELDISK | C = 0/1 | A = 0 selected, 1 no such disk | `fs_select_disk`. The tiny-C `seldisk(n)` intrinsic returns that status. |
| 0x0A | SETTRK | C = track | — | 0-based. |
| 0x0B | SETSEC | C = sector | — | 1-based. |
| 0x0C | SETDMA | DE = address | — | Default `TOS_DMA_DEFAULT(L)` = scratch base. |
| 0x0D | READ | — | A = 0 ok / 1 error | 256-byte sector → `mem[dma..dma+255]`. |
| 0x0E | WRITE | — | A = 0 ok / 1 error | `mem[dma..dma+255]` → sector. |
| 0x0F | LISTDIR | — | — | Prints `NAME.EXT` per line, or `(empty)`. |
| 0x12 | NAMECH | C = char | — | Append to the name buffer. |
| 0x13 | TYPE | — | — | Print the file NAME. |
| 0x14 | RUN | — | — | Load NAME into the TPA (> 16,128 bytes → `?`), `PC = 0x0100`; the kernel then enters RUNNING with a fresh SP. |
| 0x15 | DEL | — | — | Delete NAME. |
| 0x16 | CC | — | — | `NAME.C` → `NAME.COM` via `cc_compile_buf`; on error prints the diagnostic instead. |
| 0x17 | READLINE | — | — | Accumulates bytes into a 128-byte line across calls; `\n`/`\r` ends it; backspace (8 or 127) deletes; parks when no byte; EOF → `BIOS_EOF`. |
| 0x18 | LINEGET | C = index | A = byte, 0 past the end | |
| 0x19 | LINELEN | — | A = length | |
| 0x1A | ASM | — | — | `NAME.ASM` → `NAME.COM` via `asm_assemble`. |
| 0x1B | TM | — | — | `NAME.TM` → `NAME.COM` via `tm_compile`. |
| 0x1C | BF | — | — | `NAME.BF` → `NAME.COM` via `bf_compile`. |

`CC/ASM/TM/BF` read the source from the selected disk, overwrite `NAME.COM`, and `fs_flush()`. A compile error prints the tool's `err` string followed by a newline and writes no file. Ids 0x10, 0x11 and anything above 0x1C are unassigned.

## 8. Metadata block (`TOS_META_BASE(L)`, 256 bytes)

The kernel rewrites this block at the end of every `kernel_step`. It is ordinary tape: a program can read it with `peek`, and the visualizer reads it in place.

| Offset | Size | Field |
|---|---|---|
| 0x00 | 1 | kernel state (§5) |
| 0x01 | 4 | steps, u32 little-endian (low 32 bits) |
| 0x05 | 1 | halt reason (§10) |
| 0x06 | 1 | selected tape |
| 0x07 | 1 | tape count k |
| 0x08 | 2 | L / 256, u16 LE (0x0100 = 64K) |
| 0x0A | 4 | VSYNC frame counter, u32 LE |
| 0x0E | 1 | current key bitmask |
| 0x0F | 1 | last stop reason (`kernel_stop_t`) |
| 0x10 | 32 | dirty pages: bit p set if any byte of 256-byte page p was written during the last `kernel_step` call |
| 0x30 | 1 | PRNG seed |
| 0x31 | 4 | hz, u32 LE (0 = unthrottled) |
| 0x35 | 1 | input mode (0 console, 1 keys) |
| 0x36 | 1 | disk count |
| 0x37 | 1 | trace enabled |
| 0x38 | 1 | last BIOS function id dispatched |
| 0x3A | 2 | initial SP, u16 LE |

## 9. Display and keys

**Display.** 256 bytes at `TOS_DISPLAY_BASE(L)`: 64 × 32 pixels, 8 bytes per row, MSB first. Pixel (x, y) is `(fb[y*8 + (x>>3)] >> (7 − (x&7))) & 1`. Programs draw by writing tape cells (`poke`, or a `__at` array). The host renders the page only on `VSYNC`: the native TTY draws 64 columns × 16 rows with `▀ ▄ █` and space when `--display` is on; the browser draws it on a canvas. Rendering is observation; nothing outside the tape holds the picture.

**Keys.** `IN 03H` returns a bitmask: W 0x01, S 0x02, UP 0x04, DOWN 0x08, SPACE 0x10, ESC 0x20, ENTER 0x40, ANY 0x80. On wasm it is the last `tos_keys_set`; on POSIX it is derived from raw TTY bytes (`w`, `s`, arrow escapes, space, ESC, Enter, each held for 150 ms) OR'ed with `hal_keys_set`.

## 10. Halt reasons

| Value | Name | Written when |
|---|---|---|
| 0 | NONE | running |
| 1 | HLT | reserved; never written (a program's `HLT` returns to the shell) |
| 2 | COMMAND | `HLT` executed in SHELL state — the `halt` command |
| 3 | EOF | console input reached end of file |
| 4 | TAPE_FAULT | access at or beyond L |
| 5 | BREAKPOINT | informational, only while the last stop was a breakpoint |
| 6 | BAD_TAPE | `OUT 02H` with A ≥ k |

`main.c` prints `TuringOS halted (reason=<NAME>) after <N> steps` and exits 0.

## 11. Disk and directory format

- One image is 77 tracks × 26 sectors × 256 bytes = 512,512 bytes (`TOS_DISK_IMAGE_BYTES`). Tracks are 0-based, sectors 1-based (`SETTRK` / `SETSEC`).
- Up to two images (A: and B:), selected by `SELDISK`; the shell's `disk a|b` command switches and the prompt becomes `B> `.
- The directory has 64 entries of 32 bytes (2,048 bytes). A formatted disk has every entry marked `0xE5`. Names are 8.3, stored upper-case, matched case-insensitively (`add.c` finds `ADD.C`). The per-entry byte layout and the directory's position on the image are defined in `src/fs/fs.c`.
- Sixteen open-file handles. `fs_put_file` overwrites; `fs_delete` frees the entry.
- Images live in static host buffers, loaded and saved whole through `hal_disk_load` / `hal_disk_save` (`--disk=`, `--disk-b=` on POSIX; `tos_disk_ptr` + `tos_disk_reload` on wasm). `tools/mkdisk` creates, fills, lists and extracts from images on the host.

## 12. The `.com` format and the loader convention

A `.com` file is a header-less 8080 image loaded at `0x0100` with its entry point at byte 0, exactly as under CP/M. Absolute addresses are used throughout: the 8080 has no PC-relative jumps.

- Maximum size 16,128 bytes (the TPA). Larger files are rejected with `?` by `RUN` and `−1` by `tos_load_com`.
- The loader sets SP to `TOS_STACK_TOP(L)` before jumping to `0x0100`; programs and compilers must not emit `LXI SP`. This is what lets one binary run at every tape length (`decisions.md` B6, B14). The value is published at `TOS_META_SP_INIT`.
- The loader also resets the tape selection to 0.
- A program ends with `HLT`. In RUNNING state that reloads the shell (transition 5) and does **not** halt the machine; `halt_reason` 1 is reserved and never written.
- tiny-C images begin with `CALL main ; HLT`, then code, then globals in declaration order.

## 13. Compiler pipeline and languages

`tiny-C source → cc_lex → cc_parse (AST) → code generation → .com`. The compiler is host code (a "ROM service"): in the shell, `cc F` issues BIOS 0x16, the host compiles `F.C` from the disk image and writes `F.COM` back. The same binary compiles the shell itself (`make shell`), which is why `cc SHELL.C` inside the OS reproduces `build/bin/shell.com` byte for byte.

Intrinsics (`putchar`, `getchar`, `bios`, `peek`, `poke`, `inp`, `outp`, `vsync`, `rand`, …) compile to `OUT 01H` calls or to direct memory and port instructions. Diagnostics are `src.c:LINE:COL: message`.

The other tools share the shape `int xxx(const char *src, uint32_t len, uint8_t *out, uint32_t cap, char *err, uint32_t errcap)` and are reachable through `tos_compile(TOS_LANG_*)`, the shell (`asm`, `tm`, `bf`), the host CLIs (`build/asm`, `build/tmc`, `build/bfc`) and the browser editor. Full language references: `tiny-c.md`, `asm.md`, `tm.md`, `languages.md`.

## 14. Trace, snapshots and time travel

- **Trace ring.** 65,536 packed 8-byte events `{step, addr, kind, value}` with kinds FETCH, READ, WRITE, SYSCALL, STATE, TAPE. Enabled by the TRACE lever; `tos_trace_ptr/head/count` expose it and `Engine.drainTrace()` reads it incrementally.
- **Snapshots.** A 32-slot ring of complete machine images: all tapes, `cpu_t`, the whole `kernel_t`, BIOS state and FS state (not disk images, ages or trace). Taken every `snap_interval` steps.
- **Input log.** `tos_con_push` and `tos_keys_set` append `{step, kind, value}` to a 4,096-entry log.
- **`tos_seek(step)`.** Restore the newest snapshot at or before `step` (fails with −1 when there is none), then re-run one instruction at a time, re-applying logged inputs at the steps they were originally pushed, until `steps == step` or HALT. The result is byte-identical to an uninterrupted run.

**Determinism.** Given the same program, input log, key log, seed and levers, every run has identical tape and CPU state at every step. No wall-clock value influences the machine: `hal_time_ms` exists for the host UI only and `TICKS` counts frames.

## 15. The host boundary (HAL)

`src/hal/hal.h` is the machine's only door to the outside world: console in/out/push, keys, time, vsync, whole-image disk load/save, the shell blob, and the snapshot and display observation hooks. `hal_posix.c` implements it with termios and files; `hal_wasm.c` with buffers filled from JavaScript. No file under `src/{emu,bios,kernel,fs,lang,api}` includes `<stdio.h>`; the compiler's only exception is the path-based `cc_compile()` wrapper used by the CLI. Nothing in the machine allocates: every buffer is static, so the wasm memory is sized at build time and two machines with the same inputs cannot diverge.

## 16. Build targets

`make help` lists them: `all` (native `build/turingos` + tools), `tools`, `shell` (`build/bin/shell.com` via `build/cc_driver`), `gen` (`constants.json`, `layout.json`, the shell blob), `disk`, `demo-disk` (`build/disk/demo.img` from `demos/**` + `SHELL.C`), `test`, `wasm`, `web`, `test-web`, `test-e2e`, `run`, `bench`, `asm FILE=…`, `tm FILE=…`, `bf FILE=…`, `disasm FILE=…`, `clean`, `help`. Flags: `-std=c99 -Wall -Wextra -Werror -pedantic -I./src`. `build/libtos.a` holds every `src/**/*.c` except `main.c`, `hal_wasm.c` and `src/shell/shell_tpa.c` (which is 8080 source, not host code), plus the shell blob; the wasm build swaps `hal_posix.c` for `hal_wasm.c`.

## 17. Constants (generated)

The block below is the exact output of `build/dump_constants --markdown`. `tests/docs/test_constants.sh` compares them; edit `src/tos.h`, not this table.

<!-- constants:begin -->
| Name | Hex | Dec |
|---|---|---|
| TOS_BANK_BASE | 0x4000 | 16384 |
| TOS_BANK_END_32K | 0x5FFF | 24575 |
| TOS_BANK_END_48K | 0x9FFF | 40959 |
| TOS_BANK_END_64K | 0xDFFF | 57343 |
| TOS_BIOS_ASM | 0x001A | 26 |
| TOS_BIOS_AUXIN | 0x0004 | 4 |
| TOS_BIOS_AUXOUT | 0x0003 | 3 |
| TOS_BIOS_BASE | 0x0000 | 0 |
| TOS_BIOS_BF | 0x001C | 28 |
| TOS_BIOS_CC | 0x0016 | 22 |
| TOS_BIOS_CONIN | 0x0001 | 1 |
| TOS_BIOS_CONOUT | 0x0002 | 2 |
| TOS_BIOS_CONST | 0x0005 | 5 |
| TOS_BIOS_DEL | 0x0015 | 21 |
| TOS_BIOS_END | 0x00FF | 255 |
| TOS_BIOS_LINEGET | 0x0018 | 24 |
| TOS_BIOS_LINELEN | 0x0019 | 25 |
| TOS_BIOS_LISTDIR | 0x000F | 15 |
| TOS_BIOS_NAMECH | 0x0012 | 18 |
| TOS_BIOS_RAND | 0x0007 | 7 |
| TOS_BIOS_READ | 0x000D | 13 |
| TOS_BIOS_READLINE | 0x0017 | 23 |
| TOS_BIOS_RUN | 0x0014 | 20 |
| TOS_BIOS_SELDISK | 0x0009 | 9 |
| TOS_BIOS_SETDMA | 0x000C | 12 |
| TOS_BIOS_SETSEC | 0x000B | 11 |
| TOS_BIOS_SETTRK | 0x000A | 10 |
| TOS_BIOS_TICKS | 0x0008 | 8 |
| TOS_BIOS_TM | 0x001B | 27 |
| TOS_BIOS_TYPE | 0x0013 | 19 |
| TOS_BIOS_VSYNC | 0x0006 | 6 |
| TOS_BIOS_WRITE | 0x000E | 14 |
| TOS_DISKS_MAX | 0x0002 | 2 |
| TOS_DISK_DIR_ENTRIES | 0x0040 | 64 |
| TOS_DISK_DIR_ENTRY | 0x0020 | 32 |
| TOS_DISK_IMAGE_BYTES | 0x7D200 | 512512 |
| TOS_DISK_SECTORS | 0x001A | 26 |
| TOS_DISK_SECTOR_BYTES | 0x0100 | 256 |
| TOS_DISK_TRACKS | 0x004D | 77 |
| TOS_DISPLAY_BASE_32K | 0x7E00 | 32256 |
| TOS_DISPLAY_BASE_48K | 0xBE00 | 48640 |
| TOS_DISPLAY_BASE_64K | 0xFE00 | 65024 |
| TOS_DISPLAY_H | 0x0020 | 32 |
| TOS_DISPLAY_SIZE | 0x0100 | 256 |
| TOS_DISPLAY_W | 0x0040 | 64 |
| TOS_DMA_DEFAULT_32K | 0x6000 | 24576 |
| TOS_DMA_DEFAULT_48K | 0xA000 | 40960 |
| TOS_DMA_DEFAULT_64K | 0xE000 | 57344 |
| TOS_HALT_BAD_TAPE | 0x0006 | 6 |
| TOS_HALT_BREAKPOINT | 0x0005 | 5 |
| TOS_HALT_COMMAND | 0x0002 | 2 |
| TOS_HALT_EOF | 0x0003 | 3 |
| TOS_HALT_HLT | 0x0001 | 1 |
| TOS_HALT_NONE | 0x0000 | 0 |
| TOS_HALT_TAPE_FAULT | 0x0004 | 4 |
| TOS_INPUT_CONSOLE | 0x0000 | 0 |
| TOS_INPUT_KEYS | 0x0001 | 1 |
| TOS_KEY_ANY | 0x0080 | 128 |
| TOS_KEY_DOWN | 0x0008 | 8 |
| TOS_KEY_ENTER | 0x0040 | 64 |
| TOS_KEY_ESC | 0x0020 | 32 |
| TOS_KEY_S | 0x0002 | 2 |
| TOS_KEY_SPACE | 0x0010 | 16 |
| TOS_KEY_UP | 0x0004 | 4 |
| TOS_KEY_W | 0x0001 | 1 |
| TOS_LANG_ASM | 0x0001 | 1 |
| TOS_LANG_BF | 0x0003 | 3 |
| TOS_LANG_C | 0x0000 | 0 |
| TOS_LANG_TM | 0x0002 | 2 |
| TOS_LEVER_COUNT | 0x0008 | 8 |
| TOS_LEVER_DISKS | 0x0005 | 5 |
| TOS_LEVER_HZ | 0x0002 | 2 |
| TOS_LEVER_INPUT_MODE | 0x0004 | 4 |
| TOS_LEVER_SEED | 0x0003 | 3 |
| TOS_LEVER_SNAP_INTERVAL | 0x0007 | 7 |
| TOS_LEVER_TAPES | 0x0000 | 0 |
| TOS_LEVER_TAPE_LEN | 0x0001 | 1 |
| TOS_LEVER_TRACE | 0x0006 | 6 |
| TOS_META_BASE_32K | 0x7F00 | 32512 |
| TOS_META_BASE_48K | 0xBF00 | 48896 |
| TOS_META_BASE_64K | 0xFF00 | 65280 |
| TOS_META_DIRTY | 0x0010 | 16 |
| TOS_META_DIRTY_BYTES | 0x0020 | 32 |
| TOS_META_DISKS | 0x0036 | 54 |
| TOS_META_FRAME | 0x000A | 10 |
| TOS_META_HALT_REASON | 0x0005 | 5 |
| TOS_META_HZ | 0x0031 | 49 |
| TOS_META_INPUT_MODE | 0x0035 | 53 |
| TOS_META_KEYS | 0x000E | 14 |
| TOS_META_SEED | 0x0030 | 48 |
| TOS_META_SIZE | 0x0100 | 256 |
| TOS_META_SP_INIT | 0x003A | 58 |
| TOS_META_STATE | 0x0000 | 0 |
| TOS_META_STEPS | 0x0001 | 1 |
| TOS_META_STOP | 0x000F | 15 |
| TOS_META_SYSCALL | 0x0038 | 56 |
| TOS_META_TAPE_COUNT | 0x0007 | 7 |
| TOS_META_TAPE_PAGES | 0x0008 | 8 |
| TOS_META_TAPE_SEL | 0x0006 | 6 |
| TOS_META_TRACE | 0x0037 | 55 |
| TOS_PORT_BIOS | 0x0001 | 1 |
| TOS_PORT_KEYS | 0x0003 | 3 |
| TOS_PORT_PAGES | 0x0005 | 5 |
| TOS_PORT_TAPE | 0x0002 | 2 |
| TOS_PORT_TAPES | 0x0004 | 4 |
| TOS_SCRATCH_BASE_32K | 0x6000 | 24576 |
| TOS_SCRATCH_BASE_48K | 0xA000 | 40960 |
| TOS_SCRATCH_BASE_64K | 0xE000 | 57344 |
| TOS_SCRATCH_END_32K | 0x6FFF | 28671 |
| TOS_SCRATCH_END_48K | 0xAFFF | 45055 |
| TOS_SCRATCH_END_64K | 0xEFFF | 61439 |
| TOS_STACK_BASE_32K | 0x7000 | 28672 |
| TOS_STACK_BASE_48K | 0xB000 | 45056 |
| TOS_STACK_BASE_64K | 0xF000 | 61440 |
| TOS_STACK_TOP_32K | 0x7DFF | 32255 |
| TOS_STACK_TOP_48K | 0xBDFF | 48639 |
| TOS_STACK_TOP_64K | 0xFDFF | 65023 |
| TOS_TAPES_MAX | 0x0004 | 4 |
| TOS_TAPE_LEN_32K | 0x8000 | 32768 |
| TOS_TAPE_LEN_48K | 0xC000 | 49152 |
| TOS_TAPE_LEN_64K | 0x10000 | 65536 |
| TOS_TAPE_MAX | 0x10000 | 65536 |
| TOS_TPA_BASE | 0x0100 | 256 |
| TOS_TPA_END | 0x3FFF | 16383 |
| TOS_TPA_SIZE | 0x3F00 | 16128 |
<!-- constants:end -->
