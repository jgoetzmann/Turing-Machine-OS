/**
 * disk — track/sector occupancy grid, directory listing, file viewer (text or hex),
 * upload (`diskPutFile`), image load (`loadDiskImage`) and downloads.
 */
import type { Engine } from '../engine';
import type { Bus } from '../bus';
import type { Panel } from './panel';
import {
  basePanel,
  injectStyle,
  el,
  ctx2d,
  hex2,
  hex4,
  fmtInt,
  escapeHtml,
  isPrintable,
  latin1Decode,
  setText,
  tosConst,
  fsName,
  triggerDownload,
} from './util';
export type { Panel } from './panel';

const CSS = `
.tos-disk-body{display:grid;grid-template-columns:auto 1fr;gap:8px}
.tos-disk-grid{width:208px;height:auto;background:#010409;border:1px solid #30363d}
.tos-disk-side{min-width:0}
.tos-disk-files{list-style:none;margin:0;padding:0;max-height:140px;overflow:auto;border:1px solid #30363d;background:#010409}
.tos-disk-files li{padding:1px 6px;cursor:pointer;display:flex;justify-content:space-between;gap:8px}
.tos-disk-files li:hover{background:#21262d}
.tos-disk-files li.sel{background:#1f3a5f;color:#fff}
.tos-disk-files li.empty{color:#8b949e;cursor:default}
.tos-disk-view{margin-top:6px;max-height:200px;overflow:auto;border:1px solid #30363d;background:#010409;padding:4px;font-size:11px;white-space:pre-wrap;word-break:break-all}
.tos-disk-legend{margin-top:4px;color:#8b949e;font-size:11px}
.tos-disk-legend i{display:inline-block;width:10px;height:10px;margin:0 3px 0 8px;vertical-align:-1px}
.tos-disk input[type=file]{display:none}
`;

const CELL_W = 8;
const CELL_H = 4;

export function createDiskPanel(root: HTMLElement, engine: Engine, bus: Bus): Panel {
  basePanel(root, 'tos-disk');
  injectStyle('tos-disk-style', CSS);

  const TRACKS = tosConst(engine, 'TOS_DISK_TRACKS', 77);
  const SECTORS = tosConst(engine, 'TOS_DISK_SECTORS', 26);
  const SECTOR_BYTES = tosConst(engine, 'TOS_DISK_SECTOR_BYTES', 256);
  const IMAGE_BYTES = tosConst(engine, 'TOS_DISK_IMAGE_BYTES', TRACKS * SECTORS * SECTOR_BYTES);
  const DIR_BYTES = tosConst(engine, 'TOS_DISK_DIR_ENTRIES', 64) * tosConst(engine, 'TOS_DISK_DIR_ENTRY', 32);
  const DIR_SECTORS = Math.ceil(DIR_BYTES / SECTOR_BYTES);

  const bar = el('div', 'tos-bar');
  const diskSel = el('select');
  for (const d of [0, 1]) {
    const o = el('option', '', d === 0 ? 'disk A (0)' : 'disk B (1)');
    o.value = String(d);
    diskSel.append(o);
  }
  const addBtn = el('button', '', 'add file…');
  const addInput = el('input');
  addInput.type = 'file';
  const imgBtn = el('button', '', 'load image…');
  const imgInput = el('input');
  imgInput.type = 'file';
  imgInput.accept = '.img,application/octet-stream';
  const dlImgBtn = el('button', '', 'download image');
  const dlFileBtn = el('button', '', 'download file');
  dlFileBtn.disabled = true;
  const status = el('span', 'tos-muted', '');
  bar.append(diskSel, addBtn, addInput, imgBtn, imgInput, dlImgBtn, dlFileBtn, status);

  const body = el('div', 'tos-disk-body');
  const grid = el('canvas', 'tos-disk-grid');
  grid.width = SECTORS * CELL_W;
  grid.height = TRACKS * CELL_H;
  const side = el('div', 'tos-disk-side');
  const files = el('ul', 'tos-disk-files');
  const view = el('pre', 'tos-disk-view');
  view.hidden = true;
  side.append(files, view);
  body.append(grid, side);
  const legend = el('div', 'tos-disk-legend');
  legend.innerHTML =
    `${TRACKS}×${SECTORS} sectors · <i style="background:#1f6feb"></i>directory <i style="background:#3fb950"></i>data <i style="background:#161b22"></i>empty`;
  root.append(bar, body, legend);

  const ctx = ctx2d(grid);
  let disk = 0;
  let selected = '';
  let fileNames: string[] = [];
  let lastScan = -1e9;
  let lastList = -1e9;
  let listKey = '';

  function diskCount(): number {
    const c = engine.config ? engine.config.disks : 1;
    return c === 2 ? 2 : 1;
  }

  function setStatus(text: string, cls = 'tos-muted'): void {
    status.className = cls;
    setText(status, text);
  }

  function scanGrid(): void {
    let img: Uint8Array;
    try {
      img = engine.diskPtr(disk);
    } catch {
      return;
    }
    ctx.fillStyle = '#010409';
    ctx.fillRect(0, 0, grid.width, grid.height);
    let used = 0;
    for (let t = 0; t < TRACKS; t++) {
      for (let s = 0; s < SECTORS; s++) {
        const idx = t * SECTORS + s;
        const off = idx * SECTOR_BYTES;
        if (off + SECTOR_BYTES > img.length) break;
        let nonzero = false;
        let e5 = true;
        for (let i = 0; i < SECTOR_BYTES; i++) {
          const b = img[off + i];
          if (b !== 0) nonzero = true;
          if (b !== 0xe5) e5 = false;
          if (nonzero && !e5) break;
        }
        let color = '#161b22';
        if (idx < DIR_SECTORS) color = nonzero && !e5 ? '#1f6feb' : '#12305e';
        else if (nonzero) {
          color = '#3fb950';
          used++;
        }
        ctx.fillStyle = color;
        ctx.fillRect(s * CELL_W, t * CELL_H, CELL_W - 1, CELL_H - 1);
      }
    }
    setStatus(`${fmtInt(used)} data sectors used of ${fmtInt(TRACKS * SECTORS - DIR_SECTORS)}`);
  }

  function refreshList(force: boolean): void {
    let names: string[];
    try {
      names = engine.diskList(disk) || [];
    } catch {
      names = [];
    }
    const key = names.join('\n');
    if (!force && key === listKey) return;
    listKey = key;
    fileNames = names;
    files.replaceChildren();
    if (names.length === 0) {
      files.append(el('li', 'empty', '(empty)'));
    }
    for (const n of names) {
      const li = el('li');
      li.dataset.name = n;
      let size = -1;
      try {
        const b = engine.diskGetFile(disk, n);
        size = b ? b.length : -1;
      } catch {
        size = -1;
      }
      li.append(el('span', '', n), el('span', 'tos-muted', size >= 0 ? fmtInt(size) : '?'));
      if (n === selected) li.classList.add('sel');
      files.append(li);
    }
    if (selected && !names.includes(selected)) {
      selected = '';
      view.hidden = true;
      dlFileBtn.disabled = true;
    }
  }

  function showFile(name: string): void {
    selected = name;
    dlFileBtn.disabled = false;
    for (const li of Array.from(files.children)) li.classList.toggle('sel', (li as HTMLElement).dataset.name === name);
    let bytes: Uint8Array | null = null;
    try {
      bytes = engine.diskGetFile(disk, name);
    } catch {
      bytes = null;
    }
    view.hidden = false;
    if (!bytes) {
      view.textContent = `(cannot read ${name})`;
      return;
    }
    const limit = Math.min(bytes.length, 16384);
    let printable = 0;
    for (let i = 0; i < limit; i++) {
      const b = bytes[i];
      if (isPrintable(b) || b === 10 || b === 13 || b === 9) printable++;
    }
    const head = `${name}  ${fmtInt(bytes.length)} bytes${bytes.length > limit ? ' (first 16 KB shown)' : ''}\n`;
    if (limit > 0 && printable / limit > 0.9) {
      view.innerHTML = `<span class="tos-muted">${escapeHtml(head)}</span>${escapeHtml(latin1Decode(bytes.subarray(0, limit)))}`;
    } else {
      let dump = '';
      for (let o = 0; o < limit; o += 16) {
        let hx = '';
        let asc = '';
        for (let i = 0; i < 16; i++) {
          if (o + i < limit) {
            const b = bytes[o + i];
            hx += hex2(b) + ' ';
            asc += isPrintable(b) ? String.fromCharCode(b) : '.';
          } else hx += '   ';
        }
        dump += `${hex4(o)}  ${hx} |${asc}|\n`;
      }
      view.innerHTML = `<span class="tos-muted">${escapeHtml(head)}</span>${escapeHtml(dump)}`;
    }
  }

  function readFile(file: File): Promise<Uint8Array> {
    return file.arrayBuffer().then((b) => new Uint8Array(b));
  }

  const onDisk = (): void => {
    disk = parseInt(diskSel.value, 10) || 0;
    if (disk >= diskCount()) {
      disk = 0;
      diskSel.value = '0';
      setStatus('disk B is not mounted (disks lever = 1)', 'tos-warn');
    }
    selected = '';
    view.hidden = true;
    dlFileBtn.disabled = true;
    refreshList(true);
    scanGrid();
  };
  const onFilesClick = (ev: MouseEvent): void => {
    const t = ev.target as HTMLElement | null;
    const li = t && t.closest ? (t.closest('li') as HTMLElement | null) : null;
    if (!li || !li.dataset.name) return;
    showFile(li.dataset.name);
  };
  const onAdd = (): void => addInput.click();
  const onAddChange = (): void => {
    const f = addInput.files && addInput.files[0];
    if (!f) return;
    readFile(f)
      .then((bytes) => {
        const name = fsName(f.name);
        const ok = engine.diskPutFile(disk, name, bytes);
        if (ok) {
          setStatus(`stored ${name} (${fmtInt(bytes.length)} bytes)`, 'tos-ok');
          refreshList(true);
          scanGrid();
          showFile(name);
          bus.emit('disk-changed', { disk, name });
        } else setStatus(`could not store ${name}`, 'tos-err');
      })
      .catch((e) => setStatus(`read failed: ${String(e)}`, 'tos-err'))
      .finally(() => {
        addInput.value = '';
      });
  };
  const onImg = (): void => imgInput.click();
  const onImgChange = (): void => {
    const f = imgInput.files && imgInput.files[0];
    if (!f) return;
    readFile(f)
      .then((bytes) => {
        if (bytes.length !== IMAGE_BYTES) {
          setStatus(`image must be ${fmtInt(IMAGE_BYTES)} bytes (got ${fmtInt(bytes.length)})`, 'tos-err');
          return;
        }
        engine.loadDiskImage(disk, bytes);
        setStatus(`loaded image into disk ${disk}`, 'tos-ok');
        selected = '';
        view.hidden = true;
        refreshList(true);
        scanGrid();
        bus.emit('disk-changed', { disk });
      })
      .catch((e) => setStatus(`read failed: ${String(e)}`, 'tos-err'))
      .finally(() => {
        imgInput.value = '';
      });
  };
  const onDlImg = (): void => {
    try {
      triggerDownload(engine.diskPtr(disk), `disk${disk === 0 ? 'a' : 'b'}.img`);
    } catch (e) {
      setStatus(`download failed: ${String(e)}`, 'tos-err');
    }
  };
  const onDlFile = (): void => {
    if (!selected) return;
    const b = engine.diskGetFile(disk, selected);
    if (b) triggerDownload(b, selected);
  };

  diskSel.addEventListener('change', onDisk);
  files.addEventListener('click', onFilesClick);
  addBtn.addEventListener('click', onAdd);
  addInput.addEventListener('change', onAddChange);
  imgBtn.addEventListener('click', onImg);
  imgInput.addEventListener('change', onImgChange);
  dlImgBtn.addEventListener('click', onDlImg);
  dlFileBtn.addEventListener('click', onDlFile);

  const offs: Array<() => void> = [];
  offs.push(
    bus.on('machine-reset', () => {
      lastScan = -1e9;
      lastList = -1e9;
      listKey = '';
    }),
  );
  offs.push(
    bus.on('disk-changed', (p) => {
      if (!p || p.disk === disk) {
        lastScan = -1e9;
        lastList = -1e9;
        listKey = '';
      }
    }),
  );

  refreshList(true);
  scanGrid();

  return {
    update(nowMs: number): void {
      const opt = diskSel.options[1];
      if (opt) opt.disabled = diskCount() < 2;
      if (nowMs - lastList >= 500) {
        refreshList(false);
        lastList = nowMs;
      }
      if (nowMs - lastScan >= 300) {
        scanGrid();
        lastScan = nowMs;
      }
    },
    destroy(): void {
      for (const f of offs) f();
      diskSel.removeEventListener('change', onDisk);
      files.removeEventListener('click', onFilesClick);
      addBtn.removeEventListener('click', onAdd);
      addInput.removeEventListener('change', onAddChange);
      imgBtn.removeEventListener('click', onImg);
      imgInput.removeEventListener('change', onImgChange);
      dlImgBtn.removeEventListener('click', onDlImg);
      dlFileBtn.removeEventListener('click', onDlFile);
      root.replaceChildren();
      root.classList.remove('tos-panel', 'tos-disk');
    },
  };
}
