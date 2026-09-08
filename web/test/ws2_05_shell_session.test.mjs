// WS2-05: under Node, pushing `dir\ncc ADD.C\nrun ADD.COM\nhalt\n` to a machine whose disk 0 is
// demo.img yields console output containing `ADD.C`, `3 + 4 = 7`, and `HALT`, and
// tos_halt_reason() == TOS_HALT_COMMAND.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
import { readFileSync, existsSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const require = createRequire(import.meta.url);
const TEST_DIR = dirname(fileURLToPath(import.meta.url));
const WEB_DIR = join(TEST_DIR, '..');
const REPO_DIR = join(WEB_DIR, '..');
const PUBLIC_DIR = join(WEB_DIR, 'public');
const WASM_JS = join(PUBLIC_DIR, 'turingos.js');
const DEMO_IMG = join(PUBLIC_DIR, 'demo.img');
const ADD_C = join(REPO_DIR, 'demos', 'hello', 'add.c');

// ---- inline harness (deliberately duplicated in every wasm test file) ----
const KSTOP = { BUDGET: 0, HALT: 1, WAIT_INPUT: 2, VSYNC: 3, BREAKPOINT: 4 };
const KS = { BOOT: 0, IDLE: 1, SHELL: 2, RUNNING: 3, SYSCALL: 4, HALT: 5 };
const TOS = { HALT_NONE: 0, HALT_COMMAND: 2, TPA_SIZE: 16128, DISK_IMAGE_BYTES: 512512, LANG_C: 0 };

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
function cstr(M, s) {
  const n = M.lengthBytesUTF8(s) + 1;
  const p = M._malloc(n);
  M.stringToUTF8(s, p, n);
  return p;
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
function diskGetFile(M, disk, name) {
  const np = cstr(M, name), cap = 65536, op = M._malloc(cap);
  const n = M._tos_disk_get_file(disk, np, op, cap);
  const res = n < 0 ? null : M.HEAPU8.slice(op, op + n);
  M._free(np); M._free(op);
  return res;
}
function diskPutFile(M, disk, name, u8) {
  const np = cstr(M, name), dp = cbytes(M, u8);
  const rc = M._tos_disk_put_file(disk, np, dp, u8.length);
  M._free(np); M._free(dp);
  return rc;
}
function diskList(M, disk) {
  const cap = 8192, op = M._malloc(cap);
  M.HEAPU8[op] = 0;
  const count = M._tos_disk_list(disk, op, cap);
  const text = M.UTF8ToString(op);
  M._free(op);
  return { count, names: text.split('\n').filter((s) => s.length > 0) };
}
function loadDiskImage(M, disk, bytes) {
  const size = M._tos_disk_size() >>> 0;
  assert.equal(size, TOS.DISK_IMAGE_BYTES, 'tos_disk_size');
  assert.equal(bytes.length, size, `${DEMO_IMG} must be exactly one disk image`);
  const p = M._tos_disk_ptr(disk);
  assert.ok(p !== 0, 'tos_disk_ptr');
  M.HEAPU8.set(bytes, p);
  M._tos_disk_reload(disk);
}
// ---- end harness ----

const MAX_SESSION_STEPS = 5000000;
// Fresh machine, disk 0 = demo.img, console empty.
function freshDemoMachine(M) {
  assert.equal(M._tos_create(0), 0);
  loadDiskImage(M, 0, new Uint8Array(readFileSync(DEMO_IMG)));
  drain(M);
}
function session(M, input) {
  pushText(M, input);
  const r = runUntil(M, MAX_SESSION_STEPS);
  return { ...r, out: drain(M) };
}
function countOf(hay, needle) { return hay.split(needle).length - 1; }

let M;
try { M = await loadModule(); } catch (e) {
  throw new Error(`WS2-05: cannot load the wasm build at ${WASM_JS}: ${e && e.message}`);
}

test('WS2-05: dir / cc ADD.C / run ADD.COM / halt on demo.img prints ADD.C, 3 + 4 = 7, HALT in order and halts with TOS_HALT_COMMAND', () => {
  assert.ok(existsSync(DEMO_IMG), `${DEMO_IMG} missing (make demo-disk)`);
  freshDemoMachine(M);
  const r = session(M, 'dir\ncc ADD.C\nrun ADD.COM\nhalt\n');
  assert.equal(r.stop, KSTOP.HALT, `stop=${r.stop} state=${r.state} after ${r.steps} steps; output: ${JSON.stringify(r.out)}`);
  assert.equal(r.state, KS.HALT);
  assert.equal(M._tos_halt_reason(), TOS.HALT_COMMAND);
  const iDir = r.out.indexOf('ADD.C');
  const iSum = r.out.indexOf('3 + 4 = 7');
  const iHalt = r.out.indexOf('HALT');
  assert.ok(iDir >= 0, `ADD.C not in ${JSON.stringify(r.out)}`);
  assert.ok(iSum >= 0, `3 + 4 = 7 not in ${JSON.stringify(r.out)}`);
  assert.ok(iHalt >= 0, `HALT not in ${JSON.stringify(r.out)}`);
  assert.ok(iDir < iSum && iSum < iHalt, 'dir listing, then the program output, then HALT');
  assert.ok(r.out.includes('3 + 4 = 7\n'));
  assert.ok(r.out.includes('HALT\n'));
  assert.ok(countOf(r.out, 'A> ') >= 4, `expected a prompt per command in ${JSON.stringify(r.out)}`);
});

test('WS2-05: after `cc ADD.C` the disk holds ADD.COM identical to tos_compile of demos/hello/add.c, and ADD.C matches the source file', () => {
  freshDemoMachine(M);
  const addSrc = readFileSync(ADD_C);
  const onDisk = diskGetFile(M, 0, 'ADD.C');
  assert.ok(onDisk, 'ADD.C on the demo disk');
  assert.deepEqual(Buffer.from(onDisk), addSrc, 'demo.img carries add.c verbatim');
  assert.equal(diskGetFile(M, 0, 'ADD.COM'), null, 'no ADD.COM before cc');
  const r = session(M, 'cc ADD.C\nhalt\n');
  assert.equal(r.stop, KSTOP.HALT, JSON.stringify(r.out));
  const com = diskGetFile(M, 0, 'ADD.COM');
  assert.ok(com, 'ADD.COM was written by cc');
  assert.ok(com.length > 0 && com.length <= TOS.TPA_SIZE);
  assert.ok(diskList(M, 0).names.includes('ADD.COM'));
  const host = compile(M, TOS.LANG_C, addSrc.toString('latin1'));
  assert.ok(host.ok, host.ok ? '' : host.error);
  assert.deepEqual(Buffer.from(com), Buffer.from(host.bytes), 'in-OS cc and tos_compile agree byte for byte');
});

test('WS2-05: `help` prints exactly the frozen command list', () => {
  freshDemoMachine(M);
  const r = session(M, 'help\nhalt\n');
  assert.equal(r.stop, KSTOP.HALT, JSON.stringify(r.out));
  assert.ok(r.out.includes('dir type run cc asm tm bf del cls mem disk halt help\n'), JSON.stringify(r.out));
  assert.equal(M._tos_halt_reason(), TOS.HALT_COMMAND);
});

test('WS2-05: `type ADD.C` prints the file bytes and `mem` prints the 64K memory map lines', () => {
  freshDemoMachine(M);
  const addSrc = readFileSync(ADD_C, 'latin1');
  const r = session(M, 'type ADD.C\nmem\nhalt\n');
  assert.equal(r.stop, KSTOP.HALT, JSON.stringify(r.out));
  assert.ok(r.out.includes(addSrc), 'type output contains the source verbatim');
  for (const range of ['0000-00FF', '0100-3FFF', '4000-DFFF', 'E000-EFFF', 'F000-FDFF', 'FE00-FEFF', 'FF00-FFFF']) {
    assert.ok(new RegExp(range + ' \\S', 'i').test(r.out), `mem line ${range} missing in ${JSON.stringify(r.out)}`);
  }
});

test('WS2-05: `run NOPE.COM` for a missing file prints ? and the session still halts by command', () => {
  freshDemoMachine(M);
  const r = session(M, 'run NOPE.COM\nhalt\n');
  assert.equal(r.stop, KSTOP.HALT, JSON.stringify(r.out));
  assert.ok(r.out.includes('?\n'), `expected ? in ${JSON.stringify(r.out)}`);
  assert.ok(!r.out.includes('3 + 4 = 7'));
  assert.equal(M._tos_halt_reason(), TOS.HALT_COMMAND);
});

test('WS2-05: `cc MISSING.C` prints ? and writes no MISSING.COM', () => {
  freshDemoMachine(M);
  const r = session(M, 'cc MISSING.C\nhalt\n');
  assert.equal(r.stop, KSTOP.HALT, JSON.stringify(r.out));
  assert.ok(r.out.includes('?\n'), `expected ? in ${JSON.stringify(r.out)}`);
  assert.equal(diskGetFile(M, 0, 'MISSING.COM'), null);
  assert.equal(M._tos_halt_reason(), TOS.HALT_COMMAND);
});

test('WS2-05: an unknown command prints ? and a blank line only re-prompts', () => {
  freshDemoMachine(M);
  const r = session(M, 'frobnicate\n\nhalt\n');
  assert.equal(r.stop, KSTOP.HALT, JSON.stringify(r.out));
  assert.ok(r.out.includes('?\n'), `expected ? in ${JSON.stringify(r.out)}`);
  assert.ok(countOf(r.out, 'A> ') >= 3, 'prompt for the bad command, the blank line and halt');
  assert.equal(M._tos_halt_reason(), TOS.HALT_COMMAND);
});

test('WS2-05: with nothing pushed the shell prints A> and parks on WAIT_INPUT/IDLE with halt reason NONE; halt then ends it', () => {
  freshDemoMachine(M);
  const r = runUntil(M, 200000);
  const out = drain(M);
  assert.equal(r.stop, KSTOP.WAIT_INPUT, `stop=${r.stop} after ${r.steps} steps`);
  assert.equal(r.state, KS.IDLE);
  assert.equal(out, 'A> ');
  assert.equal(M._tos_halt_reason(), TOS.HALT_NONE);
  assert.equal(M._tos_step(4096), 0, 'no progress while parked');
  assert.equal(M._tos_stop_reason(), KSTOP.WAIT_INPUT);
  const h = session(M, 'halt\n');
  assert.equal(h.stop, KSTOP.HALT);
  assert.ok(h.out.includes('HALT\n'));
  assert.equal(M._tos_halt_reason(), TOS.HALT_COMMAND);
});

test('WS2-05: tos_step after the halt command runs 0 steps with KSTOP_HALT, keeps TOS_HALT_COMMAND and prints nothing more', () => {
  freshDemoMachine(M);
  const r = session(M, 'halt\n');
  assert.equal(r.stop, KSTOP.HALT, JSON.stringify(r.out));
  const stepsAtHalt = M._tos_steps() >>> 0;
  pushText(M, 'dir\n');
  for (let i = 0; i < 3; i++) {
    assert.equal(M._tos_step(4096), 0, `call ${i}`);
    assert.equal(M._tos_stop_reason(), KSTOP.HALT);
  }
  assert.equal(M._tos_state(), KS.HALT);
  assert.equal(M._tos_steps() >>> 0, stepsAtHalt);
  assert.equal(M._tos_halt_reason(), TOS.HALT_COMMAND);
  assert.equal(drain(M), '');
});

test('WS2-05: a machine with no disk image loaded lists (empty) for dir and has no ADD.C', async () => {
  const blank = await loadModule();      // separate instance: never had demo.img copied in
  assert.equal(blank._tos_create(0), 0);
  drain(blank);
  assert.equal(diskGetFile(blank, 0, 'ADD.C'), null);
  const r = session(blank, 'dir\nhalt\n');
  assert.equal(r.stop, KSTOP.HALT, JSON.stringify(r.out));
  assert.ok(r.out.includes('(empty)\n'), `expected (empty) in ${JSON.stringify(r.out)}`);
  assert.ok(!r.out.includes('ADD.C'));
  assert.equal(blank._tos_halt_reason(), TOS.HALT_COMMAND);
});

test('WS2-05: `cc BAD.C` on a file with a syntax error prints the src.c:1: diagnostic and writes no BAD.COM', () => {
  freshDemoMachine(M);
  assert.equal(diskPutFile(M, 0, 'BAD.C', Buffer.from('int main(){ int x = 1 }\n', 'latin1')), 0);
  assert.ok(diskList(M, 0).names.includes('BAD.C'));
  const r = session(M, 'cc BAD.C\nhalt\n');
  assert.equal(r.stop, KSTOP.HALT, JSON.stringify(r.out));
  assert.ok(r.out.includes('src.c:1:'), `diagnostic missing in ${JSON.stringify(r.out)}`);
  assert.ok(r.out.includes("expected ';'"), `message missing in ${JSON.stringify(r.out)}`);
  assert.equal(diskGetFile(M, 0, 'BAD.COM'), null, 'no output file on a compile error');
  assert.equal(M._tos_halt_reason(), TOS.HALT_COMMAND);
});
