/** Panel contract shared by every module in this directory (SPEC §S9). */
export interface Panel {
  /** Called once per animation frame with the rAF timestamp. */
  update(nowMs: number): void;
  /** Unsubscribe from the bus, drop DOM listeners, empty the root. */
  destroy(): void;
}

/** Factory signature every `create<Name>Panel` matches. */
export type PanelFactory<E = unknown, B = unknown> = (root: HTMLElement, engine: E, bus: B) => Panel;
