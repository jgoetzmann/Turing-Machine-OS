# 8080 assembly

`hello.asm` is the BIOS calling convention in a dozen instructions: put the
function id in `A`, the argument byte in `C`, and execute `OUT 1`. The loop
walks a zero-terminated string with `HL` and calls `CONOUT` (function 2) for
each byte, then `HLT` returns control to the shell.

```
A> asm HELLO.ASM
A> run HELLO.COM
HELLO FROM ASM
```

What to watch:

- The image is loaded at `0x0100`; the message bytes sit right after the
  `HLT`, so the strip panel shows the head reading code and data a few bytes
  apart.
- `OUT 1` hands the machine to the BIOS: `RUNNING -> SYSCALL`, the host prints
  the byte, `SYSCALL -> RUNNING` resumes; the syscall itself costs no steps.
- A `.com` must not set `SP`: the loader already points it at the top of the
  stack region.
- `disasm HELLO.COM` shows the same instructions back. Spaces are stored as
  `_` (`5FH`) and translated on the way out, so no data byte is an
  undocumented opcode and the listing reassembles byte for byte.

The assembler accepts labels, `EQU`, `ORG`, `DB`/`DW`/`DS`, forward references
and `$`. See `docs/asm.md`.
