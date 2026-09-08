/* Demos gallery (#/demos) and demo detail (#/demos/<name>). */
import type { AppContext, Page, Route } from '../router';
import { CONTENT } from '../generated/content';
import { DEMO_FILES } from '../generated/demos';
import { TOURS } from '../generated/tours';
import { primaryDemoFile } from '../app';

/** Extra playground query per demo (levers the demo needs or that make it worth watching). */
export const DEMO_PRESETS: Record<string, string> = {
  pong: 'input=keys',
  life: '',
  tm: 'tapes=2&speed=2000',
  bf: 'tapes=2',
  fault: 'len=32768',
  hello: '',
  asm: '',
  forth: '',
  shell: '',
};

const ORDER = ['hello', 'pong', 'life', 'tm', 'bf', 'asm', 'forth', 'fault', 'shell'];

export function playgroundLink(name: string, file?: string): string {
  const parts = [`demo=${encodeURIComponent(name)}`];
  const preset = DEMO_PRESETS[name];
  if (preset) parts.push(preset);
  if (file) parts.push(`file=${encodeURIComponent(file)}`);
  return `#/playground?${parts.join('&')}`;
}

export function demoNames(): string[] {
  const names = new Set<string>(Object.keys(DEMO_FILES));
  for (const slug of Object.keys(CONTENT)) if (slug.startsWith('demos/')) names.add(slug.slice(6));
  return Array.from(names).sort((a, b) => {
    const ia = ORDER.indexOf(a);
    const ib = ORDER.indexOf(b);
    if (ia < 0 && ib < 0) return a.localeCompare(b);
    if (ia < 0) return 1;
    if (ib < 0) return -1;
    return ia - ib;
  });
}

export function renderDemosPage(mount: HTMLElement, _ctx: AppContext, route: Route): Page {
  const root = document.createElement('section');
  root.className = 'page page-demos';
  if (route.kind === 'demo') renderDetail(root, route.name);
  else renderGallery(root);
  mount.appendChild(root);
  return { destroy: () => root.remove() };
}

function renderGallery(root: HTMLElement): void {
  const h = document.createElement('h1');
  h.textContent = 'Demos';
  const intro = document.createElement('p');
  intro.className = 'lead';
  intro.textContent =
    'Each demo is a program on the demo disk. Open it in the playground to run it on the live machine, or read what to watch for.';
  root.append(h, intro);
  const grid = document.createElement('div');
  grid.className = 'demo-grid';
  for (const name of demoNames()) {
    const entry = CONTENT[`demos/${name}`];
    const files = DEMO_FILES[name] ?? [];
    const card = document.createElement('article');
    card.className = 'card demo-card';
    const title = document.createElement('h2');
    const link = document.createElement('a');
    link.href = `#/demos/${name}`;
    link.textContent = entry?.title ?? name;
    title.appendChild(link);
    const p = document.createElement('p');
    p.textContent = excerpt(entry?.html ?? '', 180) || `${files.length} file${files.length === 1 ? '' : 's'}`;
    const chips = document.createElement('p');
    chips.className = 'chips';
    for (const f of files) {
      const chip = document.createElement('code');
      chip.textContent = f.name;
      chips.appendChild(chip);
    }
    const actions = document.createElement('p');
    actions.className = 'card-actions';
    const open = document.createElement('a');
    open.className = 'btn btn-primary';
    open.href = playgroundLink(name);
    open.textContent = 'Open in playground';
    const more = document.createElement('a');
    more.className = 'btn';
    more.href = `#/demos/${name}`;
    more.textContent = 'What to watch';
    actions.append(open, more);
    card.append(title, p, chips, actions);
    grid.appendChild(card);
  }
  root.appendChild(grid);
}

function renderDetail(root: HTMLElement, name: string): void {
  const entry = CONTENT[`demos/${name}`];
  const files = DEMO_FILES[name] ?? [];
  const crumbs = document.createElement('p');
  crumbs.className = 'crumbs';
  crumbs.innerHTML = `<a href="#/demos">Demos</a> / <span></span>`;
  crumbs.querySelector('span')!.textContent = entry?.title ?? name;
  root.appendChild(crumbs);

  if (!entry && files.length === 0) {
    const h = document.createElement('h1');
    h.textContent = 'Unknown demo';
    const p = document.createElement('p');
    p.textContent = `There is no demo named "${name}".`;
    root.append(h, p);
    return;
  }

  const actions = document.createElement('p');
  actions.className = 'card-actions';
  const open = document.createElement('a');
  open.className = 'btn btn-primary';
  open.href = playgroundLink(name);
  open.textContent = 'Open in playground';
  actions.appendChild(open);
  root.appendChild(actions);

  const article = document.createElement('article');
  article.className = 'doc';
  if (entry) article.innerHTML = entry.html;
  else article.innerHTML = `<h1>${escapeHtml(name)}</h1>`;
  root.appendChild(article);

  const tour = TOURS[name];
  if (tour && tour.length) {
    const h2 = document.createElement('h2');
    h2.textContent = 'Tour';
    const ol = document.createElement('ol');
    ol.className = 'tour-list';
    for (const step of tour) {
      const li = document.createElement('li');
      const b = document.createElement('strong');
      b.textContent = `${step.panel}: `;
      li.append(b, document.createTextNode(step.text));
      ol.appendChild(li);
    }
    root.append(h2, ol);
  }

  if (files.length) {
    const h2 = document.createElement('h2');
    h2.textContent = 'Source';
    root.appendChild(h2);
    const primary = primaryDemoFile(name, files);
    for (const f of files) {
      const details = document.createElement('details');
      details.open = f === primary;
      const summary = document.createElement('summary');
      summary.textContent = `${f.name} (${f.text.length.toLocaleString()} bytes)`;
      const bar = document.createElement('p');
      bar.className = 'card-actions';
      if (/\.(c|asm|tm|bf)$/i.test(f.name)) {
        const run = document.createElement('a');
        run.className = 'btn btn-small';
        run.href = playgroundLink(name, f.name);
        run.textContent = `Run ${f.name}`;
        bar.appendChild(run);
      }
      const pre = document.createElement('pre');
      pre.className = 'source';
      const code = document.createElement('code');
      code.textContent = f.text;
      pre.appendChild(code);
      details.append(summary, bar, pre);
      root.appendChild(details);
    }
  }
}

function excerpt(html: string, max: number): string {
  const text = html
    .replace(/<h1[^>]*>[\s\S]*?<\/h1>/, '')
    .replace(/<[^>]+>/g, ' ')
    .replace(/&amp;/g, '&')
    .replace(/&lt;/g, '<')
    .replace(/&gt;/g, '>')
    .replace(/&quot;/g, '"')
    .replace(/&#39;/g, "'")
    .replace(/\s+/g, ' ')
    .trim();
  if (text.length <= max) return text;
  return `${text.slice(0, max).replace(/\s+\S*$/, '')}…`;
}

const HTML_ESCAPES: Record<string, string> = { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' };

function escapeHtml(s: string): string {
  return s.replace(/[&<>"]/g, (c) => HTML_ESCAPES[c] ?? c);
}
