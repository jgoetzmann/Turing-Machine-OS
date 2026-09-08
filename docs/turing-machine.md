# The machine as a Turing machine

Calling something "a computer built like a Turing machine" is easy. This page is the accounting: what
the formal model asks for, what this machine actually does, which parts are a faithful realisation and
which are a deliberate departure, and what measurably changes when you give the machine more tapes.

## 1. The model, and what stands in for each part

A Turing machine is a tape of cells, a head that reads and writes one cell and moves one step, a finite
set of states, and a transition function that maps (state, symbol) to (symbol, move, state). Everything
below is that, with the sizes an implementation forces on you.

| The model | Here | Where |
|---|---|---|
| Tape: an infinite sequence of cells over a finite alphabet | 1, 2 or 4 byte arrays of 32K, 48K or 64K. The alphabet is a byte. | `src/emu/mem.c` |
| Head: reads, writes, moves one cell | The 8080 program counter. It moves only inside `cpu_step`, and every read and write goes through `mem_read` / `mem_write`. | `src/emu/cpu.c` |
| Finite control: a state from a fixed set | Six kernel states: BOOT, IDLE, SHELL, RUNNING, SYSCALL, HALT. | `src/kernel/kernel.h` |
| Transition function | `cpu_step()` for one instruction, `bios_dispatch()` for one syscall. Nothing else changes machine state. | `src/kernel/kernel.c` |
| The transition relation is finite and fixed | Twelve transitions, in a table the code walks and the visualizer draws. Adding one is a code change with a `decisions.md` entry. | `kernel.h`, `docs/decisions.md` |
| Halting | The HALT state, reachable only through the table, with a reason byte. | `TOS_HALT_*` in `src/tos.h` |
| The machine is a closed system | Everything outside the tape goes through one host boundary: console, keys, time, disks, display. | `src/hal/hal.h` |

Four places where this is not the formal model, all of them forced and all of them written down:

- **The tape is finite.** An access at or past the end is a tape fault: reads return `0xFF`, writes are
  dropped, and the kernel halts with `TOS_HALT_TAPE_FAULT`. `demos/fault` walks off the end on purpose so
  you can watch it happen.
- **A syscall does in one transition what the model would spend thousands of steps on** (reading a
  directory, compiling a file). The machine sees each one as a single `OUT 01H` instruction, and the FSM
  passes through SYSCALL, so the cost is visible even though the work is not.
- **The compilers are host code**, not programs on the tape. They are reached through `OUT 01H` like any
  other firmware routine (`decisions.md` A7). Everything the compilers *produce* runs on the tape.
- **The environment is not part of the tape.** Console input arrives through the HAL, and the machine
  parks in IDLE while it waits rather than spinning.

## 2. What had to change to make an operating system out of it

A tape and a head are not an OS. These are the parts that had to be built or bent, and each one is a
decision you can read:

**The map is anchored to the top of the tape, not to a constant.** Tape length is a lever, so the stack,
the display and the metadata block sit at `L − 0x201`, `L − 0x200` and `L − 0x100`, and the loader sets
SP from `L`. One `.com` file therefore runs unchanged on a 32K, 48K or 64K machine, which is what makes
the tape-length lever a lever rather than a rebuild (`decisions.md` B6).

**A banked window buys addressable tape beyond 64K.** The head is a 16-bit PC, so k tapes cannot all be
addressable at once. Addresses `0x0000‥0x3FFF` are common to every tape; `0x4000‥L−0x2001` is the window
into the *selected* tape, chosen with `OUT 02H`. Programs, the shell and the stack live in the common
region; only data lives in the window. Selecting a tape that does not exist halts the machine with
`TOS_HALT_BAD_TAPE` rather than reading someone else's memory.

**The state is observable, so it is real.** The metadata block at the top of tape 0 carries the state, the
step and frame counters, the last syscall, the halt reason, the levers and a dirty-page bitmap, rewritten
every `kernel_step`. Page ages record the step of the last read and write to every cell, and a head-travel
odometer sums `|addr − prev_addr|` over every access. None of that is needed to run programs; it is what
makes the machine watchable instead of merely correct.

**The core never blocks and never reads a clock.** `kernel_step` returns a stop reason (budget, halt,
waiting for input, frame, breakpoint) and the host decides what to do with it. Frame pacing and the clock
throttle live in the host loop. That one property is what makes the same C run natively and in a browser,
and what makes a run reproducible: same program, same input log, same seed, same levers, same bytes at
every step (`decisions.md` B2, B25).

**Time travel falls out of determinism.** Snapshots plus an input log let `tos_seek` restore the newest
snapshot at or before a step and replay forward. A seek either reconstructs exactly the machine of that
step or refuses (`decisions.md` B26).

## 3. What changes when you add tapes

`OUT 02H` with A = n selects tape n. The window `0x4000‥L−0x2001` then addresses tape n; everything below
`0x4000` is shared. So k tapes multiply the *data* the machine can address, not the code:

| Tapes | Window per tape | Total window | Common region |
|---|---|---|---|
| 1 × 64K | 40 KB | 40 KB | 16 KB |
| 2 × 64K | 40 KB | 80 KB | 16 KB |
| 4 × 64K | 40 KB | 160 KB | 16 KB |
| 4 × 32K | 8 KB | 32 KB | 16 KB |

The TM language uses that directly: TM tape j lives on machine tape `j mod k` at window offset
`(j div k) × 8192`, so a 2-tape TM on a 1-tape machine stacks its tapes 8 KB apart, and on a 2-tape
machine each gets its own. A TM that needs more room than the window has says so at run time
(`docs/tm.md`).

### Does a second tape actually buy anything?

Theory says yes and no: a k-tape machine is no more *powerful* than a 1-tape machine, since one tape can
simulate k of them, but the simulation costs a quadratic factor in time, and that factor is head travel.
`demos/tm` has the textbook case: `pal1.tm` checks a palindrome with one tape by erasing the leftmost
symbol, walking to the right end, checking it, and walking back; `pal2.tm` copies the input to a second
tape and then reads one forwards while reading the other backwards.

TM steps, measured by running both programs on inputs of increasing length:

| Input length | `pal1` (1 tape) | `pal2` (2 tapes) |
|---|---|---|
| 8 | 45 | 27 |
| 16 | 153 | 51 |
| 32 | 561 | 99 |
| 64 | 2,145 | 195 |

Those are exactly n²/2 + 3n/2 + 1 and 3n + 3. The one-tape program quadruples when the input doubles;
the two-tape program doubles. That is the quadratic-versus-linear separation, on a machine you can watch
do it.

### What it costs on the 8080 underneath

The TM programs are compiled to 8080 code, so each TM step is a few dozen instructions of interpreter.
Running the 64-character palindrome:

| Program | Machine tapes | 8080 instructions | Cycles | Head travel (cells) |
|---|---|---|---|---|
| `pal1` (1-tape TM) | 1 | 182,855 | 1,595,665 | 2,517,349,948 |
| `pal1` (1-tape TM) | 2 | 182,856 | 1,595,672 | 2,517,349,772 |
| `pal1` (1-tape TM) | 4 | 182,857 | 1,595,682 | 2,517,350,124 |
| `pal2` (2-tape TM) | 1 | 31,353 | 275,494 | 567,049,210 |
| `pal2` (2-tape TM) | 2 | 31,354 | 275,501 | 559,528,330 |
| `pal2` (2-tape TM) | 4 | 31,355 | 275,511 | 559,529,578 |

Three things fall out of that table, and only the first is the one people expect:

1. **The algorithm dominates.** The two-tape program does 195 TM steps where the one-tape program does
   2,145, and it finishes in a fifth of the 8080 instructions even though each of its steps is more
   expensive (161 instructions per TM step against 85).
2. **Extra machine tapes do nothing for a program that does not use them.** `pal1` is a one-tape TM: at
   k = 1, 2 and 4 it costs the same to within two instructions, and that difference is the
   compare-and-jump chain (`IN 04H`, `CPI`/`JZ`, one `JMP`) the compiled program runs once at start-up
   to place its tape according to k. The `OUT 02H` tape select runs before every cell access and costs
   the same at every k.
3. **Giving each TM tape its own machine tape is worth about 1%,** not the factor the TM-step table
   suggests. On one machine tape, `pal2`'s two tapes sit 8 KB apart, so every alternation moves the head
   8,192 cells; on two machine tapes both live at `0x4000`, so switching costs nothing. That saves 7.5 M
   cells of travel out of 567 M, because the head-travel odometer counts *every* access, and the
   interpreter's own instruction fetches swamp the TM's data motion. The asymptotic win is in the number
   of steps, not in where the tapes are.

Reproduce any row: `make demo-disk`, then `build/turingos --tapes=2 --disk=build/disk/demo.img`, `tm PAL2.TM`,
`run PAL2.COM`, and read the step and travel counters in the stats panel or the `mem` command. The
playground does the same thing with the tapes lever, and the tape map shows the second tape filling up.
