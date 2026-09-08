// WS2-03: layout.json produced by dump_layout equals the S9 shape byte-for-byte after JSON
// normalisation (cpu.size == 296), and the offsets agree with the wasm's real structs
// (tos_cpu_ptr sanity: after boot pc read through the offset equals 0x0100, ...).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const require = createRequire(import.meta.url);
const TEST_DIR = dirname(fileURLToPath(import.meta.url));
const WEB_DIR = join(TEST_DIR, '..');
const PUBLIC_DIR = join(WEB_DIR, 'public');
const WASM_JS = join(PUBLIC_DIR, 'turingos.js');
const LAYOUT_JSON = join(WEB_DIR, 'src', 'generated', 'layout.json');

// ---- inline harness (deliberately duplicated in every wasm test file) ----
const KSTOP = { BUDGET: 0, HALT: 1, WAIT_INPUT: 2, VSYNC: 3, BREAKPOINT: 4 };
const KS = { BOOT: 0, IDLE: 1, SHELL: 2, RUNNING: 3, SYSCALL: 4, HALT: 5 };
const TOS = {
  HALT_NONE: 0, HALT_COMMAND: 2, HALT_EOF: 3, HALT_TAPE_FAULT: 4, HALT_BAD_TAPE: 6,
  TPA_SIZE: 16128, LANG_C: 0,
  LEVER_TAPES: 0, LEVER_TAPE_LEN: 1, LEVER_HZ: 2, LEVER_SEED: 3, LEVER_INPUT_MODE: 4,
  LEVER_DISKS: 5, LEVER_TRACE: 6, LEVER_SNAP_INTERVAL: 7,
  META_STATE: 0x00, META_STEPS: 0x01, META_HALT_REASON: 0x05, META_TAPE_SEL: 0x06, META_TAPE_COUNT: 0x07,
  META_TAPE_PAGES: 0x08, META_FRAME: 0x0a, META_KEYS: 0x0e, META_STOP: 0x0f, META_SEED: 0x30, META_HZ: 0x31,
  META_INPUT_MODE: 0x35, META_DISKS: 0x36, META_TRACE: 0x37, META_SYSCALL: 0x38, META_SP_INIT: 0x3a,
  BIOS_CONIN: 0x01, BIOS_READLINE: 0x17,
  TR_FETCH: 0, TR_STATE: 4, TRACE_CAP: 65536,
};
const STACK_TOP = (L) => L - 0x201;
const META_BASE = (L) => L - 0x100;

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
function u16(M, p) { return M.HEAPU8[p] | (M.HEAPU8[p + 1] << 8); }
function u32(M, p) { return (M.HEAPU8[p] | (M.HEAPU8[p + 1] << 8) | (M.HEAPU8[p + 2] << 16) | (M.HEAPU8[p + 3] << 24)) >>> 0; }
function i32(M, p) { return M.HEAP32[p >> 2]; }
// ---- end harness ----

const EXPECTED_LAYOUT = {
  cpu: {
    size: 296, a: 0, b: 1, c: 2, d: 3, e: 4, h: 5, l: 6, sp: 8, pc: 10, flags: 12, halted: 16,
    io_out_pending: 20, io_out_port: 21, io_out_value: 22, io_in_ports: 23,
    interrupts_enabled: 279, rim_value: 280, sim_value: 281, cycles: 288,
  },
  trace_event: { size: 8, step: 0, addr: 4, kind: 6, value: 7 },
  config: { size: 20, tapes: 0, tape_len: 4, hz: 8, seed: 12, input_mode: 13, disks: 14, trace: 15, snap_interval: 16 },
};
function normalize(v) {
  if (Array.isArray(v)) return v.map(normalize);
  if (v && typeof v === 'object') return Object.fromEntries(Object.keys(v).sort().map((k) => [k, normalize(v[k])]));
  return v;
}
function readLayout() { return JSON.parse(readFileSync(LAYOUT_JSON, 'utf8')); }

// Reads cpu_t fields through the offsets in layout.json (not the expected literal), so a wrong
// generated file and a wrong struct both show up here.
function cpuView(M, L) {
  const p = M._tos_cpu_ptr();
  const c = L.cpu;
  return {
    a: M.HEAPU8[p + c.a], b: M.HEAPU8[p + c.b], c: M.HEAPU8[p + c.c], d: M.HEAPU8[p + c.d], e: M.HEAPU8[p + c.e],
    h: M.HEAPU8[p + c.h], l: M.HEAPU8[p + c.l],
    sp: u16(M, p + c.sp), pc: u16(M, p + c.pc), flags: M.HEAPU8[p + c.flags], halted: i32(M, p + c.halted),
    io_out_pending: M.HEAPU8[p + c.io_out_pending], io_out_port: M.HEAPU8[p + c.io_out_port],
    io_out_value: M.HEAPU8[p + c.io_out_value],
    io_in: (port) => M.HEAPU8[p + c.io_in_ports + port],
    cycles_lo: u32(M, p + c.cycles), cycles_hi: u32(M, p + c.cycles + 4),
  };
}
function writeConfig(M, L, cfg) {
  const c = L.config;
  const p = M._malloc(c.size);
  M.HEAPU8.fill(0, p, p + c.size);
  M.HEAPU8[p + c.tapes] = cfg.tapes;
  M.HEAPU32[(p + c.tape_len) >> 2] = cfg.tape_len;
  M.HEAPU32[(p + c.hz) >> 2] = cfg.hz;
  M.HEAPU8[p + c.seed] = cfg.seed;
  M.HEAPU8[p + c.input_mode] = cfg.input_mode;
  M.HEAPU8[p + c.disks] = cfg.disks;
  M.HEAPU8[p + c.trace] = cfg.trace;
  M.HEAPU32[(p + c.snap_interval) >> 2] = cfg.snap_interval;
  return p;
}
function traceEvents(M, L) {
  const base = M._tos_trace_ptr();
  const head = M._tos_trace_head() >>> 0;
  const count = M._tos_trace_count() >>> 0;
  const t = L.trace_event;
  const out = [];
  for (let i = 0; i < count; i++) {
    const idx = (((head - count + i) % TOS.TRACE_CAP) + TOS.TRACE_CAP) % TOS.TRACE_CAP;
    const ev = base + idx * t.size;
    out.push({ step: u32(M, ev + t.step), addr: u16(M, ev + t.addr), kind: M.HEAPU8[ev + t.kind], value: M.HEAPU8[ev + t.value] });
  }
  return out;
}

let M;
try { M = await loadModule(); } catch (e) {
  throw new Error(`WS2-03: cannot load the wasm build at ${WASM_JS}: ${e && e.message}`);
}

test('WS2-03: web/src/generated/layout.json equals the S9 shape after JSON normalisation; cpu.size == 296', () => {
  const layout = readLayout();
  assert.equal(layout.cpu.size, 296);
  assert.deepEqual(normalize(layout), normalize(EXPECTED_LAYOUT));
  assert.equal(JSON.stringify(normalize(layout)), JSON.stringify(normalize(EXPECTED_LAYOUT)), 'byte-for-byte after normalisation');
  assert.deepEqual(Object.keys(layout).sort(), ['config', 'cpu', 'trace_event'], 'no extra top-level keys');
});

test('WS2-03: after boot and one step the cpu_t offsets match the wasm (pc 0x0100, sp 0xFDFF, CALL main pushes 0x0103, cycles 17)', () => {
  const L = readLayout();
  assert.equal(M._tos_create(0), 0);
  let cpu = cpuView(M, L);
  assert.equal(cpu.pc, 0x0100, 'pc after boot');
  assert.equal(cpu.sp, STACK_TOP(65536), 'sp after boot');
  assert.equal(cpu.halted, 0, 'halted after boot');
  assert.equal(cpu.io_out_pending, 0, 'io_out_pending after boot');
  assert.equal(cpu.cycles_lo, 0); assert.equal(cpu.cycles_hi, 0);
  const tape0 = M._tos_tape_ptr(0);
  assert.equal(M.HEAPU8[tape0 + 0x0100], 0xcd, 'shell image begins with CALL main');
  assert.equal(M.HEAPU8[tape0 + 0x0103], 0x76, '... followed by HLT');
  const mainAddr = u16(M, tape0 + 0x0101);
  assert.equal(M._tos_step(1), 1);
  assert.equal(M._tos_steps(), 1);
  cpu = cpuView(M, L);
  assert.equal(cpu.pc, mainAddr, 'pc == operand of the CALL at 0x0100');
  assert.equal(cpu.sp, STACK_TOP(65536) - 2, 'CALL pushed two bytes');
  assert.equal(u16(M, tape0 + cpu.sp), 0x0103, 'return address on the stack');
  assert.equal(cpu.cycles_lo, 17, 'CALL = 17 cycles');
  assert.equal(cpu.cycles_hi, 0);
  assert.equal(M._tos_cycles_lo() >>> 0, 17, 'tos_cycles_lo agrees with the struct');
  assert.equal(cpu.halted, 0);
  assert.equal(cpu.io_in(2), 0, 'IN 0x02 -> selected tape');
  assert.equal(cpu.io_in(3), 0, 'IN 0x03 -> keys');
  assert.equal(cpu.io_in(4), 1, 'IN 0x04 -> tape count');
  assert.equal(cpu.io_in(5), 0x00, 'IN 0x05 -> (65536/256)&0xFF');
});

test('WS2-03: kernel_config_t offsets from layout.json drive tos_create (2 tapes, 32K, seed 7, keys, 2 disks, trace, snap 500)', () => {
  const L = readLayout();
  const cfg = { tapes: 2, tape_len: 32768, hz: 0, seed: 7, input_mode: 1, disks: 2, trace: 1, snap_interval: 500 };
  const cp = writeConfig(M, L, cfg);
  assert.equal(M._tos_create(cp), 0);
  M._free(cp);
  assert.equal(M._tos_tape_count(), 2);
  assert.equal(M._tos_tape_len(), 32768);
  assert.equal(M._tos_lever_get(TOS.LEVER_TAPES), 2);
  assert.equal(M._tos_lever_get(TOS.LEVER_TAPE_LEN), 32768);
  assert.equal(M._tos_lever_get(TOS.LEVER_HZ), 0);
  assert.equal(M._tos_lever_get(TOS.LEVER_SEED), 7);
  assert.equal(M._tos_lever_get(TOS.LEVER_INPUT_MODE), 1);
  assert.equal(M._tos_lever_get(TOS.LEVER_DISKS), 2);
  assert.equal(M._tos_lever_get(TOS.LEVER_TRACE), 1);
  assert.equal(M._tos_lever_get(TOS.LEVER_SNAP_INTERVAL), 500);
  const cpu = cpuView(M, L);
  assert.equal(cpu.sp, STACK_TOP(32768), 'initial SP for a 32K tape');
  assert.equal(M._tos_step(1), 1);
  const meta = M._tos_meta_ptr();
  assert.equal(meta, M._tos_tape_ptr(0) + META_BASE(32768), 'meta block sits at TOS_META_BASE(L) of tape 0');
  assert.equal(M.HEAPU8[meta + TOS.META_STATE], KS.SHELL);
  assert.equal(u32(M, meta + TOS.META_STEPS), 1);
  assert.equal(M.HEAPU8[meta + TOS.META_TAPE_COUNT], 2);
  assert.equal(u16(M, meta + TOS.META_TAPE_PAGES), 32768 / 256);
  assert.equal(M.HEAPU8[meta + TOS.META_SEED], 7);
  assert.equal(u32(M, meta + TOS.META_HZ), 0);
  assert.equal(M.HEAPU8[meta + TOS.META_INPUT_MODE], 1);
  assert.equal(M.HEAPU8[meta + TOS.META_DISKS], 2);
  assert.equal(M.HEAPU8[meta + TOS.META_TRACE], 1);
  assert.equal(u16(M, meta + TOS.META_SP_INIT), STACK_TOP(32768));
});

test('WS2-03: trace_event_t offsets decode the first events of a traced boot (TR_STATE 0 then TR_FETCH 0x0100 = 0xCD)', () => {
  const L = readLayout();
  const cp = writeConfig(M, L, { tapes: 1, tape_len: 65536, hz: 0, seed: 1, input_mode: 0, disks: 1, trace: 1, snap_interval: 1000 });
  assert.equal(M._tos_create(cp), 0);
  M._free(cp);
  assert.equal(M._tos_step(1), 1);
  const evs = traceEvents(M, L);
  assert.ok(evs.length >= 2, `expected at least a state event and a fetch, got ${evs.length}`);
  assert.ok(evs.length <= TOS.TRACE_CAP);
  const boot = evs.find((e) => e.kind === TOS.TR_STATE);
  assert.ok(boot, 'a TR_STATE event for BOOT->SHELL');
  assert.equal(boot.addr, (KS.BOOT << 8) | KS.SHELL);
  assert.equal(boot.value, 0, 'transition index 0');
  const fetch = evs.find((e) => e.kind === TOS.TR_FETCH);
  assert.ok(fetch, 'a TR_FETCH event');
  assert.equal(fetch.addr, 0x0100);
  assert.equal(fetch.value, 0xcd, 'opcode of CALL');
  assert.ok(fetch.step <= 1, `fetch.step = ${fetch.step}`);
  for (const e of evs) assert.ok(e.step <= 1, `event step ${e.step} after a single instruction`);
});

test('WS2-03: a machine parked on console input shows io_out_pending=1 on port 1 through the offsets and STOP=WAIT_INPUT in meta', () => {
  const L = readLayout();
  assert.equal(M._tos_create(0), 0);
  const r = runUntil(M, 200000);
  assert.equal(r.stop, KSTOP.WAIT_INPUT, `stop=${r.stop} after ${r.steps} steps`);
  assert.equal(r.state, KS.IDLE);
  const cpu = cpuView(M, L);
  assert.equal(cpu.io_out_pending, 1, 'BIOS_WAIT leaves io_out_pending set');
  assert.equal(cpu.io_out_port, 1, 'the pending OUT is on the BIOS port');
  assert.ok([TOS.BIOS_CONIN, TOS.BIOS_READLINE].includes(cpu.io_out_value), `pending fn 0x${cpu.io_out_value.toString(16)}`);
  assert.equal(cpu.io_out_value, M._tos_last_syscall(), 'io_out_value is the syscall that parked');
  assert.equal(cpu.halted, 0);
  const meta = M._tos_meta_ptr();
  assert.equal(M.HEAPU8[meta + TOS.META_STATE], KS.IDLE);
  assert.equal(M.HEAPU8[meta + TOS.META_STOP], KSTOP.WAIT_INPUT);
  assert.equal(M.HEAPU8[meta + TOS.META_SYSCALL], cpu.io_out_value);
  assert.equal(M.HEAPU8[meta + TOS.META_HALT_REASON], TOS.HALT_NONE);
  assert.ok(drain(M).endsWith('A> '), 'the prompt was printed before parking');
});

test('WS2-03: after the halt command, halted!=0 through the offset, halt reason COMMAND in tos_halt_reason and meta', () => {
  const L = readLayout();
  assert.equal(M._tos_create(0), 0);
  pushText(M, 'halt\n');
  const r = runUntil(M, 500000);
  assert.equal(r.stop, KSTOP.HALT, `stop=${r.stop} after ${r.steps} steps`);
  assert.equal(r.state, KS.HALT);
  const cpu = cpuView(M, L);
  assert.notEqual(cpu.halted, 0, 'cpu.halted through layout offset');
  assert.equal(M._tos_halt_reason(), TOS.HALT_COMMAND);
  const meta = M._tos_meta_ptr();
  assert.equal(M.HEAPU8[meta + TOS.META_STATE], KS.HALT);
  assert.equal(M.HEAPU8[meta + TOS.META_HALT_REASON], TOS.HALT_COMMAND);
  assert.equal(M.HEAPU8[meta + TOS.META_STOP], KSTOP.HALT);
  assert.ok(drain(M).includes('HALT\n'));
});

test('WS2-03: a tape fault on a 32K machine (LDA 8000H) halts after the instruction: A=0xFF, pc=0x0103, halted=0 via offsets', () => {
  const L = readLayout();
  const cp = writeConfig(M, L, { tapes: 1, tape_len: 32768, hz: 0, seed: 1, input_mode: 0, disks: 1, trace: 0, snap_interval: 1000 });
  assert.equal(M._tos_create(cp), 0);
  M._free(cp);
  const prog = new Uint8Array([0x3a, 0x00, 0x80, 0x76]); // LDA 8000H ; HLT
  const pp = cbytes(M, prog);
  assert.equal(M._tos_load_com(pp, prog.length), 0);
  M._free(pp);
  const r = runUntil(M, 1000);
  assert.equal(r.stop, KSTOP.HALT, `stop=${r.stop}`);
  assert.equal(r.state, KS.HALT);
  assert.equal(M._tos_halt_reason(), TOS.HALT_TAPE_FAULT);
  const cpu = cpuView(M, L);
  assert.equal(cpu.a, 0xff, 'out-of-range reads return 0xFF');
  assert.equal(cpu.pc, 0x0103, 'the faulting instruction finished; HLT never ran');
  assert.equal(cpu.halted, 0);
  assert.equal(M.HEAPU8[M._tos_meta_ptr() + TOS.META_HALT_REASON], TOS.HALT_TAPE_FAULT);
});

test('WS2-03: OUT 2 with A=3 on a 2-tape machine halts with BAD_TAPE: A=3, pc=0x0104 via offsets', () => {
  const L = readLayout();
  const cp = writeConfig(M, L, { tapes: 2, tape_len: 65536, hz: 0, seed: 1, input_mode: 0, disks: 1, trace: 0, snap_interval: 1000 });
  assert.equal(M._tos_create(cp), 0);
  M._free(cp);
  const prog = new Uint8Array([0x3e, 0x03, 0xd3, 0x02, 0x76]); // MVI A,3 ; OUT 2 ; HLT
  const pp = cbytes(M, prog);
  assert.equal(M._tos_load_com(pp, prog.length), 0);
  M._free(pp);
  const r = runUntil(M, 1000);
  assert.equal(r.stop, KSTOP.HALT, `stop=${r.stop}`);
  assert.equal(r.state, KS.HALT);
  assert.equal(M._tos_halt_reason(), TOS.HALT_BAD_TAPE);
  const cpu = cpuView(M, L);
  assert.equal(cpu.a, 3);
  assert.equal(cpu.pc, 0x0104, 'halted after OUT, before HLT');
  assert.equal(cpu.halted, 0);
  assert.equal(M._tos_tape_selected(), 0, 'selection unchanged by a rejected OUT 2');
  assert.equal(M.HEAPU8[M._tos_meta_ptr() + TOS.META_HALT_REASON], TOS.HALT_BAD_TAPE);
});
