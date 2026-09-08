// WS2-02: every tos_* symbol in api.h is exported from the wasm (Module._tos_step etc.)
// and tos_version() returns "2.0.0". Run with: cd web && node --test test/
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

// ---- inline harness (deliberately duplicated in every wasm test file) ----
const KSTOP = { BUDGET: 0, HALT: 1, WAIT_INPUT: 2, VSYNC: 3, BREAKPOINT: 4 };
const KS = { BOOT: 0, IDLE: 1, SHELL: 2, RUNNING: 3, SYSCALL: 4, HALT: 5 };
const TOS = {
  HALT_NONE: 0, HALT_COMMAND: 2, HALT_EOF: 3, HALT_TAPE_FAULT: 4, HALT_BAD_TAPE: 6,
  TPA_SIZE: 16128, DISK_IMAGE_BYTES: 512512,
  LANG_C: 0, LANG_ASM: 1, LANG_TM: 2, LANG_BF: 3,
  LEVER_TAPES: 0, LEVER_TAPE_LEN: 1, LEVER_HZ: 2, LEVER_SEED: 3, LEVER_INPUT_MODE: 4,
  LEVER_DISKS: 5, LEVER_TRACE: 6, LEVER_SNAP_INTERVAL: 7, LEVER_COUNT: 8,
};

function loadFactory() {
  let mod;
  try { mod = require(WASM_JS); } catch (_e) { mod = undefined; }
  if (typeof mod === 'function') return mod;
  if (mod && typeof mod.default === 'function') return mod.default;
  if (mod && typeof mod.createTuringOS === 'function') return mod.createTuringOS;
  // Fallback: evaluate public/turingos.js as a CommonJS script whatever package.json's "type" says.
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
function diskList(M, disk) {
  const cap = 8192, op = M._malloc(cap);
  M.HEAPU8[op] = 0;
  const count = M._tos_disk_list(disk, op, cap);
  const text = M.UTF8ToString(op);
  M._free(op);
  return { count, names: text.split('\n').filter((s) => s.length > 0) };
}
// ---- end harness ----

// Every tos_* declaration in src/api/api.h (frozen header, reproduced in SPEC §Surface-C).
const API_SYMBOLS = [
  'tos_create', 'tos_reset', 'tos_step', 'tos_stop_reason', 'tos_state', 'tos_steps',
  'tos_cycles_lo', 'tos_cycles_hi', 'tos_halt_reason', 'tos_frame', 'tos_last_syscall',
  'tos_tape_ptr', 'tos_tape_count', 'tos_tape_len', 'tos_tape_selected', 'tos_cpu_ptr', 'tos_meta_ptr',
  'tos_write_age_ptr', 'tos_read_age_ptr', 'tos_travel_lo', 'tos_travel_hi', 'tos_accesses_lo', 'tos_cells_written',
  'tos_trace_ptr', 'tos_trace_head', 'tos_trace_count', 'tos_trace_enable',
  'tos_con_push', 'tos_con_pop', 'tos_con_pending', 'tos_keys_set',
  'tos_disk_ptr', 'tos_disk_size', 'tos_disk_reload', 'tos_disk_put_file', 'tos_disk_get_file', 'tos_disk_list',
  'tos_lever_set', 'tos_lever_get', 'tos_bp_add', 'tos_bp_clear', 'tos_bp_hit', 'tos_seek',
  'tos_snapshot_count', 'tos_snapshot_step',
  'tos_load_com', 'tos_compile', 'tos_disasm', 'tos_state_name', 'tos_syscall_name',
  'tos_transition_count', 'tos_transition_why', 'tos_transition_from', 'tos_transition_to', 'tos_transition_fired',
  'tos_hal_option', 'tos_version',
];

let M;
try { M = await loadModule(); } catch (e) {
  throw new Error(`WS2-02: cannot load the wasm build at ${WASM_JS}: ${e && e.message}`);
}

test('WS2-02: every tos_* symbol declared in api.h is exported from the wasm as Module._tos_*', () => {
  const names = new Set(API_SYMBOLS);
  const apiH = join(REPO_DIR, 'src', 'api', 'api.h');
  assert.ok(existsSync(apiH), `${apiH} is missing`);
  for (const m of readFileSync(apiH, 'utf8').matchAll(/TOS_EXPORT[^;]*?\b(tos_[a-z0-9_]+)\s*\(/g)) names.add(m[1]);
  assert.ok(names.size >= 57, `expected at least 57 tos_* symbols, found ${names.size}`);
  const missing = [...names].filter((n) => typeof M['_' + n] !== 'function');
  assert.deepEqual(missing, [], `not exported from the wasm: ${missing.join(', ')}`);
});

test('WS2-02: _malloc/_free and the runtime methods named in the emcc flags exist; heap is a fixed 64 MB', () => {
  for (const f of ['_malloc', '_free', 'UTF8ToString', 'stringToUTF8', 'lengthBytesUTF8']) {
    assert.equal(typeof M[f], 'function', `Module.${f} should be a function`);
  }
  assert.ok(M.HEAPU8 instanceof Uint8Array, 'HEAPU8');
  assert.ok(M.HEAPU32 instanceof Uint32Array, 'HEAPU32');
  assert.ok(M.HEAP32 instanceof Int32Array, 'HEAP32');
  assert.equal(M.HEAPU8.length, 64 * 1024 * 1024, 'INITIAL_MEMORY=64MB with ALLOW_MEMORY_GROWTH=0');
});

test('WS2-02: tos_version() returns "2.0.0"', () => {
  const p = M._tos_version();
  assert.ok(p !== 0, 'tos_version returned a NULL pointer');
  assert.equal(M.UTF8ToString(p), '2.0.0');
});

test('WS2-02: tos_state_name/tos_syscall_name/tos_disasm work through the export boundary after boot', () => {
  assert.equal(M._tos_create(0), 0);
  const names = ['BOOT', 'IDLE', 'SHELL', 'RUNNING', 'SYSCALL', 'HALT'];
  for (let i = 0; i < names.length; i++) assert.equal(M.UTF8ToString(M._tos_state_name(i)), names[i], `state ${i}`);
  assert.equal(M.UTF8ToString(M._tos_syscall_name(0x01)), 'CONIN');
  assert.equal(M._tos_state(), KS.SHELL);
  assert.equal(M._tos_steps(), 0);
  // S3: every tiny-C image (the shell included) starts with `CALL main ; HLT` at 0x0100.
  const cap = 64, op = M._malloc(cap);
  const len = M._tos_disasm(0x0100, op, cap);
  const text = M.UTF8ToString(op);
  M._free(op);
  assert.equal(len, 3, `disasm length for ${JSON.stringify(text)}`);
  assert.match(text, /^CALL [0-9A-F]{4}H$/);
});

test('WS2-02: tos_syscall_name returns "?" for function ids that are not BIOS calls', () => {
  for (const fn of [0x00, 0x10, 0x11, 0x1d, 0x7f, 0xee, 0xff]) {
    assert.equal(M.UTF8ToString(M._tos_syscall_name(fn)), '?', `fn 0x${fn.toString(16)}`);
  }
});

test('WS2-02: tos_lever_set returns -1 for a bad lever id or an out-of-range value and leaves the machine alone', () => {
  assert.equal(M._tos_create(0), 0);
  assert.equal(M._tos_tape_count(), 1);
  assert.equal(M._tos_tape_len(), 65536);
  assert.equal(M._tos_lever_set(99, 1), -1, 'id 99');
  assert.equal(M._tos_lever_set(-1, 1), -1, 'id -1');
  assert.equal(M._tos_lever_set(TOS.LEVER_COUNT, 1), -1, 'id == TOS_LEVER_COUNT');
  assert.equal(M._tos_lever_set(TOS.LEVER_TAPES, 3), -1, 'tapes=3');
  assert.equal(M._tos_lever_set(TOS.LEVER_TAPES, 0), -1, 'tapes=0');
  assert.equal(M._tos_lever_set(TOS.LEVER_TAPE_LEN, 1000), -1, 'tape_len=1000');
  assert.equal(M._tos_lever_set(TOS.LEVER_DISKS, 3), -1, 'disks=3');
  assert.equal(M._tos_lever_set(TOS.LEVER_DISKS, 0), -1, 'disks=0');
  assert.equal(M._tos_tape_count(), 1, 'tape count unchanged after rejected lever writes');
  assert.equal(M._tos_tape_len(), 65536, 'tape length unchanged after rejected lever writes');
  assert.equal(M._tos_lever_get(TOS.LEVER_TAPES), 1);
  assert.equal(M._tos_lever_get(TOS.LEVER_TAPE_LEN), 65536);
  assert.equal(M._tos_lever_get(TOS.LEVER_DISKS), 1);
  assert.equal(M._tos_state(), KS.SHELL, 'still in SHELL');
});

test('WS2-02: tos_load_com rejects an image larger than the TPA (16129 bytes -> -1) and accepts 16128', () => {
  assert.equal(M._tos_create(0), 0);
  const big = new Uint8Array(TOS.TPA_SIZE + 1).fill(0x00);
  big[big.length - 1] = 0x76;
  const bp = cbytes(M, big);
  assert.equal(M._tos_load_com(bp, big.length), -1, '16129 bytes must be rejected');
  M._free(bp);
  const ok = new Uint8Array(TOS.TPA_SIZE).fill(0x00); // NOPs
  ok[ok.length - 1] = 0x76;                            // HLT
  const okp = cbytes(M, ok);
  assert.equal(M._tos_load_com(okp, ok.length), 0, '16128 bytes fits the TPA');
  M._free(okp);
  assert.equal(M._tos_state(), KS.RUNNING, 'a successful load puts the machine in RUNNING');
});

test('WS2-02: tos_compile reports a tiny-C error as -1 with "src.c:1:" ... "expected \';\'" in err', () => {
  const r = compile(M, TOS.LANG_C, 'int main(){ int x = 1 }');
  assert.equal(r.ok, false, 'compile must fail');
  assert.ok(r.error.startsWith('src.c:1:'), `err was ${JSON.stringify(r.error)}`);
  assert.ok(r.error.includes("expected ';'"), `err was ${JSON.stringify(r.error)}`);
});

test('WS2-02: tos_con_pop is -1 and tos_con_pending is 0 on a freshly created machine', () => {
  assert.equal(M._tos_create(0), 0);
  assert.equal(M._tos_con_pending(), 0);
  assert.equal(M._tos_con_pop(), -1);
  assert.equal(M._tos_con_pop(), -1, 'stays -1');
});

test('WS2-02: tos_disk_get_file returns -1 for a file that does not exist', () => {
  assert.equal(M._tos_create(0), 0);
  assert.equal(diskGetFile(M, 0, 'NOPE.TXT'), null);
  const np = cstr(M, 'NOPE.TXT'), cap = 16, op = M._malloc(cap);
  assert.equal(M._tos_disk_get_file(0, np, op, cap), -1);
  M._free(np); M._free(op);
  assert.ok(!diskList(M, 0).names.includes('NOPE.TXT'));
});

test('WS2-02: with snap_interval 0 only the step-0 anchor exists and tos_seek replays from it', () => {
  assert.equal(M._tos_create(0), 0);
  assert.equal(M._tos_lever_set(TOS.LEVER_SNAP_INTERVAL, 0), 0);
  assert.equal(M._tos_lever_get(TOS.LEVER_SNAP_INTERVAL), 0);
  const r = runUntil(M, 100);
  assert.ok(r.steps > 0, 'the shell ran some instructions');
  assert.equal(M._tos_snapshot_count(), 1, 'the step-0 anchor (SPEC S2)');
  assert.equal(M._tos_snapshot_step(0), 0);
  const target = Math.floor(r.steps / 2);              // the shell parks at ~step 21; seek inside the run
  assert.equal(M._tos_seek(target), 0);
  assert.equal(M._tos_steps(), target);
  assert.equal(M._tos_seek(r.steps + 1000), -1, 'unreachable past the parked point');
  assert.equal(M._tos_seek(0), 0);
  assert.equal(M._tos_steps(), 0);
});

test('WS2-02: tos_bp_add returns -1 once all 16 breakpoint slots are used', () => {
  assert.equal(M._tos_create(0), 0);
  M._tos_bp_clear();
  const ids = [];
  for (let i = 0; i < 16; i++) ids.push(M._tos_bp_add(0, 0x0100, 0x0100));
  assert.deepEqual(ids, [...Array(16).keys()], 'ids 0..15');
  assert.equal(M._tos_bp_add(0, 0x0100, 0x0100), -1, '17th breakpoint');
  assert.equal(M._tos_bp_hit(), -1, 'nothing has fired yet');
  M._tos_bp_clear();
  assert.equal(M._tos_bp_add(0, 0x0100, 0x0100), 0, 'slot 0 is free again after tos_bp_clear');
  M._tos_bp_clear();
});

test('WS2-02: tos_hal_option returns -1 for every key on the wasm HAL', () => {
  for (const [k, v] of [['disk_a', 'x.img'], ['raw', '0'], ['fps', '60'], ['stdin_script', 'x'], ['nonsense', '1']]) {
    const kp = cstr(M, k), vp = cstr(M, v);
    assert.equal(M._tos_hal_option(kp, vp), -1, `key ${k}`);
    M._free(kp); M._free(vp);
  }
});
