# TuringOS — Remember

> **APPEND ONLY. Never delete or overwrite entries.**  
> This file is the institutional memory of the project.  
> Format for new entries:
> ```
> ## [YYYY-MM-DD] <component> — <short title>
> <body>
> ```

---

## [PROJECT INIT] Architecture — TM model is a hard constraint

The Turing Machine mapping is not a metaphor or a design pattern. It is a hard constraint.

- Tape = `mem[65536]`. No exceptions. No allocations outside this array from inside the emulator.
- Head = 8080 PC. The only way to "move the head" is to execute 8080 instructions.
- States = the `kernel_state_t` enum. The kernel FSM is the finite state control.
- Transition function = `cpu_step()` + `bios_dispatch()`. These are the ONLY legal state-transition sites.

Any architecture that violates this mapping must be rejected, even if it would be simpler or faster.

---

## [PROJECT INIT] Architecture — Why Intel 8080, not Z80

Z80 is an extension of the 8080 that adds additional instructions (IX/IY registers, DJNZ, etc.). Targeting Z80 would require implementing a larger, more complex instruction set. The 8080 is the minimal complete 8-bit CPU that CP/M ran on historically, and it keeps the emulator scope tight. If a Z80-specific instruction appears in any test program or shell source, it must be replaced with 8080 equivalents.

---

## [PROJECT INIT] Architecture — `.com` file format

`.com` files are raw flat binaries. No headers. No relocation tables. No segments. The OS loads the entire file into memory starting at address `0x0100`. The entry point is always byte 0 of the file (address `0x0100` in memory). The compiler must generate position-dependent code assuming base address `0x0100`.

---

## [PROJECT INIT] Architecture — No malloc in kernel or emulator

The kernel and emulator code must use zero heap allocation (`malloc`, `calloc`, `realloc` are forbidden in `src/`). All data structures are statically declared. This is not a performance optimization — it is a correctness requirement. The "memory" of this system is `mem[65536]`, and allocating host memory for kernel data structures would break the TM model.

The only exception is the compiler (`src/compiler/`), which runs on the host and may use `malloc` for its AST nodes during compilation. The compiler is a host tool, not part of the emulated system.

---

## [PROJECT INIT] Architecture — Filesystem buffer cache in `mem[]`

The filesystem buffer cache is located at `mem[0xC000..0xEFFF]` (12KB). This means disk sectors read by the BIOS are written directly into the tape. This is intentional — it makes disk I/O visible on the tape visualization and keeps the TM model clean. The FS code must never use a separate host-side buffer for sector data.

---

## [PROJECT INIT] Architecture — Tape snapshot frequency

The kernel writes the tape snapshot to `/tmp/turingos_tape.bin` every 1000 ticks. This is a tunable constant (`#define TAPE_SNAP_INTERVAL 1000` in `kernel.h`). If the visualizer feels sluggish or the OS feels slow due to file I/O, reduce this to 5000 or increase to 100. The default of 1000 is a starting point, not a guarantee.

---

## [PROJECT INIT] Known pitfalls — 8080 flag computation

The 8080 flag register has several non-obvious behaviors that are frequently wrong in emulator implementations:

1. **Auxiliary Carry (AC)**: Set when there is a carry out of bit 3 into bit 4. Most ops set this; it is used by `DAA`. Easy to forget.
2. **Parity (P)**: Set when the number of set bits in the result is even. Must be computed for all arithmetic/logic ops.
3. **DAA**: The Decimal Adjust Accumulator instruction is complex. It adjusts A after BCD arithmetic using the AC and CY flags. Implement this carefully and test it explicitly.
4. **PUSH PSW / POP PSW**: Pushes/pops the A register and flags register together as a 16-bit pair. The flags are packed in a specific bit layout: `S Z 0 AC 0 P 1 CY`. Bit 1 is always 1, bits 3 and 5 are always 0. Get this wrong and the stack will corrupt A.
5. **CMP**: Does not modify the accumulator. It performs a subtraction and sets flags but discards the result. Easy to accidentally write the result back.

These are all well-documented in the Intel 8080 Programmer's Manual. The test suite must cover each of these edge cases.

---

## [PROJECT INIT] Known pitfalls — CP/M BIOS calling convention

This implementation uses `OUT 0x01` as the BIOS call trigger (port 0x01). The function ID is in register A. This differs slightly from standard CP/M which uses a jump table at 0x0000. Both approaches are used in real CP/M implementations. We use the `OUT` port approach because it's cleaner for the emulator to intercept than scanning jumps. The shell and compiler must use this convention. Document any deviation here.

---

## [PROJECT INIT] Known pitfalls — pygame shared memory on macOS

On macOS, `/tmp` is actually `/private/tmp`. The mmap path in `tape_bridge.py` should use `os.path.realpath('/tmp/turingos_tape.bin')` to resolve the symlink. Docker avoids this issue entirely. If the visualizer silently fails to find the tape file on macOS, this is likely the cause.

---

## [PROJECT INIT] Known pitfalls — raw terminal mode

The BIOS CONIN function must put the host terminal into raw mode (no echo, no line buffering) so the shell can read individual keystrokes. On POSIX this means using `tcsetattr` with `ICANON` and `ECHO` cleared. The kernel must restore the terminal to cooked mode on `KS_HALT` or on any unhandled signal (set up a `SIGINT` handler that restores terminal state before exit). Failure to restore terminal state leaves the user's terminal broken after the OS exits.

---

## [PROJECT INIT] Decision — C99 not C11

The codebase uses C99. C11 added `_Alignas`, `_Atomic`, `_Generic`, `_Static_assert`, and other features we don't need. Using C99 keeps the code more portable and avoids any temptation to use VLAs (which are technically optional in C11) or threads. The compiler flag must be `-std=c99`.

---

## [PROJECT INIT] Decision — Docker is primary, venv is visualizer-only

The full OS build and test suite runs in Docker. This ensures reproducibility. The Python virtualenv in `viz/.venv` is provided as a convenience for running the visualizer without Docker, but it is not the primary development path. Agents working on the visualizer may use either. Agents working on the kernel/emulator must use Docker or a system with gcc + make installed.

---

_Add new entries below this line._
## [2026-03-30] build/disk — Initial scaffold and bootstrap build
Bootstrapped the repository from docs-only state into a compilable C99 project skeleton.
Added `Makefile`, `Dockerfile`, `docker-compose.yml`, core directory layout, C stub modules, and `tools/mkdisk.c`.
Validated with `make all`, `make run`, and `make disk`; `disk.img` is generated at 512,512 bytes with directory bytes initialized to `0xE5`.
## [2026-03-30] emu/mem — Boundary tests wired into make test
Added `tests/emu/test_mem.c` to validate zero-init and read/write round-trips at boundary addresses (`0x0000`, `0x7FFF`, `0xFFFF`) and to verify `mem_raw()` exposes the same backing tape bytes.
Replaced placeholder `make test` behavior with `tests/run_tests.sh` so Phase 1 memory verification runs as part of the normal build flow.
## [2026-03-30] emu/cpu — Data-transfer decoder foundation
Implemented fetch/decode in `cpu_step()` with working data-transfer opcodes: `MOV`, `MVI`, `LXI`, `LDA`, `STA`, `LHLD`, `SHLD`, `XCHG`, `XTHL`, `SPHL`, `PCHL`; also wired `NOP` and `HLT` handling in the same decoder pass.
Added `tests/emu/test_cpu_data_transfer.c` and integrated it into `tests/run_tests.sh` to validate core register/memory transfer paths and `HLT` behavior while the remaining opcode groups are still pending.
## [2026-03-30] emu/cpu — Arithmetic opcodes and baseline flag behavior
Implemented arithmetic opcode coverage in `cpu_step()` for `ADD/ADI/ADC`, `SUB/SUI/SBB/SBI`, `INR/DCR`, `INX/DCX`, `DAD`, and `DAA`, including baseline updates for `S`, `Z`, `P`, `AC`, and `CY` where those instructions define flag changes.
Added `tests/emu/test_cpu_arithmetic.c` and wired it into `tests/run_tests.sh` so arithmetic register effects, carry/zero behavior, 16-bit increments/decrements, and a DAA sanity path are now regression-tested.
## [2026-03-30] emu/cpu — Logic and rotate opcodes
Implemented `ANA/ANI`, `ORA/ORI`, `XRA/XRI` with `CY` and `AC` cleared and `S/Z/P` from result; `CMP/CPI` set flags like `SUB` without changing `A`; `RLC/RRC/RAL/RAR` update `CY` only; `CMA` leaves flags; `STC`/`CMC` affect carry only.
Added `tests/emu/test_cpu_logic.c` and hooked it into `tests/run_tests.sh`.
## [2026-03-30] emu/cpu — Branch, call, and return opcodes
Implemented `JMP` and all conditional jump forms, `CALL` and all conditional call forms, and `RET` plus all conditional return forms. Added helper routines for 16-bit immediate fetch, stack push/pop for return addresses, and condition decoding from flags.
Added `tests/emu/test_cpu_branch.c` and wired it into `tests/run_tests.sh` to validate taken/not-taken branches, unconditional call/return behavior, and conditional call/return flow.
## [2026-03-30] emu/cpu — Stack PUSH/POP including PSW packing
Implemented `PUSH`/`POP` for `BC`, `DE`, `HL`, and `PSW`. For `PSW`, flag bytes are packed/unpacked as `S Z 0 AC 0 P 1 CY` so bit 1 stays set and bits 3/5 remain clear.
Added `tests/emu/test_cpu_stack.c` and integrated it into `tests/run_tests.sh` to validate stack pointer movement, memory byte order, and PSW round-trip behavior.
## [2026-03-30] emu/cpu — IN/OUT latch for BIOS dispatch
Implemented `IN` (`0xDB`) and `OUT` (`0xD3`). `IN` reads from a CPU-hosted 256-byte input port table (`io_in_ports`) into register `A`; `OUT` captures `A` into an output latch (`io_out_value`) with `io_out_port` and `io_out_pending` so kernel/BIOS code can observe and clear it.
Added `tests/emu/test_cpu_io.c` and integrated it into `tests/run_tests.sh` to verify IN reads, OUT latch values, and pending-flag behavior across successive writes.
## [2026-03-30] emu/cpu — Control opcodes RST/EI/DI/RIM/SIM
Implemented `RST n` as a call-like vector jump (push return address, jump to `n*8`), plus `EI`, `DI`, `RIM`, and `SIM`. Added small CPU state fields for interrupt enable and simple `rim_value`/`sim_value` latches to support deterministic tests and future BIOS/kernel integration.
Added `tests/emu/test_cpu_control.c` and integrated it into `tests/run_tests.sh` to validate interrupt-toggle behavior, RIM/SIM data flow, and RST stack/PC behavior.
## [2026-03-30] emu/cpu — Corrected AND auxiliary carry semantics
Adjusted flag logic so `ANA/ANI` set `AC=1` and clear `CY`, while `ORA/ORI` and `XRA/XRI` keep both `AC` and `CY` cleared. This aligns with Intel 8080 flag behavior and closes a subtle mismatch in earlier logic handling.
Expanded `tests/emu/test_cpu_logic.c` to assert `AC/CY` expectations for `ANI`, `XRI`, and `ORI`.
## [2026-03-30] tests/emu — Opcode group smoke harness
Added `tests/emu/test_opcodes.c` as a compact opcode-by-group verification pass that exercises representative instructions across all implemented groups (data transfer, arithmetic, logic, branch, stack, I/O, control) and confirms core register/PC/stack effects.
Integrated the new test into `tests/run_tests.sh` so every `make test` run now includes the opcode-group smoke suite.
## [2026-03-30] tests/emu — Dedicated HLT and boundary flag coverage
Added `tests/emu/test_cpu_hlt.c` to assert explicit `HLT` semantics: `cpu->halted` is set, `PC` advances by one for the `HLT` fetch, and subsequent `cpu_step()` calls are no-ops while halted.
Added `tests/emu/test_cpu_flags.c` to cover boundary flag behavior for `ADD`, `SUB`, `AND`, `OR`, and `XOR`, with explicit checks for `S`, `Z`, `AC`, `P`, and `CY` where applicable. Wired both tests into `tests/run_tests.sh`.
## [2026-03-30] bios — Dispatch table and output queue tests
Implemented BIOS dispatch handling for function IDs `0x01`–`0x0E`, with concrete behavior for `CONIN` (`A <- getchar()` or `0` on EOF) and `CONOUT` (enqueue register `C` into the BIOS output ring), while keeping remaining IDs as explicit stubs for later FS integration.
Kept `bios_pending_output()` / `bios_get_output()` as the queue drain interface and made dispatch clear `cpu->io_out_pending` after handling.
Added `tests/bios/test_bios.c` and integrated it into `tests/run_tests.sh` to verify queue init/drain behavior, repeated wraparound-safe enqueue/dequeue cycles, `CONIN` read path via `ungetc`, and pending-flag clearing.
## [2026-03-30] bios/fs — Disk parameter stubs and sector delegation wiring
Implemented BIOS disk-parameter state for `SELDISK`, `SETTRK`, `SETSEC`, and `SETDMA` with defaults reset by `bios_init()`, then used these values in `READ`/`WRITE` dispatch handling.
Wired BIOS `READ` (`0x0D`) and `WRITE` (`0x0E`) to `fs_read_sector` / `fs_write_sector` hooks and return status in `A` (`0` success, `1` failure), including a DMA bounds guard for 256-byte sector transfers.
Added `fs_read_sector` / `fs_write_sector` declarations and temporary stub implementations in `fs.c` (currently return `-1` until filesystem sector I/O is implemented). Extended `tests/bios/test_bios.c` to validate disk parameter persistence and READ/WRITE delegation status behavior.
## [2026-03-30] kernel — FSM loop and TM metadata writes
Implemented a first real `kernel_run()` FSM loop with `BOOT`, `IDLE`, `SHELL`, `RUNNING`, `SYSCALL`, and `HALT` handling. `SHELL` and `RUNNING` execute one `cpu_step()` per loop, update `steps`, and route BIOS syscall triggers through `KS_SYSCALL` when CPU `OUT` port `0x01` is pending.
Added BIOS output draining in-loop via `bios_pending_output()` / `bios_get_output()` and host `putchar`.
Kernel now writes TM metadata each tick: state byte to `mem[0xFF00]`, low 32 bits of `steps` to `mem[0xFF01..0xFF04]`, and a placeholder zeroed 32-byte dirty-page map at `mem[0xFF10..0xFF2F]`.
BOOT currently places a temporary `HLT` at `0x0100` and jumps there so the stub system exits deterministically until shell loading is implemented.
## [2026-03-30] kernel — Periodic `/tmp` tape and meta snapshots
Added periodic snapshot writing in `kernel_run()` every `TAPE_SNAP_INTERVAL` ticks (defined as `1000` in `kernel.h`), plus a final flush on exit.
`/tmp/turingos_tape.bin` is written as the raw 64KB tape image from `mem_raw()`. `/tmp/turingos_meta.bin` is written as a compact 64-byte header with state (`[0]`), step count LE (`[1..4]`), dirty map bytes copied from `mem[0xFF10..]` (`[5..36]`), and current PC high/low (`[37..38]`).
This keeps visualizer-facing output stable while dirty-map granularity remains the current 32-byte (256-page) in-memory representation.
## [2026-03-30] fs — Disk geometry validation and sector I/O
Implemented `fs_init()` to open a disk image in read/write mode, verify exact CP/M geometry size (`77 * 26 * 256 = 512512` bytes), and retain an internal file handle for sector operations.
Implemented `fs_read_sector()` / `fs_write_sector()` with track/sector validation (track `0..76`, sector `1..26`), deterministic offset mapping, and 256-byte fixed sector transfers.
Added `tests/fs/test_fs_sector.c` and integrated it into `tests/run_tests.sh` to verify valid init, read/write round-trip, out-of-range rejection, and geometry rejection for malformed disk files.
## [2026-03-30] fs — Directory scan and fs_open handle allocation
Added in-memory directory refresh logic that reads the first 2048 bytes (64 x 32-byte entries) and marks active records where status byte is `0x00`.
Implemented `fs_open()` name matching against CP/M 8.3 directory entries by normalizing input names to uppercase, space-padded 11-byte form, then returning a handle from a 16-slot static open table.
Extended `tests/fs/test_fs_sector.c` to create active directory entries on disk and verify `fs_open()` success for existing files, rejection for missing/invalid names, and handle-range behavior.
## [2026-03-30] fs — Sequential read/write through allocation map
Implemented per-handle file cursor state and wired `fs_read()` to resolve sequential reads through directory allocation-map entries (`entry[16..31]`) in 256-byte sector chunks.
Implemented `fs_write()` with on-demand free-sector allocation, read-modify-write for partial sectors, allocation map updates, and directory entry persistence; file size tracking uses `entry[15]` as sectors used for the current extent.
Extended `tests/fs/test_fs_sector.c` with a `DATA.BIN` round-trip using `fs_write` then `fs_read`, verifying exact payload recovery and EOF behavior once the handle cursor reaches file size.
## [2026-03-30] fs — List/delete/exists API behavior
Implemented `fs_list()` by scanning active directory entries and formatting CP/M 8.3 names into host strings (`NAME.EXT`, uppercase, no trailing spaces). Return value now reports total active entry count.
Implemented `fs_exists()` using normalized CP/M name matching against refreshed directory state.
Implemented `fs_delete()` to mark entry status `0xE5`, clear allocation map bytes for the extent, persist the directory entry, and invalidate any open handles pointing at that directory slot.
Extended `tests/fs/test_fs_sector.c` to verify list ordering/content, existence checks, successful delete semantics, and failure on repeated delete attempts.
## [2026-03-30] fs — Dirty tracking and explicit flush semantics
Introduced filesystem dirty-state tracking so write paths (`fs_write_sector` and directory-entry persistence) mark the backing disk as dirty instead of forcing immediate `fflush` on each update.
Updated `fs_flush()` to flush only when dirty and clear the dirty flag on successful sync, aligning behavior with "write dirty sectors back" semantics while preserving existing correctness through explicit flush points and file close.
## [2026-03-30] tests/fs — Covered create/readback and list/delete flows
Confirmed `tests/fs/test_fs_sector.c` now explicitly covers both remaining Phase 1 filesystem test goals: (1) create/open-write-close-reopen-readback-compare (`DATA.BIN`) and (2) directory list/delete verification (`fs_list`, `fs_delete`, and `fs_exists` checks).
No additional FS test binary was required because the existing FS test suite already executes these assertions under `make test`.
## [2026-03-30] shell — Command parsing foundation for Phase 2
Replaced the shell stub with a concrete shell core API in `src/shell/shell.c`/`shell.h`: prompt rendering (`A> `), command parsing to a typed command enum, argument capture, and minimal render behavior (`help`, unknown command `?`, generic placeholder response for recognized commands).
This intentionally establishes the command-dispatch skeleton for upcoming command-specific tasks (`dir`, `type`, `run`, `cc`, `del`, `cls`, `mem`, `halt`) without prematurely implementing their filesystem/kernel side effects.
## [2026-03-30] shell — Implemented `dir` and `type` through FS APIs
Added concrete handlers in `shell_render_result()` for `dir` and `type <file>` commands. `dir` initializes the filesystem against `disk.img`, lists active entries through `fs_list`, and prints one name per line (or `(empty)` when no files are present).
`type` now validates an argument, opens the requested file via `fs_open`, streams bytes with repeated `fs_read` calls, and writes content to the shell output stream. Both commands use `?` as an error response on missing args or FS/open failures.
## [2026-03-30] shell — Implemented `run <file>` TPA loader hook
Extended shell state with a run-request handshake (`run_requested`, `run_entry`) and updated `run` command handling to load file bytes from FS into TPA starting at `0x0100` via `mem_write`.
After successful load, shell now sets `run_requested=1` and `run_entry=0x0100`, providing a deterministic hook for kernel integration to set `cpu.pc` and transition to `KS_RUNNING`.
## [2026-03-30] shell — Added cc/del/cls/mem/halt command handlers
Implemented `cc` command wiring to `cc_compile(src, out)` with `.com` output path derivation from the provided source name.
Implemented `del` through filesystem APIs (`fs_init("disk.img")` + `fs_delete`), `cls` via ANSI clear sequence, and `mem` as a fixed tape-region summary aligned to the spec map.
Added a halt handshake flag (`halt_requested`) so `halt` command can signal kernel-facing termination intent while still producing shell output.
## [2026-03-30] shell/build — Line editing and shell.com build path
Added shell line-editing support with explicit backspace handling (`0x08` and `0x7F`) via `shell_line_push_char`, bounded 128-character input buffering, newline completion, and reset/access helpers.
Replaced the `make shell` placeholder flow with a host compile pipeline: `tools/cc_driver.c` now calls `cc_compile` to generate `bin/shell.com` from `src/shell/shell.c`.
Updated `cc_compile` stub to emit a deterministic minimal `.com` binary (includes source checksum bytes and `HLT`) so shell build and command wiring are functional until the full compiler backend is implemented.
## [2026-03-30] kernel boot — BIOS vectors and shell.com loading
Updated BOOT state to initialize `mem[0x0000..0x00FF]` (BIOS vector region) with a defined table image and to load `bin/shell.com` bytes into TPA starting at `0x0100` from the host filesystem.
BOOT now sets `cpu.pc = 0x0100` and transitions to `KS_SHELL` directly. If shell loading fails, BOOT falls back to a `HLT` at `0x0100` to preserve deterministic termination in current stub mode.
## [2026-03-30] integration — Added Phase 2 boot/halt scripts
Added `tests/integration/test_boot.sh` and `tests/integration/test_halt.sh` and wired them into `tests/run_tests.sh`.
Given the current stub runtime still exits immediately with `state=5`, integration assertions currently validate successful boot execution and HALT termination via `build/turingos` output checks; shell-interactive I/O assertions will need tightening once kernel-shell command loop is fully wired.
## [2026-03-30] build/shell — Keep generated disk image under build/
Adjusted disk generation so `mkdisk` now writes to `build/disk/disk.img` (and `make disk` ensures the parent folder exists) instead of emitting `disk.img` in the repo root.
Updated shell filesystem command handlers to use the same build-scoped disk path, keeping runtime-generated artifacts out of the top-level repository workspace.
## [2026-03-30] repo hygiene — Artifact locations normalized
Clarified artifact policy in spec/progress: generated disk image lives in `build/disk/disk.img` and generated shell binary lives in `bin/shell.com`, both treated as non-source artifacts.
Expanded `.gitignore` guidance to keep generated outputs out of commits, with `build/` as default sink and `bin/shell.com` explicitly ignored.
## [2026-03-30] repo hygiene — Shell binary moved under build/
Standardized all generated artifacts under `build/` by moving shell binary output from `bin/shell.com` to `build/bin/shell.com`.
Updated producer/consumer paths (`make shell`, kernel boot loader, spec/progress docs) so runtime loading now targets `build/bin/shell.com`; `.gitignore` now relies on `build/` instead of a separate top-level `bin` rule.
## [2026-03-30] compiler/parser — Recursive-descent AST pass integrated into compile flow
Implemented `cc_parse()` in `src/compiler/compiler.c` and exposed it in `compiler.h`. The parser now consumes lexer output and builds an AST node pool covering top-level declarations/functions, variable declarations/assignments, expression precedence (`||`..unary), calls, and control statements (`if/else`, `while`, `for`, `return`).
The parser is now executed from `cc_compile()` after lexing; compile fails early on parse errors instead of silently emitting a `.com` for invalid source. Kept node storage static-array based in the existing host compile pass to stay deterministic and avoid broad compiler-architecture changes while codegen remains pending.
## [2026-03-30] compiler/codegen — Initial AST walker emits real 8080 bytes
Added a first `cc_codegen` path in `src/compiler/compiler.c` that walks parsed AST nodes and emits 8080 machine code into a flat `.com` byte buffer rather than always writing a checksum stub.
Current supported subset is intentionally narrow: function body block traversal, `return` statements, literal expressions, and `+`/`-` binary expressions (result in register `A`), ending with `HLT`. To preserve existing `make shell` and broad-source compatibility during incremental compiler bring-up, `cc_compile` now falls back to the previous deterministic checksum-stub emission when unsupported AST shapes are encountered.
## [2026-03-30] compiler/codegen — Added symbol-backed variable load/store
Extended the code generator to resolve identifier nodes via token/source slices, maintain a fixed-size symbol table, and emit `LDA/STA` for identifier reads and assignments. Variable declarations now allocate symbols and store either initializer values or zero defaults.
Top-level variable declarations are now reserved at fixed addresses starting at `0x2000` (above TPA entry), satisfying the current global-variable placement milestone while keeping local stack-based variables and control-flow lowering for subsequent tasks.
## [2026-03-30] compiler/codegen — Local variables lowered as SP-relative storage
Added distinct global/local symbol tables in codegen and switched local declaration resolution to SP-relative slots. Local identifier loads/stores now emit `LXI H,offset; DAD SP; MOV A,M` and `MOV B,A; LXI H,offset; DAD SP; MOV M,B` patterns, while globals continue to use absolute `LDA/STA`.
Codegen now emits a simple function prologue `LXI SP,0xFDFF` before function-body lowering so local slots are anchored to the 8080 stack region. This advances local-variable support while preserving existing fallback behavior for still-unsupported control-flow and call semantics.
