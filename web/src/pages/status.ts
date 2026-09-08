/* Status page: docs/status.md plus a live self-check of the WebAssembly machine. */
import { Engine, STOP, STATE_NAMES, stateNameOf } from '../engine';
import type { AppContext, Page } from '../router';
import { CONTENT } from '../generated/content';

const REPO = 'https://github.com/jgoetzmann/Turing-Machine-OS';

export function renderStatusPage(mount: HTMLElement, ctx: AppContext): Page {
  const root = document.createElement('section');
  root.className = 'page page-doc page-status';
  const sha = typeof __BUILD_SHA__ === 'string' ? __BUILD_SHA__ : 'dev';
  const time = typeof __BUILD_TIME__ === 'string' ? __BUILD_TIME__ : '';

  const head = document.createElement('div');
  head.className = 'status-head';
  head.innerHTML = `
    <p class="badges">
      <a href="${REPO}/actions/workflows/ci.yml" rel="noopener" target="_blank"><img alt="CI status" src="${REPO}/actions/workflows/ci.yml/badge.svg"></a>
      <a href="${REPO}/actions/workflows/pages.yml" rel="noopener" target="_blank"><img alt="Pages deploy status" src="${REPO}/actions/workflows/pages.yml/badge.svg"></a>
    </p>
    <p class="build-info">Site built from <a rel="noopener" target="_blank" data-sha></a>${time ? ` at <time datetime="${time}">${time}</time>` : ''}.</p>`;
  const shaLink = head.querySelector<HTMLAnchorElement>('[data-sha]')!;
  shaLink.href = sha === 'dev' ? REPO : `${REPO}/commit/${sha}`;
  shaLink.textContent = sha === 'dev' ? 'a local dev build' : sha.slice(0, 12);
  root.appendChild(head);

  const article = document.createElement('article');
  article.className = 'doc';
  const entry = CONTENT.status;
  article.innerHTML = entry ? entry.html : '<h1>Status</h1><p>docs/status.md was not generated.</p>';
  root.appendChild(article);

  const live = document.createElement('section');
  live.className = 'self-check';
  live.setAttribute('aria-labelledby', 'self-check-title');
  live.innerHTML = `<h2 id="self-check-title">Live self-check</h2><p data-out="msg">Loading the WebAssembly machine…</p><div data-out="table"></div>`;
  root.appendChild(live);
  const msg = live.querySelector<HTMLElement>('[data-out="msg"]')!;
  const tableHost = live.querySelector<HTMLElement>('[data-out="table"]')!;

  let destroyed = false;
  (async () => {
    try {
      const e = await Engine.load(ctx.base);
      if (destroyed) return;
      e.create({});
      let steps = 0;
      let stop = 0;
      for (let i = 0; i < 16; i++) {
        const r = e.step(65536);
        steps += r.steps;
        stop = r.stop;
        if (r.stop !== STOP.BUDGET) break;
      }
      const out = e.conRead();
      const ok = stop === STOP.WAIT_INPUT && out.includes('A> ');
      msg.textContent = `${ok ? 'OK' : 'Unexpected'}: version ${e.version()}, ${e.tapeCount()} tape × ${e.tapeLen()} bytes, booted to ${stateNameOf(e.state())} in ${steps.toLocaleString()} steps, console: ${JSON.stringify(out.slice(-40))}`;
      const t = document.createElement('table');
      t.innerHTML = `<caption>Kernel transitions fired during boot</caption><thead><tr><th>#</th><th>From</th><th>To</th><th>Why</th><th>Fired</th></tr></thead>`;
      const tb = document.createElement('tbody');
      e.transitions().forEach((tr, i) => {
        const row = document.createElement('tr');
        for (const cell of [String(i), STATE_NAMES[tr.from] ?? tr.from, STATE_NAMES[tr.to] ?? tr.to, tr.why, String(tr.fired)]) {
          const td = document.createElement('td');
          td.textContent = String(cell);
          row.appendChild(td);
        }
        tb.appendChild(row);
      });
      t.appendChild(tb);
      const wrap = document.createElement('div');
      wrap.className = 'table-scroll';
      wrap.appendChild(t);
      tableHost.appendChild(wrap);
    } catch (err) {
      msg.textContent = `The WebAssembly machine could not be loaded: ${err instanceof Error ? err.message : String(err)}`;
    }
  })();

  mount.appendChild(root);
  return {
    destroy() {
      destroyed = true;
      root.remove();
    },
  };
}
