# TuringOS — Design Decisions

One entry per decision: **Context → Decision → Consequences → Alternatives rejected.** Append, don't rewrite; when a decision is superseded, mark it and point at the replacement. This file replaces the `[PROJECT INIT]` entries of the deleted `.cursor/remember.md` (Part A) and records the v2 decisions from `docs/v2-roadmap.md` (Part B). Bug-fix diary entries were deliberately not carried over — `git log` has them.

---

## Part A — Founding decisions (carried over, restated against the code as it exists)

### A1. The Turing-machine mapping is a hard constraint, not a metaphor

**Context.** The whole point of the project is that the OS *is* a Turing machine. Every subsystem must map onto a formal TM component or it doesn't belong.

**Decision.**
- **Tape** = the flat byte array in `src/emu/mem.c` (64 KB; per-tape banks in v2, see B4). Cells are bytes; the alphabet is `0x00–0xFF`.
- **Head** = the 8080 program counter plus the memory bus. The only way to move the head is to execute an 8080 instruction.
- **States** = `kernel_state_t` in `src/kernel/kernel.h` (`BOOT, IDLE, SHELL, RUNNING, SYSCALL, HALT`). Only `kernel.c` may assign `state`.
- **Transition function** = `cpu_step()` + `bios_dispatch()`. No other code changes machine state.
- **Environment** (v2) = everything behind `src/hal/hal.h`: console, keys, disk bytes, time. The environment can be observed and written to by the machine only through BIOS calls and I/O ports.

**Consequences.** No host allocations for machine state (A4); no virtual memory, paging or segmentation; addresses are `uint16_t` everywhere; the kernel loop is a switch over states and nothing else.

**Alternatives rejected.** Treating "TM" as a design vibe and using an ordinary emulator + OS split — simpler, but then the visualizer would be showing a metaphor.

### A2. Intel 8080, not Z80

**Context.** CP/M (the era the project emulates) was written for the 8080; the Z80 is a superset with IX/IY, block ops, `DJNZ`, and a second register file.

**Decision.** Emulate the 8080 only: 7 registers, 16-bit address bus (= a 64 KB tape), ~244 documented opcodes, 1–3 bytes each. Z80-only instructions are errors, not extensions.

**Consequences.** The emulator stays under 700 lines and can be exhaustively tested opcode-by-opcode; the compiler's code generator only needs the 8080 subset. Undocumented opcodes behave as the real chip does (aliases for NOP/JMP/RET/CALL — roadmap WS1-05).

**Alternatives rejected.** Z80 (bigger surface, no gain for the TM story); 6502 (not the CP/M lineage); a made-up ISA (loses the "real era hardware" credibility).

### A3. Programs are flat `.com` images loaded at `0x0100`, with absolute addresses

**Context.** CP/M's `.com` format is a header-less binary loaded at `0x0100`, entry at byte 0.

**Decision.** Same here. The compiler emits **absolute** addresses (`0x0100 +` file offset) for `CALL`/`JMP`/back-edges — the original spec said "position-independent", but the 8080 has no PC-relative jumps, so absolute at a fixed base is the honest choice. The loader (BIOS `RUN`) copies the file into the TPA and sets `PC = 0x0100`.

**Consequences.** No relocation, no linker, no symbol tables; a `.com` from the disk image runs identically in the shell, in the test harness, and in the browser. Programs larger than the TPA (16 KB − 256) are rejected with `?`.

**Alternatives rejected.** A tiny ELF-like header (nothing needs it); relocatable code (impossible to do cheaply on an 8080).

### A4. No heap anywhere in the machine

**Context.** The machine's memory *is* the tape. Host allocations for kernel/emulator state would put part of the machine outside the model.

**Decision.** `malloc`/`calloc`/`realloc` are forbidden in `src/emu`, `src/bios`, `src/kernel`, `src/fs`. Everything is statically sized: open-file table (16), BIOS output ring (1 KB), directory (64 entries), snapshot/trace rings in v2. The compiler (`src/compiler/`) is a host tool and is exempt, though today it also uses static arrays.

**Consequences.** Every limit is a documented constant; WebAssembly memory can be sized exactly at build time (B1); determinism is easy to guarantee (WS1-16).

### A5. BIOS calls are `OUT 0x01` with the function number in `A`

**Context.** Real CP/M reaches the BIOS through a jump table at `0x0000` and BDOS through `CALL 5`. An emulator has to recognise those addresses.

**Decision.** Programs execute `OUT 0x01` with the function id in `A` and arguments in `C`/`DE`. The CPU latches the `OUT`, the kernel enters `SYSCALL`, `bios_dispatch()` runs the function, and the kernel returns to the previous state. Function ids: console `0x01–0x05`, timing `0x06–0x08` (v2), disk `0x09–0x0E`, directory `0x0F`, shell services `0x12–0x19`, language tools `0x1A–0x1C` (v2).

**Consequences.** The syscall is a single, visible, interceptable instruction — it is the "environment interaction" edge in the TM picture and it shows up cleanly in the trace. Real CP/M binaries do **not** run (see roadmap §5).

**Alternatives rejected.** Jump table at `0x0000` (scanning for jumps is messier and makes the syscall invisible in the instruction stream); `RST n` (only 8 vectors).

### A6. The shell is a tiny-C program that runs on the emulated 8080

**Context.** A first version of the shell was host C that could never run on the machine.

**Decision.** `src/shell/shell_tpa.c` is written in the project's own C subset, compiled by the project's own compiler to `build/bin/shell.com` (5,115 bytes), and loaded into the TPA at boot. Line input uses BIOS `READLINE`/`LINEGET`/`LINELEN` so the 8080 code never handles raw keystrokes. `halt` returns from `main()` so the post-`main` `HLT` is the one that stops the machine.

**Consequences.** The shell is dogfood for the compiler and the strongest demo the project has ("the OS compiles its own shell", roadmap WS6-06). Its command parser is character-by-character because the compiler had no arrays at the time — a limitation WS5 removes.

### A7. The C compiler is a host-side "ROM service", not an 8080 program

**Context.** Writing a C compiler that runs *on* an 8080 in 16 KB is a project in itself.

**Decision.** `cc` in the shell issues BIOS `0x16`; the host reads the source from the disk image, runs `cc_compile()` natively, and writes the `.com` back to the disk image. From the machine's point of view the compiler is firmware, like a ROM. The same pattern is used in v2 for `asm`, `tm` and `bf` (B-series tools, roadmap WS7).

**Consequences.** Documentation must say plainly which tools run on the host (`cc`, `asm`, `tm`, `bf`) and which run on the 8080 (shell, Forth, every demo) — roadmap WS7-05. A self-hosting `cc` remains a post-v2 stretch goal.

### A8. C99, no C11, no extensions

**Decision.** `-std=c99 -Wall -Wextra -Werror -pedantic`. No VLAs, no threads, no `_Generic`; GCC extensions only if documented here.

**Consequences.** Builds identically with gcc, clang and Emscripten; the sanitizer job in CI (WS0-09) needs no special-casing.

### A9. Superseded founding decisions

| Original decision | Status | Replaced by |
|---|---|---|
| "FS buffer cache lives at `mem[0xC000–0xEFFF]`; sectors are read directly into the tape" | **Never implemented** — `fs.c` uses host static buffers; sectors land wherever `SETDMA` points | B4 repurposes the region; DMA into the tape is already how `READ`/`WRITE` work |
| "Kernel writes `/tmp/turingos_tape.bin` every 1000 ticks for the visualizer" | Superseded | B2 (HAL snapshot hook) + B3 (web visualizer reads WASM memory directly) |
| "Visualization is Python 3 + pygame only; no web servers, no Electron" | Superseded | B3 — the visualizer is a static web page; `viz/` is deleted at M3 |
| "Docker is the primary dev environment; venv is for the visualizer only" | Superseded | Native `make test` on macOS/Linux and the web build are primary; Docker remains a convenience (WS9-02) |
| "Raw terminal mode in CONIN" | Never implemented | WS1-14 (termios when stdin is a TTY, cooked when piped) |
| `.cursor/` spec / progress / remember / rules workflow | **Deleted 2026-09-07** | B7 |

---

## Part B — v2 decisions (2026-09-07)

### B1. One engine: the C core compiles to WebAssembly and the website runs it

**Context.** The site needs an interactive machine. The temptation is a JavaScript re-implementation for the demo and the "real" C for everything else.

**Decision.** Build the same `src/` with Emscripten (`make wasm`) and drive it from TypeScript. The browser runs the identical emulator, BIOS, kernel, filesystem and compiler that `make test` exercises; `make test-web` runs the same program suite through the wasm build and requires byte-identical output.

**Consequences.** Requires B2 (nothing in the core may block or touch files). Threads are unavailable on GitHub Pages (no COOP/COEP headers → no `SharedArrayBuffer`), so the engine runs on the main thread with a per-frame step budget.

**Alternatives rejected.** JS/TS rewrite (two emulators drift; the site would be lying); Pyodide/pygbag to run the pygame viewer in the browser (heavy, fragile, and still leaves two visualizers).

### B2. A host abstraction layer (HAL) is the machine's only door to the world

**Context.** Today the BIOS calls `getchar()` (blocks — impossible in a browser event loop), the kernel `fopen`s `shell.com` and `/tmp` snapshot files, and `cc` stages files under `build/`.

**Decision.** `src/hal/hal.h` declares every host touchpoint (console in/out, key state, disk bytes, shell blob, time, frame sync, snapshot hook). `hal_posix.c` and `hal_wasm.c` implement it. No file in `src/{emu,bios,kernel,fs,compiler}` includes `<stdio.h>`. The kernel exposes `kernel_step(max_steps)`; a syscall that needs input the environment hasn't provided parks the machine in `KS_IDLE` (finally giving that state a real meaning) and returns `WAIT_INPUT`.

**Consequences.** The machine becomes a pure function of (program, input log, seed) — which is what makes time travel (WS1-10) and determinism tests (WS1-16) possible.

### B3. The web visualizer is the only visualizer; `viz/` (pygame) is deleted

**Context.** The pygame viewer polls a 64 KB file every 100 ms, redraws 65,536 rectangles per frame, has no register panel, and its dirty-flash feature has never worked (the dirty map is zeroed every tick and never set). Keeping it alongside a web visualizer means every panel exists twice.

**Decision.** One visualizer, in TypeScript + Canvas2D, reading the tape straight out of WASM memory each frame. `viz/` is deleted at milestone M3, once the web visualizer covers everything the pygame one showed (roadmap WS9-01). Native builds keep an optional `--snap-dir=` debug hook through the HAL, off by default.

**Alternatives rejected.** Fix pygame and keep both (double maintenance for a viewer nobody sees on the site); a native GUI (Qt/SDL) — same problem.

### B4. Multiple tapes = a banked window, CP/M 3 style

**Context.** A k-tape Turing machine has one control and k tapes/heads. The 8080 has one address bus, and the spec's "kernel heap", "shell workspace" and "FS cache" regions (`0x4000–0xEFFF`) have never been used by anything.

**Decision.** With tape count k (1, 2 or 4), addresses `0x4000 … TOP−0x2001` (`0x4000–0xDFFF`, 40 KB on a 64 K tape) exist once per tape; everything else (BIOS vectors, TPA, scratch, stack, display, metadata) is common. `OUT 0x02, A=n` selects the tape used for every access — fetch and data — in that window; `IN 0x02` reads it back; selecting `n ≥ k` faults (halt reason 6). Boot and `RUN` reset to tape 0.

**Consequences.** A program's code and stack never disappear under the PC; a TM/Brainfuck/Forth "inner tape" gets its own bank and the visualizer can stack k strips; the 1-tape-vs-2-tape palindrome demo (WS6-05) becomes a real measurement, not an animation. This is also historically how 8080/Z80 systems escaped 64 KB.

**Alternatives rejected.** Switching the whole address space (code vanishes under the head; CP/M 3 needed a "common area" for exactly this reason); k CPUs (that's k machines, not a k-tape machine); making the alphabet or cell size a lever (the 8080 fixes a cell at 8 bits).

### B5. The display is tape: a 64×32 1-bpp framebuffer at `TOP−0x200`

**Context.** Pong needs a screen. The spec reserved 256 bytes at `0xFE00–0xFEFF` for "memory-mapped I/O" and never used them. 64×32 pixels at 1 bit = exactly 256 bytes, and is the resolution CHIP-8 used for Pong.

**Decision.** Row-major, 8 bytes per row, MSB = leftmost pixel, at `TOP−0x200 … TOP−0x101` (`0xFE00–0xFEFF` on a 64 K tape). Programs draw by writing tape cells (`poke`, or an `__at` array). The host only *renders* that page: a panel on the web, ANSI half-blocks in a native terminal. Keys arrive on `IN 0x03` as a bitmask; `VSYNC` (BIOS `0x06`) parks the machine until the host's next frame.

**Consequences.** The game is visible *inside* the tape map — the ball is a moving bit at `0xFE00+`. No new I/O device exists outside the model; rendering is observation, not interaction.

**Alternatives rejected.** ANSI cursor addressing over `CONOUT` (nothing on the tape holds the picture); a syscall-drawn display (same problem, plus hides work the 8080 should do).

### B6. Tape length is a lever, so bookkeeping regions are anchored to `TOP` and the loader sets SP

**Context.** "What happens when the tape is too short?" is a good lesson, but the memory map hard-codes `0xF000` (stack), `0xFE00` (I/O) and `0xFF00` (meta), and compiled programs emit `LXI SP,0xFDFF`.

**Decision.** `L ∈ {32K, 48K, 64K}`. Only `0x0000–0x3FFF` is fixed; the banked window ends at `TOP−0x2001`, scratch at `TOP−0x1001`, stack at `TOP−0x201`, display at `TOP−0x101`, metadata at `TOP−1`. The loader sets SP before jumping to `0x0100` (as CP/M's CCP did); the compiler stops emitting `LXI SP`. An access at or beyond `L` is a tape fault → `HALT` with reason 4.

**Consequences.** One `.com` runs at every tape size; the shell (5 KB) and every demo are tested at 32 K; the visualizer can show the head hitting the end of the tape.

### B7. The `.cursor/` agent workflow is deleted (this commit)

**Context.** `rules.mdc`, `spec.md`, `progress.md` and `remember.md` were the scaffolding for the AI-assisted build. They were the only architecture documentation, and they had drifted from the code (the spec's memory map, FSM, compiler subset and visualizer features all describe things that don't exist as written).

**Decision.** Delete the directory outright. Replacements: architecture → `docs/architecture.md`, written from the code with constants generated from source (WS0-02); decisions → this file; task tracking → `docs/v2-roadmap.md` + GitHub issues keyed by `WSn-mm`; agent rules → a short `CLAUDE.md` (WS0-05). The old files stay reachable in git history at `74a8714` and earlier.

**Consequences.** Until WS0-02 lands, `docs/v2-roadmap.md` §1 is the most accurate description of the system. Documentation must describe what *is*; anything aspirational belongs in the roadmap.

### B8. Honesty rule: every claim on the site is backed by a test or a link to the line

**Context.** The "5 test programs" were placeholders that `puts()` the expected answer; the dirty map never worked; `KS_IDLE` was unreachable; several memory-map regions were fiction. A public explainer cannot be built on that.

**Decision.** Architecture tables (addresses, syscall ids, FSM transitions, compiler features) are generated from source and checked by tests; every demo has an expected-output or golden-frame test; the site footer shows the built commit and code claims link to file:line at that commit.

### B9. Commit conventions: no co-author or tool trailers

**Context.** Automated tooling likes to append `Co-Authored-By:` and similar attribution trailers to commits.

**Decision.** Commit messages carry **no** `Co-Authored-By:`, `Generated-by:`, session links, or any other tool/AI attribution trailer. Subjects are imperative and ≤ 72 characters; the body says *why*. Noisy work-in-progress is squashed into one commit per logical change before it lands on `main`. `make test` must be green for every commit on `main`. Any change to an interface (port, syscall, memory map, file format, HAL, JS API) appends an entry to this file in the same commit.

---

## Part C — Pitfalls worth remembering (from the old `remember.md`)

- **8080 flags.** Auxiliary carry is a carry out of bit 3; parity is *even* parity of the result; `DAA` depends on both AC and CY; `PUSH PSW`/`POP PSW` pack flags as `S Z 0 AC 0 P 1 CY` (bit 1 always set, bits 3 and 5 always clear); `CMP` sets flags but must not write `A`. Each has a dedicated test in `tests/emu/`.
- **Terminal state.** If native raw mode is enabled (WS1-14) it must be restored on `HALT`, on `SIGINT`, and on every error path — a stuck terminal is the classic emulator bug.
- **Locals on the 8080 stack.** After `CALL`, `SP` points at the return address; the first local lives at `SP+2`, not `SP+0` — the original codegen overwrote the return address.
- **Logical-op scratch.** The current compiler parks `&&`/`||` intermediates at the fixed address `0x20FC` inside the TPA. Any program whose code crosses that address corrupts itself; WS1-13 removes it.
