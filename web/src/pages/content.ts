/* Content page: renders one Markdown document from generated/content.ts. */
import type { AppContext, Page } from '../router';
import { CONTENT } from '../generated/content';

const REPO = 'https://github.com/jgoetzmann/Turing-Machine-OS';
const LANGUAGE_DOCS: { slug: string; label: string }[] = [
  { slug: 'languages', label: 'Overview' },
  { slug: 'tiny-c', label: 'Tiny-C' },
  { slug: 'asm', label: '8080 assembly' },
  { slug: 'tm', label: 'Turing-machine language' },
];

export function renderContentPage(mount: HTMLElement, _ctx: AppContext, slug: string): Page {
  const root = document.createElement('section');
  root.className = 'page page-doc';
  const entry = CONTENT[slug];
  if (!entry) {
    root.innerHTML = `<h1>Missing page</h1><p>No document named <code></code> was generated. Run <code>npm run content</code>.</p>`;
    root.querySelector('code')!.textContent = slug;
    mount.appendChild(root);
    return { destroy: () => root.remove() };
  }

  if (LANGUAGE_DOCS.some((d) => d.slug === slug)) {
    const nav = document.createElement('nav');
    nav.className = 'subnav';
    nav.setAttribute('aria-label', 'Language documentation');
    for (const d of LANGUAGE_DOCS) {
      const a = document.createElement('a');
      a.href = `#/${d.slug}`;
      a.textContent = d.label;
      if (d.slug === slug) a.setAttribute('aria-current', 'page');
      nav.appendChild(a);
    }
    root.appendChild(nav);
  }

  const article = document.createElement('article');
  article.className = 'doc';
  article.innerHTML = entry.html;
  root.appendChild(article);

  const foot = document.createElement('p');
  foot.className = 'doc-source';
  const sha = typeof __BUILD_SHA__ === 'string' ? __BUILD_SHA__ : 'main';
  foot.innerHTML = `Source: <a rel="noopener" target="_blank"></a>. The site renders the same Markdown that GitHub shows.`;
  const link = foot.querySelector('a')!;
  link.href = `${REPO}/blob/${sha === 'dev' ? 'main' : sha}/docs/${slug}.md`;
  link.textContent = `docs/${slug}.md`;
  root.appendChild(foot);

  // In-document anchors (#section) must not be mistaken for routes (#/page).
  const onClick = (e: Event) => {
    const a = (e.target as HTMLElement).closest('a');
    if (!a) return;
    const href = a.getAttribute('href') ?? '';
    if (href.startsWith('#') && !href.startsWith('#/')) {
      const target = document.getElementById(href.slice(1));
      if (target) {
        e.preventDefault();
        target.scrollIntoView({ block: 'start' });
        (target as HTMLElement).tabIndex = -1;
        (target as HTMLElement).focus({ preventScroll: true });
      }
    }
  };
  article.addEventListener('click', onClick);

  mount.appendChild(root);
  return {
    destroy() {
      article.removeEventListener('click', onClick);
      root.remove();
    },
  };
}
