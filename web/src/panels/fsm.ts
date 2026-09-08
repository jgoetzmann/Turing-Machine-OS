/**
 * fsm — the kernel's finite state control as an SVG: six states, twelve transitions
 * (from `engine.transitions()`), the active state lit, per-edge fired counters, and a
 * short flash on every edge that fired since the previous frame.
 */
import type { Engine } from '../engine';
import type { Bus } from '../bus';
import type { Panel } from './panel';
import { basePanel, injectStyle, el, fmtInt, setText, STATE_LABELS, nameFor } from './util';
export type { Panel } from './panel';

const SVG_NS = 'http://www.w3.org/2000/svg';
const R = 27;

interface NodePos {
  id: number;
  name: string;
  x: number;
  y: number;
}

const NODES: NodePos[] = [
  { id: 0, name: 'BOOT', x: 58, y: 130 },
  { id: 2, name: 'SHELL', x: 185, y: 60 },
  { id: 3, name: 'RUNNING', x: 185, y: 200 },
  { id: 4, name: 'SYSCALL', x: 312, y: 130 },
  { id: 1, name: 'IDLE', x: 432, y: 60 },
  { id: 5, name: 'HALT', x: 432, y: 200 },
];

/** Fallback copy of the frozen table (kernel.h) for a machine that reports none. */
const FROZEN: Array<{ from: number; to: number; why: string }> = [
  { from: 0, to: 2, why: 'shell loaded' },
  { from: 2, to: 4, why: 'OUT 01' },
  { from: 4, to: 2, why: 'syscall done' },
  { from: 4, to: 3, why: 'program loaded / syscall done' },
  { from: 3, to: 4, why: 'OUT 01' },
  { from: 3, to: 2, why: 'program HLT' },
  { from: 4, to: 1, why: 'waiting for input' },
  { from: 1, to: 4, why: 'input available' },
  { from: 1, to: 5, why: 'console EOF' },
  { from: 2, to: 5, why: 'halt command or tape fault' },
  { from: 3, to: 5, why: 'tape fault' },
  { from: 4, to: 5, why: 'console EOF' },
];

const CSS = `
.tos-fsm svg{width:100%;height:auto;display:block;background:#010409;border:1px solid #30363d}
.tos-fsm .node circle{fill:#161b22;stroke:#30363d;stroke-width:2}
.tos-fsm .node text{fill:#c9d1d9;font:600 11px ui-monospace,Menlo,monospace;text-anchor:middle}
.tos-fsm .node .visits{fill:#8b949e;font-weight:400;font-size:9px}
.tos-fsm .node.active circle{fill:#1f6feb;stroke:#79c0ff;filter:drop-shadow(0 0 6px #388bfd)}
.tos-fsm .node.active text{fill:#fff}
.tos-fsm .node.halt.active circle{fill:#b62324;stroke:#ff7b72}
.tos-fsm .edge path{fill:none;stroke:#484f58;stroke-width:1.5;marker-end:url(#tos-fsm-arrow)}
.tos-fsm .edge.used path{stroke:#8b949e}
.tos-fsm .edge.fired path{stroke:#ffa657;stroke-width:2.5;marker-end:url(#tos-fsm-arrow-hot)}
.tos-fsm .edge text{fill:#8b949e;font:9px ui-monospace,Menlo,monospace;text-anchor:middle;paint-order:stroke;stroke:#010409;stroke-width:3px}
.tos-fsm .edge.fired text{fill:#ffa657}
.tos-fsm-list{margin:6px 0 0;padding:0;list-style:none;columns:2;column-gap:12px;font-size:11px}
.tos-fsm-list li{white-space:nowrap;overflow:hidden;text-overflow:ellipsis;color:#8b949e}
.tos-fsm-list li b{color:#c9d1d9;display:inline-block;min-width:22px}
.tos-fsm-list li.fired{color:#ffa657}
.tos-fsm-list li .cnt{color:#e6edf3;float:right}
`;

function svgEl<K extends keyof SVGElementTagNameMap>(tag: K, attrs: Record<string, string | number>): SVGElementTagNameMap[K] {
  const e = document.createElementNS(SVG_NS, tag);
  for (const k of Object.keys(attrs)) e.setAttribute(k, String(attrs[k]));
  return e;
}

function edgePath(a: NodePos, b: NodePos): { d: string; lx: number; ly: number } {
  const dx = b.x - a.x;
  const dy = b.y - a.y;
  const len = Math.hypot(dx, dy) || 1;
  const nx = -dy / len;
  const ny = dx / len;
  const bend = 32;
  const cx = (a.x + b.x) / 2 + nx * bend;
  const cy = (a.y + b.y) / 2 + ny * bend;
  const sdx = cx - a.x;
  const sdy = cy - a.y;
  const sl = Math.hypot(sdx, sdy) || 1;
  const sx = a.x + (sdx / sl) * R;
  const sy = a.y + (sdy / sl) * R;
  const edx = cx - b.x;
  const edy = cy - b.y;
  const elen = Math.hypot(edx, edy) || 1;
  const ex = b.x + (edx / elen) * (R + 7);
  const ey = b.y + (edy / elen) * (R + 7);
  const lx = 0.25 * sx + 0.5 * cx + 0.25 * ex;
  const ly = 0.25 * sy + 0.5 * cy + 0.25 * ey;
  return {
    d: `M${sx.toFixed(1)},${sy.toFixed(1)} Q${cx.toFixed(1)},${cy.toFixed(1)} ${ex.toFixed(1)},${ey.toFixed(1)}`,
    lx,
    ly,
  };
}

export function createFsmPanel(root: HTMLElement, engine: Engine, bus: Bus): Panel {
  basePanel(root, 'tos-fsm');
  injectStyle('tos-fsm-style', CSS);

  let table: Array<{ from: number; to: number; why: string; fired: number }> = [];
  try {
    table = engine.transitions();
  } catch {
    table = [];
  }
  if (!table || table.length === 0) table = FROZEN.map((t) => ({ ...t, fired: 0 }));

  const svg = svgEl('svg', { viewBox: '0 0 490 260', role: 'img' });
  const defs = svgEl('defs', {});
  for (const [id, color] of [
    ['tos-fsm-arrow', '#8b949e'],
    ['tos-fsm-arrow-hot', '#ffa657'],
  ] as const) {
    const m = svgEl('marker', { id, viewBox: '0 0 10 10', refX: 9, refY: 5, markerWidth: 7, markerHeight: 7, orient: 'auto-start-reverse' });
    const p = svgEl('path', { d: 'M0,0 L10,5 L0,10 z', fill: color });
    m.append(p);
    defs.append(m);
  }
  svg.append(defs);

  const byId = new Map<number, NodePos>();
  for (const n of NODES) byId.set(n.id, n);

  const edgeGroups: SVGGElement[] = [];
  const edgeCounts: SVGTextElement[] = [];
  const edgesLayer = svgEl('g', {});
  table.forEach((t, i) => {
    const a = byId.get(t.from);
    const b = byId.get(t.to);
    if (!a || !b) return;
    const g = svgEl('g', { class: 'edge' });
    const { d, lx, ly } = edgePath(a, b);
    const path = svgEl('path', { d });
    const title = svgEl('title', {});
    title.textContent = `#${i} ${nameFor(STATE_LABELS, t.from)} → ${nameFor(STATE_LABELS, t.to)}: ${t.why}`;
    const txt = svgEl('text', { x: lx.toFixed(1), y: (ly + 3).toFixed(1) });
    txt.textContent = `${i}:0`;
    g.append(title, path, txt);
    edgesLayer.append(g);
    edgeGroups[i] = g;
    edgeCounts[i] = txt;
  });
  svg.append(edgesLayer);

  const nodeGroups = new Map<number, SVGGElement>();
  const nodeVisits = new Map<number, SVGTextElement>();
  for (const n of NODES) {
    const g = svgEl('g', { class: n.id === 5 ? 'node halt' : 'node' });
    const c = svgEl('circle', { cx: n.x, cy: n.y, r: R });
    const t = svgEl('text', { x: n.x, y: n.y + 1 });
    t.textContent = n.name;
    const v = svgEl('text', { x: n.x, y: n.y + 13, class: 'visits' });
    v.textContent = '';
    g.append(c, t, v);
    svg.append(g);
    nodeGroups.set(n.id, g);
    nodeVisits.set(n.id, v);
  }

  const list = el('ul', 'tos-fsm-list');
  const listItems: HTMLLIElement[] = [];
  const listCounts: HTMLSpanElement[] = [];
  table.forEach((t, i) => {
    const li = el('li');
    const b = el('b', '', `#${i}`);
    const cnt = el('span', 'cnt', '0');
    li.append(b, ` ${nameFor(STATE_LABELS, t.from)}→${nameFor(STATE_LABELS, t.to)} ${t.why}`, cnt);
    li.title = t.why;
    list.append(li);
    listItems[i] = li;
    listCounts[i] = cnt;
  });

  root.append(svg, list);

  const lastFired: number[] = table.map(() => -1);
  const flashUntil: number[] = table.map(() => 0);
  let activeId = -1;

  const offs: Array<() => void> = [];
  offs.push(
    bus.on('machine-reset', () => {
      for (let i = 0; i < lastFired.length; i++) {
        lastFired[i] = -1;
        flashUntil[i] = 0;
      }
    }),
  );

  function render(nowMs: number): void {
    let cur: Array<{ from: number; to: number; why: string; fired: number }>;
    try {
      cur = engine.transitions();
    } catch {
      cur = [];
    }
    const visits = new Map<number, number>();
    for (let i = 0; i < table.length; i++) {
      const fired = cur && cur[i] ? cur[i].fired : 0;
      const g = edgeGroups[i];
      if (!g) continue;
      if (lastFired[i] >= 0 && fired > lastFired[i]) flashUntil[i] = nowMs + 350;
      lastFired[i] = fired;
      const hot = nowMs < flashUntil[i];
      g.classList.toggle('fired', hot);
      g.classList.toggle('used', fired > 0);
      const txt = edgeCounts[i];
      const s = `${i}:${fired}`;
      if (txt.textContent !== s) txt.textContent = s;
      const li = listItems[i];
      if (li) {
        li.classList.toggle('fired', hot);
        setText(listCounts[i], fmtInt(fired));
      }
      const to = table[i].to;
      visits.set(to, (visits.get(to) || 0) + fired);
    }
    for (const n of NODES) {
      const v = nodeVisits.get(n.id);
      if (v) {
        const s = n.id === 0 ? '' : fmtInt(visits.get(n.id) || 0);
        if (v.textContent !== s) v.textContent = s;
      }
    }
    const st = engine.state();
    if (st !== activeId) {
      for (const [id, g] of nodeGroups) g.classList.toggle('active', id === st);
      activeId = st;
    }
  }

  return {
    update(nowMs: number): void {
      render(nowMs);
    },
    destroy(): void {
      for (const f of offs) f();
      root.replaceChildren();
      root.classList.remove('tos-panel', 'tos-fsm');
    },
  };
}
