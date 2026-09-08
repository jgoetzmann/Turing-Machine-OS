/* Home page: hero with a live mini machine running demos/hello/count.c at 30 steps/s. */
import { Engine, STATE, STOP, LANG, stateNameOf } from '../engine';
import type { AppContext, Page } from '../router';
import { stepsForFrame } from '../speed';
import { hex16 } from '../disasmview';
import { DEMO_FILES } from '../generated/demos';
import { CONTENT } from '../generated/content';
import { createStripPanel } from '../panels/strip';

const HERO_SPEED = 30;
const RESTART_DELAY_MS = 2500;

/** Used only if demos/hello/count.c is not in the generated bundle. */
const FALLBACK_COUNT_C = `char digits[8];
int print_int(int n) {
  int i = 0;
  if (n == 0) { putchar('0'); return 0; }
  while (n > 0) { digits[i] = '0' + n % 10; n = n / 10; i = i + 1; }
  while (i > 0) { i = i - 1; putchar(digits[i]); }
  return 0;
}
int main() {
  int i;
  for (i = 1; i <= 10; i++) { print_int(i); putchar('\\n'); }
  return 0;
}
`;

export function renderHomePage(mount: HTMLElement, ctx: AppContext): Page {
  const root = document.createElement('div');
  root.className = 'page page-home';
  root.innerHTML = `
    <section class="hero" aria-labelledby="hero-title">
      <div class="hero-copy">
        <p class="eyebrow">TuringOS v2</p>
        <h1 id="hero-title">A computer built like a Turing machine.</h1>
        <p class="lead">One C99 core: an Intel 8080 reads and writes a tape, a finite-state kernel decides what happens next, and a small CP/M-style shell boots on top. The same code runs natively, and right here as WebAssembly.</p>
        <p class="hero-actions">
          <a class="btn btn-primary" href="#/playground">Open playground</a>
          <a class="btn" href="#/architecture">Read the architecture</a>
          <a class="btn" href="#/demos">Browse demos</a>
        </p>
      </div>
      <div class="hero-machine" aria-label="Live machine running count.c">
        <div class="hero-strip" data-strip aria-label="Tape strip around the head"></div>
        <pre class="hero-console" data-console aria-live="polite" aria-label="Console output"></pre>
        <p class="hero-status" data-status role="status">Loading the machine…</p>
      </div>
    </section>
    <section class="features" aria-label="Highlights">
      <article class="card">
        <h2>Watch the head move</h2>
        <p>Every instruction is a read or write on the tape. At 30 steps per second you can follow each head move; at full speed the shell boots in milliseconds.</p>
        <a href="#/playground?speed=30">Playground at 30 steps/s</a>
      </article>
      <article class="card">
        <h2>Pull the levers</h2>
        <p>Tape count (1, 2, 4), tape length (32K, 48K, 64K), clock, seed, disks and trace are levers on the machine, not compile-time settings. Machine levers reset it; view levers apply live.</p>
        <a href="#/levers">The levers</a>
      </article>
      <article class="card">
        <h2>Four languages</h2>
        <p>Tiny-C, 8080 assembly, a Turing-machine rule language and Brainfuck all compile to the same <code>.COM</code> images, from the in-page editor or the shell.</p>
        <a href="#/languages">Languages</a>
      </article>
      <article class="card">
        <h2>Time travel</h2>
        <p>Snapshots every thousand steps plus a replayable input log mean you can scrub backwards and forwards through a run and get byte-identical tapes.</p>
        <a href="#/decisions">Design decisions</a>
      </article>
    </section>
    <section class="home-demos" aria-labelledby="home-demos-title">
      <h2 id="home-demos-title">Demos</h2>
      <ul class="demo-links" data-demos></ul>
    </section>`;
  mount.appendChild(root);

  const demoList = root.querySelector<HTMLElement>('[data-demos]')!;
  for (const name of Object.keys(DEMO_FILES).sort()) {
    const li = document.createElement('li');
    const a = document.createElement('a');
    a.href = `#/demos/${name}`;
    a.textContent = CONTENT[`demos/${name}`]?.title ?? name;
    li.appendChild(a);
    demoList.appendChild(li);
  }

  const stripRoot = root.querySelector<HTMLElement>('[data-strip]')!;
  const consoleEl = root.querySelector<HTMLElement>('[data-console]')!;
  const statusEl = root.querySelector<HTMLElement>('[data-status]')!;

  let destroyed = false;
  let rafId = 0;
  let panel: { update(now: number): void; destroy(): void } | null = null;
  let engine: Engine | null = null;
  let program: Uint8Array | null = null;
  let acc = 0;
  let lastNow = 0;
  let finishedAt = 0;
  let lastStatusAt = 0;

  function loadProgram(): boolean {
    if (!engine || !program) return false;
    consoleEl.textContent = '';
    // Fresh boot (SHELL state, no parked syscall), then drop the program straight into the TPA:
    // the same path the C tests use for tos_load_com. Its HLT returns to the shell (transition 5),
    // which is the cue to restart after a pause.
    engine.reset();
    engine.conRead();
    finishedAt = 0;
    acc = 0;
    return engine.loadCom(program);
  }

  function frame(now: number): void {
    rafId = requestAnimationFrame(frame);
    if (!engine) return;
    const dt = lastNow ? Math.min(250, now - lastNow) : 16;
    lastNow = now;
    if (finishedAt) {
      if (now - finishedAt > RESTART_DELAY_MS) loadProgram();
    } else {
      acc += stepsForFrame(HERO_SPEED, dt);
      const n = Math.floor(acc);
      if (n > 0) {
        acc -= n;
        const r = engine.step(n);
        const st = engine.state();
        // count.c ends with HLT: the kernel reloads the shell (RUNNING -> SHELL), which is our cue to restart.
        if (st === STATE.SHELL || st === STATE.IDLE || st === STATE.HALT || r.stop === STOP.HALT) {
          finishedAt = now;
        }
      }
      const out = engine.conRead();
      if (out) {
        consoleEl.textContent = (consoleEl.textContent + out).slice(-600);
      }
    }
    panel?.update(now);
    if (now - lastStatusAt > 120) {
      lastStatusAt = now;
      const cpu = engine.cpu();
      statusEl.textContent = `${stateNameOf(engine.state())} · step ${engine.steps().toLocaleString()} · PC ${hex16(cpu.pc)} · ${HERO_SPEED} steps/s`;
    }
  }

  (async () => {
    try {
      const e = await Engine.load(ctx.base);
      if (destroyed) return;
      engine = e;
      e.create({ trace: true, snapInterval: 0 });
      const src = DEMO_FILES.hello?.find((f) => f.name === 'count.c')?.text ?? FALLBACK_COUNT_C;
      const compiled = e.compile(LANG.C, src);
      if (!compiled.ok) throw new Error(`count.c: ${compiled.error}`);
      program = compiled.bytes;
      panel = createStripPanel(stripRoot, e, ctx.bus);
      loadProgram();
      rafId = requestAnimationFrame(frame);
    } catch (err) {
      const msg = err instanceof Error ? err.message : String(err);
      statusEl.textContent = `The live machine is unavailable (${msg}). Build the wasm with \`make wasm\`.`;
      stripRoot.classList.add('hero-strip-empty');
    }
  })();

  return {
    destroy() {
      destroyed = true;
      if (rafId) cancelAnimationFrame(rafId);
      panel?.destroy();
      panel = null;
      root.remove();
    },
  };
}

