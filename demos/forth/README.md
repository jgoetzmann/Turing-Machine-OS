# Forth

A Forth interpreter in tiny-C, running on the 8080 inside TuringOS. In the shell: `cc FORTH.C`, then `run FORTH.COM`. The prompt `ok` appears at start and after every line.

The session checked by `session.expected`:

```
: sq dup * ;
7 sq .
bye
```

prints `ok`, `ok`, then `49 ok` (`.` prints the number followed by a space). `bye` returns to the shell.

Words: `+ - * / mod dup drop swap over rot . emit key cr @ ! c@ c! : ; if else then begin until do loop i bye words`, plus `= < > and or negate` (true is -1). Numbers are decimal, negatives allowed. Names are case-sensitive. An unknown word prints `? name` and drops the rest of the line; `if`, `begin` and `do` work only inside a `:` definition.

What to watch: all Forth state sits in the bank window of tape 0. Data stack at `0x4000`, return stack at `0x4080`, dictionary from `0x4100`, threaded code from `0x4800`: define a word and cells appear there. Memory from `0x5900` up is free: `1234 22784 ! 22784 @ .` round-trips a value. On a 64K tape `128 -512 c!` lights the top-left pixel.
