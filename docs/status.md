# Status

TuringOS 2.0.0. One C99 core, built natively (`make`) and to WebAssembly (`make wasm`); the site at <https://jgoetzmann.github.io/Turing-Machine-OS/> boots the same machine. This page says what runs where, what is tested, and where the edges are. Nothing on it is aspirational; the plan lives in `v2-roadmap.md`.

## What runs where

| On the host (C, "ROM") | On the emulated 8080 |
|---|---|
| 8080 emulator, tapes, kernel FSM, trace, snapshots | the shell (`src/shell/shell_tpa.c`, tiny-C) |
| BIOS syscalls, filesystem, disk images | every demo: `hello/*`, `pong`, `life`, `fault`, `forth` |
| tiny-C compiler, assembler, TM compiler, Brainfuck compiler | every program those compilers produce, including the TM and BF programs |
| HAL: terminal or JavaScript, files or `Uint8Array`s | — |
| Visualizer, editor, console UI (TypeScript) | — |

The compilers are ROM services by design (`decisions.md` A7, `languages.md`); a self-hosting `cc` is not planned for this version.

## Builds

| Target | Produces | Needs |
|---|---|---|
| `make` / `make all` | `build/turingos`, host tools | gcc or clang |
| `make shell` | `build/bin/shell.com` (compiled by the project's own compiler) | — |
| `make gen` | `build/gen/{shell_blob.c,constants.json,layout.json}`, copied to `web/src/generated/` | — |
| `make demo-disk` | `build/disk/demo.img`, `web/public/demo.img` | — |
| `make wasm` | `web/public/turingos.{js,wasm}` | Emscripten 6.0.9 (`web/emsdk-version.txt`) |
| `make web` | `web/dist/` | Node ≥ 22, npm |
| `docker compose up` | runs `make test` in `emscripten/emsdk:6.0.9` + gcc (not exercised by CI; built by hand) | Docker |

## Tests

- `make test` builds everything and runs `tests/run_tests.sh`: every `tests/**/test_*.c` compiled standalone against `build/libtos.a`, plus every `tests/**/*.sh`. It prints `PASS: <name>` per test and `N/N passed` at the end, and exits non-zero on any failure.
- `make test` runs 81 tests (42 C files and 39 shell tests); `make test-web` runs 63 Node tests over the wasm; `make test-e2e` runs 55 Cypress tests in a headless browser. The v2 spec lists 60 behavior ids (`WSn-mm`); every one is cited by at least one test.
- `make test-web` runs `web/test/*.test.mjs` with `node --test` against the real wasm: boot, a shell session (`dir`, `cc`, `run`, `halt`), every demo's `.expected` output, and `layout.json` against the wasm's actual struct offsets.
- `make test-e2e` builds the site and drives it with Cypress: the thirteen panels mount and draw, the toolbar
  runs, steps, resets and reports, breakpoints fire, time travel seeks, the levers rebuild the machine without
  losing the disk, every demo loads and runs, and the editor compiles, saves and reports errors on the right line.
- CI (`.github/workflows/ci.yml`) runs `make test` on ubuntu and macos and once more with `-fsanitize=address,undefined`, then the wasm build, the Node tests, the site build and the Cypress suite. `pages.yml` builds the wasm and the site and deploys on push to `main`.

Coverage by area (behavior ids from the spec):

| Area | Covered |
|---|---|
| Emulator | cycle table for all 256 opcodes (WS1-04), lengths and undocumented aliases (WS1-05), disassembler (WS1-08), ages / dirty pages / travel (WS1-03, WS4-08), tapes and faults (WS4-01, WS4-02) |
| Kernel / BIOS / API | idle parking and resume (WS1-02), the 12 transitions (WS1-06), every halt reason (WS1-07), trace (WS1-09), seek and replay (WS1-10), determinism (WS1-16), levers (WS4-03/05/07), display, keys, VSYNC (WS5-05/06/07), wasm exports and `layout.json` (WS2-02/03) |
| Filesystem / tools | `mkdisk` round-trips (WS1-11), two disks, `make help` (WS0-11), CLI exit line (WS0-10), piped stdin (WS1-14) |
| Compiler | 16-bit arithmetic (WS5-01), arrays (WS5-02), operators and control flow (WS5-03), intrinsics (WS5-04), diagnostics (WS5-08), the 8 KB no-scratch case (WS1-13) |
| Languages | assembler encodings and errors (WS7-01), TM errors (WS7-02), BF errors (WS7-03), Busy Beavers and increment vs `tm_ref.py` (WS6-04), palindrome travel ratio (WS6-05), asm round-trip (WS6-07), BF on tape 1 (WS6-08) |
| Demos | `hello/*` outputs with no `puts` shortcuts (WS1-12, WS2-04, WS5-10), Pong frames and size (WS6-02), Life golden frame (WS6-03), fault at 32K (WS6-09), Forth (WS7-04), shell self-compile (WS6-06), the demo shell session under Node (WS2-05) |
| Web | panel modules exist (WS3-01), URL state, disassembly view and speed helpers under `node --test` (WS3-02…06), the panels and controls themselves under Cypress, content slugs (WS8-02), Pages workflow (WS8-01) |
| Docs / repo | constants block matches `dump_constants` (WS0-02), `CLAUDE.md` (WS0-05), README (WS0-06), CI (WS0-09), no second visualizer (WS9-01) |

## Known limits

| Limit | Value | Why |
|---|---|---|
| Program size | 16,128 bytes (the TPA) | `.com` at `0x0100`, banked window starts at `0x4000` |
| Tapes / length | 1, 2 or 4 × 32K, 48K or 64K | 16-bit addresses; the window is what is left after the fixed regions |
| Banked window | 8 KB / 24 KB / 40 KB per tape | see the memory map |
| Files | 8.3 names, 64 directory entries, 16 open handles, 2 disks of 512,512 bytes | CP/M-style flat filesystem |
| Console | 128-byte line buffer, 4,096-byte output ring | static buffers, no heap |
| `RAND` | 17 distinct values per seed | the shift triple is fixed in the frozen `bios.h`; `levers.md` |
| Trace / snapshots / input log | 65,536 events, 32 slots (the first keeps the boot anchor), 4,096 entries | fixed rings; `tos_seek` fails beyond the oldest slot, and refuses when a program image it would have to reload has been recycled (8 kept) |
| Clock | `hz` throttles the native run loop only; the browser uses the speed control's per-frame step budget | `kernel_step` never sleeps: the host loop paces |
| Keys (native) | `w s ↑ ↓ space esc enter`, each held for 150 ms | a TTY has no key-up events |
| Browser | main thread only, ≤ 8 ms of machine per frame at `max` speed | GitHub Pages cannot send COOP/COEP headers, so no `SharedArrayBuffer` |
| tiny-C | no pointers, structs, `switch`, `?:`, `sizeof`, floats, local arrays; ≤ 4 params, ≤ 32 locals, ≤ 256 globals, ≤ 64 functions | `tiny-c.md`, `decisions.md` B16 |
| CPU | 8080 only: no Z80 opcodes, no CP/M BDOS (`CALL 5`), interrupts limited to EI/DI flag storage, and 20H/30H are NOPs rather than the 8085's RIM/SIM | `decisions.md` A2, A5, B24 |
| Speed of tiny-C code | Life costs ~290,000 instructions per generation (3.0 M cycles, so about two thirds of a generation per second at a virtual 2 MHz, dozens per second unthrottled); Pong ≤ 3,700 per frame | 16-bit `HL` arithmetic everywhere, `decisions.md` B21 |
| Native throughput | ~39 M instructions/s, ~35 M with the trace on (`make bench`, Apple M-series, `-O2`); the wasm build is 120 KB | — |

## Removed in v2

- The Python file-polling viewer and the `viz/` directory, `make viz`, and the `visualizer` Docker service. The kernel no longer writes snapshot files by itself; `--snap-dir=` is an optional debug hook through the HAL.
- The `.cursor/` agent workflow (`decisions.md` B7). `docs/` and `CLAUDE.md` replaced it.
- The fixed `0x20FC` scratch byte in the compiler and the 8-bit `int`.

## Out of scope for this version

A second visualizer, WebGL, Web Workers, an editor library, a Z80, CP/M compatibility, self-hosting the compiler, a linker or preprocessor, sound, mouse or touch controls, and performance gates in CI (`bench` prints numbers only).
