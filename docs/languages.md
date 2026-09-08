# Languages

TuringOS speaks four languages, and each of them compiles to the same thing: a flat 8080 `.com` image loaded at `0x0100`. Two more things run on the machine without a compiler of their own: the shell and a Forth interpreter, both written in tiny-C.

## What runs where

This is the honest split (`decisions.md` A7): the *tools* that turn source into machine code run on the host, as ROM services; the *programs* they produce, and everything the user interacts with, run on the emulated 8080.

| Thing | Runs on | How it is reached |
|---|---|---|
| tiny-C compiler (`cc`) | host, a ROM service (BIOS 0x16) | `cc F` in the shell, `build/cc_driver`, `tos_compile(0, …)`, editor |
| 8080 assembler (`asm`) | host, a ROM service (BIOS 0x1A) | `asm F`, `build/asm`, `tos_compile(1, …)`, editor |
| TM language compiler (`tm`) | host, a ROM service (BIOS 0x1B) | `tm F`, `build/tmc`, `tos_compile(2, …)`, editor |
| Brainfuck compiler (`bf`) | host, a ROM service (BIOS 0x1C) | `bf F`, `build/bfc`, `tos_compile(3, …)`, editor |
| The shell | **the 8080** | `src/shell/shell_tpa.c`, tiny-C, embedded at boot |
| Forth | **the 8080** | `demos/forth/forth.c`, tiny-C, runs in the TPA |
| Every demo | **the 8080** | `demos/**`, compiled by the tools above |
| The compiled TM / BF program | **the 8080** | the `.com` the tool produced |

Why: a C compiler that runs *inside* 16 KB on an 8080 is a project in itself, and the 8080 already has plenty to do. From the machine's point of view the compilers are firmware: a fixed function reached by one `OUT 01H`, like a ROM routine. The shell compiling itself (`cc SHELL.C` → a byte-identical `SHELL.COM`) is the proof that the loop closes. Self-hosting `cc` stays a stretch goal.

In the shell, a compile command reads `NAME.EXT` from the selected disk, writes `NAME.COM` (overwriting) and flushes the image; on error it prints the tool's diagnostic and writes nothing. `run NAME.COM` loads it into the TPA and executes it.

## tiny-C

The workhorse: 16-bit `int`, unsigned `char`, global arrays, the full C operator set, `if/while/do/for/break/continue`, recursion, and a set of intrinsics that map onto the BIOS and the I/O ports. The shell, Pong, Life, Forth and the `hello` programs are written in it. Reference: `tiny-c.md`.

```c
int main() { puts("Hello, TuringOS!"); return 0; }
```

## 8080 assembly

Every 8080 mnemonic, `ORG/DB/DW/DS/EQU/END`, labels, `$`, expressions, two passes. `build/disasm` reverses it. Reference: `asm.md`.

```
        ORG 0100H
        MVI C,'!'
        MVI A,02H      ; CONOUT
        OUT 01H
        HLT
```

## Turing-machine language

States, symbols, rules; single- or multi-tape; halts on `halt` or when no rule matches; prints the visited tape and `steps=N`. The inner tape lives in the banked window, so the visualizer shows a TM running on the TM. Reference: `tm.md`.

```
A _ -> 1 R B
A 1 -> 1 L B
B _ -> 1 L A
B 1 -> 1 R halt
```

## Brainfuck

The eight commands `> < + - . , [ ]`; every other character is ignored.

- Cells are bytes on machine tape 1 (the compiled program issues `OUT 02H` with A = 1) when the machine has two or more tapes (`IN 04H ≥ 2`), otherwise on tape 0. Either way they start at `0x4000` in the banked window, so on a 2-tape machine the BF tape and the program's own code and stack sit on different tapes.
- Cell count = min(30,000, window size); the pointer starts at cell 0.
- The prologue clears every cell to 0 before the first command runs, so a program never inherits a previous run's tape (you can watch the sweep in the tape map). The cell count is the same min(30000, window) computed at run time.
- `,` reads `CONIN` and `.` writes `CONOUT`. Reading past the end of the console input halts the machine with `TOS_HALT_EOF`, the same as any other program that reads past the end of its input. The program ends with `HLT`.
- Errors: `line N: unmatched '['`, `line N: unmatched ']'`.
- `build/bfc <in.bf> <out.com>` on the host; `bf F` in the shell; `tos_compile(3, …)`.

```
++++++++[>++++[>++>+++>+++>+<<<<-]>+>+>->>+[<]<-]>>.>---.+++++++..+++.>>.<-.<.+++.------.--------.>>+.>++.
```

`demos/bf/hello.bf` prints `Hello World!`; `nested.bf` prints `ABC` through nested loops.

## Forth (a program, not a tool)

`demos/forth/forth.c` is a small Forth interpreter written in tiny-C that runs inside the TPA: a REPL that prints `ok` after each line, 16-bit cells, `: name … ;` definitions, arithmetic and stack words, `.` (prints the number followed by a space), and `bye` to halt. `: sq dup * ;` then `7 sq .` prints `ok`, `ok`, `49 ok`. It exists to show a language interpreter running *on* the machine rather than beside it.

## Using the tools

| From | tiny-C | asm | tm | bf |
|---|---|---|---|---|
| Shell | `cc F` | `asm F` | `tm F` | `bf F` |
| Host CLI | `build/cc_driver in.c out.com` | `build/asm in.asm out.com` | `build/tmc in.tm out.com` | `build/bfc in.bf out.com` |
| C / wasm API | `tos_compile(TOS_LANG_C, …)` | `TOS_LANG_ASM` | `TOS_LANG_TM` | `TOS_LANG_BF` |
| Playground | editor → Compile, language selector | same | same | same |
| Disk image | `build/mkdisk img --add file.c` then `cc` inside | `--add file.asm` | `--add file.tm` | `--add file.bf` |

All four share one signature: `int tool(const char *src, uint32_t len, uint8_t *out, uint32_t cap, char *err, uint32_t errcap)` returning the image length or −1 with a `line N: …` (or `src.c:L:C: …` for tiny-C) message in `err`. None of them emits `LXI SP`: the loader sets the stack pointer for every tape length (`decisions.md` B14).
