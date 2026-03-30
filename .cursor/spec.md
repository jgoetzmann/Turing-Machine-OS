# TuringOS — Project Specification

**Version:** 1.0  
**Era:** CP/M, ~1980, Intel 8080  
**Model:** Strict Turing Machine  
**Language:** C99 (kernel/emulator), Python 3 + pygame (visualizer)  
**Environment:** Docker (primary), `.venv` (visualizer only)

---

## 1. The Big Idea

TuringOS is an operating system whose architecture is a **direct implementation of a Turing Machine**. This is not a metaphor. Every subsystem maps to a formal TM component. The hardware era is CP/M ~1980, running on an emulated Intel 8080 CPU.

The machine boots, runs a shell, compiles and executes simple C programs, and exposes its entire tape (memory) through a parallel pygame visualization window that renders in retro green-on-black phosphor style.

The TM model is an **architectural law**. Any design decision that breaks the TM mapping is wrong, not a tradeoff.

---

## 2. Turing Machine Mapping

| TM Component | TuringOS Implementation |
|---|---|
| **Tape** | 64KB flat byte array: `uint8_t mem[65536]` — the entire address space |
| **Tape cells** | Individual bytes (alphabet = {0x00 … 0xFF}) |
| **Read/Write Head** | The 8080 Program Counter (`pc`) + memory bus (reads/writes to `mem[]`) |
| **Finite State Control** | The kernel FSM (`state` enum in `kernel.c`) |
| **Transition Function** | The fetch-decode-execute loop + BIOS/syscall dispatcher |
| **Input** | Characters typed at the shell, loaded from disk image into `mem[]` |
| **Output** | Characters written to console port (BIOS I/O), mapped from `mem[]` |
| **Halt state** | `HALT` kernel state — execution stops, OS prints final tape stats |

### Kernel FSM States

```
BOOT ──► IDLE ──► SHELL ──► RUNNING ──► SYSCALL ──► SHELL
                                │                        │
                                └──────────► HALT ◄──────┘
```

State transitions are **only legal inside the transition function** (the main emulation loop + syscall handler). No component may directly mutate the kernel `state` variable except `kernel.c`.

---

## 3. Architecture — Intel 8080

### Why 8080
The Intel 8080 (1974) is the CPU that CP/M was originally written for. It has:
- 8-bit data bus, 16-bit address bus (64KB address space = our tape length)
- 7 general-purpose 8-bit registers: A, B, C, D, E, H, L
- Register pairs: BC, DE, HL (used as 16-bit addresses)
- Stack Pointer (SP) and Program Counter (PC)
- Flags: Sign, Zero, Auxiliary Carry, Parity, Carry
- ~244 opcodes, all 1–3 bytes

### Memory Map (Tape Layout)

```
0x0000 – 0x00FF   BIOS jump table & interrupt vectors      (256 bytes)
0x0100 – 0x3FFF   Transient Program Area (TPA)             (16KB — user programs load here)
0x4000 – 0x7FFF   Kernel heap / OS data structures         (16KB)
0x8000 – 0xBFFF   Shell workspace & compiler scratch       (16KB)
0xC000 – 0xEFFF   Filesystem buffer cache                  (12KB)
0xF000 – 0xFDFF   Stack (grows downward from 0xFDFF)       (3.5KB)
0xFE00 – 0xFEFF   BIOS I/O ports (memory-mapped)           (256 bytes)
0xFF00 – 0xFFFF   Kernel state / TM bookkeeping            (256 bytes)
```

The visualization tool **color-codes these regions** on the tape display.

---

## 4. Component Specifications

### 4.1 8080 Emulator (`src/emu/`)

**Files:** `cpu.c`, `cpu.h`, `mem.c`, `mem.h`

**Responsibility:** Accurately emulate the Intel 8080 instruction set. This is the "head" that reads and writes the tape.

**`mem.h` interface:**
```c
typedef uint8_t  tape_t;
typedef uint16_t addr_t;

void     mem_init(void);
uint8_t  mem_read(addr_t addr);
void     mem_write(addr_t addr, uint8_t val);
uint8_t *mem_raw(void);   // returns pointer to mem[65536] for visualizer bridge
```

**`cpu.h` interface:**
```c
typedef struct {
    uint8_t  a, b, c, d, e, h, l;
    uint16_t sp, pc;
    uint8_t  flags;   // bit: S Z AC P CY
    int      halted;
} cpu_t;

void    cpu_init(cpu_t *cpu);
void    cpu_step(cpu_t *cpu);          // execute one instruction (one head move)
void    cpu_reset(cpu_t *cpu);
int     cpu_halted(const cpu_t *cpu);
```

**Requirements:**
- Implement all 8080 opcodes. Use a 256-entry dispatch table (function pointers or switch).
- Each call to `cpu_step()` is exactly one TM transition: read tape cell at PC, decode, execute (possibly write tape), advance PC.
- `HLT` instruction must set `cpu->halted = 1` and trigger HALT state in kernel.
- Do not use `setjmp`/`longjmp`. Do not use signals for flow control.
- Cycle counting is optional for Phase 1 but the struct should have a `uint64_t cycles` field reserved.

**Must pass:** all tests in `tests/emu/`

---

### 4.2 BIOS (`src/bios/`)

**Files:** `bios.c`, `bios.h`

**Responsibility:** The BIOS is the I/O layer between the emulated 8080 and the host machine. It intercepts reads/writes to the memory-mapped I/O region (0xFE00–0xFEFF) and the BIOS call convention (RST instructions or a dedicated OUT port).

**CP/M BIOS call convention:** Programs execute `OUT 0x01, <function_id>` to trigger a BIOS call. The BIOS dispatcher inspects register A for the function.

**BIOS functions (minimum viable set):**

| ID | Name | Description |
|----|------|-------------|
| 0x01 | CONIN | Read one character from stdin → register A |
| 0x02 | CONOUT | Write register C to stdout |
| 0x03 | PUNCH / AUXOUT | Write to aux output (can be no-op) |
| 0x04 | READER / AUXIN | Read from aux input (can be no-op) |
| 0x09 | SELDISK | Select disk A (only one disk image supported) |
| 0x0A | SETTRK | Set current track |
| 0x0B | SETSEC | Set current sector |
| 0x0C | SETDMA | Set DMA (transfer) address in `mem[]` |
| 0x0D | READ | Read one sector from disk image into DMA address |
| 0x0E | WRITE | Write one sector from DMA address to disk image |

**`bios.h` interface:**
```c
void bios_init(void);
void bios_dispatch(cpu_t *cpu);   // called by emulator loop when OUT port 0x01 fires
int  bios_pending_output(void);   // returns 1 if there's a char to print
char bios_get_output(void);       // pops one char from output queue
```

**Requirements:**
- BIOS must never directly `printf`. It queues output into a ring buffer. The kernel drains the ring buffer each tick.
- BIOS reads from a disk image file (`disk.img`, raw sector format, described in §4.5).
- Terminal I/O uses raw mode (disable echo, no line buffering) on the host terminal.

---

### 4.3 Kernel FSM (`src/kernel/`)

**Files:** `kernel.c`, `kernel.h`

**Responsibility:** The kernel is the finite state control of the Turing Machine. It owns the `state` variable. It runs the main loop, driving the emulator one step at a time, checking for state transitions after each step.

**`kernel.h` interface:**
```c
typedef enum {
    KS_BOOT    = 0,
    KS_IDLE    = 1,
    KS_SHELL   = 2,
    KS_RUNNING = 3,
    KS_SYSCALL = 4,
    KS_HALT    = 5
} kernel_state_t;

typedef struct {
    kernel_state_t state;
    cpu_t          cpu;
    uint64_t       steps;      // total TM steps taken (tape head moves)
    uint32_t       tick;       // wall-clock ticks
} kernel_t;

void kernel_init(kernel_t *k);
void kernel_run(kernel_t *k);   // main loop — never returns until HALT
kernel_state_t kernel_state(const kernel_t *k);
```

**Main loop pseudocode (this IS the transition function):**
```
loop:
    switch state:
        BOOT:
            load BIOS jump table into tape[0x0000..0x00FF]
            load shell binary into tape[0x0100..]
            set cpu.pc = 0x0100
            transition → SHELL

        IDLE:
            drain BIOS output queue → host terminal
            sleep 1ms
            if input pending → transition → SHELL

        SHELL:
            cpu_step(&cpu)
            k->steps++
            if bios OUT port fired → bios_dispatch(&cpu)
            if cpu.halted → transition → HALT
            drain BIOS output queue

        RUNNING:
            cpu_step(&cpu)
            k->steps++
            if bios OUT port fired → bios_dispatch(&cpu)
            if cpu.halted → transition → SHELL  (program finished, back to shell)
            drain BIOS output queue

        SYSCALL:
            handled inside bios_dispatch, transitions back to RUNNING or SHELL

        HALT:
            print_tape_stats(k)
            return
```

**Requirements:**
- `kernel_run` is a tight loop — no `sleep()` except in IDLE state.
- The kernel must write the current `state` value to `mem[0xFF00]` every tick (visualizer reads this).
- The kernel must write `steps` (low 4 bytes) to `mem[0xFF01..0xFF04]` every tick.
- The kernel must maintain a 256-byte "dirty map" at `mem[0xFF10..0xFF0F]` — 1 bit per 256-byte page, set when that page was written this tick. This is how the visualizer knows what's active.

---

### 4.4 Filesystem (`src/fs/`)

**Files:** `fs.c`, `fs.h`

**Responsibility:** A minimal CP/M-style flat filesystem on top of a raw disk image file (`disk.img`). No directories. Files are identified by 8.3 names.

**Disk image format:**
- 256 bytes per sector
- 26 sectors per track
- 77 tracks (standard 8" floppy geometry, CP/M heritage)
- Total: ~512KB disk image
- Track 0 is reserved for the boot loader and directory
- Directory: 64 entries of 32 bytes each, starting at sector 0 of track 0

**Directory entry format (32 bytes):**
```
[0]     status (0xE5 = deleted, 0x00 = active)
[1-8]   filename (8 chars, space-padded)
[9-11]  extension (3 chars, space-padded)
[12]    extent number
[13-14] reserved
[15]    record count in this extent
[16-31] allocation map (16 x 1-byte block numbers)
```

**`fs.h` interface:**
```c
int  fs_init(const char *disk_image_path);
int  fs_open(const char *name);              // returns file handle (0–15) or -1
int  fs_read(int fh, uint8_t *buf, int len);
int  fs_write(int fh, const uint8_t *buf, int len);
void fs_close(int fh);
int  fs_list(char names[][13], int max);     // list all files → returns count
int  fs_delete(const char *name);
int  fs_exists(const char *name);
void fs_flush(void);
```

**Requirements:**
- The disk image is a real file on the host (`disk.img`). BIOS READ/WRITE calls route through `fs.c`.
- All sector I/O goes through the 12KB buffer cache (`mem[0xC000..0xEFFF]`), mapped as a direct window into `mem[]`.
- No `malloc`. All filesystem state (open file table, etc.) uses statically allocated arrays.

---

### 4.5 Shell (`src/shell/`)

**Files:** `shell.c`, `shell.h`, and the shell **8080 binary** (`bin/shell.com`)

**Responsibility:** An interactive shell that runs as an 8080 program in the TPA (0x0100). The shell uses BIOS calls for I/O and dispatches user commands.

**Shell commands (minimum):**

| Command | Description |
|---------|-------------|
| `dir` | List files on disk |
| `type <file>` | Print file contents |
| `run <file>` | Load and execute a `.com` binary |
| `cc <file.c>` | Compile a `.c` source file to `.com` using the built-in compiler |
| `del <file>` | Delete a file |
| `cls` | Clear screen |
| `mem` | Print tape memory map summary |
| `halt` | Trigger HALT state |
| `help` | Print command list |

**Implementation note:** The shell is compiled to 8080 machine code and embedded as a binary blob in `bin/shell.com`. It is loaded into the TPA at boot. The shell source can be written either as raw 8080 assembly (`shell.asm`) or as a minimal C program compiled by the host-side tiny C compiler. The assembler/compiler toolchain for producing `shell.com` runs on the host (not inside the emulator).

**Requirements:**
- The shell prompt is `A> ` (CP/M style).
- Input line buffer max: 128 characters.
- Shell must support Backspace (0x08) for line editing.
- Shell must not crash on unknown commands — print `?` and re-prompt.

---

### 4.6 Tiny C Compiler (`src/compiler/`)

**Files:** `compiler.c`, `compiler.h`

**Responsibility:** A minimal subset C compiler that runs on the **host** (not inside the emulator), takes a `.c` source file, and produces a flat 8080 binary (`.com` file) that can be loaded into the TPA.

This compiler also runs *inside the emulator* when the user types `cc <file.c>` — the shell invokes the compiler binary (a pre-built `.com` file) which reads the source from disk and writes the output `.com` to disk, all via BIOS calls.

**Supported C subset:**

```
- Data types: char, int (8-bit and 16-bit respectively)
- Variables: global and local (on 8080 stack)
- Arithmetic: +, -, *, / (integer only)
- Comparison: ==, !=, <, >, <=, >=
- Logic: &&, ||, !
- Control: if/else, while, for
- Functions: declaration and call (no recursion required in Phase 1)
- Strings: char arrays, string literals
- I/O: putchar(c), getchar(), puts(s)
- Memory: manual array indexing only (no malloc)
```

**NOT supported (Phase 1):**
- Pointers beyond array indexing
- Structs
- Floats
- Preprocessor (`#include`, `#define` are no-ops or fatal errors)
- Standard library (putchar/getchar are BIOS wrappers)

**Compiler pipeline:**
```
Source (.c) → Lexer → Parser → AST → Code Generator → 8080 binary (.com)
```

**`compiler.h` interface:**
```c
int  cc_compile(const char *src_path, const char *out_path);
// returns 0 on success, -1 on error. Prints errors to stderr.
```

**Requirements:**
- Single-pass or two-pass is fine. No optimization required.
- Generated code must be position-independent (relocatable to 0x0100).
- The `.com` format: raw binary, no header. Entry point is byte 0, loaded at 0x0100 in TPA.

---

### 4.7 Test Suite (`tests/`)

**Structure:**
```
tests/
  emu/           — unit tests for 8080 emulator (opcode tests)
  bios/          — unit tests for BIOS dispatch
  fs/            — unit tests for filesystem read/write
  compiler/      — compiler output tests
  integration/   — full-stack tests (boot → shell → run program → check output)
  programs/      — 5 test C programs (see below)
    add.c        — add two numbers, print result
    strcat.c     — concatenate two strings, print result
    count.c      — count from 1 to 10, print each number
    echo.c       — read a line, print it back
    memtest.c    — fill an array with values, sum them, print result
  run_tests.sh   — master test runner script
```

**Test harness:**
- Unit tests use a minimal hand-rolled test framework (no external libs): `tests/testfw.h`
- `testfw.h` provides: `ASSERT(expr)`, `TEST(name, body)`, `RUN_ALL_TESTS()`
- Integration tests: boot the OS headlessly (no terminal I/O, feed input via pipe), capture stdout, compare against expected output files (`*.expected`)

**The 5 integration test programs and their expected outputs:**

| File | Input | Expected stdout |
|------|-------|----------------|
| `add.c` | none | `3 + 4 = 7` |
| `strcat.c` | none | `helloworld` |
| `count.c` | none | `1\n2\n3\n4\n5\n6\n7\n8\n9\n10` |
| `echo.c` | `hello\n` | `hello` |
| `memtest.c` | none | `sum=55` (1+2+…+10) |

**`make test` must exit 0 iff all tests pass.**

**Mandatory workflow rule:** After every code change, run `make test` and do not consider the task complete until the full test suite passes.

---

### 4.8 Pygame Visualizer (`viz/`)

**Files:** `visualizer.py`, `tape_bridge.py`, `config.py`

**Responsibility:** A separate Python process that runs alongside the OS, reads the tape memory in real time via a shared memory file (or UNIX pipe), and renders a retro phosphor-green visualization of the full 64KB tape.

**Communication mechanism:**
- The kernel writes the full `mem[65536]` to a memory-mapped file: `/tmp/turingos_tape.bin` (64KB, updated every N kernel ticks where N is configurable).
- The kernel also writes a 64-byte "metadata" header to `/tmp/turingos_meta.bin`:
  - `[0]` current kernel state (0–5)
  - `[1-4]` step count (uint32 LE)
  - `[5-68]` dirty page map (64 bits = one bit per 1KB region)
  - `[8]` current PC high byte, `[9]` current PC low byte

**`tape_bridge.py`:** handles reading both files, parsing metadata, exposing Python dicts.

**Visualization layout (pygame window, 1280×800):**

```
┌─────────────────────────────────────────────────────────────┐
│  TuringOS Tape Visualizer        [STATE: SHELL]  steps: 1234│
├───────────────────┬─────────────────────────────────────────┤
│                   │                                         │
│  TAPE MAP         │  DETAIL PANEL                           │
│  (hex grid,       │  Zoom into 256-byte page at cursor      │
│   64KB as         │  Show hex + ASCII                       │
│   256×256 cells,  │  Highlight PC position                  │
│   color-coded     │  Show register values                   │
│   by region)      │                                         │
│                   │                                         │
├───────────────────┴─────────────────────────────────────────┤
│  LEGEND:  [BIOS]  [TPA]  [KERNEL]  [SHELL]  [FS CACHE]     │
│           [STACK] [IO]   [TM META] [HEAD►0x0134]            │
└─────────────────────────────────────────────────────────────┘
```

**Color scheme (phosphor green on black):**

| Region | Color |
|--------|-------|
| BIOS vectors (0x0000–0x00FF) | Bright green |
| TPA — user program (0x0100–0x3FFF) | Lime green |
| Kernel heap (0x4000–0x7FFF) | Dim green |
| Shell workspace (0x8000–0xBFFF) | Teal |
| FS cache (0xC000–0xEFFF) | Dark cyan |
| Stack (0xF000–0xFDFF) | Amber (active region) |
| I/O ports (0xFE00–0xFEFF) | Red-orange (hot) |
| TM metadata (0xFF00–0xFFFF) | Bright white |
| **Current PC position** | Blinking bright yellow cell |
| Recently written (dirty) | Flash bright then decay to region color |

**Features:**
- Click any cell in the tape map → detail panel zooms to that 256-byte page
- Press `R` to reset visualizer (re-read files)
- Press `P` to pause/unpause live updates
- Press `S` to take a snapshot (save current tape to `snapshot_NNNN.bin`)
- FPS target: 10 fps (retro feel, low CPU)
- Font: monospace, green-on-black, ideally a bitmap font like `PressStart2P` or fallback to `Courier`

**Requirements:**
- `visualizer.py` must run independently (`python3 viz/visualizer.py`) — it does not import any kernel C code.
- If `/tmp/turingos_tape.bin` doesn't exist, show a "WAITING FOR TAPE..." splash screen and poll every second.
- The window title must be: `TuringOS Tape — [current state] — step NNNN`

---

## 5. Directory Structure

```
turingos/
├── Makefile
├── Dockerfile
├── docker-compose.yml
├── .cursor/
│   └── rules/
│       └── rules.mdc
├── spec.md
├── progress.md
├── remember.md
├── disk.img                   (generated by make, not committed to git)
├── bin/
│   └── shell.com              (pre-built shell binary)
├── src/
│   ├── main.c                 (entry point: init kernel, run)
│   ├── emu/
│   │   ├── cpu.c
│   │   ├── cpu.h
│   │   ├── mem.c
│   │   └── mem.h
│   ├── bios/
│   │   ├── bios.c
│   │   └── bios.h
│   ├── kernel/
│   │   ├── kernel.c
│   │   └── kernel.h
│   ├── fs/
│   │   ├── fs.c
│   │   └── fs.h
│   ├── shell/
│   │   ├── shell.c            (shell source, compiled to bin/shell.com by make)
│   │   ├── shell.h
│   │   └── shell.asm          (optional: handwritten 8080 asm for shell)
│   └── compiler/
│       ├── compiler.c
│       └── compiler.h
├── tests/
│   ├── testfw.h
│   ├── emu/
│   ├── bios/
│   ├── fs/
│   ├── compiler/
│   ├── integration/
│   ├── programs/
│   │   ├── add.c
│   │   ├── strcat.c
│   │   ├── count.c
│   │   ├── echo.c
│   │   └── memtest.c
│   └── run_tests.sh
├── tools/
│   └── mkdisk.c               (host tool: creates/populates disk.img)
└── viz/
    ├── visualizer.py
    ├── tape_bridge.py
    └── config.py
```

---

## 6. Build System

### Makefile targets

| Target | Description |
|--------|-------------|
| `make all` | Build everything: OS binary, tools, disk image |
| `make run` | Build + run the OS in the terminal |
| `make test` | Run full test suite, exit 0 on pass |
| `make disk` | (Re)create a fresh `disk.img` |
| `make shell` | Compile shell source → `bin/shell.com` |
| `make clean` | Remove all build artifacts |
| `make viz` | Launch the pygame visualizer (separate window) |

### Docker

`Dockerfile` is based on `debian:bookworm-slim`. It installs:
- `gcc`, `make`, `binutils` (for building the OS)
- `python3`, `python3-pygame` (for the visualizer)

`docker-compose.yml` defines two services:
- `turingos` — builds and runs the OS
- `visualizer` — runs the pygame visualizer, sharing `/tmp` with `turingos`

### Python .venv (visualizer only, non-Docker)

```bash
python3 -m venv viz/.venv
source viz/.venv/bin/activate
pip install pygame
python3 viz/visualizer.py
```

---

## 7. Build Phases

### Phase 1 — Foundation (must complete before Phase 2)
1. Build system (Makefile + Dockerfile) scaffolding
2. Memory/tape implementation (`mem.c`)
3. 8080 CPU emulator (`cpu.c`) — all opcodes
4. BIOS I/O layer (`bios.c`)
5. Kernel FSM (`kernel.c`) — main loop
6. Disk image tool (`tools/mkdisk.c`)
7. Filesystem (`fs.c`)

### Phase 2 — OS Layer
8. Shell source + build to `shell.com`
9. Boot: load shell into TPA, run
10. Kernel writes tape/metadata files for visualizer

### Phase 3 — Compiler
11. Tiny C compiler — lexer
12. Tiny C compiler — parser + AST
13. Tiny C compiler — 8080 code generator

### Phase 4 — Visualization
14. `tape_bridge.py` — shared memory reader
15. `visualizer.py` — pygame tape renderer
16. Color coding, PC cursor, dirty flash

### Phase 5 — Tests
17. Emulator unit tests
18. BIOS + FS unit tests
19. Compiler output tests
20. Integration tests with 5 C programs
21. `make test` passing clean

---

## 8. What This Is NOT

- It is not an accurate cycle-perfect 8080 emulator for running commercial CP/M software (though it may accidentally work)
- It is not a general-purpose OS with virtual memory, process isolation, or security
- It is not designed for performance — correctness and clarity of the TM model come first
- It does not support multiple disks, users, or concurrent processes
- It is not designed to be extended beyond Phase 5

---

## 9. Glossary

| Term | Definition |
|------|------------|
| Tape | `mem[65536]` — the entire emulated address space |
| Head | The 8080 PC and memory bus — the entity that reads/writes the tape |
| State | The `kernel_state_t` enum value — the TM's finite state |
| Transition | One call to `cpu_step()` followed by kernel state evaluation |
| TPA | Transient Program Area — where user `.com` programs are loaded (0x0100) |
| COM file | Flat binary 8080 executable, loaded at address 0x0100 |
| BIOS | Basic I/O System — I/O bridge between 8080 and host |
| Dirty page | A 256-byte memory page written during the current tick |
| Tape bridge | `tape_bridge.py` — reads the shared memory files the kernel writes |
