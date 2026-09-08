/**
 * strip — 33 cells centred on PC, a head triangle, and a fading trail of the most
 * recent FETCH addresses taken from the shared trace tap.
 */
import type { Engine } from '../engine';
import type { Bus } from '../bus';
import type { Panel } from './panel';
import {
  basePanel,
  injectStyle,
  el,
  ctx2d,
  hex2,
  hex4,
  regionsFor,
  regionAt,
  bankWindow,
  tapeLength,
  safeTape,
  heat,
  clamp,
  StepDelta,
  TR_FETCH,
  type Region,
} from './util';
import { traceTap } from './tracefeed';
export type { Panel } from './panel';

const CELLS = 33;
const HALF = 16;
const CW = 24;
const TOP = 14;
const CH = 40;
const LBL = 14;
const H = TOP + CH + LBL;
const TRAIL_MAX = 48;

const CSS = `
.tos-strip canvas{width:100%;height:auto;background:#000;border:1px solid #30363d;cursor:pointer}
.tos-strip-caption{margin-top:4px;color:#8b949e;font-size:11px;white-space:pre}
`;

export function createStripPanel(root: HTMLElement, engine: Engine, bus: Bus): Panel {
  basePanel(root, 'tos-strip');
  injectStyle('tos-strip-style', CSS);

  const canvas = el('canvas');
  canvas.width = CELLS * CW;
  canvas.height = H;
  const caption = el('div', 'tos-strip-caption');
  root.append(canvas, caption);
  const ctx = ctx2d(canvas);

  const trail: number[] = [];
  const tap = traceTap(engine);
  const unsubTrace = tap.subscribe((evs) => {
    for (const e of evs) if (e.kind === TR_FETCH) trail.push(e.addr & 0xffff);
    if (trail.length > TRAIL_MAX) trail.splice(0, trail.length - TRAIL_MAX);
  });

  let selAddr = -1;
  let userTape: number | null = null;
  let regions: Region[] = [];
  let regionL = -1;
  const stepDelta = new StepDelta();

  const offs: Array<() => void> = [];
  offs.push(
    bus.on('select-address', (p) => {
      if (p && typeof p.addr === 'number') selAddr = p.addr & 0xffff;
    }),
  );
  offs.push(
    bus.on('select-tape', (p) => {
      userTape = p && typeof p.tape === 'number' ? p.tape : null;
    }),
  );
  offs.push(
    bus.on('machine-reset', () => {
      trail.length = 0;
      userTape = null;
      regionL = -1;
    }),
  );

  const onClick = (ev: MouseEvent): void => {
    const r = canvas.getBoundingClientRect();
    if (r.width === 0) return;
    const i = Math.floor(((ev.clientX - r.left) / r.width) * CELLS);
    const pc = engine.cpu().pc & 0xffff;
    const addr = pc - HALF + i;
    if (addr < 0 || addr > 65535) return;
    selAddr = addr;
    bus.emit('select-address', { addr });
    bus.emit('select-page', { page: addr >> 8 });
  };
  canvas.addEventListener('click', onClick);

  function render(nowMs: number): void {
    const L = tapeLength(engine);
    if (L !== regionL) {
      regions = regionsFor(engine);
      regionL = L;
    }
    const cpu = engine.cpu();
    const pc = cpu.pc & 0xffff;
    const steps = engine.steps();
    const W = clamp(stepDelta.sample(steps) * 8, 512, 1 << 20);
    const tape = safeTape(engine, userTape);
    const bw = bankWindow(engine);
    const t0 = engine.tape(0);
    const tn = tape > 0 ? engine.tape(tape) : t0;
    const w0 = engine.writeAge(0);
    const wn = tape > 0 ? engine.writeAge(tape) : w0;

    ctx.fillStyle = '#000';
    ctx.fillRect(0, 0, canvas.width, H);
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';

    const base = pc - HALF;
    for (let i = 0; i < CELLS; i++) {
      const addr = base + i;
      const x = i * CW;
      const y = TOP;
      if (addr < 0 || addr > 65535) {
        ctx.fillStyle = '#111';
        ctx.fillRect(x, y, CW - 1, CH);
        continue;
      }
      const inBank = addr >= bw.lo && addr <= bw.hi;
      const v = (inBank ? tn : t0)[addr];
      const reg = regionAt(regions, addr);
      const rgb = reg ? reg.rgb : [80, 80, 80];
      let dim = addr === pc ? 0.55 : 0.28;
      if (addr >= L) dim = 0.1;
      let r = rgb[0] * dim;
      let g = rgb[1] * dim;
      let b = rgb[2] * dim;
      const wa = (inBank ? wn : w0)[addr];
      if (wa !== 0) {
        const h = heat(steps - wa, W);
        r += (255 - r) * h * 0.8;
        g += (150 - g) * h * 0.8;
        b += (40 - b) * h * 0.8;
      }
      ctx.fillStyle = `rgb(${r | 0},${g | 0},${b | 0})`;
      ctx.fillRect(x, y, CW - 1, CH);
      ctx.fillStyle = addr === pc ? '#fff' : '#d0d7de';
      ctx.font = addr === pc ? 'bold 13px ui-monospace,Menlo,monospace' : '12px ui-monospace,Menlo,monospace';
      ctx.fillText(hex2(v), x + CW / 2, y + CH / 2 - 2);
      if (i % 4 === 0) {
        ctx.fillStyle = '#8b949e';
        ctx.font = '9px ui-monospace,Menlo,monospace';
        ctx.fillText(hex4(addr), x + CW / 2, TOP + CH + LBL / 2 + 1);
      }
      if (addr === selAddr) {
        ctx.strokeStyle = '#ffdd33';
        ctx.lineWidth = 2;
        ctx.strokeRect(x + 1, y + 1, CW - 3, CH - 2);
      }
    }

    // trail: oldest faint, newest bright
    const n = trail.length;
    for (let j = 0; j < n; j++) {
      const rel = trail[j] - base;
      if (rel < 0 || rel >= CELLS) continue;
      const a = 0.15 + (0.85 * (j + 1)) / n;
      ctx.fillStyle = `rgba(255,200,60,${a.toFixed(3)})`;
      ctx.fillRect(rel * CW, TOP + CH - 4, CW - 1, 4);
    }

    // head triangle
    const cx = HALF * CW + CW / 2;
    const blink = Math.floor(nowMs / 300) % 2 === 0;
    ctx.fillStyle = blink ? '#ff3b3b' : '#ff8a8a';
    ctx.beginPath();
    ctx.moveTo(cx - 7, 1);
    ctx.lineTo(cx + 7, 1);
    ctx.lineTo(cx, TOP - 1);
    ctx.closePath();
    ctx.fill();
    ctx.strokeStyle = '#fff';
    ctx.lineWidth = 1;
    ctx.strokeRect(HALF * CW + 0.5, TOP + 0.5, CW - 2, CH - 1);

    let dis = '';
    try {
      dis = engine.disasm(pc).text;
    } catch {
      dis = '?';
    }
    caption.textContent = `PC ${hex4(pc)}  ${dis}   tape ${tape}   trail ${n}`;
  }

  return {
    update(nowMs: number): void {
      tap.pump(nowMs);
      render(nowMs);
    },
    destroy(): void {
      unsubTrace();
      for (const f of offs) f();
      canvas.removeEventListener('click', onClick);
      root.replaceChildren();
      root.classList.remove('tos-panel', 'tos-strip');
    },
  };
}
