/**
 * cpu — registers, flags, pairs, stack top, state, cycles, steps and steps-per-second.
 * 16-bit values are clickable and emit `select-address`.
 */
import type { Engine } from '../engine';
import type { Bus } from '../bus';
import type { Panel } from './panel';
import {
  basePanel,
  injectStyle,
  el,
  hex2,
  hex4,
  fmtInt,
  fmtShort,
  setText,
  nameFor,
  HALT_LABELS,
  bankWindow,
  RateMeter,
} from './util';
export type { Panel } from './panel';

const CSS = `
.tos-cpu-grid{display:grid;grid-template-columns:repeat(4,auto 1fr);gap:2px 8px;align-items:baseline}
.tos-cpu-grid .k{color:#8b949e;text-align:right}
.tos-cpu-grid .v{color:#e6edf3;font-weight:600;white-space:pre}
.tos-cpu-grid .v.addr{cursor:pointer;text-decoration:underline dotted #8b949e}
.tos-cpu-grid .v.addr:hover{color:#79c0ff}
.tos-cpu-grid .v.wide{grid-column:span 3}
.tos-cpu-grid .v.changed{color:#ffa657}
.tos-cpu-flags b{color:#3fb950}
.tos-cpu-flags s{color:#8b949e;text-decoration:none}
.tos-cpu-instr{margin-top:6px;padding:4px;background:#010409;border:1px solid #30363d;white-space:pre}
`;

interface Field {
  key: string;
  label: string;
  addr?: boolean;
  wide?: boolean;
}

const FIELDS: Field[] = [
  { key: 'a', label: 'A' },
  { key: 'f', label: 'F' },
  { key: 'b', label: 'B' },
  { key: 'c', label: 'C' },
  { key: 'd', label: 'D' },
  { key: 'e', label: 'E' },
  { key: 'h', label: 'H' },
  { key: 'l', label: 'L' },
  { key: 'bc', label: 'BC', addr: true },
  { key: 'de', label: 'DE', addr: true },
  { key: 'hl', label: 'HL', addr: true },
  { key: 'm', label: '(HL)' },
  { key: 'sp', label: 'SP', addr: true },
  { key: 'tos', label: '(SP)', addr: true },
  { key: 'pc', label: 'PC', addr: true },
  { key: 'halted', label: 'halted' },
  { key: 'state', label: 'state' },
  { key: 'halt', label: 'reason' },
  { key: 'steps', label: 'steps' },
  { key: 'sps', label: 'steps/s' },
  { key: 'cycles', label: 'cycles' },
  { key: 'cps', label: 'MHz' },
  { key: 'frame', label: 'frame' },
  { key: 'syscall', label: 'syscall' },
  { key: 'tape', label: 'tape' },
  { key: 'bp', label: 'bp hit' },
  { key: 'stop', label: 'flags' },
];

const STOP_NAMES = ['BUDGET', 'HALT', 'WAIT_INPUT', 'VSYNC', 'BREAKPOINT'];

export function createCpuPanel(root: HTMLElement, engine: Engine, bus: Bus): Panel {
  basePanel(root, 'tos-cpu');
  injectStyle('tos-cpu-style', CSS);

  const grid = el('div', 'tos-cpu-grid');
  const values = new Map<string, HTMLElement>();
  const numeric = new Map<string, number>();
  const lastVal = new Map<string, number>();
  for (const f of FIELDS) {
    const k = el('span', 'k', f.label);
    const v = el('span', f.addr ? 'v addr' : 'v');
    if (f.wide) v.classList.add('wide');
    v.dataset.key = f.key;
    grid.append(k, v);
    values.set(f.key, v);
  }
  const instr = el('div', 'tos-cpu-instr');
  root.append(grid, instr);

  const stepsMeter = new RateMeter();
  const cycMeter = new RateMeter();

  const onClick = (ev: MouseEvent): void => {
    const t = ev.target as HTMLElement | null;
    if (!t || !t.classList.contains('addr')) return;
    const key = t.dataset.key || '';
    const a = numeric.get(key);
    if (typeof a === 'number') {
      bus.emit('select-address', { addr: a & 0xffff });
      bus.emit('select-page', { page: (a >> 8) & 0xff });
    }
  };
  grid.addEventListener('click', onClick);

  const offs: Array<() => void> = [];
  offs.push(
    bus.on('machine-reset', () => {
      stepsMeter.reset();
      cycMeter.reset();
    }),
  );

  function set(key: string, text: string, num?: number): void {
    const node = values.get(key);
    if (!node) return;
    setText(node, text);
    if (num !== undefined) {
      numeric.set(key, num);
      const prev = lastVal.get(key);
      if (prev !== undefined && prev !== num) node.classList.add('changed');
      else node.classList.remove('changed');
      lastVal.set(key, num);
    }
  }

  function flagsHtml(f: number): string {
    const bits: Array<[string, number]> = [
      ['S', 0x80],
      ['Z', 0x40],
      ['A', 0x10],
      ['P', 0x04],
      ['C', 0x01],
    ];
    return bits.map(([n, m]) => ((f & m) !== 0 ? `<b>${n}</b>` : `<s>${n}</s>`)).join('');
  }

  let lastFlags = -1;

  function render(nowMs: number): void {
    const cpu = engine.cpu();
    const t0 = engine.tape(0);
    const bw = bankWindow(engine);
    const sel = engine.tapeSelected();
    const tn = sel > 0 ? engine.tape(sel) : t0;
    const rd = (addr: number): number => {
      addr &= 0xffff;
      return (sel > 0 && addr >= bw.lo && addr <= bw.hi ? tn : t0)[addr];
    };
    const hl = ((cpu.h << 8) | cpu.l) & 0xffff;
    const bc = ((cpu.b << 8) | cpu.c) & 0xffff;
    const de = ((cpu.d << 8) | cpu.e) & 0xffff;
    const sp = cpu.sp & 0xffff;
    const pc = cpu.pc & 0xffff;
    const tos = (rd(sp) | (rd(sp + 1) << 8)) & 0xffff;
    const steps = engine.steps();
    const cycles = engine.cycles();

    set('a', hex2(cpu.a), cpu.a);
    if (cpu.flags !== lastFlags) {
      const node = values.get('f');
      if (node) {
        node.innerHTML = `<span class="tos-cpu-flags">${flagsHtml(cpu.flags)}</span> ${hex2(cpu.flags)}`;
      }
      lastFlags = cpu.flags;
    }
    set('b', hex2(cpu.b), cpu.b);
    set('c', hex2(cpu.c), cpu.c);
    set('d', hex2(cpu.d), cpu.d);
    set('e', hex2(cpu.e), cpu.e);
    set('h', hex2(cpu.h), cpu.h);
    set('l', hex2(cpu.l), cpu.l);
    set('bc', hex4(bc), bc);
    set('de', hex4(de), de);
    set('hl', hex4(hl), hl);
    set('m', hex2(rd(hl)), rd(hl));
    set('sp', hex4(sp), sp);
    set('tos', hex4(tos), tos);
    set('pc', hex4(pc), pc);
    set('halted', cpu.halted ? 'yes' : 'no');
    set('state', engine.stateName());
    set('halt', nameFor(HALT_LABELS, engine.haltReason()));
    set('steps', fmtInt(steps));
    set('sps', fmtShort(stepsMeter.sample(nowMs, steps)));
    set('cycles', fmtInt(cycles));
    set('cps', (cycMeter.sample(nowMs, cycles) / 1e6).toFixed(3));
    set('frame', fmtInt(engine.frame()));
    const fn = engine.lastSyscall();
    let sysName = '?';
    try {
      sysName = engine.syscallName(fn);
    } catch {
      /* keep ? */
    }
    set('syscall', `${sysName} (${hex2(fn)})`);
    set('tape', `${engine.tapeSelected()}/${engine.tapeCount()}`);
    const bp = engine.bpHit();
    set('bp', bp >= 0 ? `#${bp}` : '—');
    const meta = engine.meta();
    const stop = meta && meta.length > 0x0f ? meta[0x0f] : 0;
    set('stop', `${nameFor(STOP_NAMES, stop)} keys=${hex2(meta && meta.length > 0x0e ? meta[0x0e] : 0)}`);

    let dis = '?';
    let len = 1;
    try {
      const d = engine.disasm(pc);
      dis = d.text;
      len = d.len || 1;
    } catch {
      /* keep placeholder */
    }
    let bytes = '';
    for (let i = 0; i < len; i++) bytes += hex2(rd(pc + i)) + ' ';
    setText(instr, `${hex4(pc)}: ${bytes.padEnd(9)} ${dis}`);
  }

  return {
    update(nowMs: number): void {
      render(nowMs);
    },
    destroy(): void {
      for (const f of offs) f();
      grid.removeEventListener('click', onClick);
      root.replaceChildren();
      root.classList.remove('tos-panel', 'tos-cpu');
    },
  };
}
