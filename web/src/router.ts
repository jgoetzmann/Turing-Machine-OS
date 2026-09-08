/* Hash router: #/ home, #/playground, #/architecture, #/decisions, #/demos, #/demos/<name>,
 * #/languages, #/how-it-was-built, #/status, plus any other docs slug in CONTENT. */
import type { Bus } from './bus';
import { CONTENT } from './generated/content';
import { renderHomePage } from './pages/home';
import { renderPlaygroundPage } from './pages/playground';
import { renderContentPage } from './pages/content';
import { renderDemosPage } from './pages/demos';
import { renderStatusPage } from './pages/status';

export interface AppContext {
  bus: Bus;
  /** Site base URL (Vite BASE_URL), always ends with '/'. */
  base: string;
}

export type RouteKind = 'home' | 'playground' | 'demos' | 'demo' | 'status' | 'content' | 'notfound';

export interface Route {
  kind: RouteKind;
  /** Path part without the leading '#', e.g. '/demos/pong'. */
  path: string;
  /** Content slug for 'content' routes (e.g. 'architecture'), '' otherwise. */
  slug: string;
  /** Demo name for 'demo' routes, '' otherwise. */
  name: string;
  query: Record<string, string>;
  /** The full hash as given, e.g. '#/playground?demo=pong'. */
  hash: string;
}

export interface Page {
  destroy(): void;
  /** Called instead of a re-mount when the hash changes but stays on the same kind of page. */
  onRoute?(route: Route): void;
}

export const NAV: { path: string; label: string }[] = [
  { path: '/', label: 'Home' },
  { path: '/playground', label: 'Playground' },
  { path: '/architecture', label: 'Architecture' },
  { path: '/decisions', label: 'Design decisions' },
  { path: '/demos', label: 'Demos' },
  { path: '/languages', label: 'Languages' },
  { path: '/how-it-was-built', label: 'How it was built' },
  { path: '/status', label: 'Status' },
];

/** Pure hash → Route. `known` says whether a docs slug exists. */
export function parseRoute(hash: string, known: (slug: string) => boolean): Route {
  let h = String(hash ?? '');
  if (h.startsWith('#')) h = h.slice(1);
  const q = h.indexOf('?');
  let path = q >= 0 ? h.slice(0, q) : h;
  const queryText = q >= 0 ? h.slice(q + 1) : '';
  if (!path.startsWith('/')) path = `/${path}`;
  if (path.length > 1 && path.endsWith('/')) path = path.slice(0, -1);
  const query: Record<string, string> = {};
  for (const pair of queryText.split('&')) {
    if (!pair) continue;
    const eq = pair.indexOf('=');
    const k = eq < 0 ? pair : pair.slice(0, eq);
    const v = eq < 0 ? '' : pair.slice(eq + 1);
    query[decodeSafe(k)] = decodeSafe(v);
  }
  const base = { path, slug: '', name: '', query, hash: hash.startsWith('#') ? hash : `#${hash}` };
  if (path === '/' || path === '/home') return { ...base, kind: 'home' };
  if (path === '/playground') return { ...base, kind: 'playground' };
  if (path === '/demos') return { ...base, kind: 'demos' };
  const demo = /^\/demos\/([A-Za-z0-9_-]+)$/.exec(path);
  if (demo) return { ...base, kind: 'demo', name: demo[1] };
  if (path === '/status') return { ...base, kind: 'status', slug: 'status' };
  const slug = path.slice(1);
  if (/^[A-Za-z0-9_-]+$/.test(slug) && known(slug)) return { ...base, kind: 'content', slug };
  return { ...base, kind: 'notfound' };
}

export function pageTitle(route: Route): string {
  switch (route.kind) {
    case 'home':
      return 'TuringOS';
    case 'playground':
      return 'Playground · TuringOS';
    case 'demos':
      return 'Demos · TuringOS';
    case 'demo':
      return `${CONTENT[`demos/${route.name}`]?.title ?? route.name} · TuringOS`;
    case 'status':
      return 'Status · TuringOS';
    case 'content':
      return `${CONTENT[route.slug]?.title ?? route.slug} · TuringOS`;
    default:
      return 'Not found · TuringOS';
  }
}

export interface Router {
  current(): Route;
  navigate(path: string): void;
  stop(): void;
}

export function startRouter(mount: HTMLElement, ctx: AppContext, onRoute?: (route: Route) => void): Router {
  const known = (slug: string) => Object.prototype.hasOwnProperty.call(CONTENT, slug);
  let current: Route = parseRoute(location.hash || '#/', known);
  let page: Page | null = null;

  function render(route: Route): void {
    if (page && page.onRoute && route.kind === current.kind && route.kind === 'playground') {
      current = route;
      page.onRoute(route);
      onRoute?.(route);
      return;
    }
    if (page) {
      page.destroy();
      page = null;
    }
    current = route;
    mount.innerHTML = '';
    mount.scrollTop = 0;
    window.scrollTo(0, 0);
    document.title = pageTitle(route);
    switch (route.kind) {
      case 'home':
        page = renderHomePage(mount, ctx);
        break;
      case 'playground':
        page = renderPlaygroundPage(mount, ctx, route);
        break;
      case 'demos':
      case 'demo':
        page = renderDemosPage(mount, ctx, route);
        break;
      case 'status':
        page = renderStatusPage(mount, ctx);
        break;
      case 'content':
        page = renderContentPage(mount, ctx, route.slug);
        break;
      default:
        page = renderNotFound(mount, route);
        break;
    }
    onRoute?.(route);
  }

  const onHashChange = () => render(parseRoute(location.hash || '#/', known));
  window.addEventListener('hashchange', onHashChange);
  render(current);

  return {
    current: () => current,
    navigate(path: string) {
      const target = path.startsWith('#') ? path : `#${path.startsWith('/') ? path : `/${path}`}`;
      if (location.hash === target) render(parseRoute(target, known));
      else location.hash = target;
    },
    stop() {
      window.removeEventListener('hashchange', onHashChange);
      if (page) page.destroy();
      page = null;
    },
  };
}

function renderNotFound(mount: HTMLElement, route: Route): Page {
  const el = document.createElement('section');
  el.className = 'page page-narrow';
  el.innerHTML = `
    <h1>Not found</h1>
    <p>There is no page at <code></code>.</p>
    <p><a href="#/">Back to the home page</a> · <a href="#/playground">Open the playground</a></p>`;
  el.querySelector('code')!.textContent = route.hash;
  mount.appendChild(el);
  return { destroy() {} };
}

function decodeSafe(s: string): string {
  try {
    return decodeURIComponent(s.replace(/\+/g, ' '));
  } catch {
    return s;
  }
}
