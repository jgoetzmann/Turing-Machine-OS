/**
 * editor — a <textarea> with a line-number gutter, language select (C/ASM/TM/BF),
 * "Save to disk" (`diskPutFile`), "Compile & run" (`engine.compile` → `engine.loadCom`),
 * and an inline error line that also lights the offending gutter line.
 */
import type { Engine } from '../engine';
import type { Bus } from '../bus';
import type { Panel } from './panel';
import { basePanel, injectStyle, el, fmtInt, latin1Encode, latin1Decode, setText } from './util';
export type { Panel } from './panel';

const CSS = `
.tos-editor{display:flex;flex-direction:column;min-height:200px}
.tos-editor-body{flex:1;display:grid;grid-template-columns:auto 1fr;border:1px solid #30363d;background:#010409;min-height:160px;overflow:hidden}
.tos-editor-gutter{margin:0;padding:6px 6px 6px 8px;color:#484f58;text-align:right;user-select:none;overflow:hidden;border-right:1px solid #21262d;line-height:1.45;font-size:12px}
.tos-editor-gutter .err{color:#fff;background:#b62324;border-radius:2px}
.tos-editor textarea{width:100%;height:100%;min-height:160px;resize:vertical;border:0;border-radius:0;padding:6px 8px;background:transparent;color:#e6edf3;line-height:1.45;font-size:12px;white-space:pre;overflow:auto;tab-size:4;outline:none}
.tos-editor-status{margin-top:6px;min-height:1.4em;white-space:pre-wrap}
.tos-editor input[type=text]{width:12ch;text-transform:uppercase}
`;

const LANGS: Array<{ id: 0 | 1 | 2 | 3; label: string; ext: string; sample: string }> = [
  {
    id: 0,
    label: 'tiny-C',
    ext: 'C',
    sample: 'int main() {\n    puts("Hello, TuringOS!");\n    return 0;\n}\n',
  },
  {
    id: 1,
    label: '8080 asm',
    ext: 'ASM',
    sample: '        ORG 0100H\n        LXI H,MSG\nLOOP:   MOV A,M\n        ORA A\n        JZ DONE\n        MOV C,A\n        MVI A,2      ; CONOUT\n        OUT 1\n        INX H\n        JMP LOOP\nDONE:   HLT\nMSG:    DB \'HELLO FROM ASM\',10,0\n',
  },
  {
    id: 2,
    label: 'Turing machine',
    ext: 'TM',
    sample: '# busy beaver, 2 states\nblank: _\nstart: a\na _ -> 1 R b\na 1 -> 1 L b\nb _ -> 1 L a\nb 1 -> 1 R halt\n',
  },
  {
    id: 3,
    label: 'Brainfuck',
    ext: 'BF',
    sample: '++++++++[>++++[>++>+++>+++>+<<<<-]>+>+>->>+[<]<-]>>.>---.+++++++..+++.>>.<-.<.+++.------.--------.>>+.>++.\n',
  },
];

export function createEditorPanel(root: HTMLElement, engine: Engine, bus: Bus): Panel {
  basePanel(root, 'tos-editor');
  injectStyle('tos-editor-style', CSS);

  const bar = el('div', 'tos-bar');
  const langSel = el('select');
  for (const l of LANGS) {
    const o = el('option', '', l.label);
    o.value = String(l.id);
    langSel.append(o);
  }
  const nameInput = el('input');
  nameInput.type = 'text';
  nameInput.value = 'PROG';
  nameInput.maxLength = 8;
  nameInput.spellcheck = false;
  nameInput.title = 'file name (8 chars, extension follows the language)';
  const diskSel = el('select');
  for (const d of [0, 1]) {
    const o = el('option', '', d === 0 ? 'A:' : 'B:');
    o.value = String(d);
    diskSel.append(o);
  }
  const loadBtn = el('button', '', 'load from disk');
  const saveBtn = el('button', '', 'save to disk');
  const compileBtn = el('button', '', 'compile');
  const runBtn = el('button', '', 'compile & run');
  const sampleBtn = el('button', '', 'sample');
  bar.append(langSel, nameInput, diskSel, loadBtn, saveBtn, compileBtn, runBtn, sampleBtn);

  const body = el('div', 'tos-editor-body');
  const gutter = el('pre', 'tos-editor-gutter');
  const ta = el('textarea');
  ta.spellcheck = false;
  ta.wrap = 'off';
  ta.setAttribute('autocapitalize', 'off');
  ta.setAttribute('autocorrect', 'off');
  body.append(gutter, ta);
  const status = el('div', 'tos-editor-status tos-muted');
  root.append(bar, body, status);

  let errLine = -1;
  let lastLineCount = -1;
  let lastErrLine = -2;

  function lang(): { id: 0 | 1 | 2 | 3; label: string; ext: string; sample: string } {
    const id = parseInt(langSel.value, 10);
    return LANGS.find((l) => l.id === id) || LANGS[0];
  }

  function fileName(): string {
    let base = nameInput.value.toUpperCase().replace(/[^A-Z0-9]/g, '');
    if (!base) base = 'PROG';
    base = base.slice(0, 8);
    nameInput.value = base;
    return `${base}.${lang().ext}`;
  }

  function setStatus(text: string, cls = 'tos-muted'): void {
    status.className = `tos-editor-status ${cls}`;
    setText(status, text);
  }

  function renderGutter(): void {
    const n = Math.max(1, ta.value.split('\n').length);
    if (n === lastLineCount && errLine === lastErrLine) return;
    let html = '';
    for (let i = 1; i <= n; i++) html += i === errLine ? `<span class="err">${i}</span>\n` : `${i}\n`;
    gutter.innerHTML = html;
    lastLineCount = n;
    lastErrLine = errLine;
    gutter.scrollTop = ta.scrollTop;
  }

  function parseError(msg: string): number {
    const m1 = /^(?:[\w.]+:)?(\d+):(?:\d+:)?\s*/.exec(msg);
    if (m1) return parseInt(m1[1], 10);
    const m2 = /line\s+(\d+)/i.exec(msg);
    if (m2) return parseInt(m2[1], 10);
    return -1;
  }

  function compile(): Uint8Array | null {
    errLine = -1;
    const src = ta.value;
    let res: { ok: true; bytes: Uint8Array } | { ok: false; error: string };
    try {
      res = engine.compile(lang().id, src);
    } catch (e) {
      res = { ok: false, error: String(e) };
    }
    if (!res.ok) {
      errLine = parseError(res.error);
      setStatus(`error: ${res.error}`, 'tos-err');
      renderGutter();
      if (errLine > 0) {
        const lines = ta.value.split('\n');
        let pos = 0;
        for (let i = 0; i < errLine - 1 && i < lines.length; i++) pos += lines[i].length + 1;
        try {
          ta.focus();
          ta.setSelectionRange(pos, pos + (lines[errLine - 1] || '').length);
        } catch {
          /* selection is best-effort */
        }
      }
      return null;
    }
    renderGutter();
    setStatus(`compiled ${fileName()} → ${fmtInt(res.bytes.length)} bytes`, 'tos-ok');
    return res.bytes;
  }

  const onCompile = (): void => {
    compile();
  };
  const onRun = (): void => {
    const bytes = compile();
    if (!bytes) return;
    let ok = false;
    try {
      ok = engine.loadCom(bytes);
    } catch {
      ok = false;
    }
    if (!ok) {
      setStatus(`compiled ${fmtInt(bytes.length)} bytes but loadCom failed (too large for the TPA, or the machine is halted: reset it)`, 'tos-err');
      return;
    }
    setStatus(`running ${fileName()} (${fmtInt(bytes.length)} bytes)`, 'tos-ok');
    bus.emit('program-loaded', { name: fileName(), bytes: bytes.length });
    bus.emit('run-request', { running: true });   /* `run-state` is the app's notification, not a request */
  };
  const onSave = (): void => {
    const disk = parseInt(diskSel.value, 10) || 0;
    const disks = engine.config ? engine.config.disks : 1;
    if (disk >= disks) {
      setStatus('disk B is not mounted (disks lever = 1)', 'tos-err');
      return;
    }
    const name = fileName();
    const bytes = latin1Encode(ta.value.replace(/\r\n/g, '\n'));
    let ok = false;
    try {
      ok = engine.diskPutFile(disk, name, bytes);
    } catch {
      ok = false;
    }
    if (ok) {
      setStatus(`saved ${name} (${fmtInt(bytes.length)} bytes) to disk ${disk}`, 'tos-ok');
      bus.emit('disk-changed', { disk, name });
    } else setStatus(`could not save ${name} to disk ${disk}`, 'tos-err');
  };
  const onLoad = (): void => {
    const disk = parseInt(diskSel.value, 10) || 0;
    const name = fileName();
    let bytes: Uint8Array | null = null;
    try {
      bytes = engine.diskGetFile(disk, name);
    } catch {
      bytes = null;
    }
    if (!bytes) {
      setStatus(`${name} not found on disk ${disk}`, 'tos-err');
      return;
    }
    ta.value = latin1Decode(bytes);
    errLine = -1;
    renderGutter();
    setStatus(`loaded ${name} (${fmtInt(bytes.length)} bytes)`, 'tos-ok');
  };
  const onSample = (): void => {
    ta.value = lang().sample;
    errLine = -1;
    renderGutter();
    setStatus(`sample ${lang().label} program`);
  };
  const onLang = (): void => {
    errLine = -1;
    renderGutter();
    setStatus(`${lang().label} → ${fileName()}`);
  };
  const onInput = (): void => {
    if (errLine >= 0) errLine = -1;
    renderGutter();
  };
  const onScroll = (): void => {
    gutter.scrollTop = ta.scrollTop;
  };
  const onKey = (ev: KeyboardEvent): void => {
    ev.stopPropagation();
    if (ev.key === 'Tab') {
      ev.preventDefault();
      const s = ta.selectionStart;
      const e = ta.selectionEnd;
      ta.value = ta.value.slice(0, s) + '    ' + ta.value.slice(e);
      ta.selectionStart = ta.selectionEnd = s + 4;
      renderGutter();
    } else if ((ev.ctrlKey || ev.metaKey) && ev.key === 'Enter') {
      ev.preventDefault();
      onRun();
    } else if ((ev.ctrlKey || ev.metaKey) && (ev.key === 's' || ev.key === 'S')) {
      ev.preventDefault();
      onSave();
    }
  };
  const stop = (ev: Event): void => ev.stopPropagation();

  langSel.addEventListener('change', onLang);
  loadBtn.addEventListener('click', onLoad);
  saveBtn.addEventListener('click', onSave);
  compileBtn.addEventListener('click', onCompile);
  runBtn.addEventListener('click', onRun);
  sampleBtn.addEventListener('click', onSample);
  ta.addEventListener('input', onInput);
  ta.addEventListener('scroll', onScroll);
  ta.addEventListener('keydown', onKey);
  ta.addEventListener('keyup', stop);
  nameInput.addEventListener('keydown', stop);
  nameInput.addEventListener('keyup', stop);

  const offs: Array<() => void> = [];
  offs.push(
    bus.on('editor-load', (p) => {
      if (!p || typeof p.text !== 'string') return;
      ta.value = p.text;
      if (typeof p.name === 'string') {
        const dot = p.name.lastIndexOf('.');
        const base = (dot > 0 ? p.name.slice(0, dot) : p.name).toUpperCase();
        const ext = dot > 0 ? p.name.slice(dot + 1).toUpperCase() : '';
        nameInput.value = base.replace(/[^A-Z0-9]/g, '').slice(0, 8) || 'PROG';
        const byExt = LANGS.find((l) => l.ext === ext);
        if (byExt) langSel.value = String(byExt.id);
      }
      if (typeof p.lang === 'number') langSel.value = String(p.lang);
      errLine = -1;
      renderGutter();
      setStatus(`loaded ${fileName()} (${fmtInt(ta.value.length)} chars)`);
    }),
  );
  offs.push(
    bus.on('editor-run', () => {
      onRun();
    }),
  );

  ta.value = LANGS[0].sample;
  renderGutter();
  setStatus('Ctrl+Enter compiles & runs · Ctrl+S saves to disk');

  return {
    update(): void {
      const d = engine.config ? engine.config.disks : 1;
      const opt = diskSel.options[1];
      if (opt) opt.disabled = d < 2;
    },
    destroy(): void {
      for (const f of offs) f();
      langSel.removeEventListener('change', onLang);
      loadBtn.removeEventListener('click', onLoad);
      saveBtn.removeEventListener('click', onSave);
      compileBtn.removeEventListener('click', onCompile);
      runBtn.removeEventListener('click', onRun);
      sampleBtn.removeEventListener('click', onSample);
      ta.removeEventListener('input', onInput);
      ta.removeEventListener('scroll', onScroll);
      ta.removeEventListener('keydown', onKey);
      ta.removeEventListener('keyup', stop);
      nameInput.removeEventListener('keydown', stop);
      nameInput.removeEventListener('keyup', stop);
      root.replaceChildren();
      root.classList.remove('tos-panel', 'tos-editor');
    },
  };
}
