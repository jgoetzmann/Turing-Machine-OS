# TuringOS design decisions

Five decisions shaped the machine, and they are written out in full below: **Context → Decision →
Consequences → Alternatives rejected.** Everything else that got decided is one line each, in the
reference at the bottom, because the docs, the source and the tests cite these ids and the ids have to
keep resolving. Ids are stable and never reused; B7 was about tooling rather than about the machine and
went away with the tooling. Part C is the details that bite whoever changes this code next. `git log`
has the reasoning that did not fit.

---

## The five that shaped the machine

### A1. The Turing-machine mapping is a hard constraint, not a metaphor

**Context.** The whole point of the project is that the OS *is* a Turing machine. Every subsystem must map onto a formal TM component or it doesn't belong.

**Decision.**
- **Tape** = the flat byte array in `src/emu/mem.c` (64 KB; per-tape banks in v2, see B4). Cells are bytes; the alphabet is `0x00–0xFF`.
- **Head** = the 8080 program counter plus the memory bus. The only way to move the head is to execute an 8080 instruction.
- **States** = `kernel_state_t` in `src/kernel/kernel.h` (`BOOT, IDLE, SHELL, RUNNING, SYSCALL, HALT`). Only `kernel.c` may assign `state`.
- **Transition function** = `cpu_step()` + `bios_dispatch()`. No other code changes machine state.
- **Environment** (v2) = everything behind `src/hal/hal.h`: console, keys, disk bytes, time. The machine can observe and write to the environment only through BIOS calls and I/O ports.

**Consequences.** No host allocations for machine state (A4); no virtual memory, paging or segmentation; addresses are `uint16_t` everywhere; the kernel loop is a switch over states and nothing else.

**Alternatives rejected.** Treating "TM" as a design vibe and using an ordinary emulator + OS split. That is simpler, but then the visualizer would be showing a metaphor.

### A4. No heap anywhere in the machine

**Context.** The machine's memory *is* the tape. Host allocations for kernel or emulator state would put part of the machine outside the model.

**Decision.** `malloc`/`calloc`/`realloc` are forbidden in `src/emu`, `src/bios`, `src/kernel`, `src/fs`. Everything is statically sized: open-file table (16), BIOS output ring (4 KB), directory (64 entries), snapshot/trace rings in v2. The compiler (`src/compiler/`) is a host tool and is exempt, though today it also uses static arrays.

**Consequences.** Every limit is a documented constant; WebAssembly memory can be sized exactly at build time (B1); determinism is easy to guarantee (WS1-16). The cost is one machine per process: the tapes, the kernel and the filesystem are file-scope statics, so two machines cannot coexist in one address space.

### B4. Multiple tapes are a banked window, CP/M 3 style

**Context.** A k-tape Turing machine has one control and k tapes and heads. The 8080 has one address bus, and the spec's "kernel heap", "shell workspace" and "FS cache" regions (`0x4000–0xEFFF`) have never been used by anything.

**Decision.** With tape count k (1, 2 or 4), addresses `0x4000 … TOP−0x2001` (`0x4000–0xDFFF`, 40 KB on a 64 K tape) exist once per tape; everything else (BIOS vectors, TPA, scratch, stack, display, metadata) is common. `OUT 0x02, A=n` selects the tape used for every access (fetch and data) in that window; `IN 0x02` reads it back; selecting `n ≥ k` faults (halt reason 6). Boot and `RUN` reset to tape 0.

**Consequences.** A program's code and stack never disappear under the PC; a TM, Brainfuck or Forth "inner tape" gets its own bank and the visualizer can stack k strips; the 1-tape against 2-tape palindrome demo (WS6-05) becomes a real measurement rather than an animation. This is also historically how 8080 and Z80 systems escaped 64 KB.

**Alternatives rejected.** Switching the whole address space (code vanishes under the head; CP/M 3 needed a "common area" for exactly this reason); k CPUs (that's k machines, not a k-tape machine); making the alphabet or cell size a lever (the 8080 fixes a cell at 8 bits).

### B1. One engine: the C core compiles to WebAssembly and the website runs it

**Context.** The site needs an interactive machine. The temptation is a JavaScript re-implementation for the demo and the "real" C for everything else.

**Decision.** Build the same `src/` with Emscripten (`make wasm`) and drive it from TypeScript. The browser runs the identical emulator, BIOS, kernel, filesystem and compiler that `make test` exercises; `make test-web` runs the same program suite through the wasm build and requires byte-identical output.

**Consequences.** Requires B2, so nothing in the core may block or touch files. Threads are unavailable on GitHub Pages (no COOP/COEP headers, so no `SharedArrayBuffer`), so the engine runs on the main thread with a per-frame step budget.

**Alternatives rejected.** A JavaScript rewrite (two emulators drift, and the site would be lying about which one you are watching); a Python runtime in the browser (heavy, fragile, and still leaves two visualizers).

### B26. Time travel refuses rather than reconstructing the wrong machine

**Context.** The API keeps the last few loaded program images so a seek can replay a `tos_load_com`. With only four slots, the fifth load overwrote the first, and a seek into the first program silently replayed a later one. A failed seek also left the machine stranded at whatever step the replay reached, forward seeks suppressed console output the host had never seen, replays pushed the same trace events a second time, the step-0 anchor was evicted once the ring wrapped, and the dirty-page map still described writes from the abandoned future.

**Decision.** Eight images are kept, each tagged with the serial number of the load that filled it; a replayed load whose slot has been recycled fails the seek instead of loading a different program. A seek that cannot reach its target puts the machine back where it was. Output is suppressed only for steps the host has already seen. The trace is disabled during a replay. Snapshot slot 0 holds the first snapshot forever and the other 31 rotate. Restoring a snapshot clears every page age stamped after that step. The snapshot interval is honoured inside a long `kernel_step` call, so it really does bound how far a seek has to replay.

**Consequences.** A seek that returns 0 has reconstructed exactly the machine of that step, and the output of the steps it re-ran for the first time is delivered only then; a seek that returns −1 has changed nothing the caller can see, except that it puts the machine back by replaying, which needs the input log. Under the CLI, where console bytes come from the host's stdin rather than the log, that last step can itself fail and leave the machine at the nearest step it could reach. The trace ring is not rewritten: events from an abandoned future stay in it, with their original step numbers. `tests/kernel/test_v2_seek_fidelity.c` covers each case.

**Alternatives rejected.** Keeping every program image ever loaded (unbounded static memory); silently loading the closest image (the failure this fixes).

---

## Everything else, one line each

The mechanism behind each is in the code the line names. These ids are cited from the other documents,
from source comments and from test names, so they stay here whatever else changes.

**A2. Intel 8080, not Z80.** Z80-only instructions are errors rather than extensions, which keeps the emulator small enough to test opcode by opcode. Rejected: the Z80 (bigger surface, no gain for the TM story), the 6502, an invented ISA.

**A3. Programs are flat `.com` images at `0x0100`, with absolute addresses.** The 8080 has no PC-relative jump, so absolute at a fixed base is the honest choice. No relocation, no linker, no symbol table.

**A5. BIOS calls are `OUT 0x01` with the function id in `A`.** The syscall is one visible, interceptable instruction and shows up cleanly in the trace. Real CP/M binaries do not run. Rejected: a jump table at `0x0000`, `RST n`.

**A6. The shell is a tiny-C program that runs on the emulated 8080.** Compiled by this project's own compiler to a 2,643-byte image, so the compiler's output is the OS's own command line.

**A7. The C compiler is a host-side "ROM service", not an 8080 program.** `cc` raises BIOS `0x16` and the host does the work, so the docs have to say plainly which tools run on the host and which on the 8080. This is not self-hosting.

**A8. C99, no C11, no extensions.** `-std=c99 -Wall -Wextra -Werror -pedantic`, so gcc, clang and Emscripten all build it and the sanitizer job needs no special-casing.

**B2. A host abstraction layer is the machine's only door to the world.** Every host touchpoint is declared in `src/hal/hal.h` and nothing in the core includes `<stdio.h>`, which is what makes the machine a pure function of program, input log and seed.

**B3. There is one visualizer, and it reads machine memory directly.** Offsets come from `layout.json` and `constants.json`, generated from the headers, so a struct change moves the panels instead of breaking them silently. Rejected: a second native GUI, polling a snapshot file.

**B5. The display is tape.** A 64×32 1-bpp framebuffer at `TOP−0x200`: programs draw by writing tape cells and the host only renders that page, so the ball in Pong is a moving bit in the tape map. Rejected: ANSI cursor addressing, a syscall-drawn display.

**B6. Tape length is a lever, so bookkeeping regions are anchored to `TOP` and the loader sets SP.** One `.com` runs at 32K, 48K and 64K, and an access at or beyond `L` is a tape fault.

**B8. Honesty rule: every claim on the site is backed by a test or by a named file.** The architecture tables are generated from source and a test fails if the document drifts; every doc page links to its Markdown source at the built commit.

**B9. Commit conventions: no co-author or tool trailers.** Imperative subjects of at most 72 characters, the body says why, work in progress is squashed, and any interface change appends to this file in the same commit.

**B10. The site is a hash-routed single page; docs are Markdown rendered at build time.** `#/<slug>` for every `docs/<slug>.md`, and the playground's whole configuration lives in the hash query. Rejected: History-API routing with a `404.html`, fetching Markdown at run time, a static-site generator.

**B11. Time travel is a snapshot ring plus input-log replay.** 32 full snapshots taken every `snap_interval` steps and a 4,096-entry input log; a seek restores the nearest earlier slot and re-applies inputs at the steps they arrived. Rejected: per-instruction undo, re-running from boot, a snapshot per step.

**B12. Ports 4 and 5 publish the tape count and the tape length.** A binary discovers its own geometry with an ordinary instruction, visible in the trace, rather than a syscall. Rejected: a BIOS call, a fixed metadata address, values passed in registers at load.

**B13. TM tape placement.** TM tape j lives on machine tape `j mod k` at bank offset `(j div k) × 8192` with the head at `+4096`, and a machine that needs more tapes than fit fails to compile. Rejected: interleaving TM tapes cell by cell, allocating regions at run time.

**B14. The loader sets SP; no compiler emits `LXI SP`.** B6 made the top of the stack depend on `L`, so a program that sets SP itself is tied to one tape length. Rejected: a relocation header, computing SP in every prologue.

**B15. Blocking policy: the kernel never blocks; the POSIX HAL blocks only on piped stdin.** Tests are step-bounded and never spin, and the wasm HAL never blocks at all. Rejected: threads, `select()` inside the kernel, a separate native kernel loop.

**B16. Tiny-C v2 scope: 16-bit `int`, unsigned `char`, global arrays only.** No pointers, structs, `switch`, `?:`, `sizeof`, floats or local arrays. Rejected: pointers (register pressure on the 8080 and a much larger compiler), local arrays (frame-relative addressing the 8080 does badly), 8-bit `int`.

**B17. Every machine starts with a step-0 anchor snapshot; loading a program adds another.** The timeline can always go back to the start, and a target beyond the point where the machine parked waiting for input is refused because it cannot be reproduced.

**B18. Turing-machine "label states" print their name; silent halts stay silent.** A state that never appears on a rule's left-hand side prints its name when entered, which is how the palindrome checkers say `yes` or `no`. Rejected: a `print` directive in rules, printing the halting state unconditionally.

**B19. Brainfuck programs clear their cells in the prologue.** The banked window keeps whatever the previous program left, and the classic hello-world assumes zeros.

**B20. The native exit line starts on its own line.** A line beginning `A> TuringOS halted…` broke every `tail -1` check.

**B21. Life runs at what an 8080 can do, and the criterion says so.** 16-bit `HL` arithmetic costs about 290,000 instructions a generation, so the acceptance criterion is 500,000 and a test enforces it.

**B22. Addresses typed into the playground are hexadecimal; addresses in URLs are decimal.** The breakpoint form parsed bare digits as decimal, so `0104` became 104 and the breakpoint never fired.

**B23. Disk images are whole buffers behind the HAL.** Loaded once at `fs_init` and saved on flush, with sector I/O entirely inside the machine, so there is no second cache on the host side. Snapshots deliberately exclude the images.

**B24. The 8080's auxiliary carry, and 20H/30H are NOPs.** `AC` is the carry out of bit 3 of `A + ~v + !borrow`, the complement of the intuitive half-borrow; `ANA` takes it from the OR of bit 3 of both operands, the 8080 rule the 8085 breaks; `20H` and `30H` are not the 8085's `RIM` and `SIM`. Rejected: keeping the 8085 behaviour behind a lever, leaving `AC` alone because nothing in the repository reads it.

**B25. The host loop paces frames and sleeps; the kernel does neither.** `kernel_step` returns `KSTOP_VSYNC` and the caller sleeps through `hal_sleep_ms`, so the core never blocks and never reads a clock. Rejected: weakening the documented invariant instead of the code.

**B27. The clock lever is native; the browser throttles with `speed`.** Applying both made `#/playground?hz=1000` run at about 158 instructions a second while the speed control still read `max`. Rejected: two throttles multiplying into each other.

**B28. The compiler stops instead of guessing.** Depth bounds, an argument-count check and a clamped string copy replace a segfault at 550 nested calls, a read past a 16 KB buffer, and a silently dropped `__at` initialiser. Rejected: growing the parser's stack, honouring `__at` initialisers with a startup stub.

**B29. Each undocumented alias byte spells itself, so a listing re-assembles exactly.** Five bytes printed as `NOP*` and three as `CALL*`, so a `20H` inside a string literal broke the round trip. Rejected: qualifying the claim in `asm.md`, inferring the byte from context.

---

## Part C: details that bite

- 8080 flags: auxiliary carry is a carry out of bit 3; parity is *even* parity of the result; `DAA` depends on both AC and CY; `PUSH PSW`/`POP PSW` pack flags as `S Z 0 AC 0 P 1 CY` (bit 1 always set, bits 3 and 5 always clear); `CMP` sets flags but must not write `A`. Each has a dedicated test in `tests/emu/`.
- Terminal state: if native raw mode is enabled (WS1-14) it must be restored on `HALT`, on `SIGINT`, and on every error path. A stuck terminal is the classic emulator bug.
- Locals live on the 8080 stack: after `CALL`, `SP` points at the return address; the first local lives at `SP+2`, not `SP+0`. The original codegen overwrote the return address.
- **No fixed scratch address.** `&&` and `||` intermediates live in registers and on the stack. A fixed scratch cell inside the TPA corrupts any program whose code grows past it, which is why there is not one (WS1-13, B16).
- A program's `HLT` is not the machine's: in RUNNING state `HLT` reloads the shell (transition 5); only the shell's own `HLT` halts the machine (`TOS_HALT_COMMAND`). `TOS_HALT_HLT` is reserved and never written.
