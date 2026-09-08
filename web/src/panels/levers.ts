/**
 * levers — one control per lever id. Changes go through `engine.leverSet`; machine
 * levers (tapes, tape length, seed, disks) reset the machine, so those also emit
 * `machine-reset`. Every change emits `lever {id, value}`.
 */
import type { Engine } from '../engine';
import { LEVER } from '../engine';
import type { Bus } from '../bus';
import type { Panel } from './panel';
import { basePanel, injectStyle, el, fmtInt, setText } from './util';
export type { Panel } from './panel';

const CSS = `
.tos-levers-grid{display:grid;grid-template-columns:auto 1fr auto;gap:4px 8px;align-items:center}
.tos-levers-grid .k{color:#8b949e;text-align:right;white-space:nowrap}
.tos-levers-grid .tag{font-size:10px;padding:0 4px;border-radius:3px;border:1px solid #30363d;color:#8b949e;white-space:nowrap}
.tos-levers-grid .tag.machine{color:#ffa657;border-color:#9e6a03}
.tos-levers-grid input[type=number]{width:100%}
.tos-levers-grid select{width:100%}
.tos-levers-foot{margin-top:6px;display:flex;gap:6px;align-items:center;flex-wrap:wrap}
`;

interface LeverDef {
  id: number;
  label: string;
  kind: 'select' | 'number' | 'check';
  options?: Array<{ v: number; label: string }>;
  min?: number;
  max?: number;
  machine: boolean;
  hint: string;
}

const DEFS: LeverDef[] = [
  {
    id: LEVER.TAPES,
    label: 'tapes',
    kind: 'select',
    options: [1, 2, 4].map((v) => ({ v, label: String(v) })),
    machine: true,
    hint: 'number of tapes k (resets the machine)',
  },
  {
    id: LEVER.TAPE_LEN,
    label: 'tape length',
    kind: 'select',
    options: [
      { v: 32768, label: '32K (32768)' },
      { v: 49152, label: '48K (49152)' },
      { v: 65536, label: '64K (65536)' },
    ],
    machine: true,
    hint: 'bytes per tape (resets the machine)',
  },
  { id: LEVER.HZ, label: 'hz', kind: 'number', min: 0, max: 100000000, machine: false, hint: '0 = unthrottled (native only; the browser run loop uses the speed control)' },
  { id: LEVER.SEED, label: 'seed', kind: 'number', min: 0, max: 255, machine: true, hint: 'PRNG seed 0..255 (resets the machine)' },
  {
    id: LEVER.INPUT_MODE,
    label: 'input',
    kind: 'select',
    options: [
      { v: 0, label: 'console' },
      { v: 1, label: 'keys' },
    ],
    machine: false,
    hint: 'where keyboard input goes',
  },
  { id: LEVER.DISKS, label: 'disks', kind: 'select', options: [1, 2].map((v) => ({ v, label: String(v) })), machine: true, hint: 'mounted disks (resets the machine)' },
  { id: LEVER.TRACE, label: 'trace', kind: 'check', machine: false, hint: 'record fetch/read/write/syscall events' },
  { id: LEVER.SNAP_INTERVAL, label: 'snapshot every', kind: 'number', min: 0, max: 100000000, machine: false, hint: 'steps between snapshots, 0 = never' },
];

interface Control {
  def: LeverDef;
  input: HTMLInputElement | HTMLSelectElement;
  handler: () => void;
}

export function createLeversPanel(root: HTMLElement, engine: Engine, bus: Bus): Panel {
  basePanel(root, 'tos-levers');
  injectStyle('tos-levers-style', CSS);

  const grid = el('div', 'tos-levers-grid');
  const controls: Control[] = [];
  const status = el('span', 'tos-muted', '');

  function readControl(c: Control): number {
    const { def, input } = c;
    if (def.kind === 'check') return (input as HTMLInputElement).checked ? 1 : 0;
    const raw = parseInt(input.value, 10);
    if (Number.isNaN(raw)) return 0;
    let v = raw;
    if (def.min !== undefined && v < def.min) v = def.min;
    if (def.max !== undefined && v > def.max) v = def.max;
    return v;
  }

  function writeControl(c: Control, v: number): void {
    const { def, input } = c;
    if (def.kind === 'check') (input as HTMLInputElement).checked = v !== 0;
    else if (input.value !== String(v)) input.value = String(v);
  }

  function apply(c: Control): void {
    const value = readControl(c);
    let ok = false;
    // A machine lever re-creates the machine and zeroes the disks; warn the app first so it can
    // keep them.
    if (c.def.machine) bus.emit('machine-reset-pending');
    try {
      ok = engine.leverSet(c.def.id, value);
    } catch {
      ok = false;
    }
    if (!ok) {
      status.className = 'tos-err';
      setText(status, `${c.def.label}: rejected value ${fmtInt(value)}`);
      writeControl(c, engine.leverGet(c.def.id));
      return;
    }
    status.className = 'tos-ok';
    setText(status, `${c.def.label} = ${fmtInt(value)}${c.def.machine ? ' (machine reset)' : ''}`);
    bus.emit('lever', { id: c.def.id, value });
    if (c.def.machine) bus.emit('machine-reset');
  }

  for (const def of DEFS) {
    const k = el('span', 'k', def.label);
    k.title = def.hint;
    let input: HTMLInputElement | HTMLSelectElement;
    if (def.kind === 'select') {
      const s = el('select');
      for (const o of def.options || []) {
        const opt = el('option', '', o.label);
        opt.value = String(o.v);
        s.append(opt);
      }
      input = s;
    } else if (def.kind === 'check') {
      const i = el('input');
      i.type = 'checkbox';
      input = i;
    } else {
      const i = el('input');
      i.type = 'number';
      i.min = String(def.min ?? 0);
      i.max = String(def.max ?? 0);
      i.step = '1';
      input = i;
    }
    input.title = def.hint;
    input.dataset.lever = String(def.id);
    const tag = el('span', def.machine ? 'tag machine' : 'tag', def.machine ? 'resets' : 'view');
    const c: Control = { def, input, handler: () => apply(c) };
    input.addEventListener('change', c.handler);
    controls.push(c);
    grid.append(k, input, tag);
  }

  const foot = el('div', 'tos-levers-foot');
  const resetBtn = el('button', '', 'reset machine');
  const bpClearBtn = el('button', '', 'clear breakpoints');
  foot.append(resetBtn, bpClearBtn, status);
  root.append(grid, foot);

  const onReset = (): void => {
    bus.emit('machine-reset-pending');
    engine.reset();
    status.className = 'tos-ok';
    setText(status, 'machine reset');
    bus.emit('machine-reset');
  };
  const onBpClear = (): void => {
    // The app owns the breakpoint list (it reinstalls it after a reset), so ask rather than clear
    // the engine behind its back.
    status.className = 'tos-ok';
    setText(status, 'breakpoints cleared');
    bus.emit('breakpoints-clear-request');
  };
  resetBtn.addEventListener('click', onReset);
  bpClearBtn.addEventListener('click', onBpClear);

  function syncFromEngine(): void {
    const active = document.activeElement;
    for (const c of controls) {
      if (c.input === active) continue;
      let v = 0;
      try {
        v = engine.leverGet(c.def.id);
      } catch {
        continue;
      }
      writeControl(c, v);
    }
  }

  const offs: Array<() => void> = [];
  offs.push(bus.on('machine-reset', () => syncFromEngine()));
  syncFromEngine();

  let lastSync = -1e9;

  return {
    update(nowMs: number): void {
      if (nowMs - lastSync >= 250) {
        syncFromEngine();
        lastSync = nowMs;
      }
    },
    destroy(): void {
      for (const f of offs) f();
      for (const c of controls) c.input.removeEventListener('change', c.handler);
      resetBtn.removeEventListener('click', onReset);
      bpClearBtn.removeEventListener('click', onBpClear);
      root.replaceChildren();
      root.classList.remove('tos-panel', 'tos-levers');
    },
  };
}
