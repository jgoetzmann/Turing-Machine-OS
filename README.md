# TuringOS

An operating system built out of a Turing machine's parts. The tape is a byte array, the head is an
Intel 8080's program counter, and the finite control is a six-state kernel. It boots a shell and
compiles and runs programs on the emulated CPU. One C99 core does that natively in a terminal and,
compiled to WebAssembly, in your browser, where you can watch every head move, every state transition
and every tape cell change as they happen.

### [Open the live machine](https://jgoetzmann.github.io/Turing-Machine-OS/) &nbsp;·&nbsp; no install, it runs in the page

[Architecture](docs/architecture.md) · [Design decisions](docs/decisions.md) · [What is tested and where the edges are](docs/status.md) · [Where this stops being a Turing machine](docs/turing-machine.md)

![The playground: tape map, tape strip with the head, registers, kernel FSM, 64×32 display, console](docs/img/playground.png)

## Why I built it

Can a Turing machine with a handful of tapes simulate something as large and as complicated as a
computer? The equivalence is provable, and it gets stated in a lecture and then never built, so this
is the build. An 8080 for a head, a six-state kernel for the finite control, and on top of that a
filesystem, four compilers, a shell and eventually Pong, all of it living on one, two or four byte
tapes and nothing else.

Extra tapes add no computational power to the model, only speed, which makes the tape count the second
half of the question: what do they buy once a real machine is doing the work? The two palindrome
checkers in `demos/tm/` measure exactly that. `pal1.tm` uses one tape and takes
n²/2 + 3n/2 + 1 steps; `pal2.tm` uses two and takes 3n + 3. At n = 64 that is 2,145 steps against 195,
and underneath, on the emulated 8080, 182,855 instructions against 31,353. Then the head-travel
odometer reports that giving each Turing-machine tape its own machine tape buys about 1%, because the
interpreter's own instruction fetches swamp the tape motion. The asymptotics hold and the
implementation eats most of the win. [`docs/turing-machine.md`](docs/turing-machine.md) is the full
accounting, including the four places this is deliberately not the formal model.

## What it does

The shell is written in tiny-C, the C subset this project compiles, and it runs on the emulated 8080.
Typing `cc SHELL.C` inside the running OS reproduces the shipped 2,643-byte `shell.com` byte for byte,
which `cmp` asserts in
[`tests/integration/v2_ws6_06_self_compile.sh`](tests/integration/v2_ws6_06_self_compile.sh). The
compiler's output is the OS's own command line, so a code-generation bug breaks the shell.

Four front ends share one backend and one object format: tiny-C, 8080 assembly, a Turing-machine rule
language, and Brainfuck all compile to flat `.com` images loaded at `0x0100`, from the shell or from
the editor in the browser. A Forth interpreter written in tiny-C runs on top of them, inside the
machine.

Snapshots and a replayable input log make a run deterministic and seekable. You can scrub to any step,
forwards or back, and get a byte-identical tape.

The machine underneath is an 8080 on 1, 2 or 4 tapes of 32K, 48K or 64K, with a banked window per tape,
all 256 opcodes and Intel's cycle counts. An access past the end of the tape faults and halts the
machine; `demos/fault` walks off the end on purpose, and the test asserts it faults at 32K and finishes
normally at 64K, so the tape-length lever changes the semantics rather than the picture.

## A session

```
$ make demo-disk
$ printf 'dir\ncc ADD.C\nrun ADD.COM\nhalt\n' | build/turingos --disk=build/disk/demo.img
A> HELLO.ASM
HELLO.BF
...
SHELL.C
A> A> 3 + 4 = 7
A> HALT
TuringOS halted (reason=COMMAND) after 8653 steps
```

Compiled and run on the emulated CPU, by a compiler in this repository, in 8,653 instructions.

## The parts that were hard

**Syscalls that restart instead of blocking.** A browser tab cannot block, so nothing in the core is
allowed to. A BIOS call that needs console input returns without touching a register or clearing the
pending-output flag; the kernel parks in IDLE and re-dispatches the identical CPU state when a byte
arrives, so the call completes exactly as if it had never waited. That is the same discipline a
non-blocking I/O boundary needs, and it is what makes the machine a pure function of its program, seed
and input log. ([`src/kernel/kernel.c`](src/kernel/kernel.c), [`src/bios/bios.c`](src/bios/bios.c))

**Deterministic replay, which is a correctness argument rather than save-and-load.** A seek restores
the newest snapshot at or before the target and re-runs instructions one at a time, re-applying logged
inputs at the exact steps they arrived. Getting that right meant deciding whether an input recorded at
a snapshot's own step is inside or outside it, suppressing output for steps the host has already seen
while buffering output for steps being run for the first time, refusing a seek whose program image has
been recycled rather than silently replaying a different binary, and restoring the caller's machine
when a seek cannot land. ([`src/api/api.c`](src/api/api.c),
[`tests/kernel/test_v2_seek_fidelity.c`](tests/kernel/test_v2_seek_fidelity.c))

**A code generator for a CPU with no multiply, divide, shift or 16-bit compare.** All four had to be
written by hand as runtime routines, emitted lazily and to a fixed point because they call each other.
The 16-bit restoring division keeps its loop counter across a body that clobbers the accumulator, and
signed division and modulo follow C99's truncate-toward-zero rule. Locals live on a stack the 8080
cannot index directly, so the prologue's push depth has to stay exact or every local offset is wrong.
([`src/compiler/compiler.c`](src/compiler/compiler.c))

**One core, two runtimes.** Every host dependency sits behind 17 functions in
[`src/hal/hal.h`](src/hal/hal.h): no `stdio.h`, no allocation, no blocking and no clock anywhere in the
machine core. The native and WebAssembly builds differ by exactly one source file. The constraint chain
is the interesting part: a page that cannot block forces frame pacing out to the host loop, which makes
the machine deterministic, which is what made replay possible at all.

## How I know it works

- `make test` builds everything and runs the native suite: 43 C test programs holding 234 named cases,
  plus 40 shell tests that drive the real binary. `make test-web` runs 63 Node tests against the actual
  WebAssembly build. `make test-e2e` drives the built site with 59 Cypress tests in a headless browser.
- CI runs the native suite on Ubuntu and macOS, then again under AddressSanitizer and
  UndefinedBehaviorSanitizer, then builds the wasm, the site, and runs both browser suites.
- Tests are named after the behavior they pin rather than the function they call:
  `TEST("WS5-01: 16-bit arithmetic wraps", ...)`. 63 distinct behavior ids from the v2 checklist are
  cited by name across the three suites, and [`docs/v2-roadmap.md`](docs/v2-roadmap.md) is that
  checklist published with its audit verdicts left in: 53 items met, 35 partial with the specific gap
  written next to each, 1 not done.
- The constants table and the finite-state-machine table in `docs/architecture.md` are generated from
  `src/tos.h` and from the kernel's own transition table. A test regenerates both and fails on any
  drift, so those tables cannot quietly stop being true.
- The Turing-machine compiler is checked three ways at once: the compiled program running on the
  emulator, an independent 390-line Python interpreter of the same language
  ([`tools/tm_ref.py`](tools/tm_ref.py)), and checked-in expected output. The Busy Beaver demos are
  pinned to values nobody can fudge: Σ(2) = 4 ones in 6 steps, Σ(3) = 6 in 14, Σ(4) = 13 in 107.
- Disassembling the 2,643-byte shell image and reassembling the listing produces identical bytes. That
  only works because every undocumented alias byte has its own spelling, which is a bug the round trip
  found and unit tests had missed.

## What it is not

The tape is finite, so this is not a Turing machine in the formal sense; a machine with bounded memory
is a finite automaton with a very large state set. `docs/turing-machine.md` lists all four places the
implementation and the model part company, and why each one was chosen.

Cycle counts come from Intel's table, per instruction, including the taken and not-taken split for
conditional `CALL` and `RET`. There is no bus timing, no wait states and no interrupt delivery, so this
counts cycles; it is not a cycle-accurate hardware model. The emulator has not been run against the
standard 8080 exercisers.

The compiler is not self-hosting. `cc` inside the OS is a host-side ROM service reached through one
`OUT 01H`, by design ([`docs/decisions.md`](docs/decisions.md) A7). What the shell demonstrates is a
reproducible fixed point across two very different code paths, not a compiler compiling itself.

tiny-C has no pointers, structs, `switch`, `?:`, `sizeof` or floats, arrays are globals only, and it is
single-pass with no optimisation beyond constant folding. `docs/tiny-c.md` says what is missing and
`docs/decisions.md` B16 says why. Everything in `src/` is statically allocated, which fixes the
WebAssembly heap at build time and makes determinism cheap, at the cost of one machine per process.

[`docs/status.md`](docs/status.md) is the full list of limits with the measured numbers behind them.

## Run it

```sh
make test        # build the native binary and tools, run the C and shell suites
make run         # boot the OS in this terminal: try `help`, `dir`, `halt` at the A> prompt
make web         # WebAssembly + site into web/dist (needs Emscripten 6.0.9 and Node >= 22)
make help        # every target, including the wasm and browser suites
```

`make run` boots a blank disk. `build/turingos --disk=build/disk/demo.img` boots the one with the demos
on it, and `build/turingos --help` lists every lever: tape count and length, clock, seed, disks, trace
and snapshots. Runs are scriptable and deterministic, which is how the test suite drives the machine:
`printf 'dir\nhalt\n' | build/turingos --disk=build/disk/demo.img`.

## Repository map

| Path | What |
|---|---|
| `src/emu/` | the 8080, the tapes and the disassembler |
| `src/kernel/` | the finite state control, the trace ring, snapshots |
| `src/bios/`, `src/fs/` | syscalls behind `OUT 01H`; a flat CP/M-style filesystem: 8.3 names, 64 directory entries |
| `src/hal/` | the host boundary: a POSIX terminal, or JavaScript |
| `src/compiler/`, `src/lang/` | tiny-C, the assembler, the Turing-machine language, Brainfuck |
| `src/shell/` | the shell, in tiny-C, compiled by this repository's own compiler and run on the emulated 8080 |
| `src/api/` | `tos_*`, the one embedding API used by the CLI, the tests and the wasm build |
| `tools/` | `mkdisk`, `asm`, `tmc`, `bfc`, `disasm`, `bench`, and the Python reference interpreter |
| `demos/` | the programs on the site, each with a README and a guided tour |
| `web/` | the Vite and TypeScript site, and the visualizer |
| `tests/` | ten directories, one per area; 83 tests, named after the behavior they pin |

## Documentation

Two worth opening first. [Design decisions](docs/decisions.md) is the five choices that shaped the
machine, each with its consequences and the alternatives that were rejected, and a one-line reference
for everything else that got decided.
[The machine as a Turing machine](docs/turing-machine.md) is what the model asks for, what this does
instead, and what measurably changes when you add tapes.

The rest: [Architecture](docs/architecture.md) (the memory map, ports, syscalls, formats, generated
constants), [Status](docs/status.md) (what runs where, what is tested, every known limit),
[Levers](docs/levers.md), [Languages](docs/languages.md), [tiny-C](docs/tiny-c.md),
[8080 assembler](docs/asm.md), [the TM language](docs/tm.md).

## How this was built

v2 was written against a frozen specification. The interfaces in `src/tos.h` and the headers under
`src/**/*.h` were fixed first and treated as law, and the tests were written from the specification
rather than from the implementation, so a test failing means the code disagrees with the spec rather
than with itself. The work was developed on a branch and squash-merged, which is why one commit
carries most of the code.

What happened after that is the part worth reading. The machine was audited adversarially against its
own documentation, and the audit found real defects: the 8080's auxiliary carry was computed as a
half-borrow rather than as the carry out of bit 3 of the two's-complement add, so `SUB`, `SBB`, `CMP`
and `DCR` all set it wrong and `ANA` used the 8085's rule; a seek to step 0 resurrected the anchor
snapshot, which is the machine as it was before any program was loaded. The fixes were then audited in
turn and the second pass found more. `git log 96b054b..HEAD` is that trail, with the root cause in each
commit body. Two fixes did not ship, because every test I could write for them passed against the
unfixed code, and a test that cannot fail is what the audit kept finding.

`make test` has to pass before anything lands, and any change to a port, a syscall, a memory-map
region, a file format or the embedding API gets an entry in `docs/decisions.md` in the same commit.

## License

MIT. See [LICENSE](LICENSE).

[![CI](https://github.com/jgoetzmann/Turing-Machine-OS/actions/workflows/ci.yml/badge.svg)](https://github.com/jgoetzmann/Turing-Machine-OS/actions/workflows/ci.yml)
[![Pages](https://github.com/jgoetzmann/Turing-Machine-OS/actions/workflows/pages.yml/badge.svg)](https://github.com/jgoetzmann/Turing-Machine-OS/actions/workflows/pages.yml)
