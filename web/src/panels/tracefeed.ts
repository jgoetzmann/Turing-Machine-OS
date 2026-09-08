/**
 * Shared trace tap. `Engine.drainTrace()` hands out each event exactly once, so if two
 * panels drained independently they would each see half the stream. Every panel that
 * wants trace events subscribes here instead; whichever panel pumps first in a frame
 * drains the engine and fans the events out to every subscriber.
 */
import type { Engine, TraceEvent } from '../engine';

export type TraceSubscriber = (events: TraceEvent[]) => void;

export class TraceTap {
  private subs = new Set<TraceSubscriber>();
  private lastPump = -1;

  constructor(private engine: Engine) {}

  subscribe(fn: TraceSubscriber): () => void {
    this.subs.add(fn);
    return () => {
      this.subs.delete(fn);
    };
  }

  /** Drain the engine once per `nowMs` and deliver to every subscriber. */
  pump(nowMs: number): void {
    if (nowMs === this.lastPump) return;
    this.lastPump = nowMs;
    let events: TraceEvent[];
    try {
      events = this.engine.drainTrace();
    } catch {
      return;
    }
    if (!events || events.length === 0) return;
    for (const s of this.subs) {
      try {
        s(events);
      } catch {
        /* one bad subscriber must not starve the others */
      }
    }
  }
}

const taps = new WeakMap<Engine, TraceTap>();

export function traceTap(engine: Engine): TraceTap {
  let t = taps.get(engine);
  if (!t) {
    t = new TraceTap(engine);
    taps.set(engine, t);
  }
  return t;
}
