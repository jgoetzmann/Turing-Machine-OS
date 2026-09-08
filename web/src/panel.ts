import type { Bus, Panel } from './bus';
import type { Engine } from './engine';

export type { Panel } from './bus';

/** Every `web/src/panels/<name>.ts` exports a factory of this shape as `create<Name>Panel`. */
export type PanelFactory = (root: HTMLElement, engine: Engine, bus: Bus) => Panel;
