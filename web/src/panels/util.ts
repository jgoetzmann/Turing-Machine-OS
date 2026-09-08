/** Private helpers shared by the panels. Nothing here is part of the public surface. */
import type { Engine } from '../engine';
export type { Panel } from './panel';

export const TR_FETCH = 0;
export const TR_READ = 1;
export const TR_WRITE = 2;
export const TR_SYSCALL = 3;
export const TR_STATE = 4;
export const TR_TAPE = 5;

export const STATE_LABELS: readonly string[] = ['BOOT', 'IDLE', 'SHELL', 'RUNNING', 'SYSCALL', 'HALT'];
export const HALT_LABELS: readonly string[] = ['NONE', 'HLT', 'COMMAND', 'EOF', 'TAPE_FAULT', 'BREAKPOINT', 'BAD_TAPE'];
export const STOP_LABELS: readonly string[] = ['BUDGET', 'HALT', 'WAIT_INPUT', 'VSYNC', 'BREAKPOINT'];

export const BASE_CSS = `
.tos-panel{font:12px/1.4 ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;color:#c9d1d9;background:#0d1117;box-sizing:border-box;padding:6px;position:relative;overflow:hidden}
.tos-panel *{box-sizing:border-box}
.tos-panel canvas{display:block;image-rendering:pixelated;image-rendering:crisp-edges}
.tos-panel button,.tos-panel select,.tos-panel input,.tos-panel textarea{font:inherit;color:#c9d1d9;background:#161b22;border:1px solid #30363d;border-radius:3px;padding:2px 6px}
.tos-panel button{cursor:pointer}
.tos-panel button:hover{background:#21262d}
.tos-panel button.on{background:#1f6feb;border-color:#388bfd;color:#fff}
.tos-panel button:disabled{opacity:.5;cursor:default}
.tos-panel .tos-bar{display:flex;flex-wrap:wrap;gap:6px;align-items:center;margin-bottom:6px}
.tos-panel .tos-muted{color:#8b949e}
.tos-panel .tos-err{color:#ff7b72}
.tos-panel .tos-ok{color:#3fb950}
.tos-panel .tos-warn{color:#d29922}
.tos-panel pre{margin:0;font:inherit;white-space:pre}
.tos-panel label{display:inline-flex;gap:4px;align-items:center}
`;

export function injectStyle(id: string, css: string): void {
  if (typeof document === 'undefined') return;
  if (document.getElementById(id)) return;
  const s = document.createElement('style');
  s.id = id;
  s.textContent = css;
  document.head.appendChild(s);
}

export function basePanel(root: HTMLElement, cls: string): void {
  injectStyle('tos-panels-base', BASE_CSS);
  root.classList.add('tos-panel', cls);
}

export function el<K extends keyof HTMLElementTagNameMap>(
  tag: K,
  cls?: string,
  text?: string,
): HTMLElementTagNameMap[K] {
  const e = document.createElement(tag);
  if (cls) e.className = cls;
  if (text !== undefined) e.textContent = text;
  return e;
}

export function ctx2d(canvas: HTMLCanvasElement): CanvasRenderingContext2D {
  const c = canvas.getContext('2d');
  if (!c) throw new Error('Canvas2D unavailable');
  return c;
}

export function hex2(n: number): string {
  return (n & 0xff).toString(16).toUpperCase().padStart(2, '0');
}

export function hex4(n: number): string {
  return (n & 0xffff).toString(16).toUpperCase().padStart(4, '0');
}

export function fmtInt(n: number): string {
  if (!Number.isFinite(n)) return '0';
  const neg = n < 0;
  const s = Math.floor(Math.abs(n)).toString();
  let out = '';
  for (let i = 0; i < s.length; i++) {
    if (i > 0 && (s.length - i) % 3 === 0) out += ',';
    out += s[i];
  }
  return (neg ? '-' : '') + out;
}

/** 1234567 -> "1.23M", 12345 -> "12.3k" */
export function fmtShort(n: number): string {
  const a = Math.abs(n);
  if (a >= 1e9) return (n / 1e9).toFixed(2) + 'G';
  if (a >= 1e6) return (n / 1e6).toFixed(2) + 'M';
  if (a >= 1e3) return (n / 1e3).toFixed(1) + 'k';
  return Math.round(n).toString();
}

export function clamp(v: number, lo: number, hi: number): number {
  return v < lo ? lo : v > hi ? hi : v;
}

export function escapeHtml(s: string): string {
  return s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
}

export function isPrintable(b: number): boolean {
  return b >= 32 && b < 127;
}

export function latin1Encode(s: string): Uint8Array {
  const out = new Uint8Array(s.length);
  for (let i = 0; i < s.length; i++) out[i] = s.charCodeAt(i) & 0xff;
  return out;
}

export function latin1Decode(b: Uint8Array): string {
  let s = '';
  for (let i = 0; i < b.length; i += 4096) {
    const chunk = b.subarray(i, i + 4096);
    let part = '';
    for (let j = 0; j < chunk.length; j++) part += String.fromCharCode(chunk[j]);
    s += part;
  }
  return s;
}

export function suffixFor(L: number): '32K' | '48K' | '64K' {
  return L === 32768 ? '32K' : L === 49152 ? '48K' : '64K';
}

/** Numeric constant from constants.json with a computed fallback. */
export function tosConst(engine: Engine, name: string, fallback: number): number {
  const c = engine.constants;
  const v = c ? c[name] : undefined;
  return typeof v === 'number' ? v : fallback;
}

/** `NAME_32K` / `NAME_48K` / `NAME_64K` variant for the given tape length. */
export function tosConstL(engine: Engine, name: string, L: number, fallback: number): number {
  return tosConst(engine, `${name}_${suffixFor(L)}`, fallback);
}

export function tapeLength(engine: Engine): number {
  const L = engine.tapeLen();
  return L === 32768 || L === 49152 || L === 65536 ? L : 65536;
}

export interface Region {
  name: string;
  lo: number;
  hi: number;
  rgb: [number, number, number];
  css: string;
}

function mkRegion(name: string, lo: number, hi: number, rgb: [number, number, number]): Region {
  return { name, lo, hi, rgb, css: `rgb(${rgb[0]},${rgb[1]},${rgb[2]})` };
}

/** Memory-map regions for the machine's current tape length (SPEC §S2). */
export function regionsFor(engine: Engine): Region[] {
  const L = tapeLength(engine);
  const tpaBase = tosConst(engine, 'TOS_TPA_BASE', 0x0100);
  const tpaEnd = tosConst(engine, 'TOS_TPA_END', 0x3fff);
  const bankBase = tosConst(engine, 'TOS_BANK_BASE', 0x4000);
  const bankEnd = tosConstL(engine, 'TOS_BANK_END', L, L - 0x2001);
  const scratchBase = tosConstL(engine, 'TOS_SCRATCH_BASE', L, L - 0x2000);
  const scratchEnd = tosConstL(engine, 'TOS_SCRATCH_END', L, L - 0x1001);
  const stackBase = tosConstL(engine, 'TOS_STACK_BASE', L, L - 0x1000);
  const displayBase = tosConstL(engine, 'TOS_DISPLAY_BASE', L, L - 0x200);
  const metaBase = tosConstL(engine, 'TOS_META_BASE', L, L - 0x100);
  const regs: Region[] = [
    mkRegion('BIOS', 0, tpaBase - 1, [95, 115, 160]),
    mkRegion('TPA', tpaBase, tpaEnd, [70, 135, 225]),
    mkRegion('BANK', bankBase, bankEnd, [60, 185, 110]),
    mkRegion('SCRATCH', scratchBase, scratchEnd, [215, 185, 60]),
    mkRegion('STACK', stackBase, displayBase - 1, [175, 95, 225]),
    mkRegion('DISPLAY', displayBase, metaBase - 1, [40, 205, 215]),
    mkRegion('META', metaBase, L - 1, [235, 85, 165]),
  ];
  if (L < 65536) regs.push(mkRegion('OFF-TAPE', L, 65535, [60, 60, 60]));
  return regs;
}

export function regionAt(regions: Region[], addr: number): Region | null {
  for (const r of regions) if (addr >= r.lo && addr <= r.hi) return r;
  return null;
}

export function bankWindow(engine: Engine): { lo: number; hi: number } {
  const L = tapeLength(engine);
  return {
    lo: tosConst(engine, 'TOS_BANK_BASE', 0x4000),
    hi: tosConstL(engine, 'TOS_BANK_END', L, L - 0x2001),
  };
}

export function displayBase(engine: Engine): number {
  const L = tapeLength(engine);
  return tosConstL(engine, 'TOS_DISPLAY_BASE', L, L - 0x200);
}

/** Byte at `addr` as seen through tape `tape`: banked window from that tape, everything else from tape 0. */
export function tapeByte(engine: Engine, tape: number, addr: number): number {
  if (addr < 0 || addr > 65535) return 0xff;
  const bw = bankWindow(engine);
  const t = tape > 0 && addr >= bw.lo && addr <= bw.hi ? tape : 0;
  return engine.tape(t)[addr];
}

/** Clamp a user-chosen tape index to the machine's tape count. */
export function safeTape(engine: Engine, wanted: number | null): number {
  const k = engine.tapeCount() || 1;
  const t = wanted === null ? engine.tapeSelected() : wanted;
  return t >= 0 && t < k ? t : 0;
}

/** Recency 0..1 with a logarithmic fall-off over `window` steps. */
export function heat(age: number, window: number): number {
  if (age < 0) return 0;
  if (age <= 0) return 1;
  if (age >= window) return 0;
  return 1 - Math.log(1 + age) / Math.log(1 + window);
}

/** Per-second rate estimator with a short EMA so the readout does not jitter. */
export class RateMeter {
  private lastT = -1;
  private lastV = 0;
  private ema = 0;

  sample(nowMs: number, value: number): number {
    if (this.lastT < 0 || value < this.lastV) {
      this.lastT = nowMs;
      this.lastV = value;
      this.ema = 0;
      return 0;
    }
    const dt = nowMs - this.lastT;
    if (dt >= 250) {
      const r = ((value - this.lastV) * 1000) / dt;
      this.ema = this.ema === 0 ? r : this.ema * 0.6 + r * 0.4;
      this.lastT = nowMs;
      this.lastV = value;
    }
    return this.ema;
  }

  reset(): void {
    this.lastT = -1;
    this.lastV = 0;
    this.ema = 0;
  }
}

/** Steps executed since the previous frame; drives adaptive heat windows. */
export class StepDelta {
  private last = -1;
  delta = 0;
  sample(steps: number): number {
    if (this.last < 0 || steps < this.last) this.delta = 0;
    else this.delta = steps - this.last;
    this.last = steps;
    return this.delta;
  }
}

/** Set textContent only when it changed (keeps per-frame DOM work near zero). */
export function setText(node: HTMLElement, text: string): void {
  if (node.textContent !== text) node.textContent = text;
}

export function nameFor(list: readonly string[], i: number): string {
  return i >= 0 && i < list.length ? list[i] : `?${i}`;
}

/** Turn any host file name into a NAME.EXT the filesystem accepts. */
export function fsName(hostName: string, defaultExt = 'BIN'): string {
  const base = hostName.split(/[\\/]/).pop() || 'FILE';
  const dot = base.lastIndexOf('.');
  let name = (dot > 0 ? base.slice(0, dot) : base).toUpperCase().replace(/[^A-Z0-9]/g, '');
  let ext = (dot > 0 ? base.slice(dot + 1) : defaultExt).toUpperCase().replace(/[^A-Z0-9]/g, '');
  if (!name) name = 'FILE';
  if (!ext) ext = defaultExt;
  return `${name.slice(0, 8)}.${ext.slice(0, 3)}`;
}

export function triggerDownload(bytes: Uint8Array, fileName: string): void {
  const blob = new Blob([bytes.slice().buffer], { type: 'application/octet-stream' });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = fileName;
  document.body.appendChild(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}
