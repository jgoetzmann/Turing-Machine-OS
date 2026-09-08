// WS8-02: CONTENT (web/src/generated/content.ts, produced by scripts/build-content.mjs) contains
// slugs architecture, decisions, levers, tiny-c, asm, tm, languages, turing-machint, status and
// every demos/<name>. The generated .ts is loaded in a child node process with type stripping.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { readFileSync, readdirSync, existsSync } from 'node:fs';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { dirname, join } from 'node:path';

const TEST_DIR = dirname(fileURLToPath(import.meta.url));
const WEB_DIR = join(TEST_DIR, '..');
const REPO_DIR = join(WEB_DIR, '..');
const GEN_DIR = join(WEB_DIR, 'src', 'generated');
const DOCS_DIR = join(REPO_DIR, 'docs');
const DEMOS_DIR = join(REPO_DIR, 'demos');
const BUILD_CONTENT = join(WEB_DIR, 'scripts', 'build-content.mjs');

const DOC_SLUGS = ['architecture', 'decisions', 'levers', 'tiny-c', 'asm', 'tm', 'languages', 'turing-machine', 'status'];
const REQUIRED_DEMOS = ['hello', 'pong', 'life', 'fault', 'tm', 'bf', 'asm', 'forth'];

function loadTsExports(tsPath, names) {
  const code = [
    `import * as m from ${JSON.stringify(pathToFileURL(tsPath).href)};`,
    `const pick = ${JSON.stringify(names)};`,
    `process.stdout.write(JSON.stringify(Object.fromEntries(pick.map((n) => [n, n in m ? m[n] : null]))));`,
  ].join('\n');
  const out = execFileSync(process.execPath,
    ['--experimental-strip-types', '--no-warnings', '--input-type=module', '-e', code],
    { encoding: 'utf8', cwd: WEB_DIR, maxBuffer: 256 * 1024 * 1024 });
  return JSON.parse(out);
}
let generated;
function getGenerated() {
  if (generated) return generated;
  const contentTs = join(GEN_DIR, 'content.ts');
  if (!existsSync(contentTs)) {
    assert.ok(existsSync(BUILD_CONTENT), `${contentTs} missing and ${BUILD_CONTENT} does not exist to produce it`);
    execFileSync(process.execPath, [BUILD_CONTENT], { cwd: WEB_DIR, stdio: 'pipe' });
  }
  assert.ok(existsSync(contentTs), `${contentTs} still missing after scripts/build-content.mjs`);
  const content = loadTsExports(contentTs, ['CONTENT']).CONTENT;
  assert.ok(content && typeof content === 'object', 'content.ts must export CONTENT');
  const tours = existsSync(join(GEN_DIR, 'tours.ts')) ? loadTsExports(join(GEN_DIR, 'tours.ts'), ['TOURS']).TOURS : null;
  const demoFiles = existsSync(join(GEN_DIR, 'demos.ts')) ? loadTsExports(join(GEN_DIR, 'demos.ts'), ['DEMO_FILES']).DEMO_FILES : null;
  generated = { content, tours, demoFiles };
  return generated;
}
function firstHeading(mdPath) {
  const m = readFileSync(mdPath, 'utf8').match(/^# (.+?)\s*$/m);
  return m ? m[1].trim() : null;
}
function demoDirs() {
  if (!existsSync(DEMOS_DIR)) return [];
  return readdirSync(DEMOS_DIR, { withFileTypes: true })
    .filter((d) => d.isDirectory() && existsSync(join(DEMOS_DIR, d.name, 'README.md')))
    .map((d) => d.name).sort();
}
const isExcludedDemoFile = (f) => f === 'README.md' || f === 'tour.json' || f.endsWith('.expected');

test('WS8-02: CONTENT has every docs slug with a non-empty title equal to the first # heading and rendered html', () => {
  const { content } = getGenerated();
  for (const slug of DOC_SLUGS) {
    const entry = content[slug];
    assert.ok(entry, `CONTENT[${JSON.stringify(slug)}] missing (keys: ${Object.keys(content).join(', ')})`);
    assert.equal(typeof entry.title, 'string'); assert.ok(entry.title.length > 0, `${slug} title`);
    assert.equal(typeof entry.html, 'string'); assert.ok(entry.html.length > 0, `${slug} html`);
    const md = join(DOCS_DIR, slug + '.md');
    assert.ok(existsSync(md), `${md} missing`);
    assert.equal(entry.title, firstHeading(md), `${slug}: title is the first "# " heading`);
    assert.match(entry.html, /<h1[\s>]/, `${slug}: html contains the rendered heading`);
  }
});

test('WS8-02: CONTENT has demos/<name> for every demos/<name>/README.md, titled by its first heading', () => {
  const { content } = getGenerated();
  const dirs = demoDirs();
  for (const d of REQUIRED_DEMOS) assert.ok(dirs.includes(d), `demos/${d}/README.md missing`);
  for (const d of dirs) {
    const entry = content['demos/' + d];
    assert.ok(entry, `CONTENT["demos/${d}"] missing`);
    assert.equal(entry.title, firstHeading(join(DEMOS_DIR, d, 'README.md')));
    assert.ok(entry.html.length > 0);
  }
});

test('WS8-02: TOURS mirrors every demos/<name>/tour.json and DEMO_FILES carries every demo source verbatim', () => {
  const { tours, demoFiles } = getGenerated();
  assert.ok(tours && typeof tours === 'object', 'tours.ts must export TOURS');
  assert.ok(demoFiles && typeof demoFiles === 'object', 'demos.ts must export DEMO_FILES');
  for (const d of demoDirs()) {
    const tourPath = join(DEMOS_DIR, d, 'tour.json');
    if (existsSync(tourPath)) {
      const expected = JSON.parse(readFileSync(tourPath, 'utf8'));
      assert.deepEqual(tours[d], expected, `TOURS[${d}]`);
      assert.ok(Array.isArray(tours[d]));
      for (const step of tours[d]) {
        assert.equal(typeof step.panel, 'string'); assert.ok(step.panel.length > 0);
        assert.equal(typeof step.text, 'string'); assert.ok(step.text.length > 0);
      }
    }
    const files = readdirSync(join(DEMOS_DIR, d)).filter((f) => !isExcludedDemoFile(f)).sort();
    assert.ok(Array.isArray(demoFiles[d]), `DEMO_FILES[${d}]`);
    assert.deepEqual(demoFiles[d].map((f) => f.name).sort(), files, `DEMO_FILES[${d}] names`);
    for (const f of demoFiles[d]) assert.equal(f.text, readFileSync(join(DEMOS_DIR, d, f.name), 'utf8'), `DEMO_FILES[${d}]/${f.name} text`);
  }
});

test('WS8-02: slugs with no source document are absent from CONTENT', () => {
  const { content } = getGenerated();
  for (const slug of ['demos/no-such-demo', 'no-such-doc', '', 'demos', 'demos/']) {
    assert.equal(content[slug], undefined, `CONTENT[${JSON.stringify(slug)}] should not exist`);
  }
  // A bare demo name may legitimately exist when docs/<name>.md is also a document (e.g. asm, tm).
  for (const d of demoDirs()) if (!existsSync(join(DOCS_DIR, `${d}.md`))) assert.equal(content[d], undefined, `demo "${d}" must be keyed as demos/${d}, not bare`);
});

test('WS8-02: no CONTENT key keeps a .md suffix or a leading #, and no entry is empty', () => {
  const { content } = getGenerated();
  const keys = Object.keys(content);
  assert.ok(keys.length >= DOC_SLUGS.length + demoDirs().length);
  for (const k of keys) {
    assert.ok(!k.endsWith('.md'), `slug ${k} keeps its .md extension`);
    assert.ok(!k.startsWith('/') && !k.includes('..'), `slug ${k} looks like a path`);
    const e = content[k];
    assert.ok(e && typeof e.title === 'string' && e.title.length > 0, `${k}: empty title`);
    assert.ok(!e.title.startsWith('#'), `${k}: title still carries the markdown marker`);
    assert.ok(typeof e.html === 'string' && e.html.trim().length > 0, `${k}: empty html`);
    assert.ok(!/^\s*# /.test(e.html), `${k}: html is raw markdown, not rendered`);
  }
});

test('WS8-02: DEMO_FILES excludes README.md, tour.json and *.expected for every demo', () => {
  const { demoFiles } = getGenerated();
  assert.ok(demoFiles && typeof demoFiles === 'object');
  for (const [d, files] of Object.entries(demoFiles)) {
    const leaked = files.map((f) => f.name).filter(isExcludedDemoFile);
    assert.deepEqual(leaked, [], `DEMO_FILES[${d}] leaks excluded files: ${leaked.join(', ')}`);
    assert.ok(files.length > 0, `DEMO_FILES[${d}] has no sources`);
  }
});
