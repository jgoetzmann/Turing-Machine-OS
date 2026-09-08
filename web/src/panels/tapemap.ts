/**
 * tapemap — the whole tape as a 256×256 cell map (row = page, column = low byte).
 * Region tint by address, write-age heat (warm), read-age hue (cool), blinking head at PC,
 * hover tooltip, click → `select-page` (+ `select-address`).
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
  fmtInt,
  regionsFor,
  bankWindow,
  tapeLength,
  safeTape,
  isPrintable,
  StepDelta,
  clamp,
  type Region,
} from './util';
export type { Panel } from './panel';

const CSS = `
.tos-tapemap-wrap{position:relative;width:100%;max-width:512px}
.tos-tapemap-canvas{width:100%;height:auto;aspect-ratio:1/1;background:#000;border:1px solid #30363d;cursor:crosshair}
.tos-tapemap-tip{position:absolute;pointer-events:none;background:#161b22;border:1px solid #30363d;border-radius:3px;padding:3px 6px;white-space:pre;z-index:5;font-size:11px;box-shadow:0 2px 8px rgba(0,0,0,.5)}
.tos-tapemap-legend{display:flex;flex-wrap:wrap;gap:4px 10px;margin-top:6px;font-size:11px}
.tos-tapemap-legend span i{display:inline-block;width:10px;height:10px;margin-right:4px;vertical-align:-1px;border-radius:2px}
.tos-tapemap-status{margin-top:4px;color:#8b949e;font-size:11px;white-space:pre}
`;

const SIZE = 512; // canvas pixels; 2 px per cell
const SCALE = SIZE / 256;

export function createTapemapPanel(root: HTMLElement, engine: Engine, bus: Bus): Panel {
  basePanel(root, 'tos-tapemap');
  injectStyle('tos-tapemap-style', CSS);

  const wrap = el('div', 'tos-tapemap-wrap');
  const canvas = el('canvas', 'tos-tapemap-canvas');
  canvas.width = SIZE;
  canvas.height = SIZE;
  const tip = el('div', 'tos-tapemap-tip');
  tip.hidden = true;
  const legend = el('div', 'tos-tapemap-legend');
  const status = el('div', 'tos-tapemap-status');
  wrap.append(canvas, tip);
  root.append(wrap, legend, status);

  const ctx = ctx2d(canvas);
  ctx.imageSmoothingEnabled = false;
  const off = document.createElement('canvas');
  off.width = 256;
  off.height = 256;
  const octx = ctx2d(off);
  const img = octx.createImageData(256, 256);
  const px = img.data;

  let userTape: number | null = null;
  let selPage = -1;
  let selAddr = -1;
  let hoverAddr = -1;
  let regions: Region[] = [];
  let regionL = -1;
  const regionIdx = new Uint8Array(65536);
  const stepDelta = new StepDelta();

  function rebuildRegions(): void {
    regions = regionsFor(engine);
    regionL = tapeLength(engine);
    regionIdx.fill(regions.length - 1);
    regions.forEach((r, i) => {
      for (let a = Math.max(0, r.lo); a <= Math.min(65535, r.hi); a++) regionIdx[a] = i;
    });
    legend.replaceChildren();
    for (const r of regions) {
      const s = el('span');
      const sw = el('i');
      sw.style.background = r.css;
      s.append(sw, `${r.name} ${hex4(r.lo)}–${hex4(r.hi)}`);
      legend.append(s);
    }
  }

  const offs: Array<() => void> = [];
  offs.push(
    bus.on('select-tape', (p) => {
      userTape = p && typeof p.tape === 'number' ? p.tape : null;
    }),
  );
  offs.push(
    bus.on('select-page', (p) => {
      if (p && typeof p.page === 'number') selPage = p.page & 0xff;
    }),
  );
  offs.push(
    bus.on('select-address', (p) => {
      if (p && typeof p.addr === 'number') selAddr = p.addr & 0xffff;
    }),
  );
  offs.push(
    bus.on('machine-reset', () => {
      userTape = null;
      regionL = -1;
    }),
  );

  function addrFromEvent(ev: MouseEvent): number {
    const r = canvas.getBoundingClientRect();
    if (r.width === 0 || r.height === 0) return -1;
    const x = Math.floor(((ev.clientX - r.left) / r.width) * 256);
    const y = Math.floor(((ev.clientY - r.top) / r.height) * 256);
    if (x < 0 || x > 255 || y < 0 || y > 255) return -1;
    return (y << 8) | x;
  }

  function tipText(addr: number): string {
    const tape = safeTape(engine, userTape);
    const bw = bankWindow(engine);
    const t = tape > 0 && addr >= bw.lo && addr <= bw.hi ? tape : 0;
    const v = engine.tape(t)[addr];
    const wa = engine.writeAge(t)[addr];
    const ra = engine.readAge(t)[addr];
    const reg = regions[regionIdx[addr]];
    const ch = isPrintable(v) ? `'${String.fromCharCode(v)}'` : '   ';
    const lines = [
      `${hex4(addr)}  = ${hex2(v)} ${ch} (${v})`,
      `${reg ? reg.name : '?'}  page ${hex2(addr >> 8)}  tape ${t}`,
      `written @${wa ? fmtInt(wa) : 'never'}  read @${ra ? fmtInt(ra) : 'never'}`,
    ];
    return lines.join('\n');
  }

  const onMove = (ev: MouseEvent): void => {
    hoverAddr = addrFromEvent(ev);
    if (hoverAddr < 0) {
      tip.hidden = true;
      return;
    }
    tip.hidden = false;
    const r = canvas.getBoundingClientRect();
    let left = ev.clientX - r.left + 14;
    let top = ev.clientY - r.top + 14;
    if (left > r.width - 190) left = Math.max(0, ev.clientX - r.left - 190);
    if (top > r.height - 60) top = Math.max(0, ev.clientY - r.top - 60);
    tip.style.left = `${left}px`;
    tip.style.top = `${top}px`;
    tip.textContent = tipText(hoverAddr);
  };
  const onLeave = (): void => {
    hoverAddr = -1;
    tip.hidden = true;
  };
  const onClick = (ev: MouseEvent): void => {
    const a = addrFromEvent(ev);
    if (a < 0) return;
    selPage = a >> 8;
    selAddr = a;
    bus.emit('select-page', { page: selPage });
    bus.emit('select-address', { addr: a });
  };
  canvas.addEventListener('mousemove', onMove);
  canvas.addEventListener('mouseleave', onLeave);
  canvas.addEventListener('click', onClick);

  function render(nowMs: number): void {
    const L = tapeLength(engine);
    if (L !== regionL) rebuildRegions();
    const tape = safeTape(engine, userTape);
    const steps = engine.steps();
    const spf = stepDelta.sample(steps);
    const W = clamp(spf * 8, 4096, 1 << 20);
    const lnW = Math.log(1 + W);

    const t0 = engine.tape(0);
    const tn = tape > 0 ? engine.tape(tape) : t0;
    const w0 = engine.writeAge(0);
    const wn = tape > 0 ? engine.writeAge(tape) : w0;
    const r0 = engine.readAge(0);
    const rn = tape > 0 ? engine.readAge(tape) : r0;
    const bw = bankWindow(engine);

    for (let addr = 0; addr < 65536; addr++) {
      const o = addr << 2;
      if (addr >= L) {
        px[o] = 26;
        px[o + 1] = 26;
        px[o + 2] = 30;
        px[o + 3] = 255;
        continue;
      }
      const inBank = addr >= bw.lo && addr <= bw.hi;
      const bytes = inBank ? tn : t0;
      const wage = inBank ? wn : w0;
      const rage = inBank ? rn : r0;
      const reg = regions[regionIdx[addr]];
      const v = bytes[addr];
      const k = 0.14 + 0.86 * (v / 255);
      let r = reg.rgb[0] * k;
      let g = reg.rgb[1] * k;
      let b = reg.rgb[2] * k;
      const wa = wage[addr];
      if (wa !== 0) {
        const age = steps - wa;
        if (age >= 0 && age < W) {
          const h = age === 0 ? 1 : 1 - Math.log(1 + age) / lnW;
          r += (255 - r) * h;
          g += (150 - g) * h;
          b += (40 - b) * h;
        }
      }
      const ra = rage[addr];
      if (ra !== 0) {
        const age = steps - ra;
        if (age >= 0 && age < W) {
          const h = (age === 0 ? 1 : 1 - Math.log(1 + age) / lnW) * 0.7;
          r += (80 - r) * h;
          g += (180 - g) * h;
          b += (255 - b) * h;
        }
      }
      px[o] = r;
      px[o + 1] = g;
      px[o + 2] = b;
      px[o + 3] = 255;
    }
    octx.putImageData(img, 0, 0);
    ctx.clearRect(0, 0, SIZE, SIZE);
    ctx.drawImage(off, 0, 0, SIZE, SIZE);

    // selected page = one row
    if (selPage >= 0) {
      ctx.strokeStyle = 'rgba(255,255,255,0.75)';
      ctx.lineWidth = 1;
      ctx.strokeRect(0.5, selPage * SCALE - 0.5, SIZE - 1, SCALE + 1);
    }
    if (selAddr >= 0) {
      ctx.strokeStyle = 'rgba(255,255,0,0.9)';
      ctx.strokeRect((selAddr & 0xff) * SCALE - 1.5, (selAddr >> 8) * SCALE - 1.5, SCALE + 3, SCALE + 3);
    }
    // blinking head
    const cpu = engine.cpu();
    const pc = cpu.pc & 0xffff;
    const hx = (pc & 0xff) * SCALE;
    const hy = (pc >> 8) * SCALE;
    ctx.fillStyle = 'rgba(255,255,255,0.18)';
    ctx.fillRect(0, hy, SIZE, SCALE);
    ctx.fillRect(hx, 0, SCALE, SIZE);
    if (Math.floor(nowMs / 300) % 2 === 0) {
      ctx.fillStyle = '#ffffff';
      ctx.fillRect(hx - 2, hy - 2, SCALE + 4, SCALE + 4);
      ctx.fillStyle = '#ff3b3b';
      ctx.fillRect(hx, hy, SCALE, SCALE);
    }
    if (hoverAddr >= 0) {
      ctx.strokeStyle = 'rgba(255,255,255,0.9)';
      ctx.strokeRect((hoverAddr & 0xff) * SCALE - 0.5, (hoverAddr >> 8) * SCALE - 0.5, SCALE + 1, SCALE + 1);
      tip.textContent = tipText(hoverAddr);
    }
    status.textContent =
      `tape ${tape}/${engine.tapeCount()}  L=${fmtInt(L)}  pc=${hex4(pc)}  step ${fmtInt(steps)}` +
      (selPage >= 0 ? `  page ${hex2(selPage)}` : '') +
      `  heat window ${fmtInt(W)} steps`;
  }

  return {
    update(nowMs: number): void {
      render(nowMs);
    },
    destroy(): void {
      for (const f of offs) f();
      canvas.removeEventListener('mousemove', onMove);
      canvas.removeEventListener('mouseleave', onLeave);
      canvas.removeEventListener('click', onClick);
      root.replaceChildren();
      root.classList.remove('tos-panel', 'tos-tapemap');
    },
  };
}
