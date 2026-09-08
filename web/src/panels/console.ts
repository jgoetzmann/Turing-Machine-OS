/**
 * console — a small terminal over the machine's console. Handles CR, LF, BS, TAB,
 * ESC[2J (clear) and ESC[H (home) (plus ESC[K), takes keyboard input and paste,
 * and pushes bytes with `engine.conPush`. Every push also emits `console-input {text}`
 * so the run loop can leave WAIT_INPUT without polling.
 */
import type { Engine } from '../engine';
import type { Bus } from '../bus';
import type { Panel } from './panel';
import { basePanel, injectStyle, el, escapeHtml, setText } from './util';
export type { Panel } from './panel';

const COLS = 64;
const ROWS = 24;
const SCROLLBACK = 400;

const CSS = `
.tos-console{display:flex;flex-direction:column;min-height:120px}
.tos-console-screen{flex:1;background:#010409;color:#3fb950;border:1px solid #30363d;padding:4px 6px;overflow:auto;outline:none;min-height:${ROWS * 1.3}em;max-height:${ROWS * 1.35}em;cursor:text;line-height:1.3}
.tos-console-screen:focus{border-color:#388bfd}
.tos-console-screen .cur{background:#3fb950;color:#010409}
.tos-console-screen .cur.off{background:transparent;color:#3fb950}
.tos-console-line{display:flex;gap:6px;margin-top:6px}
.tos-console-line input[type=text]{flex:1}
`;

class Terminal {
  lines: string[] = [''];
  row = 0;
  col = 0;
  private esc = false;
  private escBuf = '';
  dirty = true;

  private ensureRow(): void {
    while (this.lines.length <= this.row) this.lines.push('');
    if (this.lines.length > SCROLLBACK) {
      const drop = this.lines.length - SCROLLBACK;
      this.lines.splice(0, drop);
      this.row = Math.max(0, this.row - drop);
    }
  }

  private newline(): void {
    this.row++;
    this.col = 0;
    this.ensureRow();
  }

  private putChar(ch: string): void {
    this.ensureRow();
    let line = this.lines[this.row];
    if (line.length < this.col) line = line.padEnd(this.col, ' ');
    line = line.slice(0, this.col) + ch + line.slice(this.col + 1);
    this.lines[this.row] = line;
    this.col++;
    if (this.col >= COLS) this.newline();
  }

  private csi(seq: string): void {
    const final = seq[seq.length - 1];
    const params = seq.slice(0, -1);
    if (final === 'J') {
      if (params === '' || params === '2' || params === '3') {
        this.lines = [''];
        this.row = 0;
        this.col = 0;
      } else if (params === '0') {
        this.lines.length = this.row + 1;
        this.lines[this.row] = this.lines[this.row].slice(0, this.col);
      }
    } else if (final === 'H' || final === 'f') {
      const parts = params.split(';');
      const r = Math.max(1, parseInt(parts[0] || '1', 10) || 1) - 1;
      const c = Math.max(1, parseInt(parts[1] || '1', 10) || 1) - 1;
      const top = Math.max(0, this.lines.length - ROWS);
      this.row = Math.min(top + r, top + ROWS - 1);
      this.col = Math.min(c, COLS - 1);
      this.ensureRow();
    } else if (final === 'K') {
      this.ensureRow();
      this.lines[this.row] = this.lines[this.row].slice(0, this.col);
    }
  }

  feed(text: string): void {
    for (let i = 0; i < text.length; i++) {
      const code = text.charCodeAt(i) & 0xff;
      const ch = text[i];
      if (this.esc) {
        if (this.escBuf === '' && ch !== '[') {
          this.esc = false; // ESC + something we do not interpret
          continue;
        }
        this.escBuf += ch;
        if (this.escBuf.length > 1 && code >= 0x40 && code <= 0x7e) {
          this.csi(this.escBuf.slice(1));
          this.esc = false;
          this.escBuf = '';
        } else if (this.escBuf.length > 16) {
          this.esc = false;
          this.escBuf = '';
        }
        continue;
      }
      switch (code) {
        case 27:
          this.esc = true;
          this.escBuf = '';
          break;
        case 10:
          this.newline();
          break;
        case 13:
          this.col = 0;
          break;
        case 8:
        case 127:
          if (this.col > 0) {
            this.col--;
            this.ensureRow();
            const line = this.lines[this.row];
            this.lines[this.row] = line.slice(0, this.col) + (line.length > this.col + 1 ? ' ' + line.slice(this.col + 1) : '');
          }
          break;
        case 9: {
          const next = Math.min(COLS - 1, (this.col + 8) & ~7);
          while (this.col < next) this.putChar(' ');
          break;
        }
        case 12:
          this.lines = [''];
          this.row = 0;
          this.col = 0;
          break;
        case 7:
          break;
        default:
          if (code >= 32) this.putChar(ch);
      }
    }
    this.dirty = true;
  }

  clear(): void {
    this.lines = [''];
    this.row = 0;
    this.col = 0;
    this.esc = false;
    this.escBuf = '';
    this.dirty = true;
  }

  html(cursorOn: boolean): string {
    let out = '';
    for (let i = 0; i < this.lines.length; i++) {
      const line = this.lines[i];
      if (i === this.row) {
        const before = line.slice(0, this.col);
        const at = line.length > this.col ? line[this.col] : ' ';
        const after = line.length > this.col + 1 ? line.slice(this.col + 1) : '';
        out += `${escapeHtml(before)}<span class="cur${cursorOn ? '' : ' off'}">${escapeHtml(at)}</span>${escapeHtml(after)}\n`;
      } else {
        out += `${escapeHtml(line)}\n`;
      }
    }
    return out;
  }
}

export function createConsolePanel(root: HTMLElement, engine: Engine, bus: Bus): Panel {
  basePanel(root, 'tos-console');
  injectStyle('tos-console-style', CSS);

  const bar = el('div', 'tos-bar');
  const echoLbl = el('label');
  const echoCb = el('input');
  echoCb.type = 'checkbox';
  echoCb.checked = true;
  echoLbl.append(echoCb, 'local echo');
  const clearBtn = el('button', '', 'clear');
  const status = el('span', 'tos-muted', '');
  bar.append(echoLbl, clearBtn, status);

  const screen = el('pre', 'tos-console-screen');
  screen.tabIndex = 0;
  screen.setAttribute('role', 'textbox');
  screen.setAttribute('aria-label', 'console');

  const lineRow = el('div', 'tos-console-line');
  const lineInput = el('input');
  lineInput.type = 'text';
  lineInput.placeholder = 'type a line and press Enter (or click the screen and type)';
  lineInput.spellcheck = false;
  const sendBtn = el('button', '', 'send');
  lineRow.append(lineInput, sendBtn);

  root.append(bar, screen, lineRow);

  const term = new Terminal();
  let pushed = 0;
  let received = 0;
  let cursorOn = true;
  let lastCursor = true;

  function push(text: string): void {
    if (!text) return;
    engine.conPush(text);
    pushed += text.length;
    if (echoCb.checked) term.feed(text.replace(/\r\n/g, '\n'));
    bus.emit('console-input', { text });
  }

  const onKey = (ev: KeyboardEvent): void => {
    if (ev.metaKey || (ev.ctrlKey && ev.key.length !== 1)) return;
    let out = '';
    if (ev.ctrlKey && ev.key.length === 1) {
      const c = ev.key.toUpperCase().charCodeAt(0);
      if (c >= 64 && c <= 95) out = String.fromCharCode(c - 64);
      else return;
    } else {
      switch (ev.key) {
        case 'Enter':
          out = '\n';
          break;
        case 'Backspace':
          out = '\b';
          break;
        case 'Tab':
          out = '\t';
          break;
        case 'Escape':
          out = '\x1b';
          break;
        case 'ArrowUp':
          out = '\x1b[A';
          break;
        case 'ArrowDown':
          out = '\x1b[B';
          break;
        case 'ArrowRight':
          out = '\x1b[C';
          break;
        case 'ArrowLeft':
          out = '\x1b[D';
          break;
        default:
          if (ev.key.length === 1) out = ev.key;
          else return;
      }
    }
    ev.preventDefault();
    ev.stopPropagation();
    push(out);
  };
  const onPaste = (ev: ClipboardEvent): void => {
    const text = ev.clipboardData ? ev.clipboardData.getData('text') : '';
    if (!text) return;
    ev.preventDefault();
    ev.stopPropagation();
    push(text.replace(/\r\n/g, '\n').replace(/\r/g, '\n'));
  };
  const onScreenClick = (): void => {
    screen.focus();
  };
  const onSend = (): void => {
    push(lineInput.value + '\n');
    lineInput.value = '';
  };
  const onLineKey = (ev: KeyboardEvent): void => {
    if (ev.key === 'Enter') {
      ev.preventDefault();
      ev.stopPropagation();
      onSend();
    } else {
      ev.stopPropagation();
    }
  };
  const onLinePaste = (ev: Event): void => {
    ev.stopPropagation();
  };
  const onClear = (): void => {
    term.clear();
  };

  screen.addEventListener('keydown', onKey);
  screen.addEventListener('paste', onPaste);
  screen.addEventListener('click', onScreenClick);
  sendBtn.addEventListener('click', onSend);
  lineInput.addEventListener('keydown', onLineKey);
  lineInput.addEventListener('paste', onLinePaste);
  clearBtn.addEventListener('click', onClear);

  const offs: Array<() => void> = [];
  offs.push(
    bus.on('machine-reset', () => {
      term.clear();
      pushed = 0;
      received = 0;
    }),
  );
  offs.push(
    bus.on('console-write', (p) => {
      if (p && typeof p.text === 'string') term.feed(p.text);
    }),
  );

  function render(nowMs: number): void {
    let out = '';
    try {
      out = engine.conRead();
    } catch {
      out = '';
    }
    if (out) {
      received += out.length;
      term.feed(out);
    }
    cursorOn = Math.floor(nowMs / 500) % 2 === 0;
    if (term.dirty || cursorOn !== lastCursor) {
      const atBottom = screen.scrollHeight - screen.scrollTop - screen.clientHeight < 24;
      screen.innerHTML = term.html(cursorOn);
      if (term.dirty || atBottom) screen.scrollTop = screen.scrollHeight;
      term.dirty = false;
      lastCursor = cursorOn;
    }
    const st = engine.stateName();
    setText(status, `${st === 'IDLE' ? 'waiting for input' : st.toLowerCase()} · in ${pushed} · out ${received}`);
  }

  return {
    update(nowMs: number): void {
      render(nowMs);
    },
    destroy(): void {
      for (const f of offs) f();
      screen.removeEventListener('keydown', onKey);
      screen.removeEventListener('paste', onPaste);
      screen.removeEventListener('click', onScreenClick);
      sendBtn.removeEventListener('click', onSend);
      lineInput.removeEventListener('keydown', onLineKey);
      lineInput.removeEventListener('paste', onLinePaste);
      clearBtn.removeEventListener('click', onClear);
      root.replaceChildren();
      root.classList.remove('tos-panel', 'tos-console');
    },
  };
}
