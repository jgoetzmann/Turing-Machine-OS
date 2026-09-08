/**
 * tapes — k stacked mini maps (one per tape), the machine-selected tape highlighted.
 * Each pixel summarises 8 bytes; click a tape to emit `select-tape`.
 */
import type { Engine } from '../engine';
import type { Bus } from '../bus';
import type { Panel } from './panel';
import {
  basePanel,
  injectStyle,
  el,
  ctx2d,
  fmtInt,
  hex4,
  regionsFor,
  bankWindow,
  tapeLength,
  setText,
  clamp,
  StepDelta,
  type Region,
} from './util';
export type { Panel } from './panel';

const MW = 256;
const MH = 32;
const BYTES_PER_PX = 65536 / (MW * MH); // 8

const CSS = `
.tos-tapes-row{display:grid;grid-template-columns:auto 1fr;gap:6px;align-items:center;margin-bottom:6px;cursor:pointer}
.tos-tapes-row .lbl{color:#8b949e;white-space:pre;min-width:9ch}
.tos-tapes-row canvas{width:100%;height:auto;aspect-ratio:${MW}/${MH};background:#000;border:2px solid #30363d}
.tos-tapes-row.sel .lbl{color:#e6edf3;font-weight:600}
.tos-tapes-row.sel canvas{border-color:#f0883e;box-shadow:0 0 6px #f0883e}
.tos-tapes-row.user canvas{outline:1px dashed #79c0ff}
.tos-tapes-foot{color:#8b949e;font-size:11px}
`;

interface Row {
  wrap: HTMLDivElement;
  lbl: HTMLSpanElement;
  canvas: HTMLCanvasElement;
  ctx: CanvasRenderingContext2D;
  img: ImageData;
  off: HTMLCanvasElement;
  octx: CanvasRenderingContext2D;
  onClick: () => void;
}

export function createTapesPanel(root: HTMLElement, engine: Engine, bus: Bus): Panel {
  basePanel(root, 'tos-tapes');
  injectStyle('tos-tapes-style', CSS);

  const list = el('div');
  const foot = el('div', 'tos-tapes-foot');
  root.append(list, foot);

  const rows: Row[] = [];
  let userTape: number | null = null;
  let regions: Region[] = [];
  let regionL = -1;
  const regionIdx = new Uint8Array(65536);
  let lastDraw = -1e9;
  const stepDelta = new StepDelta();

  function rebuildRegions(): void {
    regions = regionsFor(engine);
    regionL = tapeLength(engine);
    regionIdx.fill(regions.length - 1);
    regions.forEach((r, i) => {
      for (let a = Math.max(0, r.lo); a <= Math.min(65535, r.hi); a++) regionIdx[a] = i;
    });
  }

  function buildRows(k: number): void {
    for (const r of rows) r.wrap.removeEventListener('click', r.onClick);
    rows.length = 0;
    list.replaceChildren();
    for (let i = 0; i < k; i++) {
      const wrap = el('div', 'tos-tapes-row');
      const lbl = el('span', 'lbl', `tape ${i}`);
      const canvas = el('canvas');
      canvas.width = MW * 2;
      canvas.height = MH * 2;
      const ctx = ctx2d(canvas);
      ctx.imageSmoothingEnabled = false;
      const off = document.createElement('canvas');
      off.width = MW;
      off.height = MH;
      const octx = ctx2d(off);
      const img = octx.createImageData(MW, MH);
      const tapeIndex = i;
      const onClick = (): void => {
        userTape = tapeIndex;
        bus.emit('select-tape', { tape: tapeIndex });
      };
      wrap.addEventListener('click', onClick);
      wrap.append(lbl, canvas);
      list.append(wrap);
      rows.push({ wrap, lbl, canvas, ctx, img, off, octx, onClick });
    }
  }

  const offs: Array<() => void> = [];
  offs.push(
    bus.on('select-tape', (p) => {
      userTape = p && typeof p.tape === 'number' ? p.tape : null;
    }),
  );
  offs.push(
    bus.on('machine-reset', () => {
      userTape = null;
      regionL = -1;
      lastDraw = -1e9;
    }),
  );

  function drawTape(row: Row, i: number, steps: number, W: number, L: number, bw: { lo: number; hi: number }): void {
    const bytes = engine.tape(i);
    const wage = engine.writeAge(i);
    const px = row.img.data;
    const lnW = Math.log(1 + W);
    for (let p = 0; p < MW * MH; p++) {
      const base = p * BYTES_PER_PX;
      const o = p * 4;
      const inBank = base >= bw.lo && base <= bw.hi;
      if ((i > 0 && !inBank) || base >= L) {
        px[o] = 22;
        px[o + 1] = 22;
        px[o + 2] = 26;
        px[o + 3] = 255;
        continue;
      }
      let maxv = 0;
      let minAge = -1;
      for (let j = 0; j < BYTES_PER_PX; j++) {
        const a = base + j;
        const v = bytes[a];
        if (v > maxv) maxv = v;
        const wa = wage[a];
        if (wa !== 0) {
          const age = steps - wa;
          if (age >= 0 && (minAge < 0 || age < minAge)) minAge = age;
        }
      }
      const reg = regions[regionIdx[base]];
      const k = 0.16 + 0.84 * (maxv / 255);
      let r = reg.rgb[0] * k;
      let g = reg.rgb[1] * k;
      let b = reg.rgb[2] * k;
      if (minAge >= 0 && minAge < W) {
        const h = minAge === 0 ? 1 : 1 - Math.log(1 + minAge) / lnW;
        r += (255 - r) * h;
        g += (150 - g) * h;
        b += (40 - b) * h;
      }
      px[o] = r;
      px[o + 1] = g;
      px[o + 2] = b;
      px[o + 3] = 255;
    }
    row.octx.putImageData(row.img, 0, 0);
    row.ctx.drawImage(row.off, 0, 0, row.canvas.width, row.canvas.height);
    // head marker on the tape that owns PC (tape 0 unless PC is in the bank window of another tape)
    const pc = engine.cpu().pc & 0xffff;
    const sel = engine.tapeSelected();
    const pcTape = pc >= bw.lo && pc <= bw.hi ? sel : 0;
    if (pcTape === i) {
      const p = Math.floor(pc / BYTES_PER_PX);
      const x = (p % MW) * 2;
      const y = Math.floor(p / MW) * 2;
      row.ctx.fillStyle = '#ffffff';
      row.ctx.fillRect(x - 1, y - 1, 4, 4);
    }
  }

  function render(nowMs: number): void {
    const k = engine.tapeCount() || 1;
    if (rows.length !== k) buildRows(k);
    const L = tapeLength(engine);
    if (L !== regionL) rebuildRegions();
    const sel = engine.tapeSelected();
    const steps = engine.steps();
    const W = clamp(stepDelta.sample(steps) * 8, 4096, 1 << 20);
    const bw = bankWindow(engine);
    const throttle = nowMs - lastDraw >= 50;
    for (let i = 0; i < k; i++) {
      const row = rows[i];
      row.wrap.classList.toggle('sel', i === sel);
      row.wrap.classList.toggle('user', userTape === i && userTape !== sel);
      setText(row.lbl, `tape ${i}${i === sel ? ' ◀' : '  '}`);
      if (throttle) drawTape(row, i, steps, W, L, bw);
    }
    if (throttle) lastDraw = nowMs;
    setText(
      foot,
      `${k} tape${k === 1 ? '' : 's'} × ${fmtInt(L)} bytes · bank window ${hex4(bw.lo)}–${hex4(bw.hi)} · selected ${sel}` +
        (userTape !== null ? ` · viewing ${userTape}` : ''),
    );
  }

  return {
    update(nowMs: number): void {
      render(nowMs);
    },
    destroy(): void {
      for (const f of offs) f();
      for (const r of rows) r.wrap.removeEventListener('click', r.onClick);
      root.replaceChildren();
      root.classList.remove('tos-panel', 'tos-tapes');
    },
  };
}
