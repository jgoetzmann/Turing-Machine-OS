// WS2-04: under Node, the wasm build runs every demos/hello/*.c (compiled via tos_compile)
// and the .expected bytes match.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
import { readFileSync, readdirSync, existsSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const require = createRequire(import.meta.url);
const TEST_DIR = dirname(fileURLToPath(import.meta.url));
const WEB_DIR = join(TEST_DIR, '..');
const REPO_DIR = join(WEB_DIR, '..');
const PUBLIC_DIR = join(WEB_DIR, 'public');
const WASM_JS = join(PUBLIC_DIR, 'turingos.js');
const HELLO_DIR = join(REPO_DIR, 'demos', 'hello');

// ---- inline harness (deliberately duplicated in every wasm test file) ----
const KSTOP = { BUDGET: 0, HALT: 1, WAIT_INPUT: 2, VSYNC: 3, BREAKPOINT: 4 };
const KS = { BOOT: 0, IDLE: 1, SHELL: 2, RUNNING: 3, SYSCALL: 4, HALT: 5 };
const TOS = { HALT_NONE: 0, HALT_COMMAND: 2, TPA_SIZE: 16128, LANG_C: 0 };

function loadFactory() {
  let mod;
  try { mod = require(WASM_JS); } catch (_e) { mod = undefined; }
  if (typeof mod === 'function') return mod;
  if (mod && typeof mod.default === 'function') return mod.default;
  if (mod && typeof mod.createTuringOS === 'function') return mod.createTuringOS;
  const src = readFileSync(WASM_JS, 'utf8');
  const m = { exports: {} };
  new Function('module', 'exports', 'require', '__dirname', '__filename', src)(m, m.exports, require, PUBLIC_DIR, WASM_JS);
  const f = typeof m.exports === 'function' ? m.exports : (m.exports && (m.exports.createTuringOS || m.exports.default));
  if (typeof f !== 'function') throw new Error('createTuringOS factory not found in ' + WASM_JS);
  return f;
}
async function loadModule() {
  const createTuringOS = loadFactory();
  return createTuringOS({ locateFile: (p) => join(PUBLIC_DIR, p) });
}
function cbytes(M, u8) {
  const p = M._malloc(u8.length + 1);
  M.HEAPU8.set(u8, p);
  M.HEAPU8[p + u8.length] = 0;
  return p;
}
function pushText(M, s) { for (let i = 0; i < s.length; i++) M._tos_con_push(s.charCodeAt(i) & 0xff); }
function drain(M) {
  let out = '';
  for (let i = 0; i < 1 << 20; i++) { const b = M._tos_con_pop(); if (b < 0) break; out += String.fromCharCode(b); }
  return out;
}
function runUntil(M, maxSteps, chunk = 4096) {
  let steps = 0, stop = KSTOP.BUDGET;
  for (let calls = 0; steps < maxSteps && calls <= maxSteps; calls++) {
    const n = M._tos_step(Math.min(chunk, maxSteps - steps)) >>> 0;
    steps += n;
    stop = M._tos_stop_reason();
    if (stop === KSTOP.HALT || stop === KSTOP.WAIT_INPUT) break;
    if (n === 0 && stop === KSTOP.BUDGET) break;
  }
  return { steps, stop, state: M._tos_state() };
}
// Single-steps a loaded program until the kernel leaves RUNNING/SYSCALL (program HLT -> SHELL,
// parked -> IDLE, fault -> HALT). Because the shell never executes an instruction inside the
// last call, the drained console holds exactly the program's own bytes.
function runProgram(M, maxSteps) {
  let steps = 0;
  for (let calls = 0; calls < maxSteps; calls++) {
    steps += M._tos_step(1) >>> 0;
    const state = M._tos_state();
    const stop = M._tos_stop_reason();
    if (state === KS.SHELL || state === KS.IDLE || state === KS.HALT || stop === KSTOP.HALT || stop === KSTOP.WAIT_INPUT) {
      return { steps, state, stop, exhausted: false };
    }
  }
  return { steps, state: M._tos_state(), stop: M._tos_stop_reason(), exhausted: true };
}
function compile(M, lang, src) {
  const srcBytes = Buffer.from(src, 'latin1');
  const sp = cbytes(M, srcBytes);
  const cap = 65536, errcap = 512;
  const op = M._malloc(cap), ep = M._malloc(errcap);
  M.HEAPU8[ep] = 0;
  const n = M._tos_compile(lang, sp, srcBytes.length, op, cap, ep, errcap);
  const res = n < 0 ? { ok: false, error: M.UTF8ToString(ep) } : { ok: true, bytes: M.HEAPU8.slice(op, op + n) };
  M._free(sp); M._free(op); M._free(ep);
  return res;
}
function loadCom(M, bytes) {
  const p = cbytes(M, bytes);
  const rc = M._tos_load_com(p, bytes.length);
  M._free(p);
  return rc;
}
// ---- end harness ----

const REQUIRED = ['hello', 'count', 'echo', 'add', 'strcat', 'memtest'];
const INPUTS = { echo: 'hello\n' };           // S10: echo prints the input line back for input `hello\n`
const MAX_PROGRAM_STEPS = 2000000;

function helloSources() {
  if (!existsSync(HELLO_DIR)) return [];
  return readdirSync(HELLO_DIR).filter((f) => f.endsWith('.c')).map((f) => f.slice(0, -2)).sort();
}
function readDemo(name) {
  return {
    src: readFileSync(join(HELLO_DIR, name + '.c'), 'latin1'),
    expected: readFileSync(join(HELLO_DIR, name + '.expected'), 'latin1'),
  };
}

let M;
try { M = await loadModule(); } catch (e) {
  throw new Error(`WS2-04: cannot load the wasm build at ${WASM_JS}: ${e && e.message}`);
}

test('WS2-04: demos/hello contains hello, count, echo, add, strcat, memtest, each with a .expected file', () => {
  const names = helloSources();
  for (const r of REQUIRED) assert.ok(names.includes(r), `demos/hello/${r}.c missing (have: ${names.join(', ')})`);
  for (const n of names) assert.ok(existsSync(join(HELLO_DIR, n + '.expected')), `demos/hello/${n}.expected missing`);
});

for (const name of helloSources()) {
  test(`WS2-04: demos/hello/${name}.c compiled with tos_compile prints exactly ${name}.expected and returns to the shell`, () => {
    const { src, expected } = readDemo(name);
    assert.equal(M._tos_create(0), 0);
    drain(M);
    const c = compile(M, TOS.LANG_C, src);
    assert.ok(c.ok, `compile failed: ${c.ok ? '' : c.error}`);
    assert.ok(c.bytes.length > 0 && c.bytes.length <= TOS.TPA_SIZE, `image size ${c.bytes.length}`);
    assert.equal(loadCom(M, c.bytes), 0);
    assert.equal(M._tos_state(), KS.RUNNING);
    if (INPUTS[name]) pushText(M, INPUTS[name]);
    const r = runProgram(M, MAX_PROGRAM_STEPS);
    const out = drain(M);
    assert.equal(r.exhausted, false, `did not finish within ${MAX_PROGRAM_STEPS} steps; output so far ${JSON.stringify(out)}`);
    assert.equal(out, expected);
    assert.equal(r.state, KS.SHELL, `state ${r.state} (stop ${r.stop}); program HLT must return to the shell`);
    assert.equal(M._tos_halt_reason(), TOS.HALT_NONE);
    assert.ok(r.steps > 0);
  });
}

test('WS2-04: echo.c with no console input parks (KSTOP_WAIT_INPUT, KS_IDLE, nothing printed) and completes once input arrives', () => {
  const { src, expected } = readDemo('echo');
  assert.equal(M._tos_create(0), 0);
  drain(M);
  const c = compile(M, TOS.LANG_C, src);
  assert.ok(c.ok, c.ok ? '' : c.error);
  assert.equal(loadCom(M, c.bytes), 0);
  const parked = runProgram(M, MAX_PROGRAM_STEPS);
  assert.equal(parked.exhausted, false);
  assert.equal(parked.stop, KSTOP.WAIT_INPUT, `stop ${parked.stop}`);
  assert.equal(parked.state, KS.IDLE);
  assert.equal(drain(M), '', 'nothing echoed before any input');
  assert.equal(M._tos_halt_reason(), TOS.HALT_NONE);
  // With the machine still parked and no input, another call makes no progress.
  assert.equal(M._tos_step(1000), 0, 'zero steps while parked without input');
  assert.equal(M._tos_stop_reason(), KSTOP.WAIT_INPUT);
  pushText(M, 'hello\n');
  const done = runProgram(M, MAX_PROGRAM_STEPS);
  assert.equal(done.exhausted, false);
  assert.equal(drain(M), expected);
  assert.equal(done.state, KS.SHELL);
});

test('WS2-04: a truncated hello.c (final brace removed) is rejected by tos_compile with a src.c:LINE:COL: diagnostic and nothing is loaded', () => {
  const { src } = readDemo('hello');
  const cut = src.slice(0, src.lastIndexOf('}'));
  assert.notEqual(cut, src);
  assert.equal(M._tos_create(0), 0);
  const c = compile(M, TOS.LANG_C, cut);
  assert.equal(c.ok, false, 'a truncated program must not compile');
  assert.match(c.error, /^src\.c:\d+:\d+: \S/);
  assert.equal(M._tos_state(), KS.SHELL, 'machine untouched by a failed compile');
  assert.equal(M._tos_steps(), 0);
});

test('WS2-04: count.c stopped after a 10-instruction budget reports KSTOP_BUDGET, is still running, and has not printed its output', () => {
  const { src, expected } = readDemo('count');
  assert.equal(M._tos_create(0), 0);
  drain(M);
  const c = compile(M, TOS.LANG_C, src);
  assert.ok(c.ok, c.ok ? '' : c.error);
  assert.equal(loadCom(M, c.bytes), 0);
  assert.equal(M._tos_step(10), 10);
  assert.equal(M._tos_stop_reason(), KSTOP.BUDGET);
  assert.equal(M._tos_steps(), 10);
  assert.ok([KS.RUNNING, KS.SYSCALL].includes(M._tos_state()), `state ${M._tos_state()}`);
  assert.notEqual(drain(M), expected, 'ten instructions cannot have printed 1..10');
});

test('WS2-04: after hello.c returns to the shell, the next step prints the prompt and parks on WAIT_INPUT (not HALT)', () => {
  const { src, expected } = readDemo('hello');
  assert.equal(M._tos_create(0), 0);
  drain(M);
  const c = compile(M, TOS.LANG_C, src);
  assert.ok(c.ok, c.ok ? '' : c.error);
  assert.equal(loadCom(M, c.bytes), 0);
  const r = runProgram(M, MAX_PROGRAM_STEPS);
  assert.equal(r.state, KS.SHELL);
  assert.equal(drain(M), expected);
  const s = runUntil(M, 200000);
  assert.equal(s.stop, KSTOP.WAIT_INPUT, `stop ${s.stop} after ${s.steps} steps`);
  assert.equal(s.state, KS.IDLE);
  assert.equal(drain(M), 'A> ', 'only the prompt follows the program output');
  assert.equal(M._tos_halt_reason(), TOS.HALT_NONE, 'a program HLT never halts the machine');
});
