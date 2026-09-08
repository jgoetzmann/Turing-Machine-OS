# Turing machines

Six machines in the TM language, compiled by `tmc` (or `tm NAME` in the shell)
into 8080 programs that run on the machine's own tapes (TM tape `j` on
machine tape `j mod k`, head at `0x5000`).

| file | machine | prints |
|---|---|---|
| `bb2.tm` | 2-state busy beaver | `1111`, `steps=6` |
| `bb3.tm` | 3-state busy beaver | `111111`, `steps=14` |
| `bb4.tm` | 4-state busy beaver | `10111111111111`, `steps=107` |
| `inc.tm` | increment `1011` | `1100`, `steps=8` |
| `pal1.tm` | one-tape palindrome check | `yes` / `no` |
| `pal2.tm` | two-tape palindrome check (`tapes: 2`) | `yes` / `no` |

On halt each program prints the final state's name when it is not `halt`,
then each TM tape from the leftmost to the rightmost visited cell, blanks
trimmed at both ends, then `steps=N`. The tests use the inputs `abba` ->
`yes` and `abca` -> `no`; edit the `input:` line to try others.

The busy beavers are pure head motion, so watch the travel counter.
`pal1` is quadratic in the input length and `pal2` is linear: on a
32-character palindrome the one-tape run travels more than twice as far.
