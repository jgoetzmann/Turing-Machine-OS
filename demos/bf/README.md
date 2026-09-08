# Brainfuck

Two Brainfuck programs compiled by `bfc` (or `bf NAME` in the shell) into
8080 code. Cells are bytes in the banked window from `0x4000`, on machine
tape 1 when the tapes lever is 2 or 4 and on tape 0 otherwise. `.` is BIOS
`CONOUT`, `,` is `CONIN`, and the program ends with `HLT`.

| file | prints | what to watch |
|---|---|---|
| `hello.bf` | `Hello World!` | the Wikipedia classic: a nested loop seeds seven cells, then short walks between them |
| `nested.bf` | `ABC` | three nested loops multiply 4 x 4 x 4 into cell 2 |

The data pointer is a 16-bit register pair, so the tape map shows a tight
cluster of writes near `0x4000` and nothing else. Switch the tapes lever to 2
and the cluster moves to tape 1 while tape 0's window stays blank. Every `.`
is an `OUT 1`, so the FSM panel passes through SYSCALL once per character.

Anything that is not one of the eight commands is a comment, which is why the
notes at the top of each file avoid periods, commas and hyphens.
