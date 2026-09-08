# CLAUDE.md: working rules for TuringOS

TuringOS is an operating system that *is* a Turing machine: an Intel 8080 head on 1-4 byte tapes,
a six-state kernel, a BIOS behind `OUT 01H`, and one host abstraction layer. One C99 core builds
natively and to WebAssembly; the site under `web/` runs the real machine.

## Commands

- `make test`: build everything, run every C and shell test. Run it before calling anything done.
- `make test-web`: Node tests over the wasm build (needs Emscripten 6.0.9 and Node >= 22).
- `make test-e2e`: Cypress drives the built site in a headless browser (needs `npm ci` in `web/`; the browser
  binary lands in `web/node_modules/.cache/cypress`, never in a user-wide cache).
- `make web`: wasm + demo disk + generated JSON + `npm ci && npm run build` in `web/`.
- `make run`: boot the OS in this terminal. `make help` lists every target.
- Single test: `cc -std=c99 -Wall -Wextra -Werror -pedantic -I./src tests/<area>/test_x.c build/libtos.a -o build/tests/x && build/tests/x`.

## Hard constraints (the Turing-machine mapping)

- **Tape** = `src/emu/mem.c`. All machine state is static; no `malloc` anywhere in `src/`.
- **Head** = the 8080 PC. Addresses are `uint16_t` everywhere. The head moves only via `cpu_step`.
- **States** = `kernel_state_t`. Only `src/kernel/kernel.c` assigns `state`, and only through the
  12 transitions in `kernel.h`. Do not add states or transitions without a `decisions.md` entry.
- **Transition function** = `cpu_step()` + `bios_dispatch()`. Nothing else changes machine state.
- **Environment** = `src/hal/hal.h` only. No `<stdio.h>` outside `src/hal/hal_posix.c`, `tools/`,
  `tests/`, and the path-based `cc_compile()` wrapper in `src/compiler/compiler.c`.
- The core never blocks and never reads a clock. `kernel_step` never sleeps. Given the same program,
  input log, seed and levers, every run is byte-identical at every step.
- `src/tos.h` and the headers under `src/**/*.h` are frozen interfaces. Constants, ports, syscall ids,
  the memory map, halt reasons, key bits and lever ids are law; `compiler.h` enums may be appended to.
- C99 with `-std=c99 -Wall -Wextra -Werror -pedantic`. No VLAs, no GNU extensions, no C11.
- Programs are flat `.com` images at `0x0100`. The loader sets SP; compilers never emit `LXI SP`.
- Tapes are 1, 2 or 4 × 32K/48K/64K; only `0x4000 … L−0x2001` is per-tape. Regions above the TPA are
  anchored to the top of the tape. Never hard-code `0xFE00` or `0xFF00` in new code.

## Where things are

| Path | What |
|---|---|
| `src/emu/`, `src/kernel/`, `src/bios/`, `src/fs/` | CPU + tapes, FSM + trace + snapshots, syscalls, filesystem |
| `src/hal/`, `src/api/` | host boundary (POSIX / wasm); the `tos_*` embedding API |
| `src/compiler/`, `src/lang/` | tiny-C; assembler, TM language, Brainfuck (host "ROM services") |
| `src/shell/shell_tpa.c` | the shell, written in tiny-C, runs on the 8080 |
| `tools/`, `demos/`, `web/`, `tests/` | host CLIs, demo programs, the site, the test suite |

Docs: `docs/architecture.md` (what is), `docs/decisions.md` (why), `docs/levers.md`, `docs/tiny-c.md`,
`docs/asm.md`, `docs/tm.md`, `docs/languages.md`, `docs/status.md`, `docs/turing-machine.md`.
`docs/v2-roadmap.md` is the plan and is background only. Docs describe what exists; nothing aspirational.

## Testing conventions

- C tests: `tests/<area>/test_v2_*.c`, `#include "../testfw.h"`, `TEST("WSn-mm: title", fn)`,
  `RUN_ALL_TESTS()`, print `PASS: <stem>` and return 0. Linked against `build/libtos.a`.
- Shell tests: POSIX `sh` under `tests/**/*.sh`, run from the repo root, print `PASS: <name>`.
  Step-bounded: use `--stdin-script=` or piped input ending in `halt`, never timeouts.
- Web tests: `web/test/*.test.mjs` with `node --test` against `web/public/turingos.js`; browser tests:
  `web/cypress/e2e/*.cy.js`, one file per area, asserting what the user sees.
- Every behavior id (`WSn-mm`) in the spec has at least one test that cites it.
- The constants table in `docs/architecture.md` is `build/dump_constants --markdown` output; a test
  compares them. Change `src/tos.h`, then regenerate. Never hand-edit the table.

## Commit conventions

- Imperative subject, at most 72 characters; the body says *why*.
- **No `Co-Authored-By`, `Generated-by`, session links, or any other tool attribution trailers in
  commit messages or pull requests, ever.**
- `make test` is green for every commit on `main`. Squash work-in-progress before it lands.
- Any change to a port, syscall, memory-map region, file format, the HAL or the JS API appends an
  entry (Context / Decision / Consequences / Alternatives) to `docs/decisions.md` in the same commit.
- The site makes no claim that is not backed by a test or a link to the line of code.

## Things that bite

- 8080 flags: AC is carry out of bit 3; parity is even parity; `PUSH PSW` packs `S Z 0 AC 0 P 1 CY`.
- After `CALL`, the first local is at `SP+2`, not `SP+0`.
- A program's `HLT` returns to the shell (transition 5); only the shell's `HLT` halts the machine.
- Raw terminal mode must be restored on every exit path, including `SIGINT`.
- Syscalls are not steps; port reads are. `TICKS` counts VSYNC frames, never time.
