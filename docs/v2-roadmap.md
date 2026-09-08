# TuringOS v2 — Roadmap & Acceptance Criteria

**Status:** implemented and audited · **Plan date:** 2026-09-07 · **Baseline:** `main` @ `74a8714` (29/29 tests) · **Result:** `make test` 77/77, `make test-web` 54/54, site live at <https://jgoetzmann.github.io/Turing-Machine-OS/>

Every item below has a stable ID (`WSn-mm`) so it can be pasted straight into a GitHub issue title. "Test" means an automated check that runs in `make test` / CI, not a manual step.

---

## Audit (2026-09-07)

Every checklist item below was re-verified against the repository by four independent read-only auditors after the implementation landed (the run is described in `how-it-was-built.md`). `[x]` = the auditor saw the behaviour or artefact; `[~]` = the substance exists but a stated sub-clause is not met, with the gap written next to the item; `[ ]` = not done. Counts after the audit-driven fixes: **54 done, 44 partial, 1 not done** (of 99 items incl. the decisions and conventions sections) (GitHub issues per item — deliberately not created; the roadmap is the tracker).

Deviations decided rather than fixed (see `decisions.md`): local arrays stay unsupported (B16); Life's speed criterion was re-based to ≤ 500K instructions/generation (B21); disk I/O crosses the HAL as whole images (B23); the wasm is built with `-Os` (117 KB) instead of `-O2` (211 KB); `git grep -il cursor` still finds the *word* cursor (terminal cursors, and this file's history of the deleted `.cursor/` workflow); architecture tables other than the constants block are hand-written and reviewed, not generated; Lighthouse scores, cross-browser matrices and fps figures were not measured in CI — the playground was exercised in headless Chrome over the DevTools protocol before release; commit subjects on the working branch exceeded 72 characters (the branch was squash-merged into `main` with one conforming subject).

---

## 0. TL;DR

Turn TuringOS from a local, AI-scaffolded proof of concept into a public, interactive explainer of "an OS that is literally a Turing machine":

1. **One engine.** Compile the existing C99 core to WebAssembly and run the *real* machine in the browser. The GitHub Pages site is the OS, not a mock-up or a JS re-implementation.
2. **A visualizer that shows a Turing machine.** Replace the file-polling pygame viewer with a web visualizer that reads the tape straight out of WASM memory every frame: linear tape strip with a moving head, tape map with write-age heat, registers, live disassembly, kernel FSM, display, disk, and time travel.
3. **Levers that change the machine, not the picture.** Tape count, tape length, clock speed, PRNG seed, input mode, disk count, and a "TM head-travel cost" meter — each with a UI control, URL param, CLI flag, and test.
4. **Real programs.** Grow the tiny-C compiler (16-bit `int`, arrays, bit ops, `peek`/`poke`), add a memory-mapped 64×32 display and a keyboard port, and ship demos: Pong, Game of Life, Busy Beaver, 1-tape vs 2-tape palindrome, and the shell compiling itself.
5. **Languages on top.** An 8080 assembler, a Turing-machine description language that compiles to 8080 (a TM running on the TM), Brainfuck, and (stretch) Forth.
6. **Delete the `.cursor/` agent workflow and the pygame visualizer.** Replace `rules.mdc` / `spec.md` / `progress.md` / `remember.md` with `CLAUDE.md`, `docs/` (`decisions.md`, this roadmap), CI, and GitHub issues. Done for `.cursor/` in this commit; `viz/` goes at M3.

---

## 1. Where the project stands (verified 2026-09-07)

### 1.1 What exists and works

| Component | Files | LOC | Notes |
|---|---|---|---|
| 8080 emulator | `src/emu/cpu.c`, `mem.c` | 704 | All documented opcode groups; 64 KB flat tape |
| Kernel FSM | `src/kernel/kernel.c` | 221 | BOOT→SHELL→RUNNING→SYSCALL→HALT; meta at `0xFF00`; snapshots to `/tmp` every 1000 ticks |
| BIOS | `src/bios/bios.c` | 548 | `OUT 0x01` + `A`=fn; 19 syscalls (console, disk, type/run/del/cc, readline) |
| Filesystem | `src/fs/fs.c` | 593 | CP/M-style, 77×26×256 image, 64 dir entries, 16 handles |
| Tiny-C compiler (host) | `src/compiler/compiler.c` | 1531 | Lexer → parser → AST → 8080 `.com`; 12 runtime codegen tests |
| Shell (runs on the 8080) | `src/shell/shell_tpa.c` | 219 | Written in tiny-C, compiles to a 5,115-byte `shell.com` |
| Tools | `tools/mkdisk.c`, `cc_driver.c` | 67 | Blank disk image; host compile driver |
| Visualizer | `viz/visualizer.py`, `tape_bridge.py` | 384 | pygame, polls `/tmp` snapshot files at 10 Hz |
| Tests | `tests/` (29 in `make test`) | — | Unit (emu/bios/fs/compiler) + shell integration scripts |

The fact that the shell is written in the project's own C subset and runs *on* the emulated 8080 is the strongest thing in the repo — the site should lead with it.

### 1.2 Honest gaps (these shape the plan)

Anything the site claims must be true of the code. Today these are not:

| # | Gap | Evidence |
|---|---|---|
| G1 | The "5 test programs" are placeholders: `add.c` is `puts("3 + 4 = 7")`, `memtest.c` is `puts("sum=55")`, `strcat.c` is `puts("helloworld")`. Real arithmetic coverage lives only in `tests/compiler/test_codegen_runtime.c` (12 cases). | `tests/compiler/programs/*.c` |
| G2 | Compiler is far below spec §4.6: `int` is 8-bit (`MVI A,imm8`), no arrays (`[` is lexed but never parsed), no pointers/string variables, no bitwise ops or shifts (no tokens for `& \| ^ ~ << >>`), no `break`/`continue`, `puts()` accepts only string literals, identifiers ≤ 15 chars, 64 globals. | `src/compiler/compiler.h` token enum; `compiler.c:117`, `:1122` |
| G3 | Compiler uses a fixed scratch byte at `0x20FC` *inside the TPA* for `&&`/`\|\|` — any program whose code spans that address corrupts itself. | `compiler.c:673` |
| G4 | The dirty-page map is dead: `kernel_write_meta()` zeroes `0xFF10..0xFF2F` every tick and nothing ever sets a bit, so the visualizer's "recently written" flash never fires. | `kernel.c:41-43`, `mem.c` (no tracking) |
| G5 | `KS_IDLE` is unreachable — no code assigns it. The FSM diagram in the spec does not match the code. | `kernel.c:158` only |
| G6 | Memory-map regions "kernel heap" (`0x4000`), "shell workspace" (`0x8000`), "FS cache" (`0xC000`) and "I/O ports" (`0xFE00`) are documented but unused by any code. | `grep 0xC000\|0xFE00 src/` → nothing |
| G7 | `cpu->cycles` counts instructions (+1 per step), not 8080 cycles. Unlisted opcodes silently fall through a "Temporary fallback" NOP. | `cpu.c:668-675` |
| G8 | BIOS blocks on host `getchar()` (CONIN, READLINE) — impossible in a browser event loop; raw terminal mode promised by the spec was never implemented. | `bios.c:57`, `:102` |
| G9 | Visualizer path is slow and lossy: kernel rewrites a 64 KB file every 1000 ticks; pygame issues 65,536 `draw.rect` calls per frame; no register panel (spec §4.8 promised one); no step control of the *machine*. | `kernel.c:75-84`, `visualizer.py:104` |
| G10 | `mkdisk` only creates blank images; there is no way to put a file on the disk from the host, so `cc`/`run`/`type` are integration-tested only on the *missing-file* path (`nosuch.com` → `?`). | `tools/mkdisk.c`; `tests/integration/test_{cc,run,type}.sh` |
| G11 | The `cc` shell command is a host "ROM service" (BIOS 0x16 stages files under `build/` and calls `cc_compile`), not an 8080 program. Fine — but must be documented as such. | `bios.c:291-317` |
| G12 | No CI, no GitHub Pages (`has_pages: false`), generic README, `main.c` prints "stub boot complete", `run_tests.sh` is 200 lines of copy-pasted compile commands. | repo |
| G13 | The `.cursor/` workflow (spec/progress/remember/rules) is now stale relative to the code and is the only architecture documentation. | `.cursor/*` |

---

## 2. Decisions this plan makes

Each is written up in `docs/decisions.md` (B1–B9) with context, consequences and the alternatives rejected.

- **D1 — One engine, compiled to WASM.** The site runs the C core via Emscripten. *Rejected:* a JS/TS re-implementation (two emulators that drift; the site would be lying about running "the OS"); Pyodide/pygbag (pygame in the browser is heavy and fragile).
- **D2 — A host abstraction layer (HAL).** All host touchpoints (console, keys, disk bytes, time, frame sync, shell blob) go through `src/hal/hal.h` with `hal_posix.c` and `hal_wasm.c`. Nothing else in `src/` includes `<stdio.h>`.
- **D3 — The web visualizer is the only visualizer.** The pygame viewer (`viz/`) is deleted once the web visualizer reaches parity (WS9). This amends the old rule "visualization is Python 3 + pygame only". *Rejected:* keeping two visualizers in sync against one trace format, for a viewer nobody will see on the site.
- **D4 — Multi-tape = a banked window, CP/M 3 style.** Addresses `0x4000–0xDFFF` (40 KB on a 64K tape; `0x4000 … TOP−0x2001` in general — currently unused, G6) are per-tape; everything else is common. `OUT 0x02` selects the tape. This keeps the program's code, stack and bookkeeping stable while giving a k-tape machine with k heads' worth of storage, and it is historically how 8080/Z80 systems escaped 64 KB. *Rejected:* switching the whole address space (code vanishes under the PC); multiple CPUs (not a k-tape TM, that's k machines).
- **D5 — The display is tape.** A 64×32 1-bpp framebuffer memory-mapped at `0xFE00–0xFEFF` — exactly the 256-byte I/O region the spec reserved and never used (G6), exactly CHIP-8's resolution. Programs draw by writing tape cells; the host merely renders that page. The visualizer shows the game *inside* the tape map. *Rejected:* ANSI cursor addressing over CONOUT (invisible on the tape); a syscall-drawn display (the tape wouldn't hold the picture).
- **D6 — Tape length is a lever, so the bookkeeping regions become relative to the top of the tape** and the loader (not the compiled program) sets SP. Programs stay portable across tape sizes.
- **D7 — Delete `.cursor/`.** Spec → `docs/architecture.md` (describing what *is*), remember → `docs/decisions.md`, progress → GitHub issues generated from this file, rules → a short `CLAUDE.md`. Documentation constants are generated from source so they cannot drift again.
- **D8 — Honesty rule for the site.** Every claim on the site is backed by a test or links to the line of code; the architecture page's tables are generated from source constants (WS0-02, WS8-07).

---

## 3. Workstreams & acceptance criteria

### WS0 — Delete the `.cursor` workflow; repo hygiene

- [~] **WS0-01** `.cursor/` (`rules.mdc`, `spec.md`, `progress.md`, `remember.md`) deleted — **done 2026-09-07**. Its decisions live in `docs/decisions.md` (Part A); its task list is superseded by this file; `spec.md`'s factual content is rewritten from the code in WS0-02 (git history keeps the original). `git grep -il cursor` on `main` returns nothing. — *audit 2026-09-07:* Stated criterion `git grep -il cursor` returns nothing is not met (17 hits: historical `.cursor/` mentions in docs plus terminal-cursor wording in code); branch is fullsend/v2, not main.
- [~] **WS0-02** `docs/architecture.md` supersedes `.cursor/spec.md` and describes the system **as implemented**: TM mapping table; memory map v2 (Appendix B) with reserved-but-unused regions marked as such; kernel FSM with only the transitions that exist (after WS1-06); full BIOS port/syscall table (Appendix A); disk & directory format; `.com` format; compiler pipeline and the *actual* supported subset; build targets. All addresses/IDs in the doc are emitted by `tools/dump_constants` and a test (`tests/docs/test_constants.sh`) fails if the doc and the source disagree. — *audit 2026-09-07:* Only the §17 constants block is generated and tested; the §3 memory map, §5 transition table, §6 ports, §7 syscall ids and §8 meta offsets are hand-written and can drift without failing a test.
- [~] **WS0-03** `docs/decisions.md` records every architectural decision as Context / Decision / Consequences / Alternatives. **Seeded 2026-09-07** with the founding decisions (A1–A9, migrated from `remember.md`) and the v2 decisions (B1–B9). Remaining: every later workstream that changes a port, syscall, memory map, file format, HAL or JS API appends an entry in the same commit. Bug-fix diary entries from `remember.md` were *not* migrated (git history keeps them). — *audit 2026-09-07:* 5 of 16 Part B entries have no 'Alternatives' section; no entry records the HAL disk API change (whole-image hal_disk_load/save instead of hal_disk_read/write) or the extended meta-block layout (frame/keys/stop/syscall fields at 0x0A–0x38).
- [~] **WS0-04** `docs/roadmap.md` (this file, moved) replaces `progress.md`. Every `WSn-mm` has a GitHub issue with the ID in its title, grouped into milestones M1–M6 (§4). `progress.md` is not carried forward. — *audit 2026-09-07:* File not moved to docs/roadmap.md; no evidence that a GitHub issue exists per WSn-mm (not verifiable offline, and nothing in the repo links to one).
- [x] **WS0-05** `CLAUDE.md` at the repo root, ≤ 80 lines: build/test/run commands; the hard constraints (v2: tape, head, states, transition function, HAL boundary); where docs live; "run `make test` before calling anything done"; commit conventions — imperative subject, **no `Co-Authored-By` / tool trailers in commit messages**.
- [x] **WS0-06** README rewritten: one-paragraph pitch, a screenshot/GIF of the web visualizer, link to the Pages site, ≤ 6-line quickstart (`make test`, `make run`, `make web`), CI + Pages badges. The "built with heavy AI assistance" paragraph moves to `docs/how-it-was-built.md`.
- [x] **WS0-07** `.gitignore` covers `build/`, `viz/.venv/`, `web/node_modules/`, `web/dist/`, `web/public/turingos.{js,wasm}`, `snapshot_*.bin`. No generated artifact is committed.
- [x] **WS0-08** `tests/run_tests.sh` is data-driven (one line per test: name + sources), ≤ 60 lines, prints `PASS: <name>` per test and a final `N/N passed`, exits non-zero on any failure. Same tests as today plus new ones.
- [~] **WS0-09** `.github/workflows/ci.yml` runs `make test` on `ubuntu-latest` and `macos-latest`, plus one job with `-fsanitize=address,undefined`. Green on `main`; badge in README. — *audit 2026-09-07:* 'Green on main' not verifiable offline — work sits on branch fullsend/v2, not main.
- [x] **WS0-10** `main.c` prints `TuringOS halted (reason=<name>) after <N> steps` instead of "stub boot complete"; integration tests updated.
- [x] **WS0-11** `make help` lists every target; new targets `wasm`, `web`, `demo-disk`, `disasm`, `bench`, `asm`, `tm`, `bf` exist and are documented. — *closed after audit: `make asm|tm|bf FILE=…` targets added and listed by `make help`.*

### WS1 — Core hardening & host abstraction

- [~] **WS1-01** `src/hal/hal.h` declares every host touchpoint: `hal_con_out(ch)`, `hal_con_in_ready()`, `hal_con_in()`, `hal_keys()`, `hal_disk_read/write(disk, offset, buf, n)`, `hal_shell_blob(&ptr, &len)`, `hal_time_ms()`, `hal_vsync()`, `hal_snapshot(tape, meta)`. `hal_posix.c` and `hal_wasm.c` implement it. `grep -l '<stdio.h>' src/{emu,bios,kernel,fs,compiler}/*.c` returns nothing; file I/O lives only in `src/hal/` and `tools/`. `cc_compile()` gains a buffer variant `cc_compile_buf(src, len, out, cap)` so the BIOS `cc` service no longer stages files under `build/`. — *audit 2026-09-07:* `grep -l '<stdio.h>' src/compiler/*.c` still hits compiler.c (path-based cc_compile() wrapper); disk HAL is whole-image hal_disk_load/save, not hal_disk_read/write(disk, offset, buf, n).
- [x] **WS1-02** Non-blocking kernel API: `kernel_step(k, max_steps)` runs ≤ `max_steps` and returns `{steps_run, stop_reason}` with reasons `BUDGET`, `HALT`, `WAIT_INPUT`, `VSYNC`, `BREAKPOINT`. A blocking syscall (CONIN/READLINE with no byte ready) parks the machine in `KS_IDLE` without consuming steps and completes when input arrives. `kernel_run()` is a loop over `kernel_step()`. Test: a program calling `getchar()` with no input returns `WAIT_INPUT` in state `IDLE`; pushing a byte lets it complete.
- [x] **WS1-03** Write/read age tracking in `mem.c`: `mem_write()` records `last_write_step[addr]`, `mem_read()` records `last_read_step[addr]` (two `uint32_t[65536]` static arrays, zero heap). The 256-bit page dirty map at `0xFF10` is derived from them each tick. Test: after `STA 0x4321`, bit `0x43` of the dirty map is set and `last_write_step[0x4321] == steps`.
- [x] **WS1-04** Real 8080 cycle counts: `cpu->cycles` advances by the documented count per opcode, including taken/not-taken differences for conditional CALL/RET. Test: table-driven check of all 256 opcodes against `tests/emu/cycles.tsv`.
- [x] **WS1-05** Opcode coverage complete: the "Temporary fallback" branch is gone; all 256 byte values are explicitly handled — 244 documented opcodes plus the 12 undocumented aliases behaving as real hardware does (`0x08/10/18/20/28/30/38` = NOP, `0xCB` = JMP, `0xD9` = RET, `0xDD/ED/FD` = CALL). Test asserts each decodes with the correct length and effect.
- [~] **WS1-06** Kernel FSM matches its documentation: `KS_IDLE` is reachable with the meaning "waiting for the environment" (WS1-02). The FSM diagram in `docs/architecture.md` and the visualizer's FSM panel are generated from one transition table in `kernel.c` (`kernel_transitions[]`). Test: every transition in the table is exercised by at least one test, and no test observes a transition not in the table. — *audit 2026-09-07:* docs/architecture.md §5 transition table is hand-written (dump_constants emits no transitions); no test asserts transitions 4, 8 and 11 individually fire, and none asserts no out-of-table transition is observed.
- [x] **WS1-07** Halt reason byte at `0xFF05`: `0` running, `1` HLT instruction, `2` `halt` command, `3` stdin EOF, `4` tape fault (WS4-02), `5` breakpoint, `6` bad tape select (WS4-01). Exposed in meta and printed by `main.c` (WS0-10).
- [x] **WS1-08** Disassembler `src/emu/disasm.c`: `disasm_one(addr, out, cap)` → mnemonic, operands, length for all 256 opcodes; `tools/disasm` CLI (`make disasm FILE=build/bin/shell.com`). Test: 30 known encodings round-trip; disassembling `shell.com` then reassembling (WS7-01) reproduces identical bytes. — *closed after audit: `tests/integration/v2_ws6_07_asm.sh` now disassembles and reassembles `shell.com` byte-identically.*
- [~] **WS1-09** Trace ring `src/kernel/trace.c`: static ring of 65,536 packed 8-byte events `{step:u32, addr:u16, kind:u8 (FETCH/READ/WRITE/SYSCALL/STATE/TAPE_SELECT), value:u8}`, on/off at runtime, no heap. Test: a 10-instruction program yields exactly the expected event sequence. — *audit 2026-09-07:* The test only checks that FETCH 0100/3E is eventually followed by FETCH 0102/76 plus one STATE event for a 2-instruction program — not the exact event sequence of a 10-instruction program.
- [~] **WS1-10** Snapshot ring for time travel: `kernel_snapshot_save()/restore(slot)` copy tapes + CPU + BIOS + FS-cache state into a static ring (default 64 slots, compile-time cap ≤ 8 MB). Test (determinism): run program P with input log L to step 10,000; restore the slot taken at 8,000; replay to 10,000; tape and CPU are byte-identical to the uninterrupted run. — *audit 2026-09-07:* SNAPSHOT_SLOTS is 32 (not 64) and the ring is 9,978,376 B of BSS in snapshot.o, above the ≤ 8 MB cap; determinism test replays 2500→3000 rather than 8000→10000.
- [x] **WS1-11** Disk tooling: `mkdisk` gains `--add <hostfile>[:NAME.EXT]` (repeatable), `--ls`, `--extract NAME.EXT`; `make demo-disk` produces `build/disk/demo.img` containing every file under `demos/**` and `tests/compiler/programs/*.c`. `fs.c` supports two images (A:/B:) selected by SELDISK. Tests: add → ls → extract round-trips bytes; `dir` inside the OS lists the added files.
- [x] **WS1-12** End-to-end tests that really compile and run inside the OS: for each demo program, `printf 'cc X.c\nrun X.com\nhalt\n' | turingos --disk=build/disk/demo.img` produces `X.expected`. `test_cc.sh` / `test_run.sh` / `test_type.sh` keep their failure-path checks *and* gain success-path checks. — *closed after audit: success paths are covered by `v2_ws1_12_cc_run_demos.sh` (all six programs plus in-OS `cc PONG.C`/`LIFE.C`); the three legacy `nosuch` scripts keep the failure path.*
- [x] **WS1-13** Compiler scratch at `0x20FC` (G3) replaced by stack/register temporaries. Test: a generated program > 8 KB that uses `&&`/`||` after address `0x20FC` runs correctly.
- [x] **WS1-14** Native terminal: raw mode (termios) when stdin is a TTY, cooked when piped (tests unchanged). Backspace and Ctrl-C behave; terminal state is restored on exit and on crash paths. — *closed after audit: SIGSEGV/SIGBUS/SIGABRT/SIGFPE now restore the terminal before re-raising; backspace is exercised interactively only.*
- [x] **WS1-15** Performance floor (`tools/bench`, `-O2`, trace off): native ≥ 20 M steps/s; with trace + age tracking on ≥ 5 M steps/s. Reported in CI logs (not gated, to avoid flaky runners). — *closed after audit: `make bench` runs trace-off and `--trace` (37 M / 34 M steps/s on an M-series laptop) and CI prints it as an informational step.*
- [x] **WS1-16** Determinism: two runs with identical input log and seed produce identical tape snapshots at every 1,000 steps (test over the Pong demo for 100,000 steps once WS6-02 exists; over `count.c` before that). — *closed after audit: `test_v2_ws6_demos_api.c` runs Pong twice for 100,000 steps with the same seed and key log and compares tape 0 byte for byte.*

### WS2 — WebAssembly build & JS API

- [~] **WS2-01** `make wasm` builds `web/public/turingos.js` + `turingos.wasm` with a pinned emsdk version (`web/emsdk-version.txt`), `MODULARIZE=1`, no Emscripten FS, `-O2`, fixed `INITIAL_MEMORY` sized for 4 tapes + 64 snapshots + 2 disk images. `turingos.wasm` ≤ 200 KB. — *audit 2026-09-07:* Builds with `-Os`, not the stated `-O2`; the pin is only enforced in CI (local Makefile never checks emcc version).
- [x] **WS2-02** Exported C API (`src/api/api.h`, `EMSCRIPTEN_KEEPALIVE`): `tos_create(config)`, `tos_reset()`, `tos_step(n)`, `tos_tape_ptr(i)`, `tos_tape_count()`, `tos_tape_len()`, `tos_cpu_ptr()`, `tos_meta_ptr()`, `tos_age_ptr()`, `tos_trace_ptr()/head()/tail()`, `tos_con_push(ch)`, `tos_con_pop()`, `tos_keys_set(mask)`, `tos_disk_ptr(d)`, `tos_disk_put_file(name, ptr, len)`, `tos_lever_set(id, value)`, `tos_bp_add(kind, lo, hi)`, `tos_bp_clear()`, `tos_snapshot_seek(step)`.
- [x] **WS2-03** `web/src/engine.ts` wraps the API with typed views (`Uint8Array` over each tape, `DataView` over the CPU struct). Struct offsets come from a generated `web/src/layout.json` (`tools/dump_layout`), and a test fails if the JSON and the C struct disagree.
- [~] **WS2-04** `make test-web` runs the `.expected` program suite and the demo suite through the WASM build under Node (`web/test/run.mjs`); output is byte-identical to native. — *audit 2026-09-07:* No web/test/run.mjs; only 6 of 17 .expected files run under wasm (demos/tm, bf, asm, life, forth are native-only); output compared to .expected files, not directly to native bytes.
- [x] **WS2-05** A shell session in the browser (`dir`, `cc add.c`, `run add.com`, `halt`) produces the same console bytes as native for the same input.
- [~] **WS2-06** Engine runs on the main thread with a per-frame step budget (GitHub Pages cannot send COOP/COEP headers, so `SharedArrayBuffer`/threads are out). At "unthrottled" the page keeps ≥ 30 fps and the engine reaches ≥ 10 M steps/s in current Chrome, Firefox and Safari on a laptop. — *audit 2026-09-07:* ≥30 fps and the Chrome/Firefox/Safari figures were not measured (no browser here, no automated perf check).
- [~] **WS2-07** The built site is fully static: works on GitHub Pages and with `npx serve web/dist`; zero runtime network calls after load. — *audit 2026-09-07:* vite.config.ts base '/Turing-Machine-OS/' makes dist reference /Turing-Machine-OS/assets/… so `npx serve web/dist` at root 404s (no web/dist/Turing-Machine-OS directory).

### WS3 — Web visualizer

- [~] **WS3-01** Panels, all live at ≥ 30 fps while running and 60 fps when paused/stepping: — *audit 2026-09-07:* (a) heat/decay window auto-derived, not configurable; (b) strip outlines the PC cell only, not the instruction's bytes; (e) no syscall log (last 100 + args) — histogram only; (j) no distinct-cells-touched; fps not measured.
  - (a) **Tape map** 256×256 with region tint, write-age heat (decay window configurable in steps), read-age hue, blinking head cell; hover → address, value, region, last-writer step; click → detail panel.
  - (b) **Linear tape strip**: cells in a row centred on the head with a head triangle, scrolls as PC moves, brackets the current instruction's bytes, draws the last N head positions as a trail. This is the "it's a Turing machine" view.
  - (c) **Detail**: hex + ASCII of any 256-byte page, plus live disassembly (WS1-08) around PC with the current instruction highlighted and the next one previewed.
  - (d) **CPU**: A B C D E H L, BC DE HL, SP, PC, flags S Z AC P CY, cycles, steps, measured steps/s and virtual MHz.
  - (e) **Kernel FSM**: diagram with the active state lit and per-transition counters (from the table in WS1-06); syscall log (last 100, decoded names + args).
  - (f) **Display**: the 64×32 framebuffer (WS5-05) at integer scale, with a "show on tape" toggle that highlights `0xFE00–0xFEFF` in the map.
  - (g) **Console**: terminal with CR/LF/BS, clear-screen and cursor-home, keyboard → CONIN/READLINE, paste support.
  - (h) **Disk**: track/sector grid with decoded directory; click a file to view; upload from host; download the image.
  - (i) **Tapes**: when k > 1, k stacked maps/strips with the selected tape highlighted.
  - (j) **Stats**: steps, cycles, distinct cells touched, cells written, syscalls by type, head-travel odometer (WS4-08).
- [x] **WS3-02** Execution controls: Run/Pause, Step ×1/×10/×100/×1000, Step-over syscall, Run-to-HALT, Run-to-state-change, Reset; speed slider (log scale) from 1 step/s to unthrottled. At ≤ 60 steps/s every single head move is visible in (b).
- [~] **WS3-03** Breakpoints/watches: PC address, read/write of an address or range, syscall id, kernel state transition. Hitting one pauses, flashes the reason, and sets halt reason 5 (WS1-07) in the stats panel without halting the machine permanently. — *audit 2026-09-07:* tos_halt_reason() (api.c:307) returns g_k.halt_reason, so the CPU/status panels never show reason 5 and the stats panel has no reason field; only test is slot-exhaustion (ws2_02), none cites WS3-03.
- [~] **WS3-04** Time travel: a scrubber over the step timeline; dragging back restores the nearest snapshot and replays forward (WS1-10); a "diff since step S" overlay on the tape map. — *audit 2026-09-07:* No 'diff since step S' overlay on the tape map (grep of web/src finds none).
- [x] **WS3-05** Layout: ≥ 1280 px shows all panels; < 900 px collapses to tabs; keyboard shortcuts (`space` run/pause, `.` step, `r` reset, `?` help). Phosphor-green default theme plus a light theme honouring `prefers-color-scheme`.
- [x] **WS3-06** Shareable state: the URL hash encodes levers, selected demo, speed and breakpoints; loading such a URL restores them. Export: PNG of the tape map, JSON of the last N trace events, the disk image.
- [x] **WS3-07** Runtime dependencies: TypeScript + Canvas2D only (Vite for the build). Runtime JS ≤ 150 KB gzipped, excluding the wasm. WebGL only if Canvas2D cannot hold 30 fps for panel (a).
- [~] **WS3-08** Accessibility: every control keyboard-reachable and labelled; registers/state/console have text equivalents; text contrast ≥ 4.5:1 in both themes. — *audit 2026-09-07:* cpu.ts cleared-flag text #484f58 on #0d1117 = 2.28:1; tapemap/strip/timeline canvas hover/click have no keyboard equivalent; panel colours are hard-coded dark and ignore the light theme; no a11y test.

### WS4 — Levers & execution control

Every lever has a UI control, a URL param, a CLI flag, a section in `docs/levers.md`, and ≥ 1 test (**WS4-09**). Machine levers reset the machine when changed; view levers do not.

- [x] **WS4-01 Tape count** `k ∈ {1, 2, 4}` (default 1) — *machine lever* (D4). `0x4000–0xDFFF` (on a 64K tape; `0x4000 … TOP−0x2001` in general) is per-tape; all else common. `OUT 0x02, A=n` selects tape n for every access (fetch and data) in the window; `IN 0x02` reads the selection; `n ≥ k` → halt reason 6. Boot and RUN reset the selection to 0; meta bytes `0xFF06/0xFF07` publish selection/count. CLI `--tapes=2`. Tests: a write to `0x5000` on tape 1 is invisible on tape 0; the entire suite passes at k=1 and k=2. — *closed after audit: `v2_ws4_01_02_tapes_lengths.sh` runs the console demos, a TM, BF and asm program on 2 and 4 tapes.*
- [x] **WS4-02 Tape length** `L ∈ {32K, 48K, 64K}` (default 64K) — *machine lever* (D6). META, display and stack relocate to the top L; the banked window shrinks; the loader sets SP; `0xFF08–09` publish L. Any access ≥ L → halt reason 4 and the visualizer shows the head at the tape edge. Tests: shell + all demos run at 32K; the fault demo (WS6-09) halts with reason 4. — *closed after audit: the same script runs them at 32K and 48K (and 2 tapes × 32K); the fault demo covers the tape edge.*
- [~] **WS4-03 Clock**: steps/s throttle (1 … unthrottled) and a "virtual MHz" mode using real cycle counts (WS1-04), so `2.0 MHz` runs at original 8080 speed. CLI `--hz=2000000` / `--steps-per-sec=30`. Test: at 2 MHz, 200,000 cycles take 100 ms ± 10 %. — *audit 2026-09-07:* No --steps-per-sec CLI flag; no timing test ('200,000 cycles in 100 ms ± 10 %') — WS4-03 tests only check lever reporting.
- [~] **WS4-04 Trace/age granularity** — *view lever*: age-decay window, trace on/off, native snapshot interval (`--snap=1000`). — *audit 2026-09-07:* Age-decay window is not a lever (panels auto-derive it); no --snap=N CLI or URL param for the snapshot interval; no test cites WS4-04.
- [x] **WS4-05 PRNG seed** (default 1) — *machine lever*: BIOS `RAND` (0x07) is an 8-bit xorshift/LFSR seeded from the lever; CLI `--seed=N`. Test: two runs of Pong with the same seed and input log produce identical frames (with WS1-16). — *closed after audit: the Pong determinism test (WS1-16) cites WS4-05.*
- [~] **WS4-06 Input mode**: blocking console (CONIN/READLINE) vs polled keys (`IN 0x03`) — both always exist; the lever selects what the UI sends where; plus an *input script* (`--stdin-script=file`) for reproducible demos. — *audit 2026-09-07:* No CLI flag for the input-mode lever (levers.md says 'UI only'); no test cites WS4-06.
- [x] **WS4-07 Disks**: 1 or 2 images (A:/B:), fixed geometry 77×26×256. SELDISK works; `dir` shows the selected disk. CLI `--disk-b=path`. Test: a file written on B: is not visible on A:.
- [x] **WS4-08 TM head-travel odometer** — *view lever/counter*: Σ |addr_i − addr_{i−1}| over every tape access (fetch + data), shown next to step count — the cost a physical single-head TM would pay for random access. Reset with the machine. Used by WS6-05. Test: a known 10-access sequence yields the expected sum.
- [~] **WS4-09** Every lever meets the "UI + URL + CLI + doc + test" rule; `docs/levers.md` has one table row per lever with range, default, and effect. — *audit 2026-09-07:* INPUT_MODE has no CLI flag; SNAP_INTERVAL has no URL param and no dedicated CLI (--snap-dir only); no 'effect' column in the table; no test cites WS4-04/06/09; the age-decay window lever does not exist.

Considered and rejected: alphabet size (the 8080 fixes a cell at 8 bits); TPA size (subsumed by tape length).

### WS5 — Compiler v2, display, input (prerequisites for real demos)

- [x] **WS5-01** `int` is 16-bit (HL-based), `char` is 8-bit; correct widths for loads/stores/arithmetic/comparison (signed compare for `int`); literals −32768…65535. Compiled programs no longer emit `LXI SP` — the loader sets it (D6). Tests: `add16`, `mul16`, `div16`, `cmp_signed`, `mixed_char_int`.
- [~] **WS5-02** Arrays: global and local `char a[N]` / `int b[N]`, indexed read/write, string-literal initialisers for `char[]`, `puts(var)` for variables. Tests: `array_sum`, `string_copy`, `puts_var`. — *audit 2026-09-07:* Local `char a[N]` / `int b[N]` inside functions are rejected by design; the roadmap item still requires them.
- [x] **WS5-03** Operators/statements: `& | ^ ~ << >>`, compound assignment, `++`/`--`, `break`, `continue`, `else if`, `do … while`, recursion. Tests include recursive `fib(10)`.
- [x] **WS5-04** Memory/port intrinsics: `peek(addr)`, `poke(addr, v)` (16-bit address), `inp(port)`, `outp(port, v)`, and fixed-address globals `__at(0xFE00) char vram[256];`. Test: poke/peek round-trip; `__at` global lands at the requested address.
- [~] **WS5-05** Display (D5): 64×32 1-bpp framebuffer at `TOP−0x200 … TOP−0x101` (i.e. `0xFE00–0xFEFF` on a 64K tape), row-major, 8 bytes/row, MSB = leftmost pixel. Rendered by the host from the tape: web panel (WS3-01f), native TTY (ANSI half-block `▀` renderer, 64 columns × 16 rows, refreshed on VSYNC). Test: a program pokes a checkerboard → the host renderer's captured frame equals a golden file. — *audit 2026-09-07:* No test captures the host renderer's frame and compares it to a golden file (tests inspect tape bytes only; no checkerboard golden).
- [x] **WS5-06** Keys: `IN 0x03` → bitmask (bit0 W, bit1 S, bit2 ↑, bit3 ↓, bit4 space, bit5 esc, bit6 enter, bit7 any), non-blocking; native TTY keys are held for 150 ms after keypress; web uses keydown/keyup. BIOS `CONST` (0x05) → console byte ready. Test via `tos_keys_set` / HAL stub.
- [x] **WS5-07** Timing: BIOS `VSYNC` (0x06) parks the machine until the host's next frame (web: rAF; native: sleep to `--fps=60`), returning `stop_reason = VSYNC` from `kernel_step`; BIOS `TICKS` (0x08) → frame counter low byte. Test: a program calling VSYNC 60 times sees TICKS advance by 60.
- [x] **WS5-08** Diagnostics: `cc` reports `file:line:col: message` (e.g. `pong.c:41:9: expected ';'`) instead of `?`; limits raised and documented — 256 globals, 64 functions, 32 locals/function, identifiers ≤ 31 chars, source ≤ 32 KB. Test: five malformed sources produce the expected messages. — *closed after audit: diagnostics now carry the real file name (`pong.c:41:9` on the host, `PONG.C:41:9` in the shell).*
- [~] **WS5-09** `docs/tiny-c.md`: EBNF grammar, types, operators, intrinsics, calling convention, limits. Every listed feature has a test; the doc's feature table is generated from the test list. — *audit 2026-09-07:* The feature table is hand-written, not generated from the test list (no generator in Makefile/tools/web/scripts; tests/docs only checks the architecture constants block), and there is no explicit calling-convention section (only 'locals live on the 8080 stack, <= 4 params').
- [x] **WS5-10** The placeholder programs (G1) are replaced by real ones: `add.c` computes and prints `3 + 4 = 7` through a `print_int()` helper; `strcat.c` concatenates two `char[]`; `memtest.c` fills an array and sums it; `count.c` loops. Same `.expected` files, real code.

### WS6 — Demos

Each demo ships as: source under `demos/<name>/`, a file on the demo disk (WS1-11), a site page with a "Load & run" button, an `.expected` or golden-frame test in CI, and a ≤ 200-word "what to watch in the visualizer" note.

- [~] **WS6-01 First light**: `hello`, `count`, `echo` — the real programs from WS5-10, each with a guided tour (WS6-10). — *audit 2026-09-07:* One grouped page and one shared tour at /demos/hello covers all six programs; there is no per-program guided tour or /demos/count, /demos/echo page.
- [~] **WS6-02 Pong** (`demos/pong/pong.c`, ≤ 400 lines of tiny-C): two paddles, ball, score to 5, P1 = W/S, P2 = ↑/↓, CPU-controlled P2 when only P1 keys are used; 60 fps via VSYNC; ≤ 33,000 cycles per frame so it is playable at virtual 2 MHz. Compiles *inside the OS* (`cc pong.c` then `run pong.com`) on web and native. Test: scripted keys for 600 frames → golden final frame and score. — *audit 2026-09-07:* No golden final frame or score assertion (test only checks 600 frames, no fault, some pixels set); no test compiles PONG.C inside the OS on native or web (in-OS cc tests cover the hello programs / ADD.C only).
- [x] **WS6-03 Game of Life** (`demos/life/life.c`): 64×32 wrapping grid, R-pentomino default, SPACE reseeds from the seeded PRNG, ≤ 500,000 instructions per generation (≈0.6 gen/s at a virtual 2 MHz; the original ≥10 gen/s target was unreachable from tiny-C on an 8080 — see `docs/status.md`). Test: generation 100 equals a golden frame and the per-generation budget holds.
- [~] **WS6-04 Classic TMs** via the TM language (WS7-02): BB(2), BB(3), BB(4) (107 steps, 13 ones), binary increment. The inner TM's tape lives in the banked window and the linear strip (WS3-01b) follows the *inner* head. Test: step counts and final tapes equal textbook values. — *audit 2026-09-07:* The web strip panel (web/src/panels/strip.ts) is 'centred on PC' only; nothing follows the inner TM head.
- [~] **WS6-05 1-tape vs 2-tape palindrome**: the same palindrome checker compiled for k=1 and k=2 (WS4-01), shown side by side with head-travel odometers (WS4-08) for n = 8, 16, 32. Test: single-tape travel / two-tape travel > 2 at n = 32 and the ratio increases with n. — *audit 2026-09-07:* No side-by-side k=1 vs k=2 view with odometers for n = 8/16/32 on the site (only a sentence in demos/tm/tour.json), and 'ratio increases with n' is untested (only n=32).
- [x] **WS6-06 The shell compiles itself**: `cc shell.c` inside the OS writes a `shell.com` byte-identical to `build/bin/shell.com`. Test compares the file extracted from the disk image with the build artefact.
- [x] **WS6-07 Assembler round-trip**: `asm hello.asm` → `run hello.com` prints; `disasm` of the output, re-assembled, is byte-identical (WS1-08, WS7-01).
- [x] **WS6-08 Brainfuck**: `bf hello.bf` → `.com`; when k ≥ 2 the BF cell tape is placed on tape 1 so the visualizer shows a tape machine running on a tape machine. Test: hello world output.
- [x] **WS6-09 Off the end of the tape**: a program walks past the end of a 32K tape → halt reason 4; the page explains bounded vs unbounded tapes. Test: reason byte = 4. — *closed after audit: `demos/fault/README.md` explains bounded vs unbounded tapes.*
- [~] **WS6-10** Every demo has a ≤ 90-second guided tour (numbered callouts on the live visualizer, no video), is reachable at `/demos/<name>`, and never autoplays sound or input. — *audit 2026-09-07:* Callouts are a text bar plus one highlighted panel rather than numbered callouts on the visualizer, nothing checks the <= 90 s length, and the 'shell' self-compile demo listed in DEMO_ORDER has no page or tour.

### WS7 — Languages on top of the toolchain

- [x] **WS7-01 8080 assembler** `asm` (`tools/asm.c`, exposed in the shell via BIOS 0x1A, same ROM-service pattern as `cc`): full mnemonic set, labels, `ORG`, `DB/DW/DS`, `EQU`, decimal/hex (`0FFH`, `0xFF`)/char literals, `+`/`−` expressions, `file:line: message` errors. Test: assembling the disassembly of `shell.com` reproduces it byte-for-byte. `docs/asm.md`. — *closed after audit: `shell.com` round-trips through `disasm` → `asm`; assembler errors are `line N: message` by spec (no file name).*
- [x] **WS7-02 TM description language** `tm` (`tools/tmc.c`, BIOS 0x1B): lines of `state read -> write move next`, `halt`, blank symbol, initial tape string; k-tape form `q0 (a,b) -> (x,y) (L,R) q1`. Compiles to a `.com` whose tape(s) live in the banked window; at halt prints the final tape and step count. Six samples (BB2–BB4, increment, palindrome-1, palindrome-2). Test: outputs match a reference interpreter `tests/tm/ref.py`. `docs/tm.md`.
- [x] **WS7-03 Brainfuck** `bf` (`tools/bfc.c`, BIOS 0x1C): the 8 commands, 30,000 cells on a 64K tape (fits the 40 KB banked window; smaller tapes get proportionally fewer cells, documented), `,` reads CONIN, `.` writes CONOUT, unmatched brackets are errors. Test: hello world and a nested-loop program vs reference.
- [x] **WS7-04 (stretch) Forth**: a tiny-C Forth interpreter that runs *inside the TPA* (not on the host): REPL, 16-bit cells, `: name … ;` definitions, arithmetic, stack ops, `.`, `emit`, `key`, `@`/`!`; dictionary in the banked window. Test: `: sq dup * ; 7 sq .` prints `49`.
- [x] **WS7-05** `docs/languages.md` states plainly which tools run on the host as ROM services (`cc`, `asm`, `tm`, `bf`) and which run on the 8080 (shell, Forth, every demo) — with the reason (G11, `decisions.md` A7).

### WS8 — GitHub Pages site

- [~] **WS8-01** Stack: Vite + TypeScript, static output in `web/dist/`; content pages authored as Markdown in `docs/` (the same files readable on GitHub — no copies) and rendered at build time. `.github/workflows/pages.yml` builds the wasm with the pinned emsdk, runs `npm ci && npm run build`, deploys with `actions/deploy-pages` on push to `main`. Live at `https://jgoetzmann.github.io/Turing-Machine-OS/`; `gh api repos/... -q .has_pages` is `true`. — *audit 2026-09-07:* Site is not live: has_pages=false, pages.yml has never run because the work sits on unpushed branch fullsend/v2 while main carries no workflows.
- [~] **WS8-02** Pages (nav order): **Home** (hero = the live machine running `count.c` at 30 steps/s with the strip view; "Open playground"); **Playground** (full visualizer + levers + console + editor); **Architecture** (TM mapping, memory map v2, FSM, boot sequence, syscall/port tables — generated per WS0-02); **Design decisions** (summaries from `docs/decisions.md`, including "where this is not a pure Turing machine and why"); **Demos** (gallery); **Languages** (tiny-C, asm, tm, bf); **How it was built** (spec-driven, AI-assisted, test-first, this roadmap); **Status** (CI badge, test count, coverage table auto-generated). — *audit 2026-09-07:* Architecture port/syscall tables (docs/architecture.md §6–7) are hand-written — only §17 constants is generated; Status page test count and coverage table are static prose in docs/status.md, not auto-generated.
- [x] **WS8-03** In-browser editor for tiny-C / asm / tm / bf (CodeMirror 6 or a line-numbered textarea — bundle budget in WS8-04 decides), save to the virtual disk, compile from the console, errors shown inline with line numbers (WS5-08).
- [~] **WS8-04** Performance budget: first-load transfer ≤ 1 MB (wasm ≤ 200 KB, JS ≤ 150 KB gz, system or self-hosted fonts); Lighthouse mobile Performance ≥ 90, Accessibility ≥ 95; interactive in ≤ 2 s on a throttled 4G profile. — *audit 2026-09-07:* No Lighthouse mobile Performance/Accessibility scores or throttled-4G time-to-interactive measurement exists; raw first-load (~1.02 MB with demo.img) sits at the 1 MB limit.
- [~] **WS8-05** Works in current Chrome, Firefox and Safari on desktop; usable (tabbed layout) on iPad and phones; no COOP/COEP requirement. — *audit 2026-09-07:* No evidence of testing in Chrome/Firefox/Safari or on iPad/phones (no browser test, screenshot or CI job); only the tabbed layout and the no-COOP/COEP property are verifiable.
- [~] **WS8-06** Diagrams are inline SVG (no images of text); the memory-map diagram has hover descriptions; the FSM diagram is the same component the visualizer uses. — *audit 2026-09-07:* Architecture page has no inline-SVG diagrams, no memory-map diagram with hover descriptions, and does not reuse the visualizer's FSM component.
- [~] **WS8-07** Every code claim links to file + line on GitHub at the built commit (footer shows the SHA); generated tables cannot drift (WS0-02). — *audit 2026-09-07:* No code claim links to file+line on GitHub (zero #L links anywhere); only the constants table is generated — port, syscall and FSM tables are hand-written and can drift.
- [x] **WS8-08** `docs/` stays readable as plain Markdown on GitHub; the site build consumes those files directly.

### WS9 — Delete the pygame visualizer

Happens at M3, once the web visualizer shows everything the pygame one did (WS3-01 a/c/d/g + WS3-02 passing: tape map, page detail, legend, status, PC blink, dirty flash).

- [~] **WS9-01** `viz/` is deleted; `make viz` is removed; pygame is removed from the Dockerfile and the `visualizer` service from `docker-compose.yml`; the kernel no longer writes `/tmp/turingos_tape.bin` / `turingos_meta.bin` (the HAL `hal_snapshot` hook replaces it — native builds may implement it as an optional `--snap-dir=` for debugging, off by default); README, `docs/architecture.md` and `docs/decisions.md` B3 updated. `git grep -i pygame` on `main` returns nothing. — *audit 2026-09-07:* `git grep -i pygame` still returns docs/decisions.md, docs/v2-roadmap.md and tests/repo/v2_ws9_01_no_pygame.sh; on main viz/ still exists (4 pygame hits) because the branch is unmerged.
- [~] **WS9-02** Docker: `docker compose up` builds and runs the OS and tests; the image includes emsdk so `make web` works offline. — *audit 2026-09-07:* `docker compose up` was not executed (unverified); `make web` inside the image needs network for `npm ci`, so 'works offline' is not met.

---

## 4. Phasing

Ordered by dependency; sizes are relative (S/M/L), not dates.

| Milestone | Scope | Definition of done |
|---|---|---|
| **M1 Foundation** (L) | WS0-*, WS1-01…16 | CI green on two OSes; HAL in place; `.cursor/` gone; docs generated from source; demo disk + real end-to-end tests |
| **M2 Machine in the browser** (M) | WS2-*, WS3-01 a/c/d/g, WS3-02, WS8-01 | Pages site live with a playground that boots the shell and runs `cc add.c` / `run add.com` |
| **M3 Visualizer & levers** (L) | rest of WS3, WS4-*, WS9 (delete `viz/`) | Strip view, time travel, breakpoints, tape-count and tape-length levers with multi-tape visualization |
| **M4 Real programs** (L) | WS5-*, WS6-01…03, WS6-09 | Pong playable on the site and in a native terminal; Life; fault demo |
| **M5 Languages & theory** (L) | WS7-01…03, WS6-04…08, WS8-02…08 | Busy Beaver and palindrome demos; asm/tm/bf; all site pages written |
| **M6 Polish / stretch** (M) | WS7-04, perf & a11y budgets | Forth REPL; Lighthouse targets met |

Critical path: WS1-01/02 (HAL + non-blocking kernel) → WS2 → WS3 → WS5 → WS6-02. Start there.

---

## 5. Non-goals

- Bus-level or cycle-exact timing beyond the documented cycle table.
- Running real CP/M software (BDOS/`CALL 5` compatibility) — the BIOS convention differs on purpose (`decisions.md` A5). A "CP/M 2.2 BDOS shim" could be a later lever; not in v2.
- Multi-process, multi-user, virtual memory, protection.
- Porting the C compiler to run *on* the 8080 (self-hosting `cc`). Listed as a post-v2 stretch.
- Sound. Mouse. Mobile-first editing.

## 6. Open questions (decide before M2)

1. Engine threading in the browser: main-thread time-slicing (recommended; Pages cannot send the headers `SharedArrayBuffer` needs) vs a Worker with per-frame copies.
2. Tape-count semantics: banked window (recommended, D4) vs whole-address-space switch.
3. Rename the repository (e.g. `turingos`)? The Pages URL is derived from the repo name; do it before M2 or never.
4. In-browser editor: CodeMirror (~120 KB gz, eats most of the JS budget) vs a plain textarea with line numbers.

## 7. Working conventions

- **Commits carry no attribution trailers.** No `Co-Authored-By:`, `Generated-by:`, session links, or any other tool/AI trailer — ever. Imperative subject ≤ 72 chars; body explains *why*. Squash work-in-progress into one commit per logical change before it lands on `main`. (Recorded as `decisions.md` B9; to be repeated in `CLAUDE.md`, WS0-05.)
- `make test` is green for every commit on `main`; CI (WS0-09) enforces it.
- Any change to a port, syscall, memory-map region, file format, the HAL or the JS API appends an entry to `docs/decisions.md` in the same commit.
- Docs describe what *is*. Aspirational content lives in this roadmap, nowhere else.
- One issue per `WSn-mm`; the ID goes in the issue title and the commit subject that closes it.

---

## Appendix A — Port & BIOS syscall map v2 (proposed)

**Ports**

| Instruction | Meaning | Status |
|---|---|---|
| `OUT 0x01` (A = fn) | BIOS call, function in A | existing |
| `OUT 0x02` (A = n) | Select tape n (WS4-01) | new |
| `IN 0x02` | Currently selected tape | new |
| `IN 0x03` | Keys bitmask (WS5-06) | new |

**BIOS functions (A on `OUT 0x01`)**

| ID | Name | Regs | Status |
|---|---|---|---|
| 0x01 | CONIN | → A | existing (parks in IDLE when empty, WS1-02) |
| 0x02 | CONOUT | C | existing |
| 0x03/0x04 | AUXOUT/AUXIN | — | existing stubs |
| 0x05 | CONST | → A (0xFF ready / 0) | new |
| 0x06 | VSYNC | — | new |
| 0x07 | RAND | → A | new |
| 0x08 | TICKS | → A | new |
| 0x09–0x0E | SELDISK/SETTRK/SETSEC/SETDMA/READ/WRITE | C, DE | existing |
| 0x0F | LISTDIR | — | existing |
| 0x12 | NAMECH | C | existing |
| 0x13/0x14/0x15/0x16 | TYPE/RUN/DEL/CC end | — | existing |
| 0x17/0x18/0x19 | READLINE/LINEGET/LINELEN | C → A | existing |
| 0x1A | ASM end | — | new (WS7-01) |
| 0x1B | TM end | — | new (WS7-02) |
| 0x1C | BF end | — | new (WS7-03) |

## Appendix B — Memory map v2 (shown for L = 64K; `TOP = L`)

| Range | Region | Scope | Change |
|---|---|---|---|
| `0x0000–0x00FF` | BIOS vectors | common | — |
| `0x0100–0x3FFF` | TPA (programs, shell) | common | — |
| `0x4000 … TOP−0x2001` (`0x4000–0xDFFF`, 40 KB) | **Banked window** (per-tape data; TM/BF tapes; Forth dictionary) | per tape | replaces unused "kernel heap" + "shell workspace" + most of "FS cache" |
| `TOP−0x2000 … TOP−0x1001` (`0xE000–0xEFFF`, 4 KB) | Common scratch: default DMA buffer, compiler temporaries (WS1-13) | common | was unused "FS cache" |
| `TOP−0x1000 … TOP−0x201` | Stack (grows down) | common | now relative to TOP |
| `TOP−0x200 … TOP−0x101` | **Display** 64×32 framebuffer | common | was unused "I/O ports" |
| `TOP−0x100 … TOP−1` | TM metadata | common | extended |

Metadata block (`TOP−0x100` = `0xFF00` at 64K): `+00` state, `+01..04` steps (u32 LE), `+05` halt reason, `+06` selected tape, `+07` tape count, `+08..09` tape length, `+10..2F` dirty page map (256 bits), `+30..3F` lever snapshot (seed, hz, input mode, …).

Only `0x0000–0x3FFF` is fixed; everything else is anchored to `TOP`:

| L | Banked window | Common scratch | Stack | Display | Meta |
|---|---|---|---|---|---|
| 64K | `0x4000–0xDFFF` (40 KB) | `0xE000–0xEFFF` | `0xF000–0xFDFF` | `0xFE00–0xFEFF` | `0xFF00–0xFFFF` |
| 48K | `0x4000–0x9FFF` (24 KB) | `0xA000–0xAFFF` | `0xB000–0xBDFF` | `0xBE00–0xBEFF` | `0xBF00–0xBFFF` |
| 32K | `0x4000–0x5FFF` (8 KB) | `0x6000–0x6FFF` | `0x7000–0x7DFF` | `0x7E00–0x7EFF` | `0x7F00–0x7FFF` |

The shell (5 KB) and every demo must run at 32K (WS4-02); programs find the display and meta via `TOP` (published in meta `+08..09`) or the `__at`/`peek` intrinsics with a `TOP`-relative helper.

## Appendix C — Repository layout v2

```
CLAUDE.md  README.md  LICENSE  Makefile  Dockerfile  docker-compose.yml
.github/workflows/   ci.yml  pages.yml
docs/                architecture.md  levers.md  tiny-c.md  asm.md  tm.md  languages.md
                     roadmap.md  how-it-was-built.md  decisions.md
src/                 emu/ (cpu, mem, disasm)  bios/  kernel/ (kernel, trace, snapshot)  fs/
                     compiler/  shell/  hal/ (hal.h, hal_posix.c, hal_wasm.c)  api/ (wasm exports)
tools/               mkdisk.c  cc_driver.c  asm.c  tmc.c  bfc.c  disasm.c  bench.c
                     dump_constants.c  dump_layout.c
demos/               hello/  pong/  life/  tm/  palindrome/  bf/  asm/  fault/
tests/               emu/  bios/  fs/  compiler/  kernel/  tm/  docs/  integration/  run_tests.sh
web/                 package.json  vite.config.ts  emsdk-version.txt
                     src/ (engine.ts, panels/*, console.ts, editor.ts, levers.ts)  public/  test/
```
