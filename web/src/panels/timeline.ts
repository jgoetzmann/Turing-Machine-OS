/**
 * timeline — a scrubber over the snapshot ring (`snapshotCount` / `snapshotStep`).
 * Dragging, clicking the bar, or the prev/next buttons emit `seek {step}` on the bus;
 * the app performs the seek (`engine.seek`) so the run loop stays in one place.
 */
import type { Engine } from '../engine';
import type { Bus } from '../bus';
import type { Panel } from './panel';
import { basePanel, injectStyle, el, ctx2d, fmtInt, setText, clamp } from './util';
export type { Panel } from './panel';

const CSS = `
.tos-timeline canvas{width:100%;height:34px;background:#010409;border:1px solid #30363d;cursor:pointer}
.tos-timeline input[type=range]{width:100%;margin:4px 0}
.tos-timeline-read{display:flex;justify-content:space-between;gap:8px;color:#8b949e;font-size:11px}
.tos-timeline-read b{color:#e6edf3}
`;

const BAR_W = 600;
const BAR_H = 34;

export function createTimelinePanel(root: HTMLElement, engine: Engine, bus: Bus): Panel {
  basePanel(root, 'tos-timeline');
  injectStyle('tos-timeline-style', CSS);

  const bar = el('div', 'tos-bar');
  const firstBtn = el('button', '', '|◀');
  const prevBtn = el('button', '', '◀ snap');
  const nextBtn = el('button', '', 'snap ▶');
  const liveBtn = el('button', '', 'live ▶|');
  const info = el('span', 'tos-muted', '');
  bar.append(firstBtn, prevBtn, nextBtn, liveBtn, info);
  const canvas = el('canvas');
  canvas.width = BAR_W;
  canvas.height = BAR_H;
  const range = el('input');
  range.type = 'range';
  range.min = '0';
  range.max = '1';
  range.step = '1';
  range.value = '0';
  const read = el('div', 'tos-timeline-read');
  const readStep = el('span');
  const readSnap = el('span');
  read.append(readStep, readSnap);
  root.append(bar, canvas, range, read);
  const ctx = ctx2d(canvas);

  let dragging = false;
  let lastEmit = -1e9;
  let pendingSeek = -1;
  let maxStep = 1;
  let requested = -1;

  function snapshotSteps(): number[] {
    const n = engine.snapshotCount();
    const out: number[] = [];
    for (let i = 0; i < n; i++) {
      const s = engine.snapshotStep(i);
      if (s >= 0) out.push(s);
    }
    out.sort((a, b) => a - b);
    return out;
  }

  function emitSeek(step: number): void {
    step = Math.max(0, Math.floor(step));
    requested = step;
    // `seek` is the app's notification that a seek happened; `seek-request` is what asks for one.
    bus.emit('seek-request', { step });
  }

  const onInput = (): void => {
    dragging = true;
    const step = parseInt(range.value, 10) || 0;
    pendingSeek = step;
    setText(readStep, `scrub → step ${fmtInt(step)}`);
  };
  const onChange = (): void => {
    dragging = false;
    const step = parseInt(range.value, 10) || 0;
    pendingSeek = -1;
    emitSeek(step);
  };
  const onPointerDown = (): void => {
    dragging = true;
  };
  const onPointerUp = (): void => {
    dragging = false;
  };
  const onCanvasClick = (ev: MouseEvent): void => {
    const r = canvas.getBoundingClientRect();
    if (r.width === 0) return;
    const frac = clamp((ev.clientX - r.left) / r.width, 0, 1);
    emitSeek(frac * maxStep);
  };
  const onFirst = (): void => {
    const snaps = snapshotSteps();
    emitSeek(snaps.length ? snaps[0] : 0);
  };
  const onPrev = (): void => {
    const cur = engine.steps();
    const snaps = snapshotSteps().filter((s) => s < cur);
    if (snaps.length) emitSeek(snaps[snaps.length - 1]);
    else emitSeek(0);
  };
  const onNext = (): void => {
    const cur = engine.steps();
    const snaps = snapshotSteps().filter((s) => s > cur);
    if (snaps.length) emitSeek(snaps[0]);
  };
  const onLive = (): void => {
    requested = -1;
    bus.emit('run-request', { running: true });
  };

  range.addEventListener('input', onInput);
  range.addEventListener('change', onChange);
  range.addEventListener('pointerdown', onPointerDown);
  range.addEventListener('pointerup', onPointerUp);
  canvas.addEventListener('click', onCanvasClick);
  firstBtn.addEventListener('click', onFirst);
  prevBtn.addEventListener('click', onPrev);
  nextBtn.addEventListener('click', onNext);
  liveBtn.addEventListener('click', onLive);

  const offs: Array<() => void> = [];
  offs.push(
    bus.on('machine-reset', () => {
      requested = -1;
      pendingSeek = -1;
      dragging = false;
    }),
  );

  function render(nowMs: number): void {
    const cur = engine.steps();
    const snaps = snapshotSteps();
    const latest = snaps.length ? snaps[snaps.length - 1] : 0;
    maxStep = Math.max(1, cur, latest, requested);
    range.max = String(maxStep);
    if (!dragging) range.value = String(clamp(cur, 0, maxStep));

    // throttled seeks while dragging so the tape follows the thumb
    if (dragging && pendingSeek >= 0 && nowMs - lastEmit >= 120) {
      emitSeek(pendingSeek);
      pendingSeek = -1;
      lastEmit = nowMs;
    }

    ctx.fillStyle = '#010409';
    ctx.fillRect(0, 0, BAR_W, BAR_H);
    ctx.fillStyle = '#21262d';
    ctx.fillRect(0, BAR_H / 2 - 3, BAR_W, 6);
    const x = (s: number): number => (clamp(s, 0, maxStep) / maxStep) * (BAR_W - 2) + 1;
    ctx.fillStyle = '#1f6feb';
    for (const s of snaps) {
      const sx = x(s);
      ctx.fillRect(sx - 1.5, 6, 3, BAR_H - 12);
    }
    const cx = x(cur);
    ctx.fillStyle = '#ffa657';
    ctx.fillRect(cx - 1, 2, 2, BAR_H - 4);
    ctx.beginPath();
    ctx.moveTo(cx - 5, 2);
    ctx.lineTo(cx + 5, 2);
    ctx.lineTo(cx, 8);
    ctx.closePath();
    ctx.fill();
    if (requested >= 0 && requested !== cur) {
      const rx = x(requested);
      ctx.strokeStyle = '#3fb950';
      ctx.setLineDash([2, 2]);
      ctx.beginPath();
      ctx.moveTo(rx, 2);
      ctx.lineTo(rx, BAR_H - 2);
      ctx.stroke();
      ctx.setLineDash([]);
    }

    if (!dragging) {
      readStep.innerHTML = `step <b>${fmtInt(cur)}</b> of ${fmtInt(maxStep)}`;
    }
    setText(readSnap, `${snaps.length} snapshot${snaps.length === 1 ? '' : 's'}${latest ? ` · latest @${fmtInt(latest)}` : ''}`);
    const interval = engine.config ? engine.config.snapInterval : 0;
    setText(info, interval > 0 ? `every ${fmtInt(interval)} steps` : 'snapshots off (snap interval 0)');
    prevBtn.disabled = snaps.length === 0 && cur === 0;
    nextBtn.disabled = !snaps.some((s) => s > cur);
  }

  return {
    update(nowMs: number): void {
      render(nowMs);
    },
    destroy(): void {
      for (const f of offs) f();
      range.removeEventListener('input', onInput);
      range.removeEventListener('change', onChange);
      range.removeEventListener('pointerdown', onPointerDown);
      range.removeEventListener('pointerup', onPointerUp);
      canvas.removeEventListener('click', onCanvasClick);
      firstBtn.removeEventListener('click', onFirst);
      prevBtn.removeEventListener('click', onPrev);
      nextBtn.removeEventListener('click', onNext);
      liveBtn.removeEventListener('click', onLive);
      root.replaceChildren();
      root.classList.remove('tos-panel', 'tos-timeline');
    },
  };
}
