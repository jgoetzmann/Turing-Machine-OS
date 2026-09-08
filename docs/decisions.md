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

**Decision.** `src/shell/shell_tpa.c` is written in the project's own C subset, compiled by the project's own compiler to `build/bin/shell.com` (2,634 bytes; the v1 shell was 5,115), and loaded into the TPA at boot. Line input uses BIOS `READLINE`/`LINEGET`/`LINELEN` so the 8080 code never handles raw keystrokes. `halt` returns from `main()` so the post-`main` `HLT` is the one that stops the machine.

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

**Consequences.** One `.com` runs at every tape size; the shell (2.6 KB) and every demo are tested at 32 K; the visualizer can show the head hitting the end of the tape.

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

### B10. The site is a hash-routed single page; docs are Markdown rendered at build time

**Context.** GitHub Pages serves static files from `/Turing-Machine-OS/` with no server-side routing, so `/architecture` would 404 on a direct visit. The docs must stay readable as plain Markdown on GitHub and there must be exactly one copy of them. The stack is Vite + TypeScript with no runtime dependencies.

**Decision.** One `index.html`; every page is a hash route (`#/`, `#/playground`, `#/architecture`, `#/decisions`, `#/demos`, `#/demos/<name>`, `#/languages`, `#/how-it-was-built`, `#/status`). `web/scripts/build-content.mjs` converts `docs/*.md` (slug = file name without `.md`, title = first `# ` heading) and `demos/<name>/README.md` (slug `demos/<name>`) with `marked` into `web/src/generated/content.ts` at build time, and copies `tour.json` and the demo sources into `tours.ts` / `demos.ts`. The playground's whole configuration lives in the hash query (`?demo=…&tapes=…&len=…&hz=…&seed=…&input=…&disks=…&trace=…&speed=…&bp=…`).

**Consequences.** Deep links work with no 404 fallback tricks; a URL fully describes a machine configuration and can be pasted into an issue; no Markdown parser ships to the browser; `docs/` is the single source for the site and for GitHub. The router has to render generated HTML, so the content build runs before `tsc` and `vite build`.

**Alternatives rejected.** History-API routing with a `404.html` redirect (fragile on Pages); fetching `.md` at runtime (a parser in the bundle, and the docs would render differently in two places); a static-site generator (another toolchain for nine pages).

### B11. Time travel is a snapshot ring plus input-log replay

**Context.** The timeline scrubber has to show the machine at an arbitrary earlier step. Storing every step is impossible, and the 8080 has no cheap "undo" per instruction. The machine is already a pure function of its inputs (B2).

**Decision.** A 32-slot ring of full snapshots (all tapes, `cpu_t`, the whole `kernel_t`, BIOS and FS state — not disk images, ages or trace) is taken every `snap_interval` steps. `tos_con_push` and `tos_keys_set` append `{step, kind, value}` to a 4,096-entry input log in `api.c`. `tos_seek(step)` restores the newest slot at or before `step` (returns −1 when there is none) and then runs one instruction at a time, re-applying logged inputs at the steps they originally arrived, until `steps == step` or the machine halts.

**Consequences.** The result is byte-identical to an uninterrupted run (WS1-10) and the same mechanism proves determinism (WS1-16). Seeking costs at most `snap_interval` instructions. No wall-clock value may ever reach the machine — `hal_time_ms` is for the host UI only and `TICKS` counts frames. Input arrival is part of machine history, so the host records *when* a byte was pushed, not just what.

**Alternatives rejected.** Reverse execution with a per-instruction undo log (touches every opcode, huge memory); re-running from boot (linear in step count); a snapshot per step (65 KB × steps).

### B12. Ports 4 and 5 publish the tape count and the tape length

**Context.** With tape count and tape length as levers (B4, B6), a single binary has to discover at run time how many tapes it has and where the top of memory is. The metadata block has this, but the block's own address depends on L.

**Decision.** Two read-only information ports: `IN 04H` returns k; `IN 05H` returns `(L / 256) & 0xFF` (`0x80` = 32K, `0xC0` = 48K, `0x00` = 64K). The kernel refreshes `io_in_ports[2..5]` before every instruction. `OUT` on these ports is ignored.

**Consequences.** The TM and Brainfuck compilers emit code that reads the ports to decide where their tapes go; the shell's `mem` command prints the map for the actual tape; no binary bakes in `0xFF00`. Port reads are ordinary instructions, so they are visible in the trace and cost one step, unlike a syscall.

**Alternatives rejected.** A BIOS call (costs a SYSCALL transition for a constant); a fixed metadata address (contradicts B6); passing the values in registers at load (lost on the first `PUSH`).

### B13. TM tape placement, and `yes`/`no` printed from the halting state

**Context.** A `.tm` program with j TM tapes must run on a machine with k machine tapes (1, 2 or 4) and a window that is 8, 24 or 40 KB depending on L. The palindrome demos need to report a verdict, not just dump a tape.

**Decision.** TM tape j (0-based) lives on machine tape `j mod k` at bank offset `(j div k) × 8192`; the head starts at `+4096`; there is no pre-fill (0 reads as blank). If `(j div k) × 8192 + 8192` exceeds the window, `tm_compile` fails with `line N: too many tapes`. On halt the program prints each tape's visited span trimmed of leading and trailing blanks, then `steps=N`, then `HLT`. A machine that wants to say something prints it *before* the dump from the rule that enters `halt` — the palindrome checkers print `yes` or `no` this way.

**Consequences.** On a 2-tape machine each TM tape gets its own strip in the visualizer, and the 1-tape vs 2-tape palindrome becomes a real measurement of head travel (WS6-05). A 32K machine holds exactly k TM tapes. Output is deterministic and diffable against `tools/tm_ref.py`.

**Alternatives rejected.** Interleaving TM tapes cell by cell on one machine tape (unreadable in the strip view); allocating tape regions at run time (no heap, by A4); printing only the final state name (says nothing to a reader).

### B14. The loader sets SP; no compiler emits `LXI SP`

**Context.** B6 made the top of the stack depend on L. The v1 compiler emitted `LXI SP,0FDFFH` in every program, tying each binary to a 64K tape.

**Decision.** Boot, `RUN` and `tos_load_com` set `SP = TOS_STACK_TOP(L)`, `PC = 0x0100` and the tape selection to 0 before the first instruction. tiny-C, the assembler's output, the TM and Brainfuck compilers never emit `LXI SP`. The value is published at `TOS_META_SP_INIT`. A tiny-C image begins with `CALL main ; HLT`.

**Consequences.** One `.com` runs on 32K, 48K and 64K tapes; the shell and every demo are tested at 32K. A program's `HLT` returns to the shell with a fresh SP. Hand-written assembly *may* set its own SP, but is then non-portable — the docs say so.

**Alternatives rejected.** A relocation header (nothing else needs one); computing SP in every program prologue from `IN 05H` (eight bytes in every binary for something the loader knows).

### B15. Blocking policy: the kernel never blocks; the POSIX HAL blocks only on piped stdin

**Context.** B2 forbids blocking inside the core, which the browser requires. Natively, a scripted run such as `printf 'dir\nhalt\n' | build/turingos` would spin on `KSTOP_WAIT_INPUT` if the HAL merely polled, and tests must never depend on timeouts.

**Decision.** `kernel_step` never sleeps or blocks. `kernel_run` handles `KSTOP_WAIT_INPUT` by asking the HAL; the POSIX `hal_con_in_ready()` does a blocking read when stdin is a pipe or a file (returning 1 for a byte *or* EOF) and polls the raw terminal when stdin is a TTY (`--raw=1`, automatic on a TTY). `--stdin-script=<file>` feeds the file's bytes then EOF. The wasm HAL never blocks: `WAIT_INPUT` returns to JavaScript, which resumes the machine on `conPush`.

**Consequences.** Tests are step-bounded and never spin; `build/turingos </dev/null` halts with `reason=EOF`; there is one kernel loop for both hosts; interactive terminals keep the key bitmask working (keys are held for 150 ms because a TTY has no key-up event). `--raw=1` on a non-TTY is harmless.

**Alternatives rejected.** Threads (unavailable on Pages, unnecessary natively); `select()` with timeouts inside the kernel (blocking in the core); a separate native kernel loop (two kernels).

### B16. Tiny-C v2 scope: 16-bit `int`, unsigned `char`, global arrays only

**Context.** v1's `int` was 8 bits, there were no arrays, no bitwise operators, no `break`/`continue`, `puts` took only literals, and `&&`/`||` used a scratch byte at `0x20FC` inside the TPA. Pong, Life, Forth and a real shell need more; a full C would not fit the 8080's register set or the project's size.

**Decision.** `int` is signed 16-bit, `char` unsigned 8-bit, all arithmetic 16-bit, signed comparisons, logical `>>`, division by zero yields 0. Arrays are global only (`int` 2-byte little-endian, `char` 1-byte), with `__at(A)` for absolute placement and `{…}`/string initialisers. Full operator set including bitwise, shifts, compound assignment, prefix and postfix `++`/`--`; `do/while`, `break`, `continue`, `else if`; recursion; ≤ 4 parameters and ≤ 32 locals on the 8080 stack; intrinsics for the console, memory, ports, BIOS and the shell services. No pointers, structs, `switch`, `?:`, `sizeof`, floats, local arrays or string variables beyond `char[]`. Diagnostics are `src.c:LINE:COL: message`. The fixed scratch address is gone.

**Consequences.** The shell, Pong, Life and Forth fit in the TPA and are written in a language a reader can hold in their head; the code generator stays a straightforward stack machine on HL/DE; the absence of pointers means no aliasing analysis and a compiler of a few thousand lines. Programs that need a buffer declare a global array.

**Alternatives rejected.** Pointers (register pressure on the 8080 and a much larger compiler); local arrays (frame-relative addressing the 8080 does badly — every access would be `LXI H,off ; DAD SP`); keeping 8-bit `int` (cannot address the tape or count past 255).

---

### B17. Every machine starts with a step-0 anchor snapshot; loading a program adds another

**Context.** The snapshot ring only records a slot every `snap_interval` steps. With the interval set to 0 ("never"), time travel would have nothing to seek to, and even with an interval the first thousand steps of a run could not be revisited.

**Decision.** `kernel_init` always stores an anchor at step 0, and `tos_load_com` stores one at the load point. `tos_seek(step)` restores the nearest earlier slot and replays the input log forward, so any step of a run is reachable; a target beyond the point where the machine parked waiting for input is refused (`-1`) because it cannot be reproduced.

**Consequences.** `tos_snapshot_count()` is 1 on a fresh machine and 2 right after a load. The timeline scrubber can always go back to the start. Replaying from step 0 costs time proportional to the target step, which is why interval snapshots still exist.

### B18. Turing-machine "label states" print their name; silent halts stay silent

**Context.** The palindrome demos need to answer *yes* or *no*. The TM language has no output statement, and adding one would make the machines less textbook.

**Decision.** A state that never appears on the left-hand side of a rule (other than `halt`) is a *label state*. When a rule transitions **into** one, the program prints the state's name and a newline before the tape dump. Running out of matching rules inside a state that has rules, or never leaving the start state, prints nothing. Tape dumps are trimmed of leading and trailing blanks so `1011` incremented prints `1100`, not `1100_`.

**Alternatives rejected.** A `print` directive in rules (not a TM concept); printing the halting state unconditionally (every no-match halt would print a spurious state name).

### B19. Brainfuck programs clear their cells in the prologue

**Context.** BF cells live in the banked window, which is zero at boot but keeps whatever the previous program left. Running `hello.bf` and then `nested.bf` in one session printed garbage because the second program inherited the first one's cells — the classic hello-world assumes zeros.

**Decision.** The compiled prologue zeroes `min(30000, window)` cells before the first command (about 120,000 instructions — you can watch the sweep in the tape map). The TM compiler does not need this: it treats byte 0 as the blank symbol and places each machine's tapes at fixed offsets.

### B20. The native exit line starts on its own line

**Decision.** `build/turingos` prints `TuringOS halted (reason=<NAME>) after <N> steps` on a fresh line unless the halt came from the `halt` command (whose `HALT` line already ended the console output). EOF and faults usually interrupt a prompt, and a line that begins with `A> TuringOS halted…` broke every `tail -1` check.

### B21. Life runs at what an 8080 can do, and the criterion says so

**Context.** The roadmap asked for Game of Life at ≥ 10 generations/s at a virtual 2 MHz. A 64×32 board is 2,048 cells; tiny-C v2 evaluates everything in 16 bits through `HL`, so even the optimised in-place generation (column sums, a running three-column window, a table lookup for B3/S23, no shifts or multiplies) costs about 444,000 instructions — roughly 0.6 generations/s at 2 MHz, dozens per second unthrottled.

**Decision.** The acceptance criterion is now "≤ 500,000 instructions per generation" and is enforced by `tests/kernel/test_v2_ws6_demos_api.c`; `docs/status.md` states the measured number. The alternative — hand-written assembly or a compiler with 8-bit arithmetic and pointer increments — is worth doing but is a different project than "a demo written in the OS's own language".

### B22. Addresses typed into the playground are hexadecimal; addresses in URLs are decimal

**Context.** The breakpoint form is labelled "(hex)" but parsed bare digits as decimal, so `0104` became 104 and the breakpoint never fired. The URL hash, which is machine-written, uses decimal for compactness and unambiguity.

**Decision.** `parseAddress` treats bare digits as hex (`0100`, `100`, `0x100`, `100H`, `$100` all mean `0x0100`; decimal needs a `d` suffix) and every breakpoint kind — address, syscall id, state — goes through it. `formatHash`/`parseHash` keep decimal in the URL and accept `0x` there too.

### B23. Disk images are whole buffers behind the HAL; the metadata block grew

**Context.** The roadmap sketched `hal_disk_read/write(disk, offset, buf, n)`. On the web the disk image is a `Uint8Array` the page owns, and on POSIX it is a file; a sector-level host API would have meant a second cache on the host side.

**Decision.** The filesystem keeps each image in a static 512,512-byte buffer (`fs_image_ptr`). The HAL only loads a whole image at `fs_init` (`hal_disk_load`) and saves it on `fs_flush` (`hal_disk_save`); JavaScript writes straight into the buffer and calls `tos_disk_reload`. Sector I/O (`READ`/`WRITE` via DMA) is entirely inside the machine. The metadata block also gained fields the visualizer needs — `TOS_META_FRAME`, `TOS_META_KEYS`, `TOS_META_STOP`, the lever mirror at `0x30…`, `TOS_META_SYSCALL`, `TOS_META_SP_INIT` — all listed in `src/tos.h` and in the generated constants table.

**Consequences.** Two disks cost 1 MB of static memory (fine natively and in the 64 MB wasm heap). Snapshots deliberately exclude the images, so time travel restores the machine but not files written since the snapshot (`decisions.md` B11).

### B24. The 8080's auxiliary carry, and 20H/30H are NOPs

**Context.** An audit compared every ALU flag against the Intel 8080 Programmer's Manual and a reference implementation. `AC` was computed as the half-borrow on `SUB`, `SBB`, `SUI`, `SBI`, `CMP`, `CPI` and `DCR`, which is the complement of what the hardware does, and `ANA`/`ANI` used the 8085 rule of always setting it. `20H` and `30H` executed as the 8085's `RIM` and `SIM`, so `20H` clobbered `A`.

**Decision.** The 8080 subtracts by adding the two's complement, so `AC` is the carry out of bit 3 of `A + ~v + !borrow`: set when there is no borrow from bit 4. `DCR` is `r + 0FFH` by the same rule. `ANA` sets `AC` to the OR of bit 3 of its two operands. `20H` and `30H` are NOPs, in the executor and in the disassembler, matching WS1-05 and `asm.md`. The assembler still accepts the `RIM` and `SIM` mnemonics, which produce those two bytes.

**Consequences.** The manual's own `SUB A` example (A=3EH leaves A=00 with Z, P and AC set) now matches, and `PUSH PSW`, `POP PSW` and a `DAA` after a subtraction see the right bit. Two legacy tests asserted the old value and were corrected. The `rim_value`/`sim_value` fields stay in the frozen `cpu_t` layout, unused.

**Alternatives rejected.** Keeping the 8085 behaviour behind a lever (a second CPU personality to test); leaving `AC` alone because no program in the repository reads it (the machine claims to be an 8080).

### B25. The host loop paces frames and sleeps; the kernel does neither

**Context.** `kernel_step` called `hal_vsync()` on the VSYNC path, which sleeps when the native display is on. `CLAUDE.md`, both frozen kernel headers and three documents say the core never blocks and never reads a clock. The `--hz` throttle also counted time spent waiting for a person as emulated time and then busy-waited to catch up, burning a core.

**Decision.** `kernel_step` returns `KSTOP_VSYNC` and does nothing else; `src/main.c` and `kernel_run` call `hal_vsync()` on that stop. The throttle sleeps through the new `hal_sleep_ms` (a no-op in the browser, which must not block) and restarts its clock after every wait for input. `hal_con_push_pending` joins `hal_con_out_pending` as a helper outside `hal.h` so the API takes back only its own unconsumed input.

**Consequences.** `tos_step` is honest about never sleeping, and a test now proves it: with the display on and one frame per second, three VSYNC stops still return in milliseconds. Throttled runs cost the CPU almost nothing, and the emulated clock keeps its meaning across pauses.

**Alternatives rejected.** Weakening the documented invariant instead of the code; adding a full timer to the HAL (`hal_sleep_ms` is the smallest thing that removes the busy wait).

### B26. Time travel refuses rather than reconstructing the wrong machine

**Context.** The API keeps the last few loaded program images so a seek can replay a `tos_load_com`. With only four slots, the fifth load overwrote the first, and a seek into the first program silently replayed a later one. A failed seek also left the machine stranded at whatever step the replay reached, forward seeks suppressed console output the host had never seen, replays pushed the same trace events a second time, the step-0 anchor was evicted once the ring wrapped, and the dirty-page map still described writes from the abandoned future.

**Decision.** Eight images are kept, each tagged with the serial number of the load that filled it; a replayed load whose slot has been recycled fails the seek instead of loading a different program. A seek that cannot reach its target puts the machine back where it was. Output is suppressed only for steps the host has already seen. The trace is disabled during a replay. Snapshot slot 0 holds the first snapshot forever and the other 31 rotate. Restoring a snapshot clears every page age stamped after that step. The snapshot interval is honoured inside a long `kernel_step` call, so it really does bound how far a seek has to replay.

**Consequences.** `tos_seek` either reconstructs the exact machine of that step or returns -1; there is no third answer. `tests/kernel/test_v2_seek_fidelity.c` covers each case.

**Alternatives rejected.** Keeping every program image ever loaded (unbounded static memory); silently loading the closest image (the failure this fixes).

### B27. The clock lever is native; the browser throttles with `speed`

**Context.** `levers.md`, `status.md` and the levers panel all say the `hz` lever throttles the native run loop and that the browser paces with its own speed control. The browser run loop applied both, so `#/playground?hz=1000` ran about 158 instructions a second while the speed control still read `max`.

**Decision.** The browser run loop uses the speed control alone. The `hz` lever still configures the machine, still travels in the URL, and still throttles `build/turingos`.

**Consequences.** A shared link behaves the way the speed control says it will. Watching a program at a virtual 2 MHz in the browser means choosing that rate with the speed slider.

**Alternatives rejected.** Applying `hz` in the browser and rewriting the three documents (two throttles multiplying into each other is hard to reason about and harder to explain).

### B28. The compiler stops instead of guessing

**Context.** The compiler segfaulted on deeply nested expressions (550 nested calls was enough), read past a 16 KB buffer when a string literal was longer than the buffer it decoded into, accepted calls with the wrong number of arguments (the callee then read its parameters from a frame that did not match), and silently dropped the initialiser of an `__at` variable.

**Decision.** Expressions nest at most 96 deep in the parser and the code generator, reported as `expression nests too deeply`. A call whose argument count differs from the declaration is `wrong number of arguments`. An `__at` variable with an initialiser is `__at variables cannot have an initialiser`, because it names memory the image does not contain. The string copy is clamped to the buffer that was actually written.

**Consequences.** Three new diagnostics, all with the usual `file:line:col: message` shape, documented in `tiny-c.md`. Nothing that compiled before compiles differently.

**Alternatives rejected.** Growing the parser's stack (the limit moves, it does not go away); honouring `__at` initialisers with a startup stub (an image whose data is written by code the user did not ask for).

## Part C — Pitfalls worth remembering (from the old `remember.md`)

- **8080 flags.** Auxiliary carry is a carry out of bit 3; parity is *even* parity of the result; `DAA` depends on both AC and CY; `PUSH PSW`/`POP PSW` pack flags as `S Z 0 AC 0 P 1 CY` (bit 1 always set, bits 3 and 5 always clear); `CMP` sets flags but must not write `A`. Each has a dedicated test in `tests/emu/`.
- **Terminal state.** If native raw mode is enabled (WS1-14) it must be restored on `HALT`, on `SIGINT`, and on every error path — a stuck terminal is the classic emulator bug.
- **Locals on the 8080 stack.** After `CALL`, `SP` points at the return address; the first local lives at `SP+2`, not `SP+0` — the original codegen overwrote the return address.
- **Logical-op scratch.** The v1 compiler parked `&&`/`||` intermediates at the fixed address `0x20FC` inside the TPA, so any program whose code crossed that address corrupted itself. v2 keeps them in registers and on the stack (WS1-13, B16); do not reintroduce a fixed scratch cell.
- **A program's `HLT` is not the machine's.** In RUNNING state `HLT` reloads the shell (transition 5); only the shell's own `HLT` halts the machine (`TOS_HALT_COMMAND`). `TOS_HALT_HLT` is reserved and never written.
