/** Tiny synchronous event bus shared by the app and every panel. */
export interface Bus {
  on(event: string, fn: (payload: any) => void): () => void;
  emit(event: string, payload?: any): void;
}

/** A mounted panel: driven once per animation frame, torn down on page change. */
export interface Panel {
  update(nowMs: number): void;
  destroy(): void;
}

/** Events the app emits (panels may emit their own on top of these). */
export type BusEvent =
  | 'select-address' // { addr }
  | 'select-tape' // { tape }
  | 'select-page' // { page }
  | 'run-state' // { running }
  | 'speed' // { stepsPerSecond: number | 'max' }
  | 'tour-step' // { index }
  | 'machine-reset'; // (no payload)

export function createBus(): Bus {
  const listeners = new Map<string, Set<(payload: any) => void>>();
  return {
    on(event, fn) {
      let set = listeners.get(event);
      if (!set) {
        set = new Set();
        listeners.set(event, set);
      }
      set.add(fn);
      return () => {
        set!.delete(fn);
      };
    },
    emit(event, payload) {
      const set = listeners.get(event);
      if (!set) return;
      for (const fn of Array.from(set)) {
        try {
          fn(payload);
        } catch (e) {
          console.error(`bus: listener for '${event}' threw`, e);
        }
      }
    },
  };
}
