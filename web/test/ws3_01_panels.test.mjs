// WS3-01: each panel name in S9 has a module web/src/panels/<name>.ts exporting create<Name>Panel.
// TypeScript is not importable here, so this reads the panels directory listing and greps the
// source text for the export, which S9 explicitly allows.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, readdirSync, existsSync, statSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const TEST_DIR = dirname(fileURLToPath(import.meta.url));
const WEB_DIR = join(TEST_DIR, '..');
const PANELS_DIR = join(WEB_DIR, 'src', 'panels');
const BUS_TS = join(WEB_DIR, 'src', 'bus.ts');

const PANEL_NAMES = ['tapemap', 'strip', 'detail', 'cpu', 'fsm', 'display', 'console', 'disk', 'tapes', 'stats', 'timeline', 'levers', 'editor'];
const exportName = (name) => 'create' + name[0].toUpperCase() + name.slice(1) + 'Panel';
function exportsFunction(text, fn) {
  return [
    new RegExp(`export\\s+(?:async\\s+)?function\\s+${fn}\\s*[(<]`),
    new RegExp(`export\\s+const\\s+${fn}\\s*[=:]`),
    new RegExp(`export\\s*\\{[^}]*\\b${fn}\\b[^}]*\\}`),
  ].some((re) => re.test(text));
}
function panelFiles() {
  if (!existsSync(PANELS_DIR)) return [];
  return readdirSync(PANELS_DIR).filter((f) => f.endsWith('.ts'));
}

test('WS3-01: web/src/panels/<name>.ts exists for all 13 panel names', () => {
  assert.ok(existsSync(PANELS_DIR), `${PANELS_DIR} missing`);
  const present = panelFiles();
  const missing = PANEL_NAMES.filter((n) => !present.includes(n + '.ts'));
  assert.deepEqual(missing, [], `missing panel modules: ${missing.join(', ')} (have: ${present.join(', ')})`);
  for (const n of PANEL_NAMES) assert.ok(statSync(join(PANELS_DIR, n + '.ts')).size > 0, `${n}.ts is empty`);
});

test('WS3-01: every panel module exports create<Name>Panel', () => {
  const bad = [];
  for (const n of PANEL_NAMES) {
    const file = join(PANELS_DIR, n + '.ts');
    if (!existsSync(file)) { bad.push(`${n}.ts (missing)`); continue; }
    if (!exportsFunction(readFileSync(file, 'utf8'), exportName(n))) bad.push(`${n}.ts lacks export ${exportName(n)}`);
  }
  assert.deepEqual(bad, []);
});

test('WS3-01: web/src/bus.ts exists (the Bus every create<Name>Panel receives) and exports a Bus', () => {
  assert.ok(existsSync(BUS_TS), `${BUS_TS} missing`);
  const text = readFileSync(BUS_TS, 'utf8');
  assert.match(text, /\bexport\b/, 'bus.ts exports something');
  assert.match(text, /\bBus\b/, 'bus.ts defines Bus');
});

test('WS3-01: no panel module pulls in an out-of-scope editor library, WebGL, a Worker or SharedArrayBuffer', () => {
  const offenders = [];
  for (const f of panelFiles()) {
    const text = readFileSync(join(PANELS_DIR, f), 'utf8');
    for (const [label, re] of [
      ['CodeMirror', /codemirror|@codemirror|monaco-editor|ace-builds/i],
      ['WebGL', /getContext\(\s*['"]webgl/],
      ['Worker', /new\s+(?:Shared)?Worker\s*\(/],
      ['SharedArrayBuffer', /\bSharedArrayBuffer\b/],
      ['service worker', /serviceWorker\.register/],
    ]) if (re.test(text)) offenders.push(`${f}: ${label}`);
  }
  assert.deepEqual(offenders, [], `out-of-scope usage: ${offenders.join('; ')}`);
});

test('WS3-01: the editor panel is a <textarea>, not an editor library', () => {
  const file = join(PANELS_DIR, 'editor.ts');
  assert.ok(existsSync(file), `${file} missing`);
  const text = readFileSync(file, 'utf8');
  assert.match(text, /textarea/i, 'editor.ts must create/use a <textarea>');
  assert.doesNotMatch(text, /codemirror|monaco/i);
});
