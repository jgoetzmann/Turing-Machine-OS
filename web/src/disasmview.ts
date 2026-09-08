/* Disassembly-view helpers. Dependency-free so Node can import this file directly. */

export interface DisasmLine {
  addr: number;
  bytes: number[];
  text: string;
  len: number;
}

/** Decoder callback shape (Engine.disasm has exactly this shape). */
export type Decoder = (addr: number) => { text: string; len: number };
/** Memory read callback: returns the byte at addr. */
export type Reader = (addr: number) => number;

export function hex8(n: number): string {
  return (n & 0xff).toString(16).toUpperCase().padStart(2, '0');
}

export function hex16(n: number): string {
  return (n & 0xffff).toString(16).toUpperCase().padStart(4, '0');
}

/** Parse an address typed by a person. Bare digits are HEX (the fields are labelled "(hex)"):
 *  "0100", "100", "0x0100", "100H" and "$100" all mean 0x0100. Decimal needs a "d" suffix ("256d").
 *  Returns null when the text is not an address. */
export function parseAddress(text: string): number | null {
  const t = String(text).trim();
  let m = /^0x([0-9a-f]{1,4})$/i.exec(t);
  if (m) return parseInt(m[1], 16);
  m = /^([0-9a-f]{1,4})h$/i.exec(t);
  if (m) return parseInt(m[1], 16);
  m = /^\$([0-9a-f]{1,4})$/i.exec(t);
  if (m) return parseInt(m[1], 16);
  m = /^(\d{1,5})d$/i.exec(t);
  if (m) {
    const n = parseInt(m[1], 10);
    return n <= 0xffff ? n : null;
  }
  m = /^[0-9a-f]{1,4}$/i.exec(t);
  if (m) return parseInt(t, 16);
  return null;
}

/**
 * Intel 8080 instruction length by opcode (1..3), including the undocumented aliases
 * (0xCB = JMP, 0xDD/0xED/0xFD = CALL, 0x08/0x10/... = NOP). Mirrors cpu_opcode_len.
 */
export function opcodeLen(opcode: number): number {
  const op = opcode & 0xff;
  switch (op) {
    case 0x01: case 0x11: case 0x21: case 0x31: // LXI
    case 0x22: case 0x2a: case 0x32: case 0x3a: // SHLD LHLD STA LDA
    case 0xc3: case 0xcb: // JMP, JMP*
    case 0xc2: case 0xca: case 0xd2: case 0xda: case 0xe2: case 0xea: case 0xf2: case 0xfa: // Jcc
    case 0xcd: case 0xdd: case 0xed: case 0xfd: // CALL, CALL*
    case 0xc4: case 0xcc: case 0xd4: case 0xdc: case 0xe4: case 0xec: case 0xf4: case 0xfc: // Ccc
      return 3;
    case 0x06: case 0x0e: case 0x16: case 0x1e: case 0x26: case 0x2e: case 0x36: case 0x3e: // MVI
    case 0xc6: case 0xce: case 0xd6: case 0xde: case 0xe6: case 0xee: case 0xf6: case 0xfe: // ADI..CPI
    case 0xd3: case 0xdb: // OUT IN
      return 2;
    default:
      return 1;
  }
}

/** "3E 05" padded to three byte slots: "3E 05   ". */
export function formatBytes(bytes: readonly number[]): string {
  const parts: string[] = [];
  for (let i = 0; i < 3; i++) parts.push(i < bytes.length ? hex8(bytes[i]) : '  ');
  return parts.join(' ');
}

/** One listing line in the tools/disasm style: "0100: 3E 05     MVI A,05H". */
export function formatDisasmLine(line: DisasmLine): string {
  return `${hex16(line.addr)}: ${formatBytes(line.bytes)}  ${line.text}`;
}

/** Decode `count` consecutive instructions starting at `start` (wraps at 0xFFFF). */
export function decodeRange(read: Reader, decode: Decoder, start: number, count: number): DisasmLine[] {
  const out: DisasmLine[] = [];
  let addr = start & 0xffff;
  for (let i = 0; i < count; i++) {
    const d = decode(addr);
    const len = Math.min(3, Math.max(1, d.len | 0));
    const bytes: number[] = [];
    for (let j = 0; j < len; j++) bytes.push(read((addr + j) & 0xffff) & 0xff);
    out.push({ addr, bytes, text: d.text, len });
    addr = (addr + len) & 0xffff;
    if (addr < out[out.length - 1].addr) break; // wrapped past 0xFFFF
  }
  return out;
}

/**
 * Find a start address at most `before` instructions before `pc` such that decoding forward from
 * it lands exactly on `pc`. 8080 code cannot be decoded backwards unambiguously, so this tries a
 * few anchors and keeps the first walk that reaches `pc` exactly; if none does, the window starts
 * at `pc` itself.
 */
export function alignedStart(pc: number, before: number, lenAt: (addr: number) => number): number {
  const target = pc & 0xffff;
  if (before <= 0) return target;
  const span = before * 3 + 6;
  for (let k = 0; k < 6; k++) {
    let anchor = target - span - k;
    if (anchor < 0) anchor = 0;
    if (anchor >= target) return target;
    const walk: number[] = [];
    let a = anchor;
    let guard = 0;
    while (a < target && guard++ < span * 2 + 8) {
      walk.push(a);
      const len = Math.min(3, Math.max(1, lenAt(a) | 0));
      a += len;
    }
    if (a === target) {
      const idx = Math.max(0, walk.length - before);
      return walk.length ? walk[idx] : target;
    }
  }
  return target;
}

/** Instructions around pc: up to `before` lines before it and `after` lines from pc on. */
export function windowAround(
  read: Reader,
  decode: Decoder,
  pc: number,
  before: number,
  after: number,
): DisasmLine[] {
  const start = alignedStart(pc, before, (a) => opcodeLen(read(a)));
  const lines = decodeRange(read, decode, start, before + after + 1);
  const idx = lines.findIndex((l) => l.addr === (pc & 0xffff));
  if (idx < 0) return decodeRange(read, decode, pc, after + 1);
  return lines.slice(Math.max(0, idx - before), idx + after + 1);
}

/** Render a window as text with a '>' marker on the pc line (text equivalent for screen readers). */
export function formatWindow(lines: readonly DisasmLine[], pc: number): string {
  return lines.map((l) => `${l.addr === (pc & 0xffff) ? '>' : ' '} ${formatDisasmLine(l)}`).join('\n');
}

/** 8080 flag byte → "S Z - A - P - C" style string (set flags upper-case, clear as '.'). */
export function formatFlags(flags: number): string {
  const names = ['S', 'Z', '0', 'A', '0', 'P', '1', 'C'];
  let out = '';
  for (let bit = 7; bit >= 0; bit--) {
    const name = names[7 - bit];
    if (name === '0' || name === '1') continue;
    out += (flags >> bit) & 1 ? name : '.';
  }
  return out;
}
