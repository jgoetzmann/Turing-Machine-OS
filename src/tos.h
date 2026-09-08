#ifndef TURINGOS_TOS_H
#define TURINGOS_TOS_H
/* TuringOS v2 — frozen constants shared by every component.
 * Everything here is part of the interface freeze: do not change values. */
#include <stdint.h>

#define TOS_VERSION "2.0.0"

/* ---- Tape geometry ---------------------------------------------------- */
#define TOS_TAPE_MAX        65536u
#define TOS_TAPES_MAX       4u
#define TOS_TAPE_LEN_32K    32768u
#define TOS_TAPE_LEN_48K    49152u
#define TOS_TAPE_LEN_64K    65536u

/* Fixed low regions (identical for every tape length L) */
#define TOS_BIOS_BASE       0x0000u
#define TOS_BIOS_END        0x00FFu
#define TOS_TPA_BASE        0x0100u
#define TOS_TPA_END         0x3FFFu
#define TOS_TPA_SIZE        (TOS_TPA_END - TOS_TPA_BASE + 1u)   /* 16128 */
#define TOS_BANK_BASE       0x4000u

/* Regions anchored to the top of the tape (L = tape length in bytes) */
#define TOS_BANK_END(L)      ((uint32_t)(L) - 0x2001u)   /* per-tape window: BANK_BASE..BANK_END */
#define TOS_SCRATCH_BASE(L)  ((uint32_t)(L) - 0x2000u)   /* 4 KB common scratch */
#define TOS_SCRATCH_END(L)   ((uint32_t)(L) - 0x1001u)
#define TOS_STACK_BASE(L)    ((uint32_t)(L) - 0x1000u)
#define TOS_STACK_TOP(L)     ((uint32_t)(L) - 0x0201u)   /* initial SP (0xFDFF at 64K) */
#define TOS_DISPLAY_BASE(L)  ((uint32_t)(L) - 0x0200u)   /* 256-byte 64x32 1-bpp framebuffer */
#define TOS_DISPLAY_SIZE     256u
#define TOS_META_BASE(L)     ((uint32_t)(L) - 0x0100u)   /* 256-byte TM metadata block */
#define TOS_META_SIZE        256u
#define TOS_DMA_DEFAULT(L)   TOS_SCRATCH_BASE(L)

/* ---- Metadata block layout (offsets from TOS_META_BASE) --------------- */
#define TOS_META_STATE        0x00u  /* kernel_state_t */
#define TOS_META_STEPS        0x01u  /* u32 LE, low 32 bits of step count */
#define TOS_META_HALT_REASON  0x05u  /* TOS_HALT_* */
#define TOS_META_TAPE_SEL     0x06u  /* selected tape */
#define TOS_META_TAPE_COUNT   0x07u  /* tape count k */
#define TOS_META_TAPE_PAGES   0x08u  /* u16 LE: L / 256 (0x0100 = 64K) */
#define TOS_META_FRAME        0x0Au  /* u32 LE: VSYNC frame counter */
#define TOS_META_KEYS         0x0Eu  /* current key bitmask */
#define TOS_META_STOP         0x0Fu  /* last kernel_stop_t */
#define TOS_META_DIRTY        0x10u  /* 32 bytes: bit per 256-byte page written during the last kernel_step call */
#define TOS_META_DIRTY_BYTES  32u
#define TOS_META_SEED         0x30u
#define TOS_META_HZ           0x31u  /* u32 LE, 0 = unthrottled */
#define TOS_META_INPUT_MODE   0x35u
#define TOS_META_DISKS        0x36u
#define TOS_META_TRACE        0x37u
#define TOS_META_SYSCALL      0x38u  /* last BIOS function id dispatched */
#define TOS_META_SP_INIT      0x3Au  /* u16 LE */

/* ---- Display ---------------------------------------------------------- */
#define TOS_DISPLAY_W   64u
#define TOS_DISPLAY_H   32u
/* Row-major, 8 bytes per row, MSB of byte 0 = pixel (0,0) = top-left. */

/* ---- I/O ports (8080 IN/OUT) ------------------------------------------ */
#define TOS_PORT_BIOS   0x01u   /* OUT: BIOS call, function id in A */
#define TOS_PORT_TAPE   0x02u   /* OUT: select tape (A); IN: selected tape */
#define TOS_PORT_KEYS   0x03u   /* IN: key bitmask */
#define TOS_PORT_TAPES  0x04u   /* IN: tape count k */
#define TOS_PORT_PAGES  0x05u   /* IN: (tape length / 256) & 0xFF  (0x80=32K, 0xC0=48K, 0x00=64K) */

/* ---- BIOS function ids (A on OUT 0x01) -------------------------------- */
#define TOS_BIOS_CONIN     0x01u  /* -> A = byte; parks until input available */
#define TOS_BIOS_CONOUT    0x02u  /* C = byte */
#define TOS_BIOS_AUXOUT    0x03u  /* stub */
#define TOS_BIOS_AUXIN     0x04u  /* stub */
#define TOS_BIOS_CONST     0x05u  /* -> A = 0xFF if a console byte is ready else 0 */
#define TOS_BIOS_VSYNC     0x06u  /* park until next host frame; frame++ */
#define TOS_BIOS_RAND      0x07u  /* -> A = next PRNG byte */
#define TOS_BIOS_TICKS     0x08u  /* -> A = frame counter low byte */
#define TOS_BIOS_SELDISK   0x09u  /* C = disk 0/1 */
#define TOS_BIOS_SETTRK    0x0Au  /* C = track */
#define TOS_BIOS_SETSEC    0x0Bu  /* C = sector (1-based) */
#define TOS_BIOS_SETDMA    0x0Cu  /* DE = address */
#define TOS_BIOS_READ      0x0Du  /* -> A = 0 ok / 1 error */
#define TOS_BIOS_WRITE     0x0Eu  /* -> A = 0 ok / 1 error */
#define TOS_BIOS_LISTDIR   0x0Fu  /* prints directory, one name per line, or "(empty)" */
#define TOS_BIOS_NAMECH    0x12u  /* C = append char to name buffer */
#define TOS_BIOS_TYPE      0x13u  /* print file named by buffer */
#define TOS_BIOS_RUN       0x14u  /* load .com named by buffer into TPA, run */
#define TOS_BIOS_DEL       0x15u  /* delete file named by buffer */
#define TOS_BIOS_CC        0x16u  /* compile NAME.C -> NAME.COM (tiny-C) */
#define TOS_BIOS_READLINE  0x17u  /* read a line (parks until newline) */
#define TOS_BIOS_LINEGET   0x18u  /* C = index -> A = byte of line (0 past end) */
#define TOS_BIOS_LINELEN   0x19u  /* -> A = line length */
#define TOS_BIOS_ASM       0x1Au  /* assemble NAME.ASM -> NAME.COM */
#define TOS_BIOS_TM        0x1Bu  /* compile NAME.TM -> NAME.COM */
#define TOS_BIOS_BF        0x1Cu  /* compile NAME.BF -> NAME.COM */

/* ---- Halt reasons (TOS_META_HALT_REASON) ------------------------------ */
#define TOS_HALT_NONE        0u
#define TOS_HALT_HLT         1u   /* HLT executed by a program outside the shell (never halts the machine; see spec) */
#define TOS_HALT_COMMAND     2u   /* HLT executed while in SHELL state (the `halt` command) */
#define TOS_HALT_EOF         3u   /* console input reached EOF */
#define TOS_HALT_TAPE_FAULT  4u   /* access at or beyond the tape length */
#define TOS_HALT_BREAKPOINT  5u   /* informational: last stop was a breakpoint (machine not halted) */
#define TOS_HALT_BAD_TAPE    6u   /* OUT 0x02 with a tape index >= tape count */

/* ---- Key bitmask (IN 0x03 / TOS_META_KEYS) ---------------------------- */
#define TOS_KEY_W       0x01u
#define TOS_KEY_S       0x02u
#define TOS_KEY_UP      0x04u
#define TOS_KEY_DOWN    0x08u
#define TOS_KEY_SPACE   0x10u
#define TOS_KEY_ESC     0x20u
#define TOS_KEY_ENTER   0x40u
#define TOS_KEY_ANY     0x80u

/* ---- Levers ------------------------------------------------------------ */
#define TOS_LEVER_TAPES          0   /* 1 | 2 | 4            machine lever (reset) */
#define TOS_LEVER_TAPE_LEN       1   /* 32768|49152|65536    machine lever (reset) */
#define TOS_LEVER_HZ             2   /* 0 = unthrottled      view lever */
#define TOS_LEVER_SEED           3   /* 0..255               machine lever (reset) */
#define TOS_LEVER_INPUT_MODE     4   /* TOS_INPUT_*          view lever */
#define TOS_LEVER_DISKS          5   /* 1 | 2                machine lever (reset) */
#define TOS_LEVER_TRACE          6   /* 0 | 1                view lever */
#define TOS_LEVER_SNAP_INTERVAL  7   /* steps between snapshots, view lever */
#define TOS_LEVER_COUNT          8
#define TOS_INPUT_CONSOLE  0
#define TOS_INPUT_KEYS     1

/* ---- Disk geometry ----------------------------------------------------- */
#define TOS_DISK_TRACKS        77u
#define TOS_DISK_SECTORS       26u
#define TOS_DISK_SECTOR_BYTES  256u
#define TOS_DISK_IMAGE_BYTES   (TOS_DISK_TRACKS * TOS_DISK_SECTORS * TOS_DISK_SECTOR_BYTES)  /* 512512 */
#define TOS_DISK_DIR_ENTRIES   64u
#define TOS_DISK_DIR_ENTRY     32u
#define TOS_DISKS_MAX          2u

/* ---- Compiler / language tool ids (tos_compile) ----------------------- */
#define TOS_LANG_C    0
#define TOS_LANG_ASM  1
#define TOS_LANG_TM   2
#define TOS_LANG_BF   3

#endif
