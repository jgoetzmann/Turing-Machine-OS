#!/usr/bin/env python3

import sys
from pathlib import Path

import tape_bridge

WINDOW_WIDTH = 1280
WINDOW_HEIGHT = 800
WINDOW_TITLE = "TuringOS Tape Visualizer"
BACKGROUND_COLOR = (0, 0, 0)
TAPE_CELLS_W = 256
TAPE_CELLS_H = 256
CELL_W = 4
CELL_H = 3
MAP_X = 8
MAP_Y = 16
TAPE_SIZE = 65536
PAGE_COUNT = 256
DIRTY_MAP_BYTES = 32
DETAIL_X = MAP_X + (TAPE_CELLS_W * CELL_W) + 16
DETAIL_Y = MAP_Y
DETAIL_WIDTH = WINDOW_WIDTH - DETAIL_X - 8
DETAIL_HEIGHT = TAPE_CELLS_H * CELL_H
LEGEND_X = MAP_X
LEGEND_Y = MAP_Y + (TAPE_CELLS_H * CELL_H) + 8
LEGEND_WIDTH = WINDOW_WIDTH - 16
LEGEND_HEIGHT = WINDOW_HEIGHT - LEGEND_Y - 8
DETAIL_TEXT_COLOR = (128, 255, 128)
DETAIL_HEADER_COLOR = (192, 255, 192)
SPLASH_TEXT_COLOR = (160, 255, 160)
PC_HIGHLIGHT_COLOR = (255, 255, 0)
DIRTY_FLASH_COLOR = (180, 255, 180)
BLINK_MS = 500
DIRTY_DECAY_MS = 450
SNAPSHOT_PREFIX = "snapshot_"
SNAPSHOT_SUFFIX = ".bin"
POLL_MS = 100
TARGET_FPS = 30

STATE_NAMES = {
    0: "BOOT",
    1: "IDLE",
    2: "SHELL",
    3: "RUNNING",
    4: "SYSCALL",
    5: "HALT",
}

REGION_LEGEND = [
    ("BIOS", (0, 255, 96)),
    ("TPA", (80, 255, 80)),
    ("KERNEL", (0, 128, 64)),
    ("SHELL", (0, 180, 160)),
    ("FS CACHE", (0, 96, 96)),
    ("STACK", (255, 176, 64)),
    ("I/O", (255, 96, 32)),
    ("TM META", (255, 255, 255)),
    ("PC", PC_HIGHLIGHT_COLOR),
]


def _region_color(addr: int) -> tuple:
    if 0x0000 <= addr <= 0x00FF:
        return (0, 255, 96)  # BIOS: bright green
    if 0x0100 <= addr <= 0x3FFF:
        return (80, 255, 80)  # TPA: lime green
    if 0x4000 <= addr <= 0x7FFF:
        return (0, 128, 64)  # Kernel heap: dim green
    if 0x8000 <= addr <= 0xBFFF:
        return (0, 180, 160)  # Shell workspace: teal
    if 0xC000 <= addr <= 0xEFFF:
        return (0, 96, 96)  # FS cache: dark cyan
    if 0xF000 <= addr <= 0xFDFF:
        return (255, 176, 64)  # Stack: amber
    if 0xFE00 <= addr <= 0xFEFF:
        return (255, 96, 32)  # I/O: red-orange
    return (255, 255, 255)  # TM metadata: bright white


def _blend_color(base: tuple, flash: tuple, alpha: float) -> tuple:
    if alpha <= 0.0:
        return base
    if alpha >= 1.0:
        return flash
    return (
        int(base[0] + (flash[0] - base[0]) * alpha),
        int(base[1] + (flash[1] - base[1]) * alpha),
        int(base[2] + (flash[2] - base[2]) * alpha),
    )


def _update_dirty_decay(dirty_decay_ms: list, dt_ms: int, meta: dict) -> None:
    for i in range(PAGE_COUNT):
        dirty_decay_ms[i] = max(0, dirty_decay_ms[i] - dt_ms)

    if meta is None:
        return
    dirty_map = meta.get("dirty_map")
    if not isinstance(dirty_map, (bytes, bytearray)) or len(dirty_map) < DIRTY_MAP_BYTES:
        return

    for byte_i in range(DIRTY_MAP_BYTES):
        mask = dirty_map[byte_i]
        if mask == 0:
            continue
        for bit in range(8):
            if (mask >> bit) & 1:
                page = byte_i * 8 + bit
                if page < PAGE_COUNT:
                    dirty_decay_ms[page] = DIRTY_DECAY_MS


def draw_tape_map(screen, tape: bytes, pc: int, blink_on: bool, dirty_decay_ms: list, pygame) -> None:
    for addr in range(TAPE_SIZE):
        x = addr & 0xFF
        y = addr >> 8
        px = MAP_X + (x * CELL_W)
        py = MAP_Y + (y * CELL_H)
        color = _region_color(addr)
        page = addr >> 8
        if dirty_decay_ms[page] > 0:
            alpha = dirty_decay_ms[page] / float(DIRTY_DECAY_MS)
            color = _blend_color(color, DIRTY_FLASH_COLOR, alpha)
        if blink_on and addr == pc:
            color = PC_HIGHLIGHT_COLOR
        pygame.draw.rect(screen, color, (px, py, CELL_W, CELL_H))


def _ascii_char(v: int) -> str:
    if 32 <= v <= 126:
        return chr(v)
    return "."


def draw_detail_panel(screen, tape: bytes, selected_page: int, font, pygame) -> None:
    panel_rect = pygame.Rect(DETAIL_X, DETAIL_Y, DETAIL_WIDTH, DETAIL_HEIGHT)
    pygame.draw.rect(screen, (8, 24, 8), panel_rect)
    pygame.draw.rect(screen, (32, 96, 32), panel_rect, 1)

    base_addr = (selected_page & 0xFF) << 8
    header = f"PAGE 0x{selected_page:02X} (0x{base_addr:04X}-0x{base_addr+0xFF:04X})"
    header_surf = font.render(header, True, DETAIL_HEADER_COLOR)
    screen.blit(header_surf, (DETAIL_X + 6, DETAIL_Y + 6))

    for row in range(16):
        row_addr = base_addr + (row * 16)
        row_bytes = tape[row_addr : row_addr + 16]
        hex_part = " ".join(f"{b:02X}" for b in row_bytes)
        ascii_part = "".join(_ascii_char(b) for b in row_bytes)
        line = f"{row_addr:04X}: {hex_part}  {ascii_part}"
        line_surf = font.render(line, True, DETAIL_TEXT_COLOR)
        screen.blit(line_surf, (DETAIL_X + 6, DETAIL_Y + 24 + row * 13))


def draw_legend_bar(screen, pc: int, font, pygame) -> None:
    legend_rect = pygame.Rect(LEGEND_X, LEGEND_Y, LEGEND_WIDTH, LEGEND_HEIGHT)
    pygame.draw.rect(screen, (8, 24, 8), legend_rect)
    pygame.draw.rect(screen, (32, 96, 32), legend_rect, 1)

    x = LEGEND_X + 8
    y = LEGEND_Y + 8
    for name, color in REGION_LEGEND:
        swatch = pygame.Rect(x, y + 2, 12, 12)
        pygame.draw.rect(screen, color, swatch)
        pygame.draw.rect(screen, (16, 48, 16), swatch, 1)
        label = font.render(name, True, DETAIL_TEXT_COLOR)
        screen.blit(label, (x + 16, y))
        x += 16 + label.get_width() + 16

    head_text = font.render(f"HEAD=0x{pc & 0xFFFF:04X}", True, DETAIL_HEADER_COLOR)
    screen.blit(head_text, (LEGEND_X + LEGEND_WIDTH - head_text.get_width() - 10, y))


def draw_status_bar(screen, meta: dict, font, pygame) -> None:
    state = 0
    steps = 0
    if isinstance(meta, dict):
        state = int(meta.get("state", 0))
        steps = int(meta.get("steps", 0))
    state_name = STATE_NAMES.get(state, f"UNKNOWN({state})")
    status_text = f"STATE: {state_name}    steps: {steps}"
    status_surf = font.render(status_text, True, DETAIL_HEADER_COLOR)
    screen.blit(status_surf, (MAP_X, 2))


def _next_snapshot_path() -> Path:
    idx = 0
    while True:
        candidate = Path(f"{SNAPSHOT_PREFIX}{idx:04d}{SNAPSHOT_SUFFIX}")
        if not candidate.exists():
            return candidate
        idx += 1


def draw_waiting_splash(screen, font, pygame) -> None:
    line1 = font.render("WAITING FOR TAPE...", True, SPLASH_TEXT_COLOR)
    line2 = font.render("Start TuringOS to produce /tmp snapshots.", True, DETAIL_TEXT_COLOR)
    cx = WINDOW_WIDTH // 2
    cy = WINDOW_HEIGHT // 2
    screen.blit(line1, (cx - line1.get_width() // 2, cy - 16))
    screen.blit(line2, (cx - line2.get_width() // 2, cy + 8))


def _page_from_click(pos: tuple) -> int:
    mx, my = pos
    if mx < MAP_X or my < MAP_Y:
        return -1
    rel_x = mx - MAP_X
    rel_y = my - MAP_Y
    max_w = TAPE_CELLS_W * CELL_W
    max_h = TAPE_CELLS_H * CELL_H
    if rel_x >= max_w or rel_y >= max_h:
        return -1
    return (rel_y // CELL_H) & 0xFF


def main() -> int:
    try:
        import pygame
    except ImportError:
        print("pygame is not installed; run in Docker or install pygame in viz/.venv.")
        return 1

    pygame.init()
    try:
        screen = pygame.display.set_mode((WINDOW_WIDTH, WINDOW_HEIGHT))
        pygame.display.set_caption(WINDOW_TITLE)
        clock = pygame.time.Clock()
        running = True
        blank_tape = bytes(TAPE_SIZE)
        dirty_decay_ms = [0] * PAGE_COUNT
        last_ticks = pygame.time.get_ticks()
        detail_font = pygame.font.SysFont("Courier", 12)
        paused = False
        tape = blank_tape
        meta = None
        pc = 0
        selected_page_override = None
        poll_accum_ms = 0

        while running:
            for event in pygame.event.get():
                if event.type == pygame.QUIT:
                    running = False
                elif event.type == pygame.MOUSEBUTTONDOWN and event.button == 1:
                    clicked_page = _page_from_click(event.pos)
                    if clicked_page >= 0:
                        selected_page_override = clicked_page
                elif event.type == pygame.KEYDOWN:
                    if event.key == pygame.K_p:
                        paused = not paused
                    elif event.key == pygame.K_r:
                        tape_bridge.close()
                        selected_page_override = None
                    elif event.key == pygame.K_s:
                        snapshot = _next_snapshot_path()
                        snapshot.write_bytes(bytes(tape))

            screen.fill(BACKGROUND_COLOR)
            now_ticks = pygame.time.get_ticks()
            dt_ms = max(0, now_ticks - last_ticks)
            last_ticks = now_ticks
            if not paused:
                poll_accum_ms += dt_ms
            should_poll = (not paused) and (poll_accum_ms >= POLL_MS or tape is blank_tape)
            if should_poll:
                poll_accum_ms = 0
                tape_cur = tape_bridge.get_tape()
                if tape_cur is not None and len(tape_cur) >= TAPE_SIZE:
                    tape = tape_cur
                # Keep the previous frame when snapshot files are mid-write.
                # This avoids visible flicker between valid frames.
                meta_cur = tape_bridge.get_meta()
                if meta_cur is not None:
                    meta = meta_cur
            _update_dirty_decay(dirty_decay_ms, dt_ms, meta)
            if should_poll:
                pc_cur = tape_bridge.get_pc()
                if pc_cur is not None:
                    pc = pc_cur
            has_tape = tape is not blank_tape
            if has_tape:
                if selected_page_override is None:
                    selected_page = (pc >> 8) & 0xFF
                else:
                    selected_page = selected_page_override
                blink_on = (now_ticks // BLINK_MS) % 2 == 0
                draw_tape_map(screen, tape, pc & 0xFFFF, blink_on, dirty_decay_ms, pygame)
                draw_detail_panel(screen, tape, selected_page, detail_font, pygame)
                draw_legend_bar(screen, pc, detail_font, pygame)
                draw_status_bar(screen, meta, detail_font, pygame)
            else:
                draw_waiting_splash(screen, detail_font, pygame)
            pygame.display.flip()
            clock.tick(TARGET_FPS)
    finally:
        tape_bridge.close()
        pygame.quit()

    return 0


if __name__ == "__main__":
    sys.exit(main())
