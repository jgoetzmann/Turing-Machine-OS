/* Speed setting helpers. Dependency-free so Node can import this file directly. */

/** Steps per second, or 'max' = as fast as the host allows (time-budgeted per frame). */
export type Speed = number | 'max';

/** Slowest selectable speed (1 step per second). */
export const SPEED_MIN = 1;
/** Fastest numeric speed on the slider; one notch above it is 'max'. */
export const SPEED_TOP = 10_000_000;
/** Slider positions run 0..SLIDER_STEPS; SLIDER_STEPS itself means 'max'. */
export const SLIDER_STEPS = 1000;
/** At 'max' the run loop executes for at most this many milliseconds per animation frame. */
export const MAX_FRAME_MS = 8;

/** Clamp a numeric speed into [SPEED_MIN, SPEED_TOP]; NaN/negative become SPEED_MIN. */
export function clampSpeed(x: number): number {
  if (!Number.isFinite(x) || x < SPEED_MIN) return SPEED_MIN;
  if (x > SPEED_TOP) return SPEED_TOP;
  return x;
}

/**
 * Step budget for one frame of `dtMs` milliseconds at `speed`.
 * Numeric speeds return `speed * dtMs / 1000` (fractional; the caller accumulates the remainder
 * so that 1 step/s really is one step per second). 'max' returns Number.MAX_SAFE_INTEGER — the
 * caller must bound it by wall-clock time (MAX_FRAME_MS) instead.
 */
export function stepsForFrame(speed: Speed, dtMs: number): number {
  if (speed === 'max') return Number.MAX_SAFE_INTEGER;
  if (!Number.isFinite(dtMs) || dtMs <= 0) return 0;
  const s = Number(speed);
  if (!Number.isFinite(s) || s <= 0) return 0;
  const raw = (s * dtMs) / 1000;
  return Math.round(raw * 1e6) / 1e6;
}

/** Log-scale slider (0..SLIDER_STEPS) → Speed. */
export function sliderToSpeed(pos: number): Speed {
  const p = Math.round(Number.isFinite(pos) ? pos : 0);
  if (p >= SLIDER_STEPS) return 'max';
  if (p <= 0) return SPEED_MIN;
  const t = p / SLIDER_STEPS;
  const raw = Math.exp(Math.log(SPEED_MIN) + t * (Math.log(SPEED_TOP) - Math.log(SPEED_MIN)));
  return snapSpeed(raw);
}

/** Speed → slider position (0..SLIDER_STEPS). */
export function speedToSlider(speed: Speed): number {
  if (speed === 'max') return SLIDER_STEPS;
  const s = clampSpeed(speed);
  const t = (Math.log(s) - Math.log(SPEED_MIN)) / (Math.log(SPEED_TOP) - Math.log(SPEED_MIN));
  return Math.min(SLIDER_STEPS - 1, Math.max(0, Math.round(t * SLIDER_STEPS)));
}

/** Round to two significant digits so slider values read as 1, 2, 5, 10, 120, 1500, ... */
export function snapSpeed(x: number): number {
  const s = clampSpeed(x);
  if (s < 10) return Math.round(s);
  const mag = Math.pow(10, Math.floor(Math.log10(s)) - 1);
  return clampSpeed(Math.round(s / mag) * mag);
}

/** Human label: '1 step/s', '250 steps/s', '1.2k steps/s', '3.5M steps/s', 'max'. */
export function formatSpeed(speed: Speed): string {
  if (speed === 'max') return 'max';
  const s = clampSpeed(speed);
  if (s === 1) return '1 step/s';
  if (s < 1000) return `${Math.round(s)} steps/s`;
  if (s < 1_000_000) return `${trimNumber(s / 1000)}k steps/s`;
  return `${trimNumber(s / 1_000_000)}M steps/s`;
}

/** Parse 'max' | '120' | '1.5k' | '2M' into a Speed; null if unparseable. */
export function parseSpeed(text: string): Speed | null {
  const t = String(text).trim().toLowerCase();
  if (t === 'max' || t === 'unthrottled' || t === 'inf') return 'max';
  const m = /^(\d+(?:\.\d+)?)\s*([km])?$/.exec(t);
  if (!m) return null;
  let v = parseFloat(m[1]);
  if (m[2] === 'k') v *= 1000;
  if (m[2] === 'm') v *= 1_000_000;
  return clampSpeed(v);
}

function trimNumber(x: number): string {
  const s = x.toFixed(1);
  return s.endsWith('.0') ? s.slice(0, -2) : s;
}
