# Tiny-C

Tiny-C is the C subset that TuringOS programs, its demos and its own shell are written in. The compiler (`src/compiler/compiler.c`) is host code — a "ROM service" reached through BIOS 0x16 (`cc F` in the shell), the `build/cc_driver` CLI, `tos_compile(TOS_LANG_C, …)` and the playground editor. Its output is a plain `.com` image that runs on the emulated 8080.

## Grammar

```
program     := { func_decl | global_decl }
global_decl := [ "__at" "(" number ")" ] type ident [ "[" number "]" ] [ "=" init ] ";"
init        := expr | string | "{" number { "," number } "}"
type        := "int" | "char"
func_decl   := type ident "(" [ param { "," param } ] ")" block          (* <= 4 params *)
param       := type ident
block       := "{" { stmt } "}"
stmt        := type ident [ "=" expr ] ";"                                  (* scalar locals only *)
             | "if" "(" expr ")" stmt [ "else" stmt ]
             | "while" "(" expr ")" stmt
             | "do" stmt "while" "(" expr ")" ";"
             | "for" "(" [ type ident "=" expr | expr ] ";" [ expr ] ";" [ expr ] ")" stmt
             | "break" ";" | "continue" ";" | "return" [ expr ] ";"
             | block | expr ";" | ";"
expr        := assign
assign      := lvalue ( "=" | "+=" | "-=" | "*=" | "/=" | "%=" | "&=" | "|=" | "^=" | "<<=" | ">>=" ) assign | logor
lvalue      := ident | ident "[" expr "]"
logor       := logand { "||" logand }
logand      := bitor { "&&" bitor }
bitor       := bitxor { "|" bitxor }
bitxor      := bitand { "^" bitand }
bitand      := equality { "&" equality }
equality    := relational { ( "==" | "!=" ) relational }
relational  := shift { ( "<" | "<=" | ">" | ">=" ) shift }
shift       := additive { ( "<<" | ">>" ) additive }
additive    := term { ( "+" | "-" ) term }
term        := unary { ( "*" | "/" | "%" ) unary }
unary       := ( "-" | "!" | "~" | "++" | "--" ) unary | postfix
postfix     := primary [ "++" | "--" ]
primary     := number | charlit | ident | ident "[" expr "]" | ident "(" [ expr { "," expr } ] ")" | "(" expr ")"
```

Lexical rules: `//` and `/* … */` comments; numbers are decimal or `0x` hex; character literals `'A'`, `'\n'`,
`'\0'`, `'\\'`, `'\''`; strings use the same escapes, and also `\t \r \a \b \f \v \e` and `\xNN`; identifiers are at
most 31 characters. Lines beginning with `#` are ignored (there is no preprocessor).

## Semantics

- `int` is a signed 16-bit two's-complement value; `char` is unsigned 8-bit. All arithmetic is 16-bit; chars zero-extend when read and truncate when stored (`char c = 300` stores 44). `300 * 100` is 30000; `65535 + 1` is 0.
- `<`, `<=`, `>`, `>=` are signed comparisons (`-1 < 1` is 1). `/` and `%` truncate toward zero (`-5 / 2` is −2); division by zero yields 0. `>>` is a logical shift.
- `!`, `&&`, `||` and the comparisons yield 0 or 1. Any non-zero value is true. `&&` and `||` short-circuit.
- Arrays are **global only**. `int a[N]` stores 2-byte little-endian elements; `char s[N]` stores bytes. Indexing is unchecked and the index may be any `int` expression. `char s[N] = "txt"` zero-pads; `int a[3] = {1, 2, 3}` initialises in order. Declaring an array inside a function is the error `local arrays are not supported`.
- A string literal may appear only as an initialiser or as the argument of `puts`.
- Globals are zero unless initialised and are laid out in declaration order after the code. `__at(A) type name[N];`
  places a global at absolute address A, where A is any constant expression. It occupies no image space, which is
  the way to name the display (`__at(0xFE00) char vram[256];`) or any other tape region; for the same reason it
  cannot have an initialiser, and one is rejected with `__at variables cannot have an initialiser`.
- Functions may be called before they are defined; recursion works; locals live on the 8080 stack (at most 32 per
  function, at most 4 parameters). `f(void)` is accepted for a function with no parameters. A call has to pass
  exactly as many arguments as the function declares, or compilation stops with `wrong number of arguments`,
  because the callee reads its parameters at fixed offsets from the frame. A function without `return` returns 0.
  `main` is the entry point and its return value is ignored.
- Names are case-sensitive. Intrinsic names cannot be redefined.

## Intrinsics

Recognised by name and compiled inline. Every intrinsic returns an `int` (0 when the underlying syscall returns nothing). `bios(fn, c)` is the general escape hatch: A = fn, C = c, `OUT 01H`, result = A.

| Intrinsic | Does | BIOS / instruction |
|---|---|---|
| `putchar(c)` | write one byte to the console | CONOUT 0x02 |
| `getchar()` | read one byte (parks until one exists) | CONIN 0x01 |
| `puts(s)` | print a string literal or `char` array, then `\n` | CONOUT per byte |
| `peek(a)` / `poke(a, v)` | read / write a tape byte | `LDA` / `STA` |
| `peekw(a)` / `pokew(a, v)` | read / write a little-endian 16-bit word | `LHLD` / `SHLD` |
| `inp(p)` / `outp(p, v)` | port I/O | `IN` / `OUT` |
| `bios(fn, c)` | raw syscall, returns A | `OUT 01H` |
| `kbhit()` | 0xFF if a console byte is ready | CONST 0x05 (= `bios(5, 0)`) |
| `vsync()` | wait for the next host frame | VSYNC 0x06 |
| `rand()` | next PRNG byte | RAND 0x07 |
| `ticks()` | frame counter & 0xFF | TICKS 0x08 |
| `keys()` | key bitmask | `IN 03H` (= `inp(3)`) |
| `tape(n)` | select tape n for the banked window | `OUT 02H` (= `outp(2, n)`) |
| `seldisk(n)` | select disk 0 or 1; returns 0 on success, 1 if that disk is not mounted | SELDISK 0x09 |
| `listdir()` | print the directory | LISTDIR 0x0F |
| `namech(c)` | append a character to the name buffer | NAMECH 0x12 |
| `namend()` | type the named file | TYPE 0x13 |
| `runend()` | run the named `.com` | RUN 0x14 |
| `delend()` | delete the named file | DEL 0x15 |
| `ccend()` / `asmend()` / `tmend()` / `bfend()` | compile the named source | CC 0x16 / ASM 0x1A / TM 0x1B / BF 0x1C |
| `readline()` | read a line into the BIOS line buffer | READLINE 0x17 |
| `lineget(i)` | byte i of the line, 0 past the end | LINEGET 0x18 |
| `linelen()` | length of the line | LINELEN 0x19 |

Key bits for `keys()`: W 1, S 2, UP 4, DOWN 8, SPACE 16, ESC 32, ENTER 64, ANY 128. `inp(4)` returns the tape count and `inp(5)` the tape length divided by 256 (`0x80` 32K, `0xC0` 48K, `0` 64K).

## Image layout and limits

```
0x0100  CALL main
0x0103  HLT                 ; returning from main halts the program (the shell reloads)
0x0104  code for every function, in source order
        runtime helpers (multiply, divide, shift, compare, puts, ...; only the ones used)
        data: globals in declaration order, with initial values
        string literals
```

- The loader sets SP; the compiler **must not emit `LXI SP`**. The same binary therefore runs on a 32K, 48K or 64K tape.
- Output ≤ 16,128 bytes (the TPA). Source ≤ 32,768 bytes. At most 256 globals and 64 functions. Expressions nest
  at most 96 deep; past that the compiler stops with `expression nests too deeply` rather than running out of
  its own stack.
- `&&` / `||` intermediates are kept in registers and on the stack — there is no fixed scratch address, so a program larger than 8 KB whose code crosses `0x20FC` is safe (WS1-13).

## Diagnostics

`cc_compile_buf` returns −1 and writes `src.c:LINE:COL: message` (1-based). Messages:

| Message | Cause |
|---|---|
| `expected ';'` | missing statement terminator |
| `expected ')'` | unbalanced call, condition or grouping |
| `undefined function 'name'` | call to a function that is never defined |
| `undefined variable 'name'` | use of an undeclared identifier |
| `too many locals` | more than 32 locals in one function |
| `program too large` | image would exceed 16,128 bytes |
| `unexpected token` | anything the grammar cannot place |
| `local arrays are not supported` | `int x[2];` inside a function |

The `cc_driver` CLI prints the same text to stderr and exits 1; inside the OS, `cc F` prints it to the console and writes no `.COM`.

## Examples

Print a number (there is no `printf`; the demos define `print_int`):

```c
char buf[8];                          /* arrays are global only */

int print_int(int n) {
    int i = 0;
    if (n < 0) { putchar('-'); n = -n; }
    do { buf[i++] = '0' + n % 10; n /= 10; } while (n);
    while (i > 0) putchar(buf[--i]);
    return 0;
}
```

Draw on the display:

```c
__at(0xFE00) char vram[256];          /* 64K tape; use inp(5) to find the display on other lengths */

int main() {
    int y = 0;
    while (y < 32) { vram[y * 8] = 0x80; y++; }   /* left column */
    while (!(keys() & 32)) vsync();               /* until ESC */
    return 0;
}
```

Talk to the BIOS directly:

```c
int main() {
    int c;
    puts("type a line:");
    readline();
    c = 0;
    while (c < linelen()) { putchar(lineget(c)); c++; }
    putchar('\n');
    return 0;
}
```

## What is deliberately missing

Pointers, structs, `switch`, the ternary operator, `sizeof`, floats, local arrays, string variables other than `char[]`, a preprocessor, more than 4 parameters, and any library beyond the intrinsics. `decisions.md` B16 explains the scope.
