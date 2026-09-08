/**
 * display — the 64×32 1-bpp framebuffer (`engine.display()`), scaled on a canvas.
 * A toggle highlights the display page on the tape map by emitting `select-page`.
 */
import type { Engine } from '../engine';
import type { Bus } from '../bus';
import type { Panel } from './panel';
import { basePanel, injectStyle, el, ctx2d, fmtInt, hex4, setText, displayBase } from './util';
export type { Panel } from './panel';

const W = 64;
const H = 32;
const SCALE = 8;

const CSS = `
.tos-display canvas{width:100%;max-width:${W * SCALE}px;height:auto;aspect-ratio:2/1;background:#000;border:2px solid #30363d}
.tos-display canvas.hl{border-color:#2ee6d6;box-shadow:0 0 8px #2ee6d6}
`;

export function createDisplayPanel(root: HTMLElement, engine: Engine, bus: Bus): Panel {
  basePanel(root, 'tos-display');
  injectStyle('tos-display-style', CSS);

  const bar = el('div', 'tos-bar');
  const hlBtn = el('button', '', 'highlight display page');
  const frameLbl = el('span', 'tos-muted', 'frame 0');
  const baseLbl = el('span', 'tos-muted', '');
  bar.append(hlBtn, frameLbl, baseLbl);
  const canvas = el('canvas');
  canvas.width = W * SCALE;
  canvas.height = H * SCALE;
  root.append(bar, canvas);

  const ctx = ctx2d(canvas);
  ctx.imageSmoothingEnabled = false;
  const off = document.createElement('canvas');
  off.width = W;
  off.height = H;
  const octx = ctx2d(off);
  const img = octx.createImageData(W, H);
  const px = img.data;
  const last = new Uint8Array(256);
  let first = true;
  let highlight = false;

  const onToggle = (): void => {
    highlight = !highlight;
    hlBtn.classList.toggle('on', highlight);
    canvas.classList.toggle('hl', highlight);
    if (highlight) {
      const base = displayBase(engine);
      bus.emit('select-page', { page: (base >> 8) & 0xff });
      bus.emit('select-address', { addr: base & 0xffff });
    }
  };
  hlBtn.addEventListener('click', onToggle);

  const offs: Array<() => void> = [];
  offs.push(
    bus.on('machine-reset', () => {
      first = true;
    }),
  );

  function render(): void {
    const fb = engine.display();
    let changed = first;
    if (!changed) {
      for (let i = 0; i < 256; i++) {
        if (fb[i] !== last[i]) {
          changed = true;
          break;
        }
      }
    }
    if (changed) {
      for (let y = 0; y < H; y++) {
        for (let x = 0; x < W; x++) {
          const on = (fb[y * 8 + (x >> 3)] >> (7 - (x & 7))) & 1;
          const o = (y * W + x) * 4;
          if (on) {
            px[o] = 120;
            px[o + 1] = 255;
            px[o + 2] = 140;
          } else {
            px[o] = 6;
            px[o + 1] = 18;
            px[o + 2] = 8;
          }
          px[o + 3] = 255;
        }
      }
      octx.putImageData(img, 0, 0);
      ctx.drawImage(off, 0, 0, W * SCALE, H * SCALE);
      last.set(fb.subarray(0, 256));
      first = false;
    }
    setText(frameLbl, `frame ${fmtInt(engine.frame())}`);
    setText(baseLbl, `@${hex4(displayBase(engine))}`);
  }

  return {
    update(): void {
      render();
    },
    destroy(): void {
      for (const f of offs) f();
      hlBtn.removeEventListener('click', onToggle);
      root.replaceChildren();
      root.classList.remove('tos-panel', 'tos-display');
    },
  };
}
