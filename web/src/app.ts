/* App: owns the run loop, execution controls, breakpoints, demo loading, keyboard handling,
 * the panel registry and the URL-state sync for the playground. DOM use is limited to keyboard
 * listeners and the panel slots handed in by the playground page. */
import {
  Engine,
  STATE,
  STOP,
  LEVER,
  KEY,
  LANG,
  haltNameOf,
  stateNameOf,
  isMachineLever,
  type StopReason,
} from './engine';
import { createBus, type Bus, type Panel } from './bus';
import type { PanelFactory } from './panel';
import { stepsForFrame, MAX_FRAME_MS, type Speed } from './speed';
import {
  formatHash,
  stateToConfig,
  defaultState,
  BP_KIND_NAMES,
  type PlaygroundState,
  type Breakpoint,
  type BpKind,
} from './urlstate';
import { hex16 } from './disasmview';
import { DEMO_FILES } from './generated/demos';
import { createTapemapPanel } from './panels/tapemap';
import { createStripPanel } from './panels/strip';
import { createDetailPanel } from './panels/detail';
import { createCpuPanel } from './panels/cpu';
import { createFsmPanel } from './panels/fsm';
import { createDisplayPanel } from './panels/display';
import { createConsolePanel } from './panels/console';
import { createDiskPanel } from './panels/disk';
import { createTapesPanel } from './panels/tapes';
import { createStatsPanel } from './panels/stats';
import { createTimelinePanel } from './panels/timeline';
import { createLeversPanel } from './panels/levers';
import { createEditorPanel } from './panels/editor';

export const PANEL_NAMES = [
  'tapemap',
  'strip',
  'detail',
  'cpu',
  'fsm',
  'display',
  'console',
  'disk',
  'tapes',
  'stats',
  'timeline',
  'levers',
  'editor',
] as const;
export type PanelName = (typeof PANEL_NAMES)[number];

export const PANEL_TITLES: Record<PanelName, string> = {
  tapemap: 'Tape map',
  strip: 'Tape strip',
  detail: 'Page detail',
  cpu: 'CPU',
  fsm: 'Kernel FSM',
  display: 'Display',
  console: 'Console',
  disk: 'Disk',
  tapes: 'Tapes',
  stats: 'Stats',
  timeline: 'Timeline',
  levers: 'Levers',
  editor: 'Editor',
};

export const PANEL_FACTORIES: Record<PanelName, PanelFactory> = {
  tapemap: createTapemapPanel,
  strip: createStripPanel,
  detail: createDetailPanel,
  cpu: createCpuPanel,
  fsm: createFsmPanel,
  display: createDisplayPanel,
  console: createConsolePanel,
  disk: createDiskPanel,
  tapes: createTapesPanel,
  stats: createStatsPanel,
  timeline: createTimelinePanel,
  levers: createLeversPanel,
  editor: createEditorPanel,
};

export type RunMode = 'paused' | 'run' | 'to-halt' | 'to-state' | 'over-syscall';

export interface LoadResult {
  ok: boolean;
  error?: string;
  file?: string;
  diskName?: string;
  bytes?: number;
}

export interface KeyboardOptions {
  onHelp?: () => void;
}

/** Steps handed to one tos_step call at 'max' speed. */
const STEP_CHUNK = 65536;
/** Upper bound on the fractional step debt carried between frames. */
const ACC_CAP = 1_000_000;

const LANG_BY_EXT: Record<string, 0 | 1 | 2 | 3> = { c: LANG.C, asm: LANG.ASM, tm: LANG.TM, bf: LANG.BF };
const CMD_BY_EXT: Record<string, string> = { c: 'cc', asm: 'asm', tm: 'tm', bf: 'bf' };

export class App {
  readonly engine: Engine;
  readonly bus: Bus;

  private _mode: RunMode = 'paused';
  private _speed: Speed = 'max';
  private acc = 0;
  private hzAcc = 0;
  private _demo: string | null = null;
  private _demoFile: string | null = null;
  private _bps: Breakpoint[] = [];
  private _lastStop: StopReason = 0;
  private _lastEvent = '';
  private _lastEventAt = 0;
  private startState = 0;
  private syscallMark = 0;
  private booted = false;

  private panels = new Map<PanelName, Panel>();
  private brokenPanels = new Set<PanelName>();
  private keysRoot: HTMLElement | null = null;
  private frameListeners = new Set<(nowMs: number) => void>();
  private rafId = 0;
  private lastNow = 0;

  private keyMask = 0;
  private keyTarget: Document | HTMLElement | null = null;
  private keyOpts: KeyboardOptions = {};

  private diskBackup: Uint8Array | null = null;
  private pendingDisks: Uint8Array[] | null = null;
  private resetting = false;
  private urlSyncEnabled = true;
  private urlSyncQueued = false;
  private unsubscribers: (() => void)[] = [];

  constructor(engine: Engine, bus?: Bus) {
    this.engine = engine;
    this.bus = bus ?? createBus();
    this.unsubscribers.push(
      this.bus.on('console-input', (p: { text?: string } | string) => {
        const text = typeof p === 'string' ? p : p && typeof p.text === 'string' ? p.text : '';
        if (text) this.pushConsole(text);
      }),
      this.bus.on('seek-request', (p: { step?: number }) => {
        if (p && typeof p.step === 'number') this.seek(p.step);
      }),
      this.bus.on('run-request', (p: { running?: boolean }) => {
        if (p && p.running === false) this.pause();
        else this.run();
      }),
      this.bus.on('breakpoints-clear-request', () => this.clearBreakpoints()),
      // A lever moved in the levers panel: the hash has to follow, or the link no longer
      // describes the machine on screen.
      this.bus.on('lever', () => this.syncUrl()),
      this.bus.on('machine-reset-pending', () => {
        // Something outside the app is about to re-create the machine, which zeroes every disk
        // image. Take the same snapshot the toolbar's own reset takes.
        if (!this.resetting) this.pendingDisks = this.snapshotDisks();
      }),
      this.bus.on('machine-reset', () => {
        if (this.resetting) return;
        // A lever panel reset the machine behind our back: restore what the kernel forgot.
        this.restoreAfterExternalReset();
      }),
    );
  }

  // ---- read-only state -----------------------------------------------------

  get mode(): RunMode {
    return this._mode;
  }
  get running(): boolean {
    return this._mode !== 'paused';
  }
  get speed(): Speed {
    return this._speed;
  }
  get demo(): string | null {
    return this._demo;
  }
  get demoFile(): string | null {
    return this._demoFile;
  }
  get breakpoints(): readonly Breakpoint[] {
    return this._bps;
  }
  get lastStop(): StopReason {
    return this._lastStop;
  }
  /** Parked in WAIT_INPUT: the machine cannot advance until a byte reaches the console. */
  get waitingForInput(): boolean {
    return this._lastStop === STOP.WAIT_INPUT && this.engine.state() === STATE.IDLE;
  }
  get lastEvent(): string {
    return this._lastEvent;
  }
  get lastEventAt(): number {
    return this._lastEventAt;
  }

  // ---- execution controls --------------------------------------------------

  run(): void {
    if (this.engine.state() === STATE.HALT) {
      this.note(`Machine is halted (${haltNameOf(this.engine.haltReason())}); reset to run again`);
      return;
    }
    this.setMode('run');
  }

  pause(): void {
    this.setMode('paused');
  }

  toggle(): void {
    if (this.running) this.pause();
    else this.run();
  }

  /** Execute exactly n instructions (or fewer if the machine stops), synchronously. */
  step(n: number): void {
    this.setMode('paused');
    const before = this.engine.steps();
    this.runBudget(Math.max(1, n | 0), Infinity, 2000);
    const ran = this.engine.steps() - before;
    if (this._lastStop === STOP.BUDGET || this._lastStop === STOP.VSYNC) {
      this.note(`Stepped ${ran} (${stateNameOf(this.engine.state())}, PC ${hex16(this.engine.cpu().pc)})`);
    } else if (this._lastStop === STOP.WAIT_INPUT) {
      // Parked at a CONIN: stepping cannot move the head, so say so instead of doing nothing.
      this.note(
        ran === 0
          ? 'Waiting for input: type a command in the Console panel'
          : `Stepped ${ran}, then the machine stopped for input`,
      );
    }
  }

  /** Run until the next BIOS call has completed (or the machine stops). */
  stepOverSyscall(): void {
    this.syscallMark = this.engine.syscallsDone();
    this.setMode('over-syscall');
  }

  runToHalt(): void {
    this.setMode('to-halt');
  }

  runToStateChange(): void {
    this.startState = this.engine.state();
    this.setMode('to-state');
  }

  /** Reset to boot; if a demo is loaded it is loaded again. Keeps the run/pause state. */
  reset(): void {
    const wasRunning = this.running;
    this.setMode('paused');
    if (this._demo) {
      const r = this.loadDemo(this._demo, this._demoFile ?? undefined, { autorun: wasRunning });
      if (!r.ok) this.note(r.error ?? 'demo failed to load');
      return;
    }
    this.resetMachine();
    const clean = this.bootToPrompt();
    this.note(clean ? 'Machine reset' : `Machine reset · ${this._lastEvent}`);
    if (wasRunning && clean) this.setMode('run');
  }

  /** Time travel: restore the nearest earlier snapshot and replay to `step`. */
  seek(step: number): boolean {
    this.setMode('paused');
    const ok = this.engine.seek(step);
    if (ok) {
      this.note(`Seeked to step ${this.engine.steps()}`);
      this.bus.emit('seek', { step: this.engine.steps() });
    } else {
      this.note(`No snapshot at or before step ${step}`);
    }
    return ok;
  }

  setSpeed(speed: Speed): void {
    this._speed = speed;
    this.acc = 0;
    this.bus.emit('speed', { stepsPerSecond: speed });
    this.syncUrl();
  }

  // ---- breakpoints ---------------------------------------------------------

  addBreakpoint(kind: BpKind, lo: number, hi: number): number {
    const a = Math.min(lo, hi) & 0xffff;
    const b = Math.max(lo, hi) & 0xffff;
    const id = this.engine.bpAdd(kind, a, b);
    if (id >= 0) {
      this._bps.push({ kind, lo: a, hi: b });
      this.bus.emit('breakpoints', { list: this._bps.slice() });
      this.syncUrl();
    } else {
      this.note('Breakpoint table is full (16)');
    }
    return id;
  }

  removeBreakpoint(index: number): void {
    if (index < 0 || index >= this._bps.length) return;
    this._bps.splice(index, 1);
    this.reinstallBreakpoints();
    this.bus.emit('breakpoints', { list: this._bps.slice() });
    this.syncUrl();
  }

  clearBreakpoints(): void {
    this._bps = [];
    this.engine.bpClear();
    this.bus.emit('breakpoints', { list: [] });
    this.syncUrl();
  }

  private reinstallBreakpoints(): void {
    this.engine.bpClear();
    for (const b of this._bps) this.engine.bpAdd(b.kind, b.lo, b.hi);
  }

  // ---- input ---------------------------------------------------------------

  pushConsole(text: string): void {
    this.engine.conPush(text);
  }

  keyDown(bit: number): void {
    this.keyMask |= bit & 0x7f;
    this.pushKeys();
  }

  keyUp(bit: number): void {
    this.keyMask &= ~bit & 0x7f;
    this.pushKeys();
  }

  private pushKeys(): void {
    const m = this.keyMask ? this.keyMask | KEY.ANY : 0;
    this.engine.keysSet(m);
  }

  // ---- programs & demos ----------------------------------------------------

  /** Fetch `${base}demo.img` into disk 0. Returns false (and keeps going) if it is missing. */
  async fetchDemoDisk(base: string): Promise<boolean> {
    const url = `${base.endsWith('/') ? base : `${base}/`}demo.img`;
    try {
      const res = await fetch(url);
      if (!res.ok) return false;
      const bytes = new Uint8Array(await res.arrayBuffer());
      if (bytes.length < 1024) return false;
      this.engine.loadDiskImage(0, bytes);
      this.diskBackup = bytes;
      return true;
    } catch {
      return false;
    }
  }

  /** Compile `text`, save source + .COM on disk 0, reset, boot the shell and `run` it. */
  runProgram(fileName: string, text: string, opts: { autorun?: boolean } = {}): LoadResult {
    const ext = (fileName.split('.').pop() ?? '').toLowerCase();
    const lang = LANG_BY_EXT[ext];
    if (lang === undefined) return { ok: false, error: `unsupported file type .${ext}` };
    const stem = (fileName.split('/').pop() ?? fileName).replace(/\.[^.]*$/, '');
    const diskName = `${stem}.${ext}`.toUpperCase().slice(0, 12);
    const comName = `${stem}.COM`.toUpperCase();

    const compiled = this.engine.compile(lang, text);
    if (!compiled.ok) return { ok: false, error: compiled.error, file: fileName, diskName };

    this.setMode('paused');
    this.resetMachine();
    this.engine.diskPutFile(0, diskName, new TextEncoder().encode(text));
    this.engine.diskPutFile(0, comName, compiled.bytes);
    this.diskBackup = this.engine.diskPtr(0).slice();
    const clean = this.bootToPrompt();
    this.engine.conPush(`run ${comName}\n`);
    this.bus.emit('program-loaded', { file: fileName, diskName, bytes: compiled.bytes.length, command: CMD_BY_EXT[ext] });
    if (opts.autorun !== false && clean) this.setMode('run');
    return { ok: true, file: fileName, diskName, bytes: compiled.bytes.length };
  }

  /** Load a demo from generated/demos.ts (its primary file unless `file` is given). */
  loadDemo(name: string, file?: string, opts: { autorun?: boolean } = {}): LoadResult {
    const files = DEMO_FILES[name];
    if (!files || files.length === 0) return { ok: false, error: `unknown demo '${name}'` };
    const chosen = file ? files.find((f) => f.name === file) : primaryDemoFile(name, files);
    if (!chosen) return { ok: false, error: `demo '${name}' has no file '${file}'` };
    const r = this.runProgram(chosen.name, chosen.text, opts);
    if (r.ok) {
      this._demo = name;
      this._demoFile = chosen.name;
      this.bus.emit('demo-loaded', { name, file: chosen.name, text: chosen.text });
      this.note(`Loaded ${name}/${chosen.name} (${r.bytes} bytes)`);
      this.syncUrl();
    }
    return r;
  }

  /** Forget the demo (the next reset boots to the bare shell). */
  clearDemo(): void {
    this._demo = null;
    this._demoFile = null;
    this.syncUrl();
  }

  // ---- shareable state -----------------------------------------------------

  currentState(): PlaygroundState {
    const cfg = this.engine.config;
    const s = defaultState();
    s.demo = this._demo;
    s.tapes = cfg.tapes;
    s.len = cfg.tapeLen;
    s.hz = cfg.hz;
    s.seed = cfg.seed;
    s.input = cfg.inputMode === 1 ? 'keys' : 'console';
    s.disks = cfg.disks;
    s.trace = cfg.trace;
    s.speed = this._speed;
    s.bp = this._bps.map((b) => ({ ...b }));
    return s;
  }

  /** Apply a parsed hash: levers (recreating the machine if a machine lever changed), breakpoints, speed, demo. */
  applyState(s: PlaygroundState): void {
    this.urlSyncEnabled = false;
    try {
      this.setMode('paused');
      const want = stateToConfig(s);
      const cur = this.engine.config;
      const machineChanged =
        !this.booted ||
        want.tapes !== cur.tapes ||
        want.tapeLen !== cur.tapeLen ||
        want.seed !== cur.seed ||
        want.disks !== cur.disks;
      if (machineChanged) {
        const imgs = this.snapshotDisks();
        this.resetting = true;
        this.engine.create({ ...want, snapInterval: cur.snapInterval || 1000 });
        this.resetting = false;
        this.restoreDisks(imgs);
        this.booted = true;
      } else {
        if (want.hz !== cur.hz) this.engine.leverSet(LEVER.HZ, want.hz);
        if (want.inputMode !== cur.inputMode) this.engine.leverSet(LEVER.INPUT_MODE, want.inputMode);
        if (want.trace !== cur.trace) this.engine.leverSet(LEVER.TRACE, want.trace ? 1 : 0);
        this.resetMachine();
      }
      this._bps = [];
      this.engine.bpClear();
      for (const b of s.bp) {
        if (this.engine.bpAdd(b.kind, b.lo, b.hi) >= 0) this._bps.push({ ...b });
      }
      this.setSpeed(s.speed);
      this.keyMask = 0;
      this.engine.keysSet(0);
      if (s.demo) {
        const r = this.loadDemo(s.demo);
        if (!r.ok) {
          this._demo = null;
          this._demoFile = null;
          this.note(r.error ?? 'demo failed to load');
          this.setMode('run');
        }
      } else {
        this._demo = null;
        this._demoFile = null;
        this.setMode('run');
      }
      this.bus.emit('breakpoints', { list: this._bps.slice() });
      this.bus.emit('machine-reset');
    } finally {
      this.urlSyncEnabled = true;
    }
    this.syncUrl();
  }

  /** Absolute URL that restores the current playground state. */
  shareUrl(): string {
    const loc = typeof location !== 'undefined' ? location : null;
    const prefix = loc ? `${loc.origin}${loc.pathname}` : '';
    return `${prefix}${formatHash(this.currentState())}`;
  }

  private syncUrl(): void {
    if (!this.urlSyncEnabled || this.urlSyncQueued) return;
    if (typeof history === 'undefined' || typeof location === 'undefined') return;
    this.urlSyncQueued = true;
    const flush = () => {
      this.urlSyncQueued = false;
      if (!location.hash.startsWith('#/playground')) return;
      const next = formatHash(this.currentState());
      if (location.hash !== next) history.replaceState(null, '', next);
    };
    if (typeof queueMicrotask === 'function') queueMicrotask(flush);
    else Promise.resolve().then(flush);
  }

  // ---- panels --------------------------------------------------------------

  mountPanels(slots: Partial<Record<PanelName, HTMLElement>>): void {
    this.unmountPanels();
    for (const name of PANEL_NAMES) {
      const root = slots[name];
      if (!root) continue;
      try {
        this.panels.set(name, PANEL_FACTORIES[name](root, this.engine, this.bus));
      } catch (e) {
        console.error(`panel '${name}' failed to mount`, e);
        root.textContent = `${PANEL_TITLES[name]} panel failed to mount: ${(e as Error).message ?? e}`;
      }
    }
    this.keysRoot = slots.display ?? null;
  }

  unmountPanels(): void {
    for (const [name, p] of this.panels) {
      try {
        p.destroy();
      } catch (e) {
        console.error(`panel '${name}' failed to destroy`, e);
      }
    }
    this.panels.clear();
    this.brokenPanels.clear();
    this.keysRoot = null;
  }

  panel(name: PanelName): Panel | undefined {
    return this.panels.get(name);
  }

  // ---- run loop ------------------------------------------------------------

  start(): void {
    if (this.rafId) return;
    this.lastNow = 0;
    this.rafId = requestAnimationFrame(this.frame);
  }

  stop(): void {
    if (this.rafId) cancelAnimationFrame(this.rafId);
    this.rafId = 0;
  }

  onFrame(fn: (nowMs: number) => void): () => void {
    this.frameListeners.add(fn);
    return () => {
      this.frameListeners.delete(fn);
    };
  }

  destroy(): void {
    this.stop();
    this.detachKeyboard();
    this.unmountPanels();
    for (const u of this.unsubscribers) u();
    this.unsubscribers = [];
    this.frameListeners.clear();
    this.engine.keysSet(0);
  }

  private frame = (now: number): void => {
    this.rafId = requestAnimationFrame(this.frame);
    const dt = this.lastNow ? Math.min(250, Math.max(0, now - this.lastNow)) : 16;
    this.lastNow = now;
    if (this._mode !== 'paused') this.runSlice(dt);
    for (const [name, p] of this.panels) {
      if (this.brokenPanels.has(name)) continue;
      try {
        p.update(now);
      } catch (e) {
        this.brokenPanels.add(name);
        console.error(`panel '${name}' threw in update(); it will not be updated again`, e);
      }
    }
    for (const fn of this.frameListeners) fn(now);
  };

  private runSlice(dt: number): void {
    const engine = this.engine;
    switch (this._mode) {
      case 'run': {
        let maxSteps = Infinity;
        if (this._speed !== 'max') {
          this.acc = Math.min(ACC_CAP, this.acc + stepsForFrame(this._speed, dt));
          maxSteps = Math.floor(this.acc);
          if (maxSteps <= 0) return;
          this.acc -= maxSteps;
        }
        // The clock lever throttles the native run loop only: in the browser the speed control
        // above is the single throttle (docs/levers.md, and the levers panel says so too).
        const done = this.runBudget(maxSteps, Infinity, MAX_FRAME_MS);
        if (this._speed !== 'max') {
          if (this._lastStop === STOP.VSYNC && !done.timedOut) this.acc += Math.max(0, maxSteps - done.steps);
          else if (done.timedOut || this._lastStop !== STOP.BUDGET) this.acc = 0;
        }
        break;
      }
      case 'to-halt':
        this.runBudget(Infinity, Infinity, MAX_FRAME_MS);
        break;
      case 'to-state':
        this.runUntil(() => engine.state() !== this.startState, MAX_FRAME_MS, () =>
          this.note(`State changed to ${stateNameOf(engine.state())}`),
        );
        break;
      case 'over-syscall':
        this.runUntil(
          () => engine.syscallsDone() > this.syscallMark,
          MAX_FRAME_MS,
          () => this.note(`Stepped over ${engine.syscallName(engine.lastSyscall())}`),
        );
        break;
      default:
        break;
    }
  }

  /** Run up to maxSteps instructions / maxCycles cycles / timeMs wall time; honours stops. */
  private runBudget(
    maxSteps: number,
    maxCycles: number,
    timeMs: number,
  ): { steps: number; cycles: number; timedOut: boolean } {
    const engine = this.engine;
    const t0 = performance.now();
    const c0 = engine.cycles();
    let left = maxSteps;
    let ran = 0;
    let timedOut = false;
    while (left > 0) {
      let chunk = Math.min(left, STEP_CHUNK);
      if (maxCycles !== Infinity) {
        const cLeft = maxCycles - (engine.cycles() - c0);
        if (cLeft <= 0) break;
        chunk = Math.min(chunk, Math.max(1, Math.floor(cLeft / 12)));
      }
      const r = engine.step(chunk);
      ran += r.steps;
      left -= r.steps;
      if (!this.handleStop(r.stop)) break;
      if (r.steps === 0) break;
      if (performance.now() - t0 >= timeMs) {
        timedOut = left > 0;
        break;
      }
    }
    return { steps: ran, cycles: engine.cycles() - c0, timedOut };
  }

  /** Single-step until pred() holds; bounded by wall time (continues next frame). */
  private runUntil(pred: () => boolean, timeMs: number, onDone: () => void): void {
    const t0 = performance.now();
    for (let i = 0; i < 1_000_000; i++) {
      const r = this.engine.step(1);
      if (pred()) {
        this.setMode('paused');
        onDone();
        return;
      }
      if (!this.handleStop(r.stop)) return;
      if (r.steps === 0) return;
      if ((i & 255) === 255 && performance.now() - t0 >= timeMs) return;
    }
    this.setMode('paused');
  }

  /** Returns true when the current frame may keep stepping. */
  private handleStop(stop: StopReason): boolean {
    this._lastStop = stop;
    switch (stop) {
      case STOP.BUDGET:
        return true;
      case STOP.HALT:
        this.setMode('paused');
        this.note(`Machine halted: ${haltNameOf(this.engine.haltReason())} after ${this.engine.steps()} steps`);
        return false;
      case STOP.WAIT_INPUT:
        this.hzAcc = 0;
        // A targeted run (step over, run to halt, run to a state change) cannot reach its target
        // while the machine is parked at a CONIN, so hand control back instead of spinning.
        if (this._mode !== 'run' && this._mode !== 'paused') {
          this.setMode('paused');
          this.note('Waiting for input: type a command in the Console panel');
        }
        return false;
      case STOP.VSYNC:
        return false;
      case STOP.BREAKPOINT: {
        const i = this.engine.bpHit();
        const bp = this._bps[i];
        this.setMode('paused');
        const where = bp ? ` (${BP_KIND_NAMES[bp.kind]} ${hex16(bp.lo)}..${hex16(bp.hi)})` : '';
        this.note(`Breakpoint #${i} hit${where} at step ${this.engine.steps()}`);
        this.bus.emit('breakpoint', { index: i, ...(bp ?? {}), step: this.engine.steps() });
        return false;
      }
      default:
        return false;
    }
  }

  private setMode(mode: RunMode): void {
    if (this._mode === mode) return;
    const wasRunning = this._mode !== 'paused';
    this._mode = mode;
    const nowRunning = mode !== 'paused';
    if (wasRunning !== nowRunning) this.bus.emit('run-state', { running: nowRunning });
  }

  note(text: string): void {
    this._lastEvent = text;
    this._lastEventAt = typeof performance !== 'undefined' ? performance.now() : Date.now();
    this.bus.emit('note', { text });
  }

  // ---- machine housekeeping --------------------------------------------------

  /** Run the shell from boot to its first prompt (bounded). Returns false when a breakpoint or a
   *  halt interrupted the boot. The stop has then been handled (paused + noted) and callers must
   *  not switch back to run mode. */
  bootToPrompt(): boolean {
    for (let i = 0; i < 64; i++) {
      const r = this.engine.step(STEP_CHUNK);
      this._lastStop = r.stop;
      if (r.stop === STOP.BUDGET) continue;
      if (r.stop === STOP.BREAKPOINT || r.stop === STOP.HALT) {
        this.handleStop(r.stop);
        return false;
      }
      break;
    }
    return true;
  }

  private resetMachine(): void {
    const imgs = this.snapshotDisks();
    this.resetting = true;
    this.engine.reset();
    this.resetting = false;
    this.restoreDisks(imgs);
    this.reinstallBreakpoints();
    this.acc = 0;
    this.hzAcc = 0;
    this.keyMask = 0;
    this.engine.keysSet(0);
  }

  private snapshotDisks(): Uint8Array[] {
    const out: Uint8Array[] = [];
    const n = this.engine.config.disks;
    for (let d = 0; d < n; d++) out.push(this.engine.diskPtr(d).slice());
    return out;
  }

  private restoreDisks(imgs: Uint8Array[]): void {
    const n = this.engine.config.disks;
    for (let d = 0; d < n && d < imgs.length; d++) this.engine.loadDiskImage(d, imgs[d]);
  }

  private restoreAfterExternalReset(): void {
    if (this.pendingDisks) {
      this.restoreDisks(this.pendingDisks);
      this.pendingDisks = null;
    } else if (this.diskBackup && this.engine.diskList(0).length === 0) {
      this.engine.loadDiskImage(0, this.diskBackup);
    }
    this.reinstallBreakpoints();
    this.acc = 0;
    this.hzAcc = 0;
    // The machine came back in BOOT; run it to the prompt so the console is usable again.
    if (this.engine.state() === STATE.BOOT) this.bootToPrompt();
    this.syncUrl();
  }

  /** Whether a lever id resets the machine (re-exported for UI code). */
  static isMachineLever(id: number): boolean {
    return isMachineLever(id);
  }

  // ---- keyboard ------------------------------------------------------------

  attachKeyboard(target: Document | HTMLElement, opts: KeyboardOptions = {}): void {
    this.detachKeyboard();
    this.keyTarget = target;
    this.keyOpts = opts;
    target.addEventListener('keydown', this.onKeyDown as EventListener);
    target.addEventListener('keyup', this.onKeyUp as EventListener);
    window.addEventListener('blur', this.onBlur);
  }

  detachKeyboard(): void {
    if (!this.keyTarget) return;
    this.keyTarget.removeEventListener('keydown', this.onKeyDown as EventListener);
    this.keyTarget.removeEventListener('keyup', this.onKeyUp as EventListener);
    window.removeEventListener('blur', this.onBlur);
    this.keyTarget = null;
    this.keyMask = 0;
  }

  private machineKeysActive(target: EventTarget | null): boolean {
    if (isEditable(target)) return false;
    const active = typeof document !== 'undefined' ? document.activeElement : null;
    const inDisplay = !!(this.keysRoot && active && this.keysRoot.contains(active));
    if (inDisplay) return true;
    if (isInteractive(target)) return false;
    return this.engine.config.inputMode === 1;
  }

  private onKeyDown = (e: KeyboardEvent): void => {
    const bit = keyBit(e);
    if (bit && this.machineKeysActive(e.target)) {
      e.preventDefault();
      if (!e.repeat) this.keyDown(bit);
      return;
    }
    if (isEditable(e.target) || e.ctrlKey || e.metaKey || e.altKey) return;
    switch (e.key) {
      case ' ':
        e.preventDefault();
        this.toggle();
        break;
      case '.':
        e.preventDefault();
        this.step(1);
        break;
      case 'r':
      case 'R':
        e.preventDefault();
        this.reset();
        break;
      case '?':
        e.preventDefault();
        this.keyOpts.onHelp?.();
        break;
      default:
        break;
    }
  };

  private onKeyUp = (e: KeyboardEvent): void => {
    const bit = keyBit(e);
    if (bit && (this.keyMask & bit)) this.keyUp(bit);
  };

  private onBlur = (): void => {
    this.keyMask = 0;
    this.pushKeys();
  };
}

export function createApp(engine: Engine, bus?: Bus): App {
  return new App(engine, bus);
}

/** Map a keyboard event to a TOS_KEY_* bit (0 = not a machine key). */
export function keyBit(e: KeyboardEvent): number {
  switch (e.key) {
    case 'w':
    case 'W':
      return KEY.W;
    case 's':
    case 'S':
      return KEY.S;
    case 'ArrowUp':
      return KEY.UP;
    case 'ArrowDown':
      return KEY.DOWN;
    case ' ':
      return KEY.SPACE;
    case 'Escape':
      return KEY.ESC;
    case 'Enter':
      return KEY.ENTER;
    default:
      return 0;
  }
}

export function isEditable(target: EventTarget | null): boolean {
  const el = target as HTMLElement | null;
  if (!el || typeof el.tagName !== 'string') return false;
  const tag = el.tagName.toLowerCase();
  if (tag === 'input' || tag === 'textarea' || tag === 'select') return true;
  return !!el.isContentEditable;
}

/** Buttons, links, summaries and the like keep Enter/Space for themselves. */
export function isInteractive(target: EventTarget | null): boolean {
  const el = target as HTMLElement | null;
  if (!el || typeof el.tagName !== 'string') return false;
  const tag = el.tagName.toLowerCase();
  return tag === 'button' || tag === 'a' || tag === 'summary' || tag === 'option' || el.getAttribute('role') === 'button';
}

/** The demo's headline file: <name>.<ext> when present, else a known headliner, else the first file. */
export function primaryDemoFile(
  name: string,
  files: { name: string; text: string }[],
): { name: string; text: string } | undefined {
  const preferred: Record<string, string> = {
    hello: 'hello.c',
    tm: 'bb4.tm',
    bf: 'hello.bf',
    asm: 'hello.asm',
  };
  const wanted = preferred[name];
  if (wanted) {
    const f = files.find((x) => x.name === wanted);
    if (f) return f;
  }
  const byStem = files.find((f) => f.name.replace(/\.[^.]*$/, '') === name);
  if (byStem) return byStem;
  const runnable = files.find((f) => /\.(c|asm|tm|bf)$/i.test(f.name));
  return runnable ?? files[0];
}

/** Language id for a file name by extension, or null. */
export function langForFile(fileName: string): 0 | 1 | 2 | 3 | null {
  const ext = (fileName.split('.').pop() ?? '').toLowerCase();
  const l = LANG_BY_EXT[ext];
  return l === undefined ? null : l;
}
