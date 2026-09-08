# Brainfuck

Two Brainfuck programs compiled by `bfc` (or `bf NAME` in the shell) into
8080 code. Cells are bytes in the banked window from `0x4000`, on machine
tape 1 when the tapes lever is 2 or 4 and on tape 0 otherwise. `.` is BIOS
`CONOUT`, `,` is `CONIN`, and the program ends with `HLT`.

| file | prints | what to watch |
|---|---|---|
| `hello.bf` | `Hello World!` | the Wikipedia classic: a nested loop seeds seven cells, then short walks between them |
| `nested.bf` | `ABC` | three nested loops multiply 4 x 4 x 4 into cell 2 |

The prologue clears every cell first, so the tape map fills 30,000 cells from
`0x4000` in one sweep, and the program then works inside a tight cluster near
the start of that range. Switch the tapes lever to 2 and both the sweep and the
cluster move to tape 1 while tape 0's window stays blank. Every `.` is an
`OUT 1`, so the FSM panel passes through SYSCALL once per character.

Anything that is not one of the eight commands is a comment, which is why the
notes at the top of each file avoid periods, commas and hyphens.
