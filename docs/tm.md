# Turing-Machine Language

A `.tm` file describes a classical Turing machine — states, symbols, a transition table. `tm_compile` (`src/lang/tm.c`) turns it into an 8080 `.com` program whose inner tape lives in TuringOS's banked window, so the visualizer shows a Turing machine running on a Turing machine. It is a host ROM service: `tm F` in the shell (BIOS 0x1B, `F.TM` → `F.COM`), `build/tmc <in.tm> <out.com>` on the host, `tos_compile(TOS_LANG_TM, …)`, or the playground editor.

## Syntax

```
# comment
tapes: 2            # 1..4 (default 1)
blank: _            # single printable char (default '_')
start: q0           # default: the state of the first rule
input: 0110         # initial content of TM tape 1 from the head position (default empty)
q0 0 -> 1 R q1                    # single-tape rule: state read -> write move next
q0 (0,_) -> (0,0) (R,R) q1        # k-tape rule
```

- One rule per line: `state read -> write move next`. For k tapes the read, write and move fields are parenthesised, comma-separated k-tuples.
- Moves: `L`, `R`, `S` (stay).
- Symbols are single printable ASCII characters. A cell holding byte 0 reads as the blank symbol.
- State names are identifiers; `halt` is the halting state.
- The machine halts when the next state is `halt` or when no rule matches the current (state, symbols).
- Directives (`tapes:`, `blank:`, `start:`, `input:`) may appear in any order before the rules.

## Where the tapes live

TM tape j (0-based) is placed on machine tape `j mod k` at bank offset `(j div k) × 8192`, where k is the machine's tape count (read at run time with `IN 04H`). The head starts at offset `+4096` of that region, so it can move 4,096 cells left before hitting the edge. There is no pre-fill: the window is zero at boot and zero reads as blank.

On a 1-tape machine a 2-tape TM stacks its tapes at `0x4000` and `0x6000`; on a 2-tape machine each TM tape gets its own machine tape at `0x4000`, and the strip view can follow either head. If a TM needs more room than the window has — `(j div k) × 8192 + 8192 > window size` — compilation fails with `too many tapes` (a 32K machine has an 8 KB window, so it holds exactly k TM tapes).

## Output

When the machine halts the program prints, for each TM tape in order, the cells from the leftmost to the rightmost visited position, **trimmed of leading and trailing blanks** (interior blanks are shown as the blank symbol; an all-blank tape prints an empty line), followed by a newline; then `steps=N` (N = number of rules applied, decimal, up to 2³²−1) and a newline; then executes `HLT` to return to the shell.

A program may also print before the dump — the palindrome demos print `yes` or `no` from the rule that enters the halting state (`decisions.md` B13).

## Errors

| Error | Cause |
|---|---|
| `line N: bad rule` | Wrong field count, an unknown move (`X`), a tuple of the wrong arity, a malformed directive |
| `line N: too many tapes` | `tapes:` above 4, or more TM tapes than fit in the window |
| `line N: duplicate rule` | Two rules for the same (state, symbols) |

## Example: BB(2)

```
# 2-state busy beaver: 6 steps, 4 ones
blank: _
start: A
A _ -> 1 R B
A 1 -> 1 L B
B _ -> 1 L A
B 1 -> 1 R halt
```

Prints `1111`, then `steps=6`.

## Demos (`demos/tm/`)

| File | Result |
|---|---|
| `bb2.tm` | 4 ones, `steps=6` |
| `bb3.tm` | 6 ones, `steps=14` — the Σ(3) champion (the 21-step S(3) champion leaves only 5 ones) |
| `bb4.tm` | 13 ones, `steps=107` |
| `inc.tm` | binary increment: input `1011` → `1100` |
| `pal1.tm` | 1-tape palindrome checker; prints `yes` for `abba`, `no` for `abca` |
| `pal2.tm` | the same check with `tapes: 2`; copies the input to tape 2 and compares the ends |

`pal1` and `pal2` are the head-travel demo: on a 32-character palindrome the single-tape machine walks the head back and forth across the whole input for every character, and its odometer (`tos_travel_lo`) is more than twice the two-tape machine's (WS6-05).

## Reference interpreter

`python3 tools/tm_ref.py <in.tm>` (standard library only) interprets a `.tm` directly and prints exactly what the compiled program prints — tape dumps and `steps=N` — for cross-checking the compiler. The test suite runs both on every demo and compares them (WS6-04).
