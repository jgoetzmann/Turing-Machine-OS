/**
 * detail — hex + ASCII dump of the selected page, and a disassembly window around PC.
 * The page follows PC until the user picks one (bus `select-page` / `select-address`).
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
  escapeHtml,
  isPrintable,
  regionsFor,
  regionAt,
  bankWindow,
  tapeLength,
  safeTape,
  setText,
  clamp,
  StepDelta,
  type Region,
} from './util';
export type { Panel } from './panel';

const CSS = `
.tos-detail-hex{background:#010409;border:1px solid #30363d;padding:4px;overflow-x:auto;font-size:11px;line-height:1.35}
.tos-detail-dis{background:#010409;border:1px solid #30363d;padding:4px;margin-top:6px;overflow-x:auto;font-size:11px;line-height:1.35}
.tos-detail-hex .b{cursor:pointer}
.tos-detail-hex .b:hover{background:#30363d}
.tos-detail .w{color:#ffa657}
.tos-detail .r{color:#79c0ff}
.tos-detail .pc{background:#b62324;color:#fff}
.tos-detail .sel{background:#9e6a03;color:#fff}
.tos-detail .cur{background:#1f3a5f;color:#fff}
.tos-detail .adr{color:#8b949e}
.tos-detail .asc{color:#8b949e}
.tos-detail-dis .line{cursor:pointer}
.tos-detail-dis .line:hover{background:#21262d}
`;

const DIS_BEFORE = 8;
const DIS_TOTAL = 22;

export function createDetailPanel(root: HTMLElement, engine: Engine, bus: Bus): Panel {
  basePanel(root, 'tos-detail');
  injectStyle('tos-detail-style', CSS);

  const bar = el('div', 'tos-bar');
  const prev = el('button', '', '◀');
  const next = el('button', '', '▶');
  const pageLbl = el('span', '', 'page --');
  const followLbl = el('label');
  const followCb = el('input');
  followCb.type = 'checkbox';
  followCb.checked = true;
  followLbl.append(followCb, 'follow PC');
  const tapeLbl = el('span', 'tos-muted', '');
  bar.append(prev, pageLbl, next, followLbl, tapeLbl);
  const hexPre = el('pre', 'tos-detail-hex');
  const disPre = el('pre', 'tos-detail-dis');
  root.append(bar, hexPre, disPre);

  let page = -1;
  let follow = true;
  let selAddr = -1;
  let userTape: number | null = null;
  let lastHex = '';
  let lastDis = '';
  let regions: Region[] = [];
  let regionL = -1;
  const stepDelta = new StepDelta();

  const offs: Array<() => void> = [];
  offs.push(
    bus.on('select-page', (p) => {
      if (p && typeof p.page === 'number') {
        page = p.page & 0xff;
        follow = false;
        followCb.checked = false;
      }
    }),
  );
  offs.push(
    bus.on('select-address', (p) => {
      if (p && typeof p.addr === 'number') {
        selAddr = p.addr & 0xffff;
        page = selAddr >> 8;
        follow = false;
        followCb.checked = false;
      }
    }),
  );
  offs.push(
    bus.on('select-tape', (p) => {
      userTape = p && typeof p.tape === 'number' ? p.tape : null;
    }),
  );
  offs.push(
    bus.on('machine-reset', () => {
      follow = true;
      followCb.checked = true;
      userTape = null;
      selAddr = -1;
      regionL = -1;
    }),
  );

  const onFollow = (): void => {
    follow = followCb.checked;
  };
  const onPrev = (): void => {
    page = ((page < 0 ? engine.cpu().pc >> 8 : page) + 255) & 0xff;
    follow = false;
    followCb.checked = false;
    bus.emit('select-page', { page });
  };
  const onNext = (): void => {
    page = ((page < 0 ? engine.cpu().pc >> 8 : page) + 1) & 0xff;
    follow = false;
    followCb.checked = false;
    bus.emit('select-page', { page });
  };
  const onHexClick = (ev: MouseEvent): void => {
    const t = ev.target as HTMLElement | null;
    const node = t && t.closest ? (t.closest('[data-addr]') as HTMLElement | null) : null;
    if (!node) return;
    const a = parseInt(node.dataset.addr || '', 16);
    if (Number.isNaN(a)) return;
    selAddr = a;
    bus.emit('select-address', { addr: a });
  };
  followCb.addEventListener('change', onFollow);
  prev.addEventListener('click', onPrev);
  next.addEventListener('click', onNext);
  hexPre.addEventListener('click', onHexClick);
  disPre.addEventListener('click', onHexClick);

  function byteView(tape: number): { t0: Uint8Array; tn: Uint8Array; lo: number; hi: number } {
    const bw = bankWindow(engine);
    const t0 = engine.tape(0);
    const tn = tape > 0 ? engine.tape(tape) : t0;
    return { t0, tn, lo: bw.lo, hi: bw.hi };
  }

  /** Walk forward from a start a few bytes before PC until a start lands exactly on PC. */
  function disasmStart(pc: number): number {
    for (let back = 24; back >= 1; back--) {
      const start = pc - back;
      if (start < 0) continue;
      let a = start;
      let steps = 0;
      while (a < pc && steps < 32) {
        const len = engine.disasm(a).len || 1;
        a += len;
        steps++;
      }
      if (a === pc) {
        // count instructions between start and pc; keep only ~DIS_BEFORE of them
        let cnt = 0;
        let b = start;
        while (b < pc) {
          b += engine.disasm(b).len || 1;
          cnt++;
        }
        if (cnt <= DIS_BEFORE) return start;
        let c = start;
        while (cnt > DIS_BEFORE) {
          c += engine.disasm(c).len || 1;
          cnt--;
        }
        return c;
      }
    }
    return pc;
  }

  function render(): void {
    const L = tapeLength(engine);
    if (L !== regionL) {
      regions = regionsFor(engine);
      regionL = L;
    }
    const cpu = engine.cpu();
    const pc = cpu.pc & 0xffff;
    const steps = engine.steps();
    const W = clamp(stepDelta.sample(steps) * 8, 512, 1 << 20);
    if (follow || page < 0) page = pc >> 8;
    const tape = safeTape(engine, userTape);
    const { t0, tn, lo, hi } = byteView(tape);
    const w0 = engine.writeAge(0);
    const r0 = engine.readAge(0);
    const wn = tape > 0 ? engine.writeAge(tape) : w0;
    const rn = tape > 0 ? engine.readAge(tape) : r0;
    const reg = regionAt(regions, page << 8);
    setText(pageLbl, `page ${hex2(page)}  ${hex4(page << 8)}–${hex4((page << 8) | 0xff)}  ${reg ? reg.name : ''}`);
    setText(tapeLbl, `tape ${tape}`);

    let html = '';
    for (let row = 0; row < 16; row++) {
      const base = (page << 8) | (row << 4);
      html += `<span class="adr">${hex4(base)}</span>  `;
      let asc = '';
      for (let i = 0; i < 16; i++) {
        const addr = base + i;
        const inBank = addr >= lo && addr <= hi;
        const v = (inBank ? tn : t0)[addr];
        const wa = (inBank ? wn : w0)[addr];
        const ra = (inBank ? rn : r0)[addr];
        let cls = 'b';
        if (addr === pc) cls += ' pc';
        else if (addr === selAddr) cls += ' sel';
        else if (wa !== 0 && steps - wa >= 0 && steps - wa < W) cls += ' w';
        else if (ra !== 0 && steps - ra >= 0 && steps - ra < W) cls += ' r';
        if (addr >= L) cls += ' adr';
        html += `<span class="${cls}" data-addr="${hex4(addr)}">${hex2(v)}</span>${i === 7 ? '  ' : ' '}`;
        asc += isPrintable(v) ? escapeHtml(String.fromCharCode(v)) : '.';
      }
      html += ` <span class="asc">|${asc}|</span>\n`;
    }
    if (html !== lastHex) {
      hexPre.innerHTML = html;
      lastHex = html;
    }

    let dis = '';
    let a = disasmStart(pc);
    for (let n = 0; n < DIS_TOTAL && a <= 0xffff; n++) {
      let text = '?';
      let len = 1;
      try {
        const d = engine.disasm(a);
        text = d.text;
        len = d.len || 1;
      } catch {
        /* keep placeholder */
      }
      let bytes = '';
      for (let i = 0; i < len; i++) {
        const addr = a + i;
        if (addr > 0xffff) break;
        const inBank = addr >= lo && addr <= hi;
        bytes += hex2((inBank ? tn : t0)[addr]) + ' ';
      }
      const cls = a === pc ? 'line cur' : 'line';
      const mark = a === pc ? '▶' : ' ';
      dis += `<span class="${cls}" data-addr="${hex4(a)}">${mark} ${hex4(a)}  ${bytes.padEnd(9)} ${escapeHtml(text)}</span>\n`;
      a += len;
    }
    if (dis !== lastDis) {
      disPre.innerHTML = dis;
      lastDis = dis;
    }
  }

  return {
    update(): void {
      render();
    },
    destroy(): void {
      for (const f of offs) f();
      followCb.removeEventListener('change', onFollow);
      prev.removeEventListener('click', onPrev);
      next.removeEventListener('click', onNext);
      hexPre.removeEventListener('click', onHexClick);
      disPre.removeEventListener('click', onHexClick);
      root.replaceChildren();
      root.classList.remove('tos-panel', 'tos-detail');
    },
  };
}
