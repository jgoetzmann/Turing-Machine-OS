# TuringOS — Progress Tracker

> Status key: `[ ]` not started · `[~]` in progress · `[x]` complete  
> Update this file at the END of every agent session.  
> Add discovered sub-tasks under the relevant component. Never delete entries.

---

## PHASE 1 — Foundation

### Build System
- [x] P1-01 Create `Makefile` with targets: `all`, `run`, `test`, `disk`, `shell`, `clean`, `viz`
- [x] P1-02 Create `Dockerfile` (debian:bookworm-slim, gcc, make, python3, pygame)
- [x] P1-03 Create `docker-compose.yml` (two services: `turingos`, `visualizer`, shared `/tmp`)
- [x] P1-04 Verify `make all` produces a runnable binary with empty stub components

### Memory / Tape (`src/emu/mem.c`)
- [x] P1-05 Define `tape_t` and `addr_t` types in `mem.h`
- [x] P1-06 Implement `mem_init()` — zero-fill `mem[65536]`
- [x] P1-07 Implement `mem_read(addr)` and `mem_write(addr, val)`
- [x] P1-08 Implement `mem_raw()` — return pointer to backing array
- [x] P1-09 Unit test: read-after-write round trips for boundary addresses (0x0000, 0x7FFF, 0xFFFF)

### 8080 CPU Emulator (`src/emu/cpu.c`)
- [x] P1-10 Define `cpu_t` struct in `cpu.h`
- [x] P1-11 Implement `cpu_init()` and `cpu_reset()`
- [x] P1-12 Implement fetch-decode loop skeleton (`cpu_step`)
- [x] P1-13 Implement data transfer opcodes (MOV, MVI, LXI, LDA, STA, LHLD, SHLD, XCHG, XTHL, SPHL, PCHL)
- [x] P1-14 Implement arithmetic opcodes (ADD, ADI, ADC, SUB, SUI, SBB, SBI, INR, DCR, INX, DCX, DAD, DAA)
- [x] P1-15 Implement logic opcodes (ANA, ANI, ORA, ORI, XRA, XRI, CMP, CPI, RLC, RRC, RAL, RAR, CMA, CMC, STC)
- [x] P1-16 Implement branch opcodes (JMP, JC, JNC, JZ, JNZ, JM, JP, JPE, JPO, CALL, CC, CNC, CZ, CNZ, CM, CP, CPE, CPO, RET, RC, RNC, RZ, RNZ, RM, RP, RPE, RPO)
- [x] P1-17 Implement stack opcodes (PUSH, POP)
- [x] P1-18 Implement I/O opcodes (IN, OUT) — OUT must set a flag readable by kernel
- [x] P1-19 Implement HLT, NOP, RST, EI, DI, RIM, SIM
- [x] P1-20 Implement flag register logic (S, Z, AC, P, CY) correctly for all arithmetic/logic ops
- [x] P1-21 Unit tests: opcode-by-opcode tests for all groups in `tests/emu/`
- [x] P1-22 Unit test: HLT sets `cpu->halted = 1`
- [x] P1-23 Unit test: flags after ADD, SUB, AND, OR, XOR boundary cases

### BIOS (`src/bios/bios.c`)
- [x] P1-24 Implement `bios_init()`
- [x] P1-25 Implement `bios_dispatch()` — dispatch table for function IDs 0x01–0x0E
- [x] P1-26 Implement CONIN (0x01) — read char from host stdin, place in register A
- [x] P1-27 Implement CONOUT (0x02) — write register C to output ring buffer
- [x] P1-28 Implement `bios_pending_output()` and `bios_get_output()` (ring buffer drain)
- [x] P1-29 Implement SELDISK, SETTRK, SETSEC, SETDMA stubs (full impl after fs.c)
- [x] P1-30 Implement READ (0x0D) and WRITE (0x0E) — delegate to `fs_read_sector` / `fs_write_sector`
- [x] P1-31 Unit tests: CONOUT queues correctly, ring buffer wraps, drain works

### Kernel FSM (`src/kernel/kernel.c`)
- [x] P1-32 Define `kernel_state_t` enum and `kernel_t` struct in `kernel.h`
- [x] P1-33 Implement `kernel_init()`
- [x] P1-34 Implement `kernel_run()` main loop with BOOT → IDLE → SHELL → RUNNING → HALT transitions
- [x] P1-35 Implement BIOS output drain inside main loop
- [x] P1-36 Kernel writes `state` to `mem[0xFF00]` every tick
- [x] P1-37 Kernel writes `steps` (uint32 LE) to `mem[0xFF01..0xFF04]` every tick
- [x] P1-38 Kernel writes dirty page map to `mem[0xFF10..]` every tick
- [x] P1-39 Kernel writes tape snapshot to `/tmp/turingos_tape.bin` every 1000 ticks
- [x] P1-40 Kernel writes metadata to `/tmp/turingos_meta.bin` every 1000 ticks
- [x] P1-41 Kernel handles `KS_SYSCALL` transition from BIOS dispatch and back

### Disk Tool (`tools/mkdisk.c`)
- [x] P1-42 Implement `mkdisk` host tool: creates a blank `build/disk/disk.img` (77 tracks × 26 sectors × 256 bytes)
- [x] P1-43 `mkdisk` writes a blank directory (64 entries of 0xE5) to track 0
- [x] P1-44 `make disk` target calls `mkdisk` to produce fresh `build/disk/disk.img`

### Filesystem (`src/fs/fs.c`)
- [x] P1-45 Implement `fs_init()` — open disk image, verify geometry
- [x] P1-46 Implement internal `fs_read_sector(track, sector, buf)` and `fs_write_sector`
- [x] P1-47 Implement directory parsing — scan track 0 for active entries
- [x] P1-48 Implement `fs_open()` — find file by name, return handle
- [x] P1-49 Implement `fs_read()` — sequential sector read via allocation map
- [x] P1-50 Implement `fs_write()` — sequential sector write, allocate blocks
- [x] P1-51 Implement `fs_close()`, `fs_list()`, `fs_delete()`, `fs_exists()`
- [x] P1-52 Implement `fs_flush()` — write all dirty sectors back to disk image
- [x] P1-53 Unit tests: create file, write data, close, reopen, read back, compare
- [x] P1-54 Unit tests: list directory, delete file, verify deleted

---

## PHASE 2 — OS Layer

### Shell
- [x] P2-01 Write `src/shell/shell.c` — shell logic as a C program (compiled by host CC to 8080 .com)
- [x] P2-02 Implement `dir` command
- [x] P2-03 Implement `type <file>` command
- [x] P2-04 Implement `run <file>` — load `.com` into TPA, set PC=0x0100, switch to RUNNING state
- [x] P2-05 Implement `cc <file.c>` — invoke compiler
- [x] P2-06 Implement `del`, `cls`, `mem`, `halt`, `help`
- [x] P2-07 Implement line editing (backspace)
- [x] P2-08 Add `make shell` target that compiles `shell.c` → `build/bin/shell.com`
- [x] P2-09 Integration test: boot OS, shell prompt appears, `dir` runs cleanly

### Boot
- [x] P2-10 BOOT state loads BIOS jump table into `mem[0x0000..0x00FF]`
- [x] P2-11 BOOT state loads `build/bin/shell.com` into `mem[0x0100..]` from disk or embedded blob
- [x] P2-12 BOOT sets `cpu.pc = 0x0100` and transitions to SHELL
- [x] P2-13 Integration test: full boot sequence, shell prompt, `halt` command

---

## PHASE 3 — Tiny C Compiler

### Compiler
- [x] P3-01 Implement lexer (`cc_lex`) — tokenize C source into token stream
- [x] P3-02 Define token types and AST node types in `compiler.h`
- [ ] P3-03 Implement parser — recursive descent, produce AST
- [ ] P3-04 Implement AST for: variable decl, assignment, arithmetic expr, if/else, while, for, function decl/call, return
- [ ] P3-05 Implement code generator — walk AST, emit 8080 instructions
- [ ] P3-06 Code gen: global variables (at fixed addresses above TPA entry)
- [ ] P3-07 Code gen: local variables (on 8080 stack)
- [ ] P3-08 Code gen: arithmetic (+, -, *, /)
- [ ] P3-09 Code gen: comparisons and conditional jumps
- [ ] P3-10 Code gen: while loop, for loop
- [ ] P3-11 Code gen: function call and return
- [ ] P3-12 Code gen: `putchar()`, `getchar()`, `puts()` as BIOS wrappers
- [ ] P3-13 Emit flat binary `.com` output
- [ ] P3-14 Compiler unit tests: compile each test program, verify binary runs in emulator and produces expected output

---

## PHASE 4 — Visualization

### Tape Bridge
- [ ] P4-01 Implement `tape_bridge.py` — mmap `/tmp/turingos_tape.bin` (64KB)
- [ ] P4-02 Read `/tmp/turingos_meta.bin` — parse state, steps, dirty map, PC
- [ ] P4-03 Expose `get_tape()`, `get_meta()`, `get_pc()`, `get_state()` functions
- [ ] P4-04 Handle missing files gracefully (return None, don't crash)

### Pygame Visualizer
- [ ] P4-05 Set up pygame window 1280×800, title bar, black background
- [ ] P4-06 Implement tape map panel: 256×256 grid of 4×3 pixel cells
- [ ] P4-07 Color-code cells by memory region (see spec §4.8 color table)
- [ ] P4-08 Highlight current PC cell (blinking yellow)
- [ ] P4-09 Flash recently-written (dirty) cells bright then decay
- [ ] P4-10 Implement detail panel: hex+ASCII dump of 256-byte page at clicked address
- [ ] P4-11 Implement legend bar at bottom
- [ ] P4-12 Implement status bar: current state name, step count
- [ ] P4-13 Keyboard: R=reset, P=pause, S=snapshot
- [ ] P4-14 Click handling: click tape map → update detail panel
- [ ] P4-15 Waiting-for-tape splash screen when files absent
- [ ] P4-16 FPS cap at 10
- [ ] P4-17 `make viz` launches `python3 viz/visualizer.py`

---

## PHASE 5 — Tests

### Test Framework
- [ ] P5-01 Write `tests/testfw.h` — `ASSERT`, `TEST`, `RUN_ALL_TESTS` macros

### Emulator Tests
- [ ] P5-02 `tests/emu/test_opcodes.c` — test all opcode groups
- [ ] P5-03 `tests/emu/test_flags.c` — test flag computation edge cases
- [ ] P5-04 `tests/emu/test_mem.c` — test memory boundary conditions

### BIOS + FS Tests
- [ ] P5-05 `tests/bios/test_conout.c` — ring buffer tests
- [ ] P5-06 `tests/fs/test_fs.c` — create/read/write/delete file tests

### Compiler Tests
- [ ] P5-07 `tests/compiler/test_cc.c` — compile each test program, verify binary is non-empty
- [ ] P5-08 Emulator-run compiler output for each of 5 test programs, compare stdout to `.expected`

### Integration Tests
- [ ] P5-09 `tests/integration/test_boot.sh` — boot + prompt appears
- [ ] P5-10 `tests/integration/test_add.sh` — compile and run `add.c`, check output = `3 + 4 = 7`
- [ ] P5-11 `tests/integration/test_strcat.sh` — compile and run `strcat.c`, check `helloworld`
- [ ] P5-12 `tests/integration/test_count.sh` — compile and run `count.c`, check 1–10
- [ ] P5-13 `tests/integration/test_echo.sh` — compile and run `echo.c` with input, check round-trip
- [ ] P5-14 `tests/integration/test_memtest.sh` — compile and run `memtest.c`, check `sum=55`
- [ ] P5-15 `tests/run_tests.sh` — runs all above, reports pass/fail count, exits 0 on all pass
- [ ] P5-16 `make test` calls `run_tests.sh` and forwards exit code

---

## Blockers Log

_None yet. Add blockers here as: `> BLOCKER [P1-XX]: description`_
