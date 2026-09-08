# Tape fault

A program that deliberately runs off the end of the tape. From `0x4000` it
writes one byte into every cell: through the banked window, the scratch area,
the stack (except the 256 bytes at its top, where `main`'s frame lives), the
display and the metadata block, and finally address `L`, the first cell that
does not exist.

What happens depends on the tape-length lever:

- **32K** (`len=32768`): the write to `0x8000` faults. The kernel lets the
  `STA` finish, then takes `RUNNING -> HALT` with reason `TAPE_FAULT` (4), and
  `turingos` exits with `reason=TAPE_FAULT`.
- **48K**: the same at `0xC000`.
- **64K**: every 16-bit address exists. The walk wraps past `0xFFFF`, prints
  `no fault: tape is 64K`, and returns to the shell.

The address is printed at every 4 KB boundary (`4000`, `5000`, ...), so the
console shows how far it got. The display region is overwritten on the way,
but the host only paints it on a vsync, which never comes on a faulting tape.
The metadata block is overwritten too; the kernel rewrites it after every
step batch.

Run with `cc FAULT.C` then `run FAULT.COM`, or from the playground with
`len=32768`.

## Bounded tape, unbounded tape

A textbook Turing machine has an **unbounded** tape: it can always move one more cell to the right and find a blank. TuringOS's tape is **bounded** (`L` bytes, chosen by the tape-length lever) because the head is a 16-bit program counter and every address has to fit in it. This demo shows the difference. On a 32K tape the walk reaches address `0x8000`, the read returns `0xFF`, the write is dropped, and the kernel halts with `reason=TAPE_FAULT` (halt reason 4, `docs/architecture.md`). On a 64K tape there is no address past the end (`0xFFFF + 1` wraps to `0x0000`), so the same program overwrites the metadata block, the display and eventually its own code, and returns to the shell like any other program. The odometer in the stats panel still counts every cell the head crossed, which is the cost an unbounded-tape machine would pay too.
