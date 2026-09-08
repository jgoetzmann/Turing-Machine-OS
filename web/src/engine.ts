/* Engine: typed TypeScript wrapper over the Emscripten build of the TuringOS core (src/api/api.h).
 * Struct offsets come from generated/layout.json (tools/dump_layout); constants from constants.json. */
import layoutJson from './generated/layout.json';
import constantsJson from './generated/constants.json';

export type StopReason = 0 | 1 | 2 | 3 | 4; // KSTOP_*

export interface TosConfig {
  tapes: 1 | 2 | 4;
  tapeLen: 32768 | 49152 | 65536;
  hz: number;
  seed: number;
  inputMode: 0 | 1;
  disks: 1 | 2;
  trace: boolean;
  snapInterval: number;
}

export interface CpuRegs {
  a: number;
  b: number;
  c: number;
  d: number;
  e: number;
  h: number;
  l: number;
  sp: number;
  pc: number;
  flags: number;
  halted: boolean;
  cycles: number;
}

export interface TraceEvent {
  step: number;
  addr: number;
  kind: number;
  value: number;
}

export interface Transition {
  from: number;
  to: number;
  why: string;
  fired: number;
}

export type CompileResult = { ok: true; bytes: Uint8Array } | { ok: false; error: string };

export const STATE_NAMES: string[] = ['BOOT', 'IDLE', 'SHELL', 'RUNNING', 'SYSCALL', 'HALT'];
export const LEVER = {
  TAPES: 0,
  TAPE_LEN: 1,
  HZ: 2,
  SEED: 3,
  INPUT_MODE: 4,
  DISKS: 5,
  TRACE: 6,
  SNAP_INTERVAL: 7,
} as const;
export const KEY = { W: 1, S: 2, UP: 4, DOWN: 8, SPACE: 16, ESC: 32, ENTER: 64, ANY: 128 } as const;
export const STATE = { BOOT: 0, IDLE: 1, SHELL: 2, RUNNING: 3, SYSCALL: 4, HALT: 5 } as const;
export const STOP = { BUDGET: 0, HALT: 1, WAIT_INPUT: 2, VSYNC: 3, BREAKPOINT: 4 } as const;
export const STOP_NAMES: string[] = ['BUDGET', 'HALT', 'WAIT_INPUT', 'VSYNC', 'BREAKPOINT'];
export const BP_KIND = { PC: 0, READ: 1, WRITE: 2, SYSCALL: 3, STATE: 4 } as const;
export const HALT_NAMES: string[] = ['NONE', 'HLT', 'COMMAND', 'EOF', 'TAPE_FAULT', 'BREAKPOINT', 'BAD_TAPE'];
export const TRACE_KIND_NAMES: string[] = ['FETCH', 'READ', 'WRITE', 'SYSCALL', 'STATE', 'TAPE'];
export const LANG = { C: 0, ASM: 1, TM: 2, BF: 3 } as const;

export const TRACE_CAP = 65536;
export const TAPE_BYTES = 65536;
export const TPA_BASE = 0x0100;
export const TPA_SIZE = 16128;
export const META_SIZE = 256;
export const DISPLAY_SIZE = 256;
export const DISK_IMAGE_BYTES = 512512;

interface StructLayout {
  size: number;
  [field: string]: number;
}
interface Layout {
  cpu: StructLayout;
  trace_event: StructLayout;
  config: StructLayout;
}

const layout = layoutJson as unknown as Layout;
const constants = constantsJson as unknown as Record<string, number>;

/** The subset of the Emscripten module the engine touches. */
export interface WasmModule {
  HEAPU8: Uint8Array;
  HEAPU32: Uint32Array;
  HEAP32: Int32Array;
  UTF8ToString(ptr: number, maxBytes?: number): string;
  stringToUTF8(str: string, ptr: number, maxBytes: number): void;
  lengthBytesUTF8(str: string): number;
  _malloc(n: number): number;
  _free(p: number): void;

  _tos_create(cfg: number): number;
  _tos_reset(): void;
  _tos_step(maxSteps: number): number;
  _tos_stop_reason(): number;
  _tos_state(): number;
  _tos_steps(): number;
  _tos_cycles_lo(): number;
  _tos_cycles_hi(): number;
  _tos_halt_reason(): number;
  _tos_frame(): number;
  _tos_last_syscall(): number;

  _tos_tape_ptr(tape: number): number;
  _tos_tape_count(): number;
  _tos_tape_len(): number;
  _tos_tape_selected(): number;
  _tos_cpu_ptr(): number;
  _tos_meta_ptr(): number;
  _tos_write_age_ptr(tape: number): number;
  _tos_read_age_ptr(tape: number): number;
  _tos_travel_lo(): number;
  _tos_travel_hi(): number;
  _tos_accesses_lo(): number;
  _tos_cells_written(): number;

  _tos_trace_ptr(): number;
  _tos_trace_head(): number;
  _tos_trace_count(): number;
  _tos_trace_enable(on: number): void;

  _tos_con_push(ch: number): void;
  _tos_con_pop(): number;
  _tos_con_pending(): number;
  _tos_keys_set(mask: number): void;

  _tos_disk_ptr(disk: number): number;
  _tos_disk_size(): number;
  _tos_disk_reload(disk: number): void;
  _tos_disk_put_file(disk: number, name: number, data: number, len: number): number;
  _tos_disk_get_file(disk: number, name: number, out: number, cap: number): number;
  _tos_disk_list(disk: number, out: number, cap: number): number;

  _tos_lever_set(id: number, value: number): number;
  _tos_lever_get(id: number): number;
  _tos_bp_add(kind: number, lo: number, hi: number): number;
  _tos_bp_clear(): void;
  _tos_bp_hit(): number;
  _tos_seek(step: number): number;
  _tos_snapshot_count(): number;
  _tos_snapshot_step(slot: number): number;

  _tos_load_com(bytes: number, len: number): number;
  _tos_compile(lang: number, src: number, len: number, out: number, cap: number, err: number, errcap: number): number;
  _tos_disasm(addr: number, out: number, cap: number): number;
  _tos_state_name(state: number): number;
  _tos_syscall_name(fn: number): number;
  _tos_transition_count(): number;
  _tos_transition_why(index: number): number;
  _tos_transition_from(index: number): number;
  _tos_transition_to(index: number): number;
  _tos_transition_fired(index: number): number;
  _tos_hal_option(key: number, value: number): number;
  _tos_version(): number;
}

type ModuleFactory = (opts: Record<string, unknown>) => Promise<WasmModule>;

const u32 = (x: number): number => x >>> 0;

const factoryCache = new Map<string, Promise<ModuleFactory>>();

function defaultBase(): string {
  const env = (import.meta as unknown as { env?: { BASE_URL?: string } }).env;
  const b = env && typeof env.BASE_URL === 'string' ? env.BASE_URL : '/';
  return b;
}

function normalizeBase(base: string): string {
  return base.endsWith('/') ? base : `${base}/`;
}

function isNode(): boolean {
  const p = (globalThis as unknown as { process?: { versions?: { node?: string } } }).process;
  return !!(p && p.versions && p.versions.node) && typeof document === 'undefined';
}

/** Load the classic (non-ES6) MODULARIZE=1 script and return its factory. */
function loadFactory(url: string): Promise<ModuleFactory> {
  let cached = factoryCache.get(url);
  if (cached) return cached;
  cached = (async () => {
    const g = globalThis as unknown as { createTuringOS?: ModuleFactory };
    if (isNode()) {
      // Non-literal specifiers keep Vite from trying to resolve Node built-ins for the browser bundle.
      const modSpec = 'node:' + 'module';
      const urlSpec = 'node:' + 'url';
      const nodeModule = await import(/* @vite-ignore */ modSpec);
      const nodeUrl = await import(/* @vite-ignore */ urlSpec);
      const req = nodeModule.createRequire(import.meta.url);
      const path = url.startsWith('file:') ? nodeUrl.fileURLToPath(url) : url;
      const mod = req(path) as ModuleFactory | { default: ModuleFactory };
      return typeof mod === 'function' ? mod : mod.default;
    }
    if (typeof g.createTuringOS === 'function') return g.createTuringOS;
    await new Promise<void>((resolve, reject) => {
      const s = document.createElement('script');
      s.src = url;
      s.async = true;
      s.onload = () => resolve();
      s.onerror = () => reject(new Error(`could not load ${url} — run \`make wasm\` first`));
      document.head.appendChild(s);
    });
    if (typeof g.createTuringOS !== 'function') {
      throw new Error(`${url} loaded but did not define createTuringOS`);
    }
    return g.createTuringOS;
  })();
  factoryCache.set(url, cached);
  return cached;
}

export class Engine {
  readonly module: WasmModule;
  readonly constants: Record<string, number> = constants;
  private _config: TosConfig;
  private view: DataView;
  private tapeViews: (Uint8Array | null)[] = [null, null, null, null];
  private lastTraceHead = 0;

  private constructor(m: WasmModule) {
    this.module = m;
    this.view = new DataView(m.HEAPU8.buffer);
    this._config = defaultConfig();
  }

  /** Loads `${baseUrl}turingos.js` (+ .wasm) and boots a default machine. */
  static async load(baseUrl?: string): Promise<Engine> {
    const base = normalizeBase(baseUrl ?? defaultBase());
    const factory = await loadFactory(`${base}turingos.js`);
    const m = await factory({
      locateFile: (path: string) => `${base}${path}`,
      print: (text: string) => console.log(text),
      printErr: (text: string) => console.warn(text),
    });
    const engine = new Engine(m);
    engine.create({});
    return engine;
  }

  // ---- lifecycle -----------------------------------------------------------

  create(cfg: Partial<TosConfig>): void {
    const full: TosConfig = { ...defaultConfig(), ...stripUndefined(cfg) };
    const m = this.module;
    const L = layout.config;
    const p = m._malloc(L.size);
    m.HEAPU8.fill(0, p, p + L.size);
    this.view.setUint8(p + L.tapes, full.tapes);
    this.view.setUint32(p + L.tape_len, full.tapeLen, true);
    this.view.setUint32(p + L.hz, u32(full.hz), true);
    this.view.setUint8(p + L.seed, full.seed & 0xff);
    this.view.setUint8(p + L.input_mode, full.inputMode);
    this.view.setUint8(p + L.disks, full.disks);
    this.view.setUint8(p + L.trace, full.trace ? 1 : 0);
    this.view.setUint32(p + L.snap_interval, u32(full.snapInterval), true);
    const rc = m._tos_create(p);
    m._free(p);
    if (rc !== 0) throw new Error(`tos_create failed (${rc})`);
    this._config = full;
    this.afterMachineChange();
  }

  reset(): void {
    this.module._tos_reset();
    this.afterMachineChange();
  }

  step(maxSteps: number): { steps: number; stop: StopReason } {
    const n = u32(this.module._tos_step(u32(maxSteps)));
    return { steps: n, stop: (this.module._tos_stop_reason() & 7) as StopReason };
  }

  get config(): TosConfig {
    return { ...this._config };
  }

  // ---- machine state -------------------------------------------------------

  state(): number {
    return this.module._tos_state();
  }
  stateName(): string {
    return this.module.UTF8ToString(this.module._tos_state_name(this.state()));
  }
  steps(): number {
    return u32(this.module._tos_steps());
  }
  cycles(): number {
    return u32(this.module._tos_cycles_lo()) + u32(this.module._tos_cycles_hi()) * 4294967296;
  }
  haltReason(): number {
    return this.module._tos_halt_reason() & 0xff;
  }
  frame(): number {
    return u32(this.module._tos_frame());
  }
  lastSyscall(): number {
    return this.module._tos_last_syscall() & 0xff;
  }
  stopReason(): StopReason {
    return (this.module._tos_stop_reason() & 7) as StopReason;
  }

  tapeCount(): number {
    return this.module._tos_tape_count() & 0xff;
  }
  tapeLen(): number {
    return u32(this.module._tos_tape_len());
  }
  tapeSelected(): number {
    return this.module._tos_tape_selected() & 0xff;
  }

  /** Live 65536-byte view of tape i. */
  tape(i: number): Uint8Array {
    const idx = i & 3;
    let v = this.tapeViews[idx];
    if (!v || v.buffer !== this.module.HEAPU8.buffer) {
      v = new Uint8Array(this.module.HEAPU8.buffer, u32(this.module._tos_tape_ptr(idx)), TAPE_BYTES);
      this.tapeViews[idx] = v;
    }
    return v;
  }

  /** Live 256-byte view of the metadata block (tape 0 at TOS_META_BASE(L)). */
  meta(): Uint8Array {
    return new Uint8Array(this.module.HEAPU8.buffer, u32(this.module._tos_meta_ptr()), META_SIZE);
  }

  /** Live 256-byte view of the framebuffer (tape 0 at TOS_DISPLAY_BASE(L) = L - 0x200). */
  display(): Uint8Array {
    const base = u32(this.module._tos_tape_ptr(0)) + (this.tapeLen() - 0x200);
    return new Uint8Array(this.module.HEAPU8.buffer, base, DISPLAY_SIZE);
  }

  cpu(): CpuRegs {
    const p = u32(this.module._tos_cpu_ptr());
    const L = layout.cpu;
    const v = this.view;
    return {
      a: v.getUint8(p + L.a),
      b: v.getUint8(p + L.b),
      c: v.getUint8(p + L.c),
      d: v.getUint8(p + L.d),
      e: v.getUint8(p + L.e),
      h: v.getUint8(p + L.h),
      l: v.getUint8(p + L.l),
      sp: v.getUint16(p + L.sp, true),
      pc: v.getUint16(p + L.pc, true),
      flags: v.getUint8(p + L.flags),
      halted: v.getInt32(p + L.halted, true) !== 0,
      cycles: v.getUint32(p + L.cycles, true) + v.getUint32(p + L.cycles + 4, true) * 4294967296,
    };
  }

  /** Raw pointer to cpu_t inside the wasm heap (for panels that want their own DataView). */
  cpuPtr(): number {
    return u32(this.module._tos_cpu_ptr());
  }

  writeAge(i: number): Uint32Array {
    return new Uint32Array(this.module.HEAPU8.buffer, u32(this.module._tos_write_age_ptr(i & 3)), TAPE_BYTES);
  }
  readAge(i: number): Uint32Array {
    return new Uint32Array(this.module.HEAPU8.buffer, u32(this.module._tos_read_age_ptr(i & 3)), TAPE_BYTES);
  }
  travel(): number {
    return u32(this.module._tos_travel_lo()) + u32(this.module._tos_travel_hi()) * 4294967296;
  }
  accesses(): number {
    return u32(this.module._tos_accesses_lo());
  }
  cellsWritten(): number {
    return u32(this.module._tos_cells_written());
  }

  // ---- trace ---------------------------------------------------------------

  /** Events pushed since the previous drain (at most TRACE_CAP; older ones are lost). */
  drainTrace(): TraceEvent[] {
    const head = u32(this.module._tos_trace_head());
    const count = u32(this.module._tos_trace_count());
    let start = this.lastTraceHead;
    if (head < start) start = 0; // ring was reset
    if (head - start > count) start = head - count;
    if (head - start > TRACE_CAP) start = head - TRACE_CAP;
    const out = this.readTrace(start, head);
    this.lastTraceHead = head;
    return out;
  }

  /** The newest `n` events without moving the drain cursor. */
  traceTail(n: number): TraceEvent[] {
    const head = u32(this.module._tos_trace_head());
    const count = Math.min(u32(this.module._tos_trace_count()), TRACE_CAP);
    const want = Math.min(Math.max(0, n | 0), count);
    return this.readTrace(head - want, head);
  }

  traceHead(): number {
    return u32(this.module._tos_trace_head());
  }
  traceCount(): number {
    return u32(this.module._tos_trace_count());
  }
  traceEnable(on: boolean): void {
    this.module._tos_trace_enable(on ? 1 : 0);
    this._config.trace = on;
  }

  private readTrace(from: number, to: number): TraceEvent[] {
    const base = u32(this.module._tos_trace_ptr());
    const L = layout.trace_event;
    const v = this.view;
    const out: TraceEvent[] = [];
    for (let i = from; i < to; i++) {
      const p = base + (i % TRACE_CAP) * L.size;
      out.push({
        step: v.getUint32(p + L.step, true),
        addr: v.getUint16(p + L.addr, true),
        kind: v.getUint8(p + L.kind),
        value: v.getUint8(p + L.value),
      });
    }
    return out;
  }

  // ---- console & keys ------------------------------------------------------

  conPush(text: string): void {
    for (let i = 0; i < text.length; i++) this.module._tos_con_push(text.charCodeAt(i) & 0xff);
  }
  conPushBytes(bytes: Uint8Array): void {
    for (let i = 0; i < bytes.length; i++) this.module._tos_con_push(bytes[i]);
  }
  conPop(): number {
    return this.module._tos_con_pop();
  }
  conPending(): number {
    return this.module._tos_con_pending();
  }
  /** Pops everything pending as a Latin-1 string. */
  conRead(): string {
    const codes: number[] = [];
    for (;;) {
      const c = this.module._tos_con_pop();
      if (c < 0) break;
      codes.push(c & 0xff);
      if (codes.length >= 8192) break;
    }
    let s = '';
    for (let i = 0; i < codes.length; i += 4096) {
      s += String.fromCharCode.apply(null, codes.slice(i, i + 4096));
    }
    return s;
  }
  keysSet(mask: number): void {
    this.module._tos_keys_set(mask & 0xff);
  }

  // ---- disks ---------------------------------------------------------------

  diskSize(): number {
    return u32(this.module._tos_disk_size());
  }
  diskPtr(d: number): Uint8Array {
    return new Uint8Array(this.module.HEAPU8.buffer, u32(this.module._tos_disk_ptr(d & 1)), this.diskSize());
  }
  loadDiskImage(d: number, bytes: Uint8Array): void {
    const dst = this.diskPtr(d);
    const n = Math.min(bytes.length, dst.length);
    dst.set(bytes.subarray(0, n));
    if (n < dst.length) dst.fill(0, n);
    this.module._tos_disk_reload(d & 1);
  }
  diskPutFile(d: number, name: string, bytes: Uint8Array): boolean {
    const m = this.module;
    const namePtr = this.allocString(name);
    const dataPtr = m._malloc(Math.max(1, bytes.length));
    m.HEAPU8.set(bytes, dataPtr);
    const rc = m._tos_disk_put_file(d & 1, namePtr, dataPtr, bytes.length);
    m._free(dataPtr);
    m._free(namePtr);
    return rc === 0;
  }
  diskGetFile(d: number, name: string): Uint8Array | null {
    const m = this.module;
    const namePtr = this.allocString(name);
    const cap = DISK_IMAGE_BYTES;
    const out = m._malloc(cap);
    const n = m._tos_disk_get_file(d & 1, namePtr, out, cap);
    let result: Uint8Array | null = null;
    if (n >= 0) result = m.HEAPU8.slice(out, out + n);
    m._free(out);
    m._free(namePtr);
    return result;
  }
  diskList(d: number): string[] {
    const m = this.module;
    const cap = 64 * 14 + 16;
    const out = m._malloc(cap);
    m.HEAPU8.fill(0, out, out + cap);
    const count = m._tos_disk_list(d & 1, out, cap);
    const text = m.UTF8ToString(out, cap);
    m._free(out);
    if (count <= 0) return [];
    return text
      .split('\n')
      .map((s) => s.trim())
      .filter((s) => s.length > 0);
  }

  // ---- levers, breakpoints, time travel -------------------------------------

  leverSet(id: number, value: number): boolean {
    const rc = this.module._tos_lever_set(id | 0, u32(value));
    if (rc !== 0) return false;
    switch (id) {
      case LEVER.TAPES:
        this._config.tapes = value as 1 | 2 | 4;
        break;
      case LEVER.TAPE_LEN:
        this._config.tapeLen = value as 32768 | 49152 | 65536;
        break;
      case LEVER.HZ:
        this._config.hz = u32(value);
        break;
      case LEVER.SEED:
        this._config.seed = value & 0xff;
        break;
      case LEVER.INPUT_MODE:
        this._config.inputMode = value ? 1 : 0;
        break;
      case LEVER.DISKS:
        this._config.disks = value as 1 | 2;
        break;
      case LEVER.TRACE:
        this._config.trace = !!value;
        break;
      case LEVER.SNAP_INTERVAL:
        this._config.snapInterval = u32(value);
        break;
    }
    if (isMachineLever(id)) this.afterMachineChange();
    return true;
  }
  leverGet(id: number): number {
    return u32(this.module._tos_lever_get(id | 0));
  }

  bpAdd(kind: number, lo: number, hi: number): number {
    return this.module._tos_bp_add(kind | 0, lo & 0xffff, hi & 0xffff);
  }
  bpClear(): void {
    this.module._tos_bp_clear();
  }
  bpHit(): number {
    return this.module._tos_bp_hit();
  }

  seek(step: number): boolean {
    return this.module._tos_seek(u32(step)) === 0;
  }
  snapshotCount(): number {
    return this.module._tos_snapshot_count();
  }
  snapshotStep(slot: number): number {
    return u32(this.module._tos_snapshot_step(slot | 0));
  }

  // ---- programs & tools ----------------------------------------------------

  loadCom(bytes: Uint8Array): boolean {
    const m = this.module;
    if (bytes.length === 0 || bytes.length > TPA_SIZE) return false;
    const p = m._malloc(bytes.length);
    m.HEAPU8.set(bytes, p);
    const rc = m._tos_load_com(p, bytes.length);
    m._free(p);
    return rc === 0;
  }

  compile(lang: 0 | 1 | 2 | 3, src: string): CompileResult {
    const m = this.module;
    const srcLen = m.lengthBytesUTF8(src);
    const srcPtr = m._malloc(srcLen + 1);
    m.stringToUTF8(src, srcPtr, srcLen + 1);
    const outCap = TPA_SIZE;
    const outPtr = m._malloc(outCap);
    const errCap = 512;
    const errPtr = m._malloc(errCap);
    m.HEAPU8.fill(0, errPtr, errPtr + errCap);
    const n = m._tos_compile(lang, srcPtr, srcLen, outPtr, outCap, errPtr, errCap);
    let result: CompileResult;
    if (n >= 0) result = { ok: true, bytes: m.HEAPU8.slice(outPtr, outPtr + n) };
    else result = { ok: false, error: m.UTF8ToString(errPtr, errCap) || 'compile error' };
    m._free(errPtr);
    m._free(outPtr);
    m._free(srcPtr);
    return result;
  }

  disasm(addr: number): { text: string; len: number } {
    const m = this.module;
    const cap = 64;
    const p = m._malloc(cap);
    m.HEAPU8.fill(0, p, p + cap);
    const len = m._tos_disasm(addr & 0xffff, p, cap);
    const text = m.UTF8ToString(p, cap);
    m._free(p);
    return { text, len: len > 0 ? len : 1 };
  }

  syscallName(fn: number): string {
    return this.module.UTF8ToString(this.module._tos_syscall_name(fn & 0xff));
  }

  transitions(): Transition[] {
    const m = this.module;
    const n = m._tos_transition_count();
    const out: Transition[] = [];
    for (let i = 0; i < n; i++) {
      out.push({
        from: m._tos_transition_from(i),
        to: m._tos_transition_to(i),
        why: m.UTF8ToString(m._tos_transition_why(i)),
        fired: u32(m._tos_transition_fired(i)),
      });
    }
    return out;
  }

  version(): string {
    return this.module.UTF8ToString(this.module._tos_version());
  }

  halOption(key: string, value: string): boolean {
    const k = this.allocString(key);
    const v = this.allocString(value);
    const rc = this.module._tos_hal_option(k, v);
    this.module._free(v);
    this.module._free(k);
    return rc === 0;
  }

  // ---- internals -----------------------------------------------------------

  private afterMachineChange(): void {
    this.lastTraceHead = 0;
    this.tapeViews = [null, null, null, null];
    if (this.view.buffer !== this.module.HEAPU8.buffer) this.view = new DataView(this.module.HEAPU8.buffer);
    // The C side is the authority on the effective configuration after a create/reset/lever.
    const m = this.module;
    const tapes = m._tos_lever_get(LEVER.TAPES);
    const len = u32(m._tos_lever_get(LEVER.TAPE_LEN));
    const disks = m._tos_lever_get(LEVER.DISKS);
    if (tapes === 1 || tapes === 2 || tapes === 4) this._config.tapes = tapes;
    if (len === 32768 || len === 49152 || len === 65536) this._config.tapeLen = len;
    if (disks === 1 || disks === 2) this._config.disks = disks;
    this._config.hz = u32(m._tos_lever_get(LEVER.HZ));
    this._config.seed = m._tos_lever_get(LEVER.SEED) & 0xff;
    this._config.inputMode = m._tos_lever_get(LEVER.INPUT_MODE) ? 1 : 0;
    this._config.trace = !!m._tos_lever_get(LEVER.TRACE);
    this._config.snapInterval = u32(m._tos_lever_get(LEVER.SNAP_INTERVAL));
  }

  private allocString(s: string): number {
    const m = this.module;
    const n = m.lengthBytesUTF8(s) + 1;
    const p = m._malloc(n);
    m.stringToUTF8(s, p, n);
    return p;
  }
}

export function defaultConfig(): TosConfig {
  return { tapes: 1, tapeLen: 65536, hz: 0, seed: 1, inputMode: 0, disks: 1, trace: true, snapInterval: 1000 };
}

/** Machine levers reset the machine; view levers do not. */
export function isMachineLever(id: number): boolean {
  return id === LEVER.TAPES || id === LEVER.TAPE_LEN || id === LEVER.SEED || id === LEVER.DISKS;
}

export function stateNameOf(state: number): string {
  return STATE_NAMES[state] ?? `?${state}`;
}

export function haltNameOf(reason: number): string {
  return HALT_NAMES[reason] ?? `?${reason}`;
}

export function stopNameOf(stop: number): string {
  return STOP_NAMES[stop] ?? `?${stop}`;
}

/** Pixel (x,y) of a 64x32 framebuffer: bit 7 of byte y*8 + x/8 is the leftmost pixel. */
export function displayPixel(fb: Uint8Array, x: number, y: number): number {
  return (fb[y * 8 + (x >> 3)] >> (7 - (x & 7))) & 1;
}

function stripUndefined<T extends object>(o: T): Partial<T> {
  const out: Partial<T> = {};
  for (const k of Object.keys(o) as (keyof T)[]) {
    if (o[k] !== undefined) out[k] = o[k];
  }
  return out;
}
