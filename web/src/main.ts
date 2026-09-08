/* Entry point: site chrome (header, nav, footer, theme toggle) and the hash router. */
import './theme.css';
import { createBus } from './bus';
import { startRouter, NAV, type Route } from './router';

const REPO = 'https://github.com/jgoetzmann/Turing-Machine-OS';
const THEME_KEY = 'tos-theme';
type Theme = 'auto' | 'light' | 'dark';

function readTheme(): Theme {
  try {
    const v = localStorage.getItem(THEME_KEY);
    return v === 'light' || v === 'dark' ? v : 'auto';
  } catch {
    return 'auto';
  }
}

function applyTheme(theme: Theme): void {
  const html = document.documentElement;
  if (theme === 'auto') delete html.dataset.theme;
  else html.dataset.theme = theme;
  try {
    if (theme === 'auto') localStorage.removeItem(THEME_KEY);
    else localStorage.setItem(THEME_KEY, theme);
  } catch {
    /* storage unavailable: theme still applies for this page */
  }
}

function themeLabel(theme: Theme): string {
  return theme === 'auto' ? 'Theme: auto' : theme === 'dark' ? 'Theme: phosphor' : 'Theme: light';
}

function boot(): void {
  const root = document.getElementById('app');
  if (!root) throw new Error('#app missing');
  const base = import.meta.env.BASE_URL;
  const sha = typeof __BUILD_SHA__ === 'string' ? __BUILD_SHA__ : 'dev';

  root.innerHTML = `
    <header class="site-header">
      <a class="brand" href="#/" aria-label="TuringOS home">
        <span class="brand-mark" aria-hidden="true">▮▯▮</span><span>TuringOS</span>
      </a>
      <button type="button" class="nav-toggle btn" aria-expanded="false" aria-controls="site-nav" aria-label="Menu">☰</button>
      <nav id="site-nav" class="site-nav" aria-label="Site"></nav>
      <button type="button" class="theme-toggle btn" data-theme-toggle aria-live="polite"></button>
    </header>
    <main id="main" class="site-main" tabindex="-1"></main>
    <footer class="site-footer">
      <span>TuringOS 2.0.0 · built from <a rel="noopener" target="_blank" data-sha></a></span>
      <span><a href="${REPO}" rel="noopener" target="_blank">GitHub</a> · <a href="#/status">Status</a> · <a href="#/how-it-was-built">How it was built</a></span>
    </footer>`;

  const nav = root.querySelector<HTMLElement>('#site-nav')!;
  for (const item of NAV) {
    const a = document.createElement('a');
    a.href = `#${item.path}`;
    a.textContent = item.label;
    a.dataset.path = item.path;
    nav.appendChild(a);
  }
  const navToggle = root.querySelector<HTMLButtonElement>('.nav-toggle')!;
  navToggle.addEventListener('click', () => {
    const open = nav.classList.toggle('open');
    navToggle.setAttribute('aria-expanded', open ? 'true' : 'false');
  });

  const shaLink = root.querySelector<HTMLAnchorElement>('[data-sha]')!;
  shaLink.href = sha === 'dev' ? REPO : `${REPO}/commit/${sha}`;
  shaLink.textContent = sha === 'dev' ? 'dev' : sha.slice(0, 7);

  let theme = readTheme();
  applyTheme(theme);
  const themeBtn = root.querySelector<HTMLButtonElement>('[data-theme-toggle]')!;
  themeBtn.textContent = themeLabel(theme);
  themeBtn.addEventListener('click', () => {
    theme = theme === 'auto' ? 'light' : theme === 'light' ? 'dark' : 'auto';
    applyTheme(theme);
    themeBtn.textContent = themeLabel(theme);
  });

  const main = root.querySelector<HTMLElement>('#main')!;
  const bus = createBus();
  startRouter(main, { bus, base }, (route: Route) => {
    for (const a of nav.querySelectorAll<HTMLAnchorElement>('a')) {
      const p = a.dataset.path ?? '';
      const active =
        p === route.path ||
        (p === '/demos' && route.kind === 'demo') ||
        (p === '/languages' && ['tiny-c', 'asm', 'tm'].includes(route.slug));
      if (active) a.setAttribute('aria-current', 'page');
      else a.removeAttribute('aria-current');
    }
    nav.classList.remove('open');
    navToggle.setAttribute('aria-expanded', 'false');
    document.body.dataset.route = route.kind;
  });
}

boot();
