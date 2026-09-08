// WS3-02 … WS3-06: the pure helpers behind the playground's controls, URL state and disassembly
// view. The panels themselves need a browser (cypress/e2e covers those); these are the functions
// the browser code is built out of, and they run here with no DOM at all.
import test from 'node:test';
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { dirname, join } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const WEB_DIR = join(dirname(fileURLToPath(import.meta.url)), '..');
const SRC = join(WEB_DIR, 'src');

/** Call exported functions of a TypeScript module in a child node that strips the types. */
function callTs(tsPath, calls) {
  const code = [
    `import * as m from ${JSON.stringify(pathToFileURL(tsPath).href)};`,
    `const calls = ${JSON.stringify(calls)};`,
    `const out = calls.map(([fn, ...args]) => {`,
    `  try { return { ok: true, value: m[fn](...args) }; }`,
    `  catch (e) { return { ok: false, error: String(e && e.message || e) }; }`,
    `});`,
    `process.stdout.write(JSON.stringify(out));`,
  ].join('\n');
  const out = execFileSync(process.execPath,
    ['--experimental-strip-types', '--no-warnings', '--input-type=module', '-e', code],
    { encoding: 'utf8', cwd: WEB_DIR, maxBuffer: 64 * 1024 * 1024 });
  return JSON.parse(out);
}

const values = (tsFile, calls) => callTs(join(SRC, tsFile), calls).map((r) => {
  assert.ok(r.ok, `call threw: ${r.error}`);
  return r.value;
});

test('WS3-02: parseAddress reads hex by default and decimal only with a d suffix', () => {
  const [a, b, c, d, e, f, g, h] = values('disasmview.ts', [
    ['parseAddress', '0100'], ['parseAddress', '0x100'], ['parseAddress', '100h'],
    ['parseAddress', '$100'], ['parseAddress', '256d'], ['parseAddress', ' ff '],
    ['parseAddress', 'zzz'], ['parseAddress', '70000d'],
  ]);
  assert.equal(a, 0x0100);
  assert.equal(b, 0x0100);
  assert.equal(c, 0x0100);
  assert.equal(d, 0x0100);
  assert.equal(e, 256);          // the only decimal form
  assert.equal(f, 0x00ff);
  assert.equal(g, null);
  assert.equal(h, null);         // out of a 16-bit address space
});

test('WS3-02: hex8/hex16 pad and mask to the address width', () => {
  const [a, b, c] = values('disasmview.ts', [['hex8', 5], ['hex16', 0x1f], ['hex16', 0x1ffff]]);
  assert.equal(a, '05');
  assert.equal(b, '001F');
  assert.equal(c, 'FFFF');
});

test('WS3-02: opcodeLen matches the 8080 table, aliases included', () => {
  const [nop, lxi, jmp, mvi, cbAlias, ddAlias] = values('disasmview.ts', [
    ['opcodeLen', 0x00], ['opcodeLen', 0x01], ['opcodeLen', 0xc3], ['opcodeLen', 0x3e],
    ['opcodeLen', 0xcb], ['opcodeLen', 0xdd],
  ]);
  assert.equal(nop, 1);
  assert.equal(lxi, 3);
  assert.equal(jmp, 3);
  assert.equal(mvi, 2);
  assert.equal(cbAlias, 3);      // CB acts as JMP
  assert.equal(ddAlias, 3);      // DD acts as CALL
});

test('WS3-04: the speed slider round-trips and reports itself', () => {
  const [top, bottom, fmtMax, fmtOne, parsed, parsedMax, bad] = values('speed.ts', [
    ['sliderToSpeed', 1000], ['sliderToSpeed', 0],
    ['formatSpeed', 'max'], ['formatSpeed', 1],
    ['parseSpeed', '1000'], ['parseSpeed', 'max'], ['parseSpeed', 'quick'],
  ]);
  assert.equal(top, 'max');
  assert.equal(bottom, 1);
  assert.match(String(fmtMax), /max/i);
  assert.ok(String(fmtOne).length > 0);
  assert.equal(parsed, 1000);
  assert.equal(parsedMax, 'max');
  assert.equal(bad, null);

  // every slider position maps to a speed that maps back to the same position
  const positions = [0, 1, 250, 500, 750, 999, 1000];
  const speeds = values('speed.ts', positions.map((p) => ['sliderToSpeed', p]));
  const back = values('speed.ts', speeds.map((s) => ['speedToSlider', s]));
  for (let i = 0; i < positions.length; i++) {
    assert.ok(Math.abs(back[i] - positions[i]) <= 1,
      `slider ${positions[i]} -> ${speeds[i]} -> ${back[i]}`);
  }
});

test('WS3-04: stepsForFrame scales with the frame time and is unbounded at max', () => {
  const [slow, twice, atMax] = values('speed.ts', [
    ['stepsForFrame', 1000, 16], ['stepsForFrame', 1000, 32], ['stepsForFrame', 'max', 16],
  ]);
  assert.ok(twice >= slow, `${twice} >= ${slow}`);
  assert.ok(atMax > slow, 'max speed steps more than 1000/s');
});

test('WS3-05: the URL hash round-trips levers, demo, speed and breakpoints', () => {
  const state = {
    demo: 'pong', tapes: 4, len: 32768, hz: 2000000, seed: 7,
    input: 'keys', disks: 2, trace: false, snap: 250, speed: 5000,
    bp: [{ kind: 0, lo: 0x0100, hi: 0x0100 }, { kind: 2, lo: 0x4000, hi: 0x40ff }],
  };
  const [hash] = values('urlstate.ts', [['formatHash', state]]);
  assert.ok(hash.startsWith('#/playground'), hash);
  const [back] = values('urlstate.ts', [['parseHash', hash]]);
  for (const k of ['demo', 'tapes', 'len', 'hz', 'seed', 'input', 'disks', 'trace', 'snap', 'speed']) {
    assert.deepEqual(back[k], state[k], `${k} survived the round trip (${hash})`);
  }
  assert.deepEqual(back.bp, state.bp);
});

test('WS3-05: a hash with junk in it falls back to the defaults instead of throwing', () => {
  const [def, junk, partial] = values('urlstate.ts', [
    ['defaultState'],
    ['parseHash', '#/playground?tapes=3&len=999&seed=nope&input=telepathy&speed=&bp=nonsense'],
    ['parseHash', '#/playground?tapes=2'],
  ]);
  assert.deepEqual(junk, def, 'every unusable value is ignored');
  assert.equal(partial.tapes, 2);
  assert.equal(partial.len, def.len);
});

test('WS3-03: breakpoints in the URL are decimal and survive both directions', () => {
  const [text] = values('urlstate.ts', [['formatBreakpoints', [{ kind: 0, lo: 256, hi: 256 }]]]);
  assert.equal(text, 'pc:256:256', 'the URL form is decimal');
  const [bps] = values('urlstate.ts', [['parseBreakpoints', 'pc:256:256,write:16384:16639']]);
  assert.deepEqual(bps, [{ kind: 0, lo: 256, hi: 256 }, { kind: 2, lo: 16384, hi: 16639 }]);
  const [bad] = values('urlstate.ts', [['parseBreakpoints', 'wat:1:2,pc:notanumber']]);
  assert.deepEqual(bad, []);
});

test('WS3-06: stateToConfig hands the machine levers to the engine unchanged', () => {
  const [cfg] = values('urlstate.ts', [['stateToConfig', {
    demo: null, tapes: 2, len: 49152, hz: 0, seed: 3,
    input: 'keys', disks: 2, trace: true, snap: 500, speed: 'max', bp: [],
  }]]);
  assert.equal(cfg.tapes, 2);
  assert.equal(cfg.tapeLen, 49152);
  assert.equal(cfg.seed, 3);
  assert.equal(cfg.disks, 2);
  assert.equal(cfg.snapInterval, 500);
});
