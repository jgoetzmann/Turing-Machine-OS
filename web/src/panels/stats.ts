/**
 * stats — steps, cycles, virtual MHz, steps/s, cells written, the travel odometer,
 * accesses, and a syscall histogram built from TR_SYSCALL trace events.
 */
import type { Engine } from '../engine';
import type { Bus } from '../bus';
import type { Panel } from './panel';
import { basePanel, injectStyle, el, fmtInt, fmtShort, hex2, setText, RateMeter, TR_SYSCALL } from './util';
import { traceTap } from './tracefeed';
export type { Panel } from './panel';

const CSS = `
.tos-stats-grid{display:grid;grid-template-columns:auto 1fr auto 1fr;gap:2px 10px;align-items:baseline}
.tos-stats-grid .k{color:#8b949e;text-align:right}
.tos-stats-grid .v{color:#e6edf3;font-weight:600;white-space:pre}
.tos-stats-odo{margin:8px 0;display:flex;align-items:baseline;gap:8px}
.tos-stats-odo .digits{font-size:20px;letter-spacing:2px;background:#010409;border:1px solid #30363d;padding:2px 8px;color:#f0f6fc;font-variant-numeric:tabular-nums}
.tos-stats-odo .unit{color:#8b949e}
.tos-stats-hist{margin-top:6px}
.tos-stats-hist h4{margin:0 0 4px;font-size:11px;color:#8b949e;font-weight:400}
.tos-stats-hist .row{display:grid;grid-template-columns:11ch 1fr 7ch;gap:6px;align-items:center;font-size:11px;margin-bottom:2px}
.tos-stats-hist .bar{height:9px;background:#238636;border-radius:2px;min-width:1px}
.tos-stats-hist .cnt{text-align:right;color:#e6edf3}
.tos-stats-hist .none{color:#8b949e;font-size:11px}
`;

const FIELDS: Array<[string, string]> = [
  ['steps', 'steps'],
  ['sps', 'steps/s'],
  ['cycles', 'cycles'],
  ['mhz', 'virtual MHz'],
  ['cells', 'cells written'],
  ['acc', 'accesses'],
  ['frame', 'frames'],
  ['snaps', 'snapshots'],
  ['state', 'state'],
  ['tape', 'tape sel'],
];

export function createStatsPanel(root: HTMLElement, engine: Engine, bus: Bus): Panel {
  basePanel(root, 'tos-stats');
  injectStyle('tos-stats-style', CSS);

  const grid = el('div', 'tos-stats-grid');
  const values = new Map<string, HTMLElement>();
  for (const [key, label] of FIELDS) {
    const k = el('span', 'k', label);
    const v = el('span', 'v', '—');
    grid.append(k, v);
    values.set(key, v);
  }
  const odo = el('div', 'tos-stats-odo');
  const odoLbl = el('span', 'unit', 'head travel');
  const odoDigits = el('span', 'digits', '000000000');
  const odoUnit = el('span', 'unit', 'cells');
  odo.append(odoLbl, odoDigits, odoUnit);
  const hist = el('div', 'tos-stats-hist');
  const histTitle = el('h4', '', 'syscalls (from trace)');
  const histBody = el('div');
  hist.append(histTitle, histBody);
  root.append(grid, odo, hist);

  const counts = new Map<number, number>();
  let histDirty = true;
  let totalSyscalls = 0;
  const tap = traceTap(engine);
  const unsubTrace = tap.subscribe((evs) => {
    for (const e of evs) {
      if (e.kind === TR_SYSCALL) {
        const fn = e.addr & 0xff;
        counts.set(fn, (counts.get(fn) || 0) + 1);
        totalSyscalls++;
        histDirty = true;
      }
    }
  });

  const stepsMeter = new RateMeter();
  const cycMeter = new RateMeter();
  let lastHist = -1e9;

  const offs: Array<() => void> = [];
  offs.push(
    bus.on('machine-reset', () => {
      counts.clear();
      totalSyscalls = 0;
      histDirty = true;
      stepsMeter.reset();
      cycMeter.reset();
    }),
  );

  function set(key: string, text: string): void {
    const n = values.get(key);
    if (n) setText(n, text);
  }

  function renderHist(): void {
    histBody.replaceChildren();
    if (counts.size === 0) {
      const traceOn = engine.config && engine.config.trace;
      histBody.append(el('div', 'none', traceOn ? 'no syscalls yet' : 'trace lever is off: enable it to count syscalls'));
      return;
    }
    const rows = Array.from(counts.entries()).sort((a, b) => b[1] - a[1]).slice(0, 14);
    const max = rows[0][1] || 1;
    for (const [fn, c] of rows) {
      const row = el('div', 'row');
      let name = '?';
      try {
        name = engine.syscallName(fn);
      } catch {
        /* keep ? */
      }
      const lbl = el('span', '', `${name} ${hex2(fn)}`);
      lbl.title = `function ${fn}`;
      const bar = el('div', 'bar');
      bar.style.width = `${Math.max(1, (c / max) * 100)}%`;
      const cnt = el('span', 'cnt', fmtInt(c));
      row.append(lbl, bar, cnt);
      histBody.append(row);
    }
  }

  function render(nowMs: number): void {
    const steps = engine.steps();
    const cycles = engine.cycles();
    set('steps', fmtInt(steps));
    set('sps', fmtShort(stepsMeter.sample(nowMs, steps)));
    set('cycles', fmtInt(cycles));
    set('mhz', (cycMeter.sample(nowMs, cycles) / 1e6).toFixed(3));
    set('cells', fmtInt(engine.cellsWritten()));
    set('acc', fmtInt(engine.accesses()));
    set('frame', fmtInt(engine.frame()));
    set('snaps', fmtInt(engine.snapshotCount()));
    set('state', engine.stateName());
    set('tape', `${engine.tapeSelected()}/${engine.tapeCount()}`);
    const travel = engine.travel();
    setText(odoDigits, Math.floor(travel).toString().padStart(9, '0'));
    setText(histTitle, `syscalls (from trace) · ${fmtInt(totalSyscalls)} total`);
    if (histDirty && nowMs - lastHist >= 200) {
      renderHist();
      histDirty = false;
      lastHist = nowMs;
    }
  }

  return {
    update(nowMs: number): void {
      tap.pump(nowMs);
      render(nowMs);
    },
    destroy(): void {
      unsubTrace();
      for (const f of offs) f();
      root.replaceChildren();
      root.classList.remove('tos-panel', 'tos-stats');
    },
  };
}
