#ifndef TURINGOS_API_H
#define TURINGOS_API_H
/* The single embedding API: used by main.c, the C tests, and (exported) the WebAssembly build.
 * One global machine. All pointers returned are stable for the life of the process. */
#include "../tos.h"
#include "../kernel/kernel.h"
#include "../kernel/trace.h"
#include <stdint.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define TOS_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define TOS_EXPORT
#endif

typedef kernel_config_t tos_config_t;

TOS_EXPORT int       tos_create(const tos_config_t *cfg);   /* cfg NULL = defaults; (re)creates the machine; 0 ok */
TOS_EXPORT void      tos_reset(void);
TOS_EXPORT uint32_t  tos_step(uint32_t max_steps);          /* returns steps run; tos_stop_reason() tells why it stopped */
TOS_EXPORT int       tos_stop_reason(void);                 /* kernel_stop_t */
TOS_EXPORT int       tos_state(void);
TOS_EXPORT uint32_t  tos_steps(void);                       /* low 32 bits */
TOS_EXPORT uint32_t  tos_cycles_lo(void);
TOS_EXPORT uint32_t  tos_cycles_hi(void);
TOS_EXPORT uint8_t   tos_halt_reason(void);
TOS_EXPORT uint32_t  tos_frame(void);
TOS_EXPORT uint8_t   tos_last_syscall(void);

/* Tape access (live pointers into the machine) */
TOS_EXPORT uint8_t  *tos_tape_ptr(uint8_t tape);
TOS_EXPORT uint8_t   tos_tape_count(void);
TOS_EXPORT uint32_t  tos_tape_len(void);
TOS_EXPORT uint8_t   tos_tape_selected(void);
TOS_EXPORT uint8_t  *tos_cpu_ptr(void);                     /* cpu_t; offsets in layout.json */
TOS_EXPORT uint8_t  *tos_meta_ptr(void);                    /* tape 0 + TOS_META_BASE(L) */
TOS_EXPORT uint32_t *tos_write_age_ptr(uint8_t tape);
TOS_EXPORT uint32_t *tos_read_age_ptr(uint8_t tape);
TOS_EXPORT uint32_t  tos_travel_lo(void);
TOS_EXPORT uint32_t  tos_travel_hi(void);
TOS_EXPORT uint32_t  tos_accesses_lo(void);
TOS_EXPORT uint32_t  tos_cells_written(void);

/* Trace */
TOS_EXPORT const trace_event_t *tos_trace_ptr(void);
TOS_EXPORT uint32_t  tos_trace_head(void);
TOS_EXPORT uint32_t  tos_trace_count(void);
TOS_EXPORT void      tos_trace_enable(int on);

/* Console & keys */
TOS_EXPORT void      tos_con_push(uint8_t ch);              /* also recorded in the input log for replay */
TOS_EXPORT int       tos_con_pop(void);                     /* next output byte or -1 */
TOS_EXPORT int       tos_con_pending(void);
TOS_EXPORT void      tos_keys_set(uint8_t mask);            /* also recorded in the input log */

/* Disks */
TOS_EXPORT uint8_t  *tos_disk_ptr(uint8_t disk);
TOS_EXPORT uint32_t  tos_disk_size(void);
TOS_EXPORT void      tos_disk_reload(uint8_t disk);         /* after the host wrote into tos_disk_ptr */
TOS_EXPORT int       tos_disk_put_file(uint8_t disk, const char *name, const uint8_t *data, uint32_t len);
TOS_EXPORT int       tos_disk_get_file(uint8_t disk, const char *name, uint8_t *out, uint32_t cap);   /* length or -1 */
TOS_EXPORT int       tos_disk_list(uint8_t disk, char *out, uint32_t cap);   /* "NAME.EXT\n" per file; returns count */

/* Levers, breakpoints, time travel */
TOS_EXPORT int       tos_lever_set(int id, uint32_t value); /* machine levers reset the machine; 0 ok, -1 bad id/value */
TOS_EXPORT uint32_t  tos_lever_get(int id);
TOS_EXPORT int       tos_bp_add(int kind, uint16_t lo, uint16_t hi);
TOS_EXPORT void      tos_bp_clear(void);
TOS_EXPORT int       tos_bp_hit(void);                      /* index of last breakpoint hit, -1 */
TOS_EXPORT int       tos_seek(uint32_t step);               /* restore the nearest earlier snapshot and replay the input log to `step`; 0 ok */
TOS_EXPORT int       tos_snapshot_count(void);
TOS_EXPORT uint32_t  tos_snapshot_step(int slot);

/* Programs & tools */
TOS_EXPORT int       tos_load_com(const uint8_t *bytes, uint32_t len);   /* into TPA; state RUNNING; 0 ok, -1 too big */
TOS_EXPORT int       tos_compile(int lang, const char *src, uint32_t len, uint8_t *out, uint32_t cap, char *err, uint32_t errcap); /* TOS_LANG_*; length or -1 */
TOS_EXPORT int       tos_disasm(uint16_t addr, char *out, int cap);      /* disassemble at addr on the current tape view; returns length */
TOS_EXPORT const char *tos_state_name(int state);                       /* "BOOT","IDLE","SHELL","RUNNING","SYSCALL","HALT" */
TOS_EXPORT const char *tos_syscall_name(int fn);                        /* "CONIN"... or "?" */
TOS_EXPORT int       tos_transition_count(void);
TOS_EXPORT const char *tos_transition_why(int index);
TOS_EXPORT int       tos_transition_from(int index);
TOS_EXPORT int       tos_transition_to(int index);
TOS_EXPORT uint32_t  tos_transition_fired(int index);
TOS_EXPORT int       tos_hal_option(const char *key, const char *value);
TOS_EXPORT const char *tos_version(void);

#endif
