/* Playground page: toolbar, breakpoints UI, time-travel scrubber, tour bar, panel grid, help overlay. */
import { Engine, stateNameOf, haltNameOf } from '../engine';
import { createApp, PANEL_NAMES, PANEL_TITLES, primaryDemoFile, type App, type PanelName } from '../app';
import type { AppContext, Page, Route } from '../router';
import { parseHash, BP_KIND_NAMES, type BpKind, type Breakpoint } from '../urlstate';
import { sliderToSpeed, speedToSlider, formatSpeed, SLIDER_STEPS, type Speed } from '../speed';
import { hex16, parseAddress } from '../disasmview';
import { DEMO_FILES } from '../generated/demos';
import { TOURS } from '../generated/tours';

const TABBED_QUERY = '(max-width: 899px)';

export function renderPlaygroundPage(mount: HTMLElement, ctx: AppContext, route: Route): Page {
  const root = document.createElement('section');
  root.className = 'playground';
  root.setAttribute('aria-labelledby', 'pg-title');
  root.innerHTML = `
    <h1 id="pg-title" class="visually-hidden">Playground</h1>
    <div class="pg-toolbar" role="toolbar" aria-label="Execution controls">
      <button type="button" class="btn btn-primary" data-act="toggle" aria-keyshortcuts="Space" aria-pressed="false">Run</button>
      <span class="btn-group" role="group" aria-label="Step">
        <button type="button" class="btn" data-step="1" aria-keyshortcuts="." title="Step one instruction (.)">Step</button>
        <button type="button" class="btn" data-step="10" aria-label="Step 10 instructions">×10</button>
        <button type="button" class="btn" data-step="100" aria-label="Step 100 instructions">×100</button>
        <button type="button" class="btn" data-step="1000" aria-label="Step 1000 instructions">×1000</button>
      </span>
      <button type="button" class="btn" data-act="over">Step over syscall</button>
      <button type="button" class="btn" data-act="to-halt">Run to HALT</button>
      <button type="button" class="btn" data-act="to-state">Run to state change</button>
      <button type="button" class="btn" data-act="reset" aria-keyshortcuts="r">Reset</button>
      <label class="pg-speed">
        <span>Speed</span>
        <input type="range" min="0" max="${SLIDER_STEPS}" step="1" value="${SLIDER_STEPS}" data-ctl="speed" aria-valuetext="max" aria-label="Speed, logarithmic from 1 step per second to unthrottled">
        <output data-out="speed" aria-live="off">max</output>
      </label>
      <button type="button" class="btn" data-act="help" aria-keyshortcuts="?" aria-haspopup="dialog">Help</button>
    </div>
    <div class="pg-toolbar pg-toolbar-secondary">
      <label>Demo
        <select data-ctl="demo" aria-label="Demo"><option value="">(shell only)</option></select>
      </label>
      <label>File
        <select data-ctl="file" aria-label="Demo file"></select>
      </label>
      <button type="button" class="btn" data-act="load">Load &amp; run</button>
      <form class="pg-bp" data-form="bp" aria-label="Add breakpoint">
        <label>Break on
          <select data-ctl="bp-kind" aria-label="Breakpoint kind">
            <option value="0">PC</option><option value="1">read</option><option value="2">write</option>
            <option value="3">syscall</option><option value="4">state</option>
          </select>
        </label>
        <label>from <input data-ctl="bp-lo" size="6" value="0100" aria-label="Range start (hex)" autocomplete="off"></label>
        <label>to <input data-ctl="bp-hi" size="6" value="" placeholder="same" aria-label="Range end (hex)" autocomplete="off"></label>
        <button type="submit" class="btn">Add</button>
        <button type="button" class="btn" data-act="bp-clear">Clear all</button>
      </form>
      <ul class="pg-bp-list" data-out="bp-list" aria-label="Breakpoints"></ul>
      <label class="pg-scrub">
        <span>Time travel</span>
        <input type="range" min="0" max="0" value="0" data-ctl="scrub" aria-label="Seek to step" disabled>
        <output data-out="scrub">step 0</output>
      </label>
      <span class="btn-group" role="group" aria-label="Export">
        <button type="button" class="btn" data-act="export-png">Tape map PNG</button>
        <button type="button" class="btn" data-act="export-trace">Trace JSON</button>
        <button type="button" class="btn" data-act="export-disk">Disk image</button>
        <button type="button" class="btn" data-act="share">Copy link</button>
      </span>
    </div>
    <p class="pg-status" role="status" aria-live="polite" data-out="status">Loading the machine…</p>
    <div class="pg-tour" data-tour hidden>
      <strong>Tour</strong>
      <span data-out="tour-text"></span>
      <span class="pg-tour-nav">
        <button type="button" class="btn" data-act="tour-prev" aria-label="Previous tour step">‹</button>
        <output data-out="tour-pos"></output>
        <button type="button" class="btn" data-act="tour-next" aria-label="Next tour step">›</button>
        <button type="button" class="btn" data-act="tour-close" aria-label="Close tour">×</button>
      </span>
    </div>
    <div class="pg-tabs" role="tablist" aria-label="Panels" data-tabs hidden></div>
    <div class="pg-grid" data-grid></div>
    <div class="pg-help" role="dialog" aria-modal="true" aria-labelledby="pg-help-title" data-help hidden>
      <div class="pg-help-card">
        <h2 id="pg-help-title">Keyboard shortcuts</h2>
        <dl>
          <dt><kbd>Space</kbd></dt><dd>Run / pause</dd>
          <dt><kbd>.</kbd></dt><dd>Step one instruction</dd>
          <dt><kbd>r</kbd></dt><dd>Reset (reloads the current demo)</dd>
          <dt><kbd>?</kbd></dt><dd>Toggle this help</dd>
          <dt><kbd>W</kbd> <kbd>S</kbd> <kbd>↑</kbd> <kbd>↓</kbd> <kbd>Space</kbd> <kbd>Esc</kbd> <kbd>Enter</kbd></dt>
          <dd>Machine keys (IN 0x03) while the display has focus or the input lever is <em>keys</em></dd>
        </dl>
        <p>Typing in the console panel sends bytes to the machine's console input. Levers marked <em>machine</em> reset the machine; <em>view</em> levers apply live. The URL hash always reflects the current levers, demo, speed and breakpoints — copy it to share.</p>
        <button type="button" class="btn btn-primary" data-act="help-close">Close</button>
      </div>
    </div>`;
  mount.appendChild(root);

  const $ = <T extends Element>(sel: string): T => {
    const el = root.querySelector<T>(sel);
    if (!el) throw new Error(`playground: missing ${sel}`);
    return el;
  };
  const grid = $<HTMLElement>('[data-grid]');
  const tabs = $<HTMLElement>('[data-tabs]');
  const status = $<HTMLElement>('[data-out="status"]');
  const runBtn = $<HTMLButtonElement>('[data-act="toggle"]');
  const speedInput = $<HTMLInputElement>('[data-ctl="speed"]');
  const speedOut = $<HTMLOutputElement>('[data-out="speed"]');
  const demoSel = $<HTMLSelectElement>('[data-ctl="demo"]');
  const fileSel = $<HTMLSelectElement>('[data-ctl="file"]');
  const bpForm = $<HTMLFormElement>('[data-form="bp"]');
  const bpKind = $<HTMLSelectElement>('[data-ctl="bp-kind"]');
  const bpLo = $<HTMLInputElement>('[data-ctl="bp-lo"]');
  const bpHi = $<HTMLInputElement>('[data-ctl="bp-hi"]');
  const bpList = $<HTMLElement>('[data-out="bp-list"]');
  const scrub = $<HTMLInputElement>('[data-ctl="scrub"]');
  const scrubOut = $<HTMLOutputElement>('[data-out="scrub"]');
  const tour = $<HTMLElement>('[data-tour]');
  const tourText = $<HTMLElement>('[data-out="tour-text"]');
  const tourPos = $<HTMLOutputElement>('[data-out="tour-pos"]');
  const help = $<HTMLElement>('[data-help]');

  // ---- panel slots -----------------------------------------------------------
  const slots: Partial<Record<PanelName, HTMLElement>> = {};
  for (const name of PANEL_NAMES) {
    const slot = document.createElement('section');
    slot.className = 'slot';
    slot.dataset.panel = name;
    slot.id = `panel-${name}`;
    slot.setAttribute('aria-labelledby', `panel-${name}-title`);
    slot.tabIndex = -1;
    const h = document.createElement('h2');
    h.className = 'slot-title';
    h.id = `panel-${name}-title`;
    h.textContent = PANEL_TITLES[name];
    const body = document.createElement('div');
    body.className = 'slot-body';
    if (name === 'display') {
      body.tabIndex = 0;
      body.setAttribute('aria-label', 'Display; focus to send W/S/arrow/space keys to the machine');
    }
    slot.append(h, body);
    grid.appendChild(slot);
    slots[name] = body;

    const tab = document.createElement('button');
    tab.type = 'button';
    tab.className = 'pg-tab';
    tab.setAttribute('role', 'tab');
    tab.dataset.tab = name;
    tab.textContent = PANEL_TITLES[name];
    tab.addEventListener('click', () => selectTab(name));
    tabs.appendChild(tab);
  }

  let activeTab: PanelName = 'tapemap';
  const media = window.matchMedia(TABBED_QUERY);
  function applyTabbed(): void {
    const tabbed = media.matches;
    tabs.hidden = !tabbed;
    grid.classList.toggle('tabbed', tabbed);
    for (const slot of grid.querySelectorAll<HTMLElement>('.slot')) {
      slot.classList.toggle('active', !tabbed || slot.dataset.panel === activeTab);
    }
    for (const tab of tabs.querySelectorAll<HTMLElement>('.pg-tab')) {
      const on = tab.dataset.tab === activeTab;
      tab.setAttribute('aria-selected', on ? 'true' : 'false');
      tab.classList.toggle('active', on);
    }
  }
  function selectTab(name: PanelName): void {
    activeTab = name;
    applyTabbed();
  }
  media.addEventListener('change', applyTabbed);
  applyTabbed();

  // ---- demo pickers ----------------------------------------------------------
  const demoNames = Object.keys(DEMO_FILES).sort(demoOrder);
  for (const name of demoNames) {
    const opt = document.createElement('option');
    opt.value = name;
    opt.textContent = name;
    demoSel.appendChild(opt);
  }
  function fillFiles(name: string, selected?: string | null): void {
    fileSel.innerHTML = '';
    const files = (DEMO_FILES[name] ?? []).filter((f) => /\.(c|asm|tm|bf)$/i.test(f.name));
    const primary = primaryDemoFile(name, files);
    for (const f of files) {
      const opt = document.createElement('option');
      opt.value = f.name;
      opt.textContent = f.name;
      opt.selected = selected ? f.name === selected : f === primary;
      fileSel.appendChild(opt);
    }
    fileSel.disabled = files.length === 0;
  }
  demoSel.addEventListener('change', () => fillFiles(demoSel.value));
  fillFiles('');

  // ---- help overlay ----------------------------------------------------------
  let lastFocus: HTMLElement | null = null;
  function toggleHelp(force?: boolean): void {
    const open = force ?? help.hidden;
    if (open) {
      lastFocus = document.activeElement as HTMLElement | null;
      help.hidden = false;
      $<HTMLButtonElement>('[data-act="help-close"]').focus();
    } else {
      help.hidden = true;
      lastFocus?.focus();
    }
  }
  help.addEventListener('keydown', (e) => {
    if (e.key === 'Escape') {
      e.stopPropagation();
      toggleHelp(false);
    }
  });
  help.addEventListener('click', (e) => {
    if (e.target === help) toggleHelp(false);
  });

  // ---- tour ------------------------------------------------------------------
  let tourSteps: { panel: string; text: string }[] = [];
  let tourIndex = 0;
  function showTour(name: string | null): void {
    tourSteps = name ? TOURS[name] ?? [] : [];
    tourIndex = 0;
    tour.hidden = tourSteps.length === 0;
    renderTour();
  }
  function renderTour(): void {
    for (const slot of grid.querySelectorAll<HTMLElement>('.slot')) slot.classList.remove('tour-target');
    if (tourSteps.length === 0) return;
    const step = tourSteps[tourIndex];
    tourText.textContent = step.text;
    tourPos.value = `${tourIndex + 1} / ${tourSteps.length}`;
    const target = grid.querySelector<HTMLElement>(`.slot[data-panel="${cssEscape(step.panel)}"]`);
    if (target) {
      target.classList.add('tour-target');
      if (media.matches && isPanelName(step.panel)) selectTab(step.panel);
    }
    ctx.bus.emit('tour-step', { index: tourIndex, panel: step.panel, text: step.text });
  }

  // ---- engine + app ----------------------------------------------------------
  let app: App | null = null;
  let destroyed = false;
  // The hash may change while the wasm is still loading; the newest route wins once the app is up.
  let latestRoute: Route = route;
  const unsub: (() => void)[] = [];
  let lastStatusAt = 0;
  let lastScrubAt = 0;

  function setStatus(text: string, flash = false): void {
    status.textContent = text;
    status.classList.toggle('flash', flash);
  }

  function refreshRunButton(): void {
    if (!app) return;
    const running = app.running;
    runBtn.textContent = running ? 'Pause' : 'Run';
    runBtn.setAttribute('aria-pressed', running ? 'true' : 'false');
  }

  function refreshSpeed(): void {
    if (!app) return;
    if (document.activeElement !== speedInput) speedInput.value = String(speedToSlider(app.speed));
    speedOut.value = formatSpeed(app.speed);
    speedInput.setAttribute('aria-valuetext', formatSpeed(app.speed));
  }

  function refreshBreakpoints(list: readonly Breakpoint[]): void {
    bpList.innerHTML = '';
    list.forEach((b, i) => {
      const li = document.createElement('li');
      const label = document.createElement('span');
      label.textContent = `#${i} ${BP_KIND_NAMES[b.kind]} ${b.kind === 0 || b.kind === 1 || b.kind === 2 ? `${hex16(b.lo)}..${hex16(b.hi)}` : `${b.lo}..${b.hi}`}`;
      const rm = document.createElement('button');
      rm.type = 'button';
      rm.className = 'btn btn-small';
      rm.textContent = '×';
      rm.setAttribute('aria-label', `Remove breakpoint ${i}`);
      rm.addEventListener('click', () => app?.removeBreakpoint(i));
      li.append(label, rm);
      bpList.appendChild(li);
    });
  }

  function refreshScrub(force: boolean): void {
    if (!app) return;
    const now = performance.now();
    if (!force && now - lastScrubAt < 250) return;
    lastScrubAt = now;
    const steps = app.engine.steps();
    const snaps = app.engine.snapshotCount();
    scrub.max = String(steps);
    if (document.activeElement !== scrub) scrub.value = String(steps);
    scrub.disabled = snaps === 0;
    scrubOut.value = `step ${steps.toLocaleString()} · ${snaps} snapshot${snaps === 1 ? '' : 's'}`;
  }

  function refreshStatus(now: number): void {
    if (!app) return;
    if (now - lastStatusAt < 100) return;
    lastStatusAt = now;
    const e = app.engine;
    const cpu = e.cpu();
    const parts = [
      app.running ? `Running (${formatSpeed(app.speed)})` : 'Paused',
      stateNameOf(e.state()),
      `step ${e.steps().toLocaleString()}`,
      `PC ${hex16(cpu.pc)}`,
      `frame ${e.frame()}`,
    ];
    const halt = e.haltReason();
    if (halt !== 0) parts.push(`halt ${haltNameOf(halt)}`);
    const recent = app.lastEvent && now - app.lastEventAt < 6000;
    if (recent) parts.push(app.lastEvent);
    setStatus(parts.join(' · '), !!recent && now - app.lastEventAt < 1500);
    refreshRunButton();
  }

  function wire(a: App): void {
    const bus = ctx.bus;
    root.addEventListener('click', (e) => {
      const btn = (e.target as HTMLElement).closest<HTMLElement>('[data-act],[data-step]');
      if (!btn || !root.contains(btn)) return;
      const stepN = btn.dataset.step;
      if (stepN) {
        a.step(parseInt(stepN, 10));
        refreshScrub(true);
        return;
      }
      switch (btn.dataset.act) {
        case 'toggle':
          a.toggle();
          break;
        case 'over':
          a.stepOverSyscall();
          break;
        case 'to-halt':
          a.runToHalt();
          break;
        case 'to-state':
          a.runToStateChange();
          break;
        case 'reset':
          a.reset();
          refreshScrub(true);
          break;
        case 'help':
          toggleHelp(true);
          break;
        case 'help-close':
          toggleHelp(false);
          break;
        case 'load': {
          const name = demoSel.value;
          if (!name) {
            a.clearDemo();
            a.reset();
            showTour(null);
            break;
          }
          const r = a.loadDemo(name, fileSel.value || undefined);
          if (!r.ok) a.note(`Load failed: ${r.error ?? 'unknown error'}`);
          break;
        }
        case 'bp-clear':
          a.clearBreakpoints();
          break;
        case 'export-png':
          exportTapemapPng(slots.tapemap ?? null, a);
          break;
        case 'export-trace':
          downloadBlob('trace.json', new Blob([JSON.stringify(a.engine.traceTail(4096))], { type: 'application/json' }));
          a.note('Exported the last 4096 trace events');
          break;
        case 'export-disk':
          downloadBlob('disk.img', new Blob([a.engine.diskPtr(0).slice()], { type: 'application/octet-stream' }));
          a.note('Exported disk A');
          break;
        case 'share':
          copyText(a.shareUrl()).then(
            () => a.note('Link copied to the clipboard'),
            () => a.note(`Share this link: ${a.shareUrl()}`),
          );
          break;
        case 'tour-prev':
          if (tourSteps.length) {
            tourIndex = (tourIndex + tourSteps.length - 1) % tourSteps.length;
            renderTour();
          }
          break;
        case 'tour-next':
          if (tourSteps.length) {
            tourIndex = (tourIndex + 1) % tourSteps.length;
            renderTour();
          }
          break;
        case 'tour-close':
          showTour(null);
          break;
        default:
          break;
      }
      refreshRunButton();
    });

    speedInput.addEventListener('input', () => {
      const s: Speed = sliderToSpeed(parseInt(speedInput.value, 10));
      a.setSpeed(s);
      speedOut.value = formatSpeed(s);
      speedInput.setAttribute('aria-valuetext', formatSpeed(s));
    });

    bpForm.addEventListener('submit', (e) => {
      e.preventDefault();
      const kind = parseInt(bpKind.value, 10) as BpKind;
      const lo = parseAddress(bpLo.value);
      const hi = bpHi.value.trim() === '' ? lo : parseAddress(bpHi.value);
      if (lo === null || hi === null) {
        a.note('Breakpoint: enter hex — an address like 0100, a syscall id like 17, or a state 0-5');
        bpLo.focus();
        return;
      }
      a.addBreakpoint(kind, lo, hi);
    });

    scrub.addEventListener('input', () => {
      a.seek(parseInt(scrub.value, 10));
      refreshScrub(true);
    });

    unsub.push(
      bus.on('run-state', () => refreshRunButton()),
      bus.on('speed', () => refreshSpeed()),
      bus.on('breakpoints', (p: { list: Breakpoint[] }) => refreshBreakpoints(p.list)),
      bus.on('breakpoint', () => {
        root.classList.add('bp-flash');
        setTimeout(() => root.classList.remove('bp-flash'), 600);
        refreshScrub(true);
      }),
      bus.on('demo-loaded', (p: { name: string; file: string }) => {
        demoSel.value = p.name;
        fillFiles(p.name, p.file);
        showTour(p.name);
      }),
      bus.on('machine-reset', () => refreshScrub(true)),
      bus.on('seek', () => refreshScrub(true)),
      a.onFrame((now) => {
        refreshStatus(now);
        refreshScrub(false);
      }),
    );
  }

  (async () => {
    try {
      const engine = await Engine.load(ctx.base);
      if (destroyed) return;
      const a = createApp(engine, ctx.bus);
      app = a;
      setStatus('Loading demo disk…');
      await a.fetchDemoDisk(ctx.base);
      if (destroyed) {
        a.destroy();
        return;
      }
      a.mountPanels(slots);
      wire(a);
      applyRoute(latestRoute);
      a.attachKeyboard(document, { onHelp: () => toggleHelp() });
      a.start();
      refreshSpeed();
      refreshBreakpoints(a.breakpoints);
      refreshRunButton();
      refreshScrub(true);
    } catch (e) {
      const msg = e instanceof Error ? e.message : String(e);
      setStatus(`Could not load the machine: ${msg}. Build it with \`make wasm\` and reload.`);
      grid.innerHTML = `<p class="pg-error">The WebAssembly build (<code>turingos.js</code> / <code>turingos.wasm</code>) is missing. Run <code>make web</code> to produce it.</p>`;
    }
  })();

  function applyRoute(r: Route): void {
    if (!app) return;
    const s = parseHash(r.hash);
    app.applyState(s);
    const file = r.query.file;
    if (s.demo && file && file !== app.demoFile) {
      const res = app.loadDemo(s.demo, file);
      if (!res.ok) app.note(res.error ?? 'demo file failed to load');
    }
    demoSel.value = s.demo ?? '';
    fillFiles(s.demo ?? '', app.demoFile);
    showTour(s.demo);
    refreshSpeed();
    refreshBreakpoints(app.breakpoints);
  }

  return {
    onRoute(r: Route) {
      latestRoute = r;
      applyRoute(r);
    },
    destroy() {
      destroyed = true;
      media.removeEventListener('change', applyTabbed);
      for (const u of unsub) u();
      app?.destroy();
      app = null;
      root.remove();
    },
  };
}

// ---- helpers -----------------------------------------------------------------

const DEMO_ORDER = ['hello', 'pong', 'life', 'tm', 'bf', 'asm', 'forth', 'fault'];
function demoOrder(a: string, b: string): number {
  const ia = DEMO_ORDER.indexOf(a);
  const ib = DEMO_ORDER.indexOf(b);
  if (ia < 0 && ib < 0) return a.localeCompare(b);
  if (ia < 0) return 1;
  if (ib < 0) return -1;
  return ia - ib;
}

function isPanelName(s: string): s is PanelName {
  return (PANEL_NAMES as readonly string[]).includes(s);
}

function cssEscape(s: string): string {
  return s.replace(/[^A-Za-z0-9_-]/g, '');
}


function downloadBlob(name: string, blob: Blob): void {
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = name;
  a.rel = 'noopener';
  document.body.appendChild(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}

function exportTapemapPng(slot: HTMLElement | null, app: App): void {
  const canvas = slot?.querySelector('canvas') ?? null;
  if (!canvas) {
    app.note('No tape map canvas to export');
    return;
  }
  canvas.toBlob((blob) => {
    if (!blob) {
      app.note('Could not encode the tape map');
      return;
    }
    downloadBlob(`tapemap-step${app.engine.steps()}.png`, blob);
    app.note('Exported the tape map');
  }, 'image/png');
}

async function copyText(text: string): Promise<void> {
  if (navigator.clipboard && window.isSecureContext) {
    await navigator.clipboard.writeText(text);
    return;
  }
  const ta = document.createElement('textarea');
  ta.value = text;
  ta.setAttribute('readonly', '');
  ta.style.position = 'fixed';
  ta.style.left = '-9999px';
  document.body.appendChild(ta);
  ta.select();
  const ok = document.execCommand('copy');
  ta.remove();
  if (!ok) throw new Error('copy failed');
}

