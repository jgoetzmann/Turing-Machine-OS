/* Shareable playground state <-> URL hash. Dependency-free so Node can import this file directly.
 *
 * Hash shape (spec S9):
 *   #/playground?demo=<name>&tapes=1|2|4&len=32768|49152|65536&hz=0|N&seed=N
 *               &input=console|keys&disks=1|2&trace=0|1&snap=N&speed=<steps per second|max>
 *               &bp=<kind:lo:hi,...>
 * Keys equal to their default are omitted by formatHash; parseHash tolerates any order,
 * a missing '#', a missing '/playground' prefix, hex numbers (0x..) and numeric breakpoint kinds.
 */

export type Speed = number | 'max';
export type Tapes = 1 | 2 | 4;
export type TapeLen = 32768 | 49152 | 65536;
export type InputMode = 'console' | 'keys';
export type Disks = 1 | 2;
/** 0 pc, 1 read, 2 write, 3 syscall, 4 state (kernel_bp_kind_t). */
export type BpKind = 0 | 1 | 2 | 3 | 4;

export interface Breakpoint {
  kind: BpKind;
  lo: number;
  hi: number;
}

export interface PlaygroundState {
  demo: string | null;
  tapes: Tapes;
  len: TapeLen;
  hz: number;
  seed: number;
  input: InputMode;
  disks: Disks;
  trace: boolean;
  /** Steps between automatic snapshots (TOS_LEVER_SNAP_INTERVAL); 0 = never. */
  snap: number;
  speed: Speed;
  bp: Breakpoint[];
}

export const BP_KIND_NAMES: readonly string[] = ['pc', 'read', 'write', 'syscall', 'state'];
export const PLAYGROUND_PATH = '#/playground';

export function defaultState(): PlaygroundState {
  return {
    demo: null,
    tapes: 1,
    len: 65536,
    hz: 0,
    seed: 1,
    input: 'console',
    disks: 1,
    trace: true,
    snap: 1000,
    speed: 'max',
    bp: [],
  };
}

/** Parse a decimal or 0x-hex integer; null when the text is not a number. */
export function parseNumber(text: string | null | undefined): number | null {
  if (text === null || text === undefined) return null;
  const t = String(text).trim();
  if (/^-?0x[0-9a-f]+$/i.test(t)) return parseInt(t, 16);
  if (/^-?\d+$/.test(t)) return parseInt(t, 10);
  return null;
}

export function parseBpKind(text: string): BpKind | null {
  const t = text.trim().toLowerCase();
  const idx = BP_KIND_NAMES.indexOf(t);
  if (idx >= 0) return idx as BpKind;
  const n = parseNumber(t);
  if (n !== null && n >= 0 && n <= 4) return n as BpKind;
  return null;
}

/** "pc:256:256,write:0xFE00:0xFEFF" → breakpoints; malformed entries are skipped. */
export function parseBreakpoints(text: string | null | undefined): Breakpoint[] {
  if (!text) return [];
  const out: Breakpoint[] = [];
  for (const item of String(text).split(',')) {
    const parts = item.trim().split(':');
    if (parts.length < 2 || parts.length > 3) continue;
    const kind = parseBpKind(parts[0]);
    const lo = parseNumber(parts[1]);
    const hi = parts.length === 3 ? parseNumber(parts[2]) : lo;
    if (kind === null || lo === null || hi === null) continue;
    const a = clamp16(Math.min(lo, hi));
    const b = clamp16(Math.max(lo, hi));
    out.push({ kind, lo: a, hi: b });
  }
  return out;
}

export function formatBreakpoints(bps: readonly Breakpoint[]): string {
  return bps.map((b) => `${BP_KIND_NAMES[b.kind] ?? b.kind}:${b.lo}:${b.hi}`).join(',');
}

export function parseSpeedValue(text: string | null | undefined): Speed | null {
  if (text === null || text === undefined) return null;
  const t = String(text).trim().toLowerCase();
  if (t === 'max' || t === 'unthrottled') return 'max';
  const n = Number(t);
  if (!Number.isFinite(n) || n <= 0) return null;
  return Math.max(1, n);
}

/** Split the query part of a hash into key → (decoded) value. Later keys win. */
export function parseQuery(query: string): Record<string, string> {
  const out: Record<string, string> = {};
  for (const pair of query.split('&')) {
    if (!pair) continue;
    const eq = pair.indexOf('=');
    const k = eq < 0 ? pair : pair.slice(0, eq);
    const v = eq < 0 ? '' : pair.slice(eq + 1);
    out[safeDecode(k)] = safeDecode(v);
  }
  return out;
}

/** Full parse: any hash (or bare query) → a complete PlaygroundState with defaults filled in. */
export function parseHash(hash: string): PlaygroundState {
  const state = defaultState();
  let h = String(hash ?? '');
  if (h.startsWith('#')) h = h.slice(1);
  const q = h.indexOf('?');
  let query = '';
  if (q >= 0) query = h.slice(q + 1);
  else if (!h.startsWith('/')) query = h; // bare "demo=pong&speed=max"
  const kv = parseQuery(query);

  if (kv.demo !== undefined) {
    const d = kv.demo.trim();
    state.demo = /^[A-Za-z0-9_-]{1,32}$/.test(d) ? d : null;
  }
  const tapes = parseNumber(kv.tapes);
  if (tapes === 1 || tapes === 2 || tapes === 4) state.tapes = tapes;
  const len = parseNumber(kv.len);
  if (len === 32768 || len === 49152 || len === 65536) state.len = len;
  const hz = parseNumber(kv.hz);
  if (hz !== null && hz >= 0) state.hz = Math.min(hz, 0xffffffff);
  const seed = parseNumber(kv.seed);
  if (seed !== null && seed >= 0 && seed <= 255) state.seed = seed;
  if (kv.input === 'keys' || kv.input === '1') state.input = 'keys';
  else if (kv.input === 'console' || kv.input === '0') state.input = 'console';
  const disks = parseNumber(kv.disks);
  if (disks === 1 || disks === 2) state.disks = disks;
  if (kv.trace !== undefined) {
    const t = kv.trace.trim().toLowerCase();
    if (t === '0' || t === 'false' || t === 'off') state.trace = false;
    else if (t === '1' || t === 'true' || t === 'on') state.trace = true;
  }
  const snap = parseNumber(kv.snap);
  if (snap !== null && snap >= 0) state.snap = Math.min(snap, 0xffffffff);
  const speed = parseSpeedValue(kv.speed);
  if (speed !== null) state.speed = speed;
  state.bp = parseBreakpoints(kv.bp);
  return state;
}

/** Inverse of parseHash: "#/playground" plus only the keys that differ from the defaults. */
export function formatHash(state: PlaygroundState): string {
  const d = defaultState();
  const parts: string[] = [];
  if (state.demo) parts.push(`demo=${encodeURIComponent(state.demo)}`);
  if (state.tapes !== d.tapes) parts.push(`tapes=${state.tapes}`);
  if (state.len !== d.len) parts.push(`len=${state.len}`);
  if (state.hz !== d.hz) parts.push(`hz=${state.hz}`);
  if (state.seed !== d.seed) parts.push(`seed=${state.seed}`);
  if (state.input !== d.input) parts.push(`input=${state.input}`);
  if (state.disks !== d.disks) parts.push(`disks=${state.disks}`);
  if (state.trace !== d.trace) parts.push(`trace=${state.trace ? 1 : 0}`);
  if (state.snap !== d.snap) parts.push(`snap=${state.snap}`);
  if (state.speed !== d.speed) parts.push(`speed=${state.speed}`);
  if (state.bp.length > 0) parts.push(`bp=${formatBreakpoints(state.bp)}`);
  return parts.length ? `${PLAYGROUND_PATH}?${parts.join('&')}` : PLAYGROUND_PATH;
}

/** Machine configuration implied by a state, in the Engine.create() field names. */
export function stateToConfig(state: PlaygroundState): {
  tapes: Tapes;
  tapeLen: TapeLen;
  hz: number;
  seed: number;
  inputMode: 0 | 1;
  disks: Disks;
  trace: boolean;
  snapInterval: number;
} {
  return {
    tapes: state.tapes,
    tapeLen: state.len,
    hz: state.hz,
    seed: state.seed,
    inputMode: state.input === 'keys' ? 1 : 0,
    disks: state.disks,
    trace: state.trace,
    snapInterval: state.snap,
  };
}

export function statesEqual(a: PlaygroundState, b: PlaygroundState): boolean {
  return formatHash(a) === formatHash(b);
}

function clamp16(n: number): number {
  if (n < 0) return 0;
  if (n > 0xffff) return 0xffff;
  return n | 0;
}

function safeDecode(s: string): string {
  try {
    return decodeURIComponent(s.replace(/\+/g, ' '));
  } catch {
    return s;
  }
}
