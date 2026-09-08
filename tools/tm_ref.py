#!/usr/bin/env python3
"""tm_ref.py - reference interpreter for the TuringOS Turing-machine language (SPEC S5).

usage: python3 tools/tm_ref.py <in.tm> [--tapes 1|2|4] [--len 32768|49152|65536] [--max-steps N]

Prints exactly what the .com produced by `tmc` prints when it runs on a machine
with K tapes (default 1) of L bytes (default 65536):

  - when the machine halts because no rule matches (the halting state is not
    `halt`), that state's name + newline;
  - for each TM tape in order, the cells from the leftmost to the rightmost
    visited position, trimmed of leading and trailing blank symbols (interior
    blanks print as the blank symbol; an all-blank tape prints an empty line),
    followed by a newline;
  - `steps=N` + newline, N = number of rules applied (32-bit wrap, like the .com).

If the TM tapes do not fit the bank window for (K, L) it prints
`line N: too many tapes` (N = the `tapes:` line) exactly like the .com does.
Compile errors go to stderr as `line N: message` with exit status 1.
Standard library only.

Language (mirrors src/lang/tm.c):
  - `#` starts a comment anywhere; blank lines are ignored.
  - directives: `tapes:` (1..4, must precede every rule), `blank:` (one printable
    char, default `_`), `start:` (state name, default: state of the first rule),
    `input:` (text written on TM tape 0 from the head; those cells count as visited).
  - rule: `state read -> write move next`; k-tape groups are `(a,b)`; moves L R S;
    `halt` as the next state stops the machine without printing a name.
  - TM tape j lives on machine tape j mod K at 0x4000 + (j div K)*8192, head at
    +4096; a cell holding byte 0 reads as the blank symbol; addresses are 16-bit.
"""
import sys

MAX_TAPES = 4
MAX_STATES = 64
MAX_SYMS = 64
MAX_RULES = 1024
NAME_MAX = 63
INPUT_MAX = 4095      # TM_INPUT_MAX in src/lang/tm.c: the two must agree or this is not a reference
HALT = -1
WS = ' \t\r\f\v'
_MOVES = {'L': 'L', 'l': 'L', 'R': 'R', 'r': 'R', 'S': 'S', 's': 'S'}


class TmError(Exception):
    def __init__(self, line, msg):
        Exception.__init__(self, 'line %d: %s' % (line, msg))


class TapeFault(Exception):
    pass


class StepLimit(Exception):
    pass


def is_sym(c):
    return 0x21 <= ord(c) <= 0x7E


def split_ws(s):
    """Split on the same whitespace set the C tokenizer uses."""
    toks = []
    cur = []
    for c in s:
        if c in WS:
            if cur:
                toks.append(''.join(cur))
                cur = []
        else:
            cur.append(c)
    if cur:
        toks.append(''.join(cur))
    return toks


def parse_group(tok, nt):
    """'(a,b)' with exactly nt items, or a single char when nt == 1. None = bad."""
    if len(tok) == 1:
        if nt != 1 or not is_sym(tok):
            return None
        return (tok,)
    if tok[0] != '(':
        return None
    i = 1
    out = []
    for j in range(nt):
        if i >= len(tok) or not is_sym(tok[i]):
            return None
        out.append(tok[i])
        i += 1
        if j < nt - 1:
            if i >= len(tok) or tok[i] != ',':
                return None
            i += 1
    if i >= len(tok) or tok[i] != ')':
        return None
    i += 1
    if i != len(tok):
        return None
    return tuple(out)


def parse_moves(tok, nt):
    g = parse_group(tok, nt)
    if g is None:
        return None
    out = []
    for c in g:
        if c not in _MOVES:
            return None
        out.append(_MOVES[c])
    return tuple(out)


class Program(object):
    def __init__(self):
        self.nt = 1
        self.blank = '_'
        self.start = None
        self.input = ''
        self.rules = []      # (state, read tuple, write tuple, move tuple, next)
        self.names = []
        self.tapes_line = 1


def parse(text):
    prog = Program()
    ids = {}
    syms = set()
    start_name = None
    start_line = 1
    lineno = 0

    def state_id(name, line):
        if len(name) == 0 or len(name) > NAME_MAX:
            raise TmError(line, 'bad rule')
        if name in ids:
            return ids[name]
        if len(prog.names) >= MAX_STATES:
            raise TmError(line, 'too many states')
        ids[name] = len(prog.names)
        prog.names.append(name)
        return ids[name]

    for raw in text.split('\n'):
        lineno += 1
        s = raw.split('#', 1)[0].strip(WS)
        if not s:
            continue
        key = s[:6]
        if key in ('tapes:', 'blank:', 'start:', 'input:'):
            val = s[6:].lstrip(WS)
            if key == 'tapes:':
                if len(val) == 0 or len(val) > 3 or prog.rules:
                    raise TmError(lineno, 'bad rule')
                for c in val:
                    if not ('0' <= c <= '9'):
                        raise TmError(lineno, 'bad rule')
                v = int(val)
                if v > MAX_TAPES:
                    raise TmError(lineno, 'too many tapes')
                if v == 0:
                    raise TmError(lineno, 'bad rule')
                prog.nt = v
                prog.tapes_line = lineno
            elif key == 'blank:':
                if len(val) != 1 or not is_sym(val):
                    raise TmError(lineno, 'bad rule')
                prog.blank = val
            elif key == 'start:':
                if len(val) == 0 or len(val) > NAME_MAX:
                    raise TmError(lineno, 'bad rule')
                for c in val:
                    if c in WS:
                        raise TmError(lineno, 'bad rule')
                start_name = val
                start_line = lineno
            else:
                if len(val) > INPUT_MAX:
                    raise TmError(lineno, 'bad rule')
                for c in val:
                    if not (0x20 <= ord(c) <= 0x7E):
                        raise TmError(lineno, 'bad rule')
                prog.input = val
            continue
        toks = split_ws(s)
        if len(toks) != 6 or toks[2] != '->':
            raise TmError(lineno, 'bad rule')
        if toks[0] == 'halt':
            raise TmError(lineno, 'bad rule')
        if len(prog.rules) >= MAX_RULES:
            raise TmError(lineno, 'too many rules')
        rd = parse_group(toks[1], prog.nt)
        wr = parse_group(toks[3], prog.nt)
        mv = parse_moves(toks[4], prog.nt)
        if rd is None or wr is None or mv is None:
            raise TmError(lineno, 'bad rule')
        for c in rd + wr:
            syms.add(c)
            if len(syms) > MAX_SYMS:
                raise TmError(lineno, 'too many symbols')
        st = state_id(toks[0], lineno)
        if toks[5] == 'halt':
            nx = HALT
        else:
            nx = state_id(toks[5], lineno)
        for r in prog.rules:
            if r[0] == st and r[1] == rd:
                raise TmError(lineno, 'duplicate rule')
        prog.rules.append((st, rd, wr, mv, nx))

    if start_name is not None:
        if start_name == 'halt':
            prog.start = HALT
        else:
            prog.start = state_id(start_name, start_line)
    elif prog.rules:
        prog.start = prog.rules[0][0]
    else:
        raise TmError(lineno if lineno else 1, 'bad rule')
    return prog


def run(prog, k, L, max_steps=None):
    """Returns (output_text, error_or_None). Output is exactly what the .com prints."""
    out = []
    nt = prog.nt
    tapes = []  # [head, min, max, machine_tape]
    for j in range(nt):
        q, mt = j // k, j % k
        head = 0x4000 + q * 0x2000 + 0x1000
        tapes.append([head, head, head, mt])
    qmax = (nt - 1) // k
    if L // 256 < 0x80 + qmax * 0x20:
        out.append('line %d: too many tapes\n' % prog.tapes_line)
        return ''.join(out), None

    mem = {}
    blank = prog.blank

    def rd(mt, addr):
        if addr >= L:
            raise TapeFault()
        v = mem.get((mt, addr), 0)
        return blank if v == 0 else chr(v)

    def wr(mt, addr, ch):
        if addr >= L:
            raise TapeFault()
        mem[(mt, addr)] = ord(ch)

    by_state = {}
    for r in prog.rules:
        by_state.setdefault(r[0], []).append(r)

    cur = prog.start
    steps = 0
    try:
        if prog.input:
            t = tapes[0]
            for i, ch in enumerate(prog.input):
                wr(t[3], (t[0] + i) & 0xFFFF, ch)
            t[2] = (t[0] + len(prog.input) - 1) & 0xFFFF
        while cur != HALT:
            syms = tuple(rd(t[3], t[0]) for t in tapes)
            rule = None
            for r in by_state.get(cur, ()):
                if r[1] == syms:
                    rule = r
                    break
            if rule is None:
                # Label state: entered by a transition (steps > 0) and has no rules of its own.
                if steps > 0 and not by_state.get(cur):
                    out.append(prog.names[cur] + '\n')
                break
            for j, t in enumerate(tapes):
                wr(t[3], t[0], rule[2][j])
            for j, t in enumerate(tapes):
                m = rule[3][j]
                if m == 'L':
                    t[0] = (t[0] - 1) & 0xFFFF
                elif m == 'R':
                    t[0] = (t[0] + 1) & 0xFFFF
                if t[0] < t[1]:
                    t[1] = t[0]
                if t[0] > t[2]:
                    t[2] = t[0]
            cur = rule[4]
            steps += 1
            if max_steps is not None and steps >= max_steps and cur != HALT:
                raise StepLimit()
        for t in tapes:
            cells = []
            addr = t[1]
            while True:
                cells.append(rd(t[3], addr))
                if addr == t[2]:
                    break
                addr = (addr + 1) & 0xFFFF
            first = 0
            while first < len(cells) and cells[first] == blank:
                first += 1
            last = len(cells) - 1
            while last >= first and cells[last] == blank:
                last -= 1
            if first <= last:
                out.append(''.join(cells[first:last + 1]))
            out.append('\n')
        out.append('steps=%d\n' % (steps & 0xFFFFFFFF))
    except TapeFault:
        return ''.join(out), 'tape fault (address >= %d)' % L
    except StepLimit:
        return ''.join(out), 'step limit %d reached' % max_steps
    return ''.join(out), None


def usage():
    sys.stderr.write('usage: tm_ref.py <in.tm> [--tapes 1|2|4] [--len 32768|49152|65536] [--max-steps N]\n')


def main(argv):
    path = None
    k = 1
    L = 65536
    max_steps = None
    i = 1
    try:
        while i < len(argv):
            a = argv[i]
            if a == '--tapes' and i + 1 < len(argv):
                k = int(argv[i + 1])
                i += 2
            elif a.startswith('--tapes='):
                k = int(a[len('--tapes='):])
                i += 1
            elif a == '--len' and i + 1 < len(argv):
                L = int(argv[i + 1])
                i += 2
            elif a.startswith('--len='):
                L = int(a[len('--len='):])
                i += 1
            elif a == '--max-steps' and i + 1 < len(argv):
                max_steps = int(argv[i + 1])
                i += 2
            elif a.startswith('--max-steps='):
                max_steps = int(a[len('--max-steps='):])
                i += 1
            elif a.startswith('-') and a != '-':
                usage()
                return 2
            else:
                path = a
                i += 1
    except ValueError:
        usage()
        return 2
    if path is None:
        usage()
        return 2
    if k not in (1, 2, 4) or L not in (32768, 49152, 65536):
        usage()
        return 2
    try:
        if path == '-':
            data = sys.stdin.buffer.read()
        else:
            with open(path, 'rb') as f:
                data = f.read()
    except IOError as e:
        sys.stderr.write('tm_ref: cannot read %s: %s\n' % (path, e))
        return 1
    text = data.decode('latin-1')
    try:
        prog = parse(text)
    except TmError as e:
        sys.stderr.write(str(e) + '\n')
        return 1
    out, err = run(prog, k, L, max_steps)
    sys.stdout.buffer.write(out.encode('latin-1'))
    sys.stdout.flush()
    if err is not None:
        sys.stderr.write('tm_ref: ' + err + '\n')
        return 2
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
