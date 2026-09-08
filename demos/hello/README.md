# Hello: six small console programs

Six tiny-C programs that exercise the console path end to end: `cc` compiles
them on the host side of the ROM boundary, `run` loads the `.com` at `0x0100`,
and every character leaves through BIOS `CONOUT` (`OUT 1` with A=2).

| file | prints | what to watch |
|---|---|---|
| `hello.c` | `Hello, TuringOS!` | RUNNING -> SYSCALL once per character |
| `count.c` | `1` .. `10` | `print_int` filling `digits[]` |
| `echo.c` | the line you typed | `CONIN` parking the machine in IDLE |
| `add.c` | `3 + 4 = 7` | a real `CALL`, arguments on the 8080 stack |
| `strcat.c` | `helloworld` | bytes copied between three `char` arrays |
| `memtest.c` | `sum=55` | ten little-endian 16-bit ints written and read back |

```
A> cc ECHO.C
A> run ECHO.COM
hello
hello
```

`echo` needs one input line (`hello`) after `run ECHO.COM`; the others take no
input. Each `.expected` file holds the exact bytes the program prints, without
the shell prompts. Nothing uses a `puts("answer")` shortcut: numbers come out
of `print_int`, strings out of array copies.
