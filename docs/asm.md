# 8080 Assembler

`asm_assemble` (`src/lang/asm.c`) turns 8080 assembly source into a flat `.com` image. It is a host tool — a ROM service like the C compiler — reachable as `asm F` in the shell (BIOS 0x1A, `F.ASM` → `F.COM`), as `build/asm <in.asm> <out.com>` on the host, as `tos_compile(TOS_LANG_ASM, …)` and from the playground editor. `build/disasm` is its inverse, and `disasm` output re-assembles to identical bytes for all 256 opcodes.

## Source format

One statement per line:

```
[label:] [mnemonic operands] [; comment]
```

- Mnemonics and register names are case-insensitive; labels are case-sensitive.
- A label is an identifier followed by `:`. It may stand alone on a line.
- Operands are separated by commas; whitespace around them is ignored.
- Everything after `;` is a comment.

## Registers

`A B C D E H L M SP PSW`. `M` is the byte at `(HL)`. `SP` and `PSW` are valid only where the 8080 allows them (`LXI SP,…`, `DAD SP`, `INX/DCX SP`, `PUSH/POP PSW`).

## Mnemonics

All of the 8080:

| Group | Mnemonics |
|---|---|
| Data transfer | `MOV r,r` `MVI r,imm8` `LXI rp,imm16` `LDA addr` `STA addr` `LHLD addr` `SHLD addr` `LDAX B/D` `STAX B/D` `XCHG` |
| Arithmetic | `ADD r` `ADI imm8` `ADC r` `ACI imm8` `SUB r` `SUI imm8` `SBB r` `SBI imm8` `INR r` `DCR r` `INX rp` `DCX rp` `DAD rp` `DAA` |
| Logical | `ANA r` `ANI imm8` `ORA r` `ORI imm8` `XRA r` `XRI imm8` `CMP r` `CPI imm8` `RLC` `RRC` `RAL` `RAR` `CMA` `CMC` `STC` |
| Branch | `JMP addr` `JNZ JZ JNC JC JPO JPE JP JM addr` `CALL addr` `CNZ CZ CNC CC CPO CPE CP CM addr` `RET` `RNZ RZ RNC RC RPO RPE RP RM` `RST n` (0–7) `PCHL` |
| Stack / misc | `PUSH rp` `POP rp` `XTHL` `SPHL` `IN port` `OUT port` `EI` `DI` `HLT` `NOP` `RIM` `SIM` |

`r` is `A B C D E H L M`; `rp` is `B D H SP` (or `PSW` for `PUSH`/`POP`).

## Directives

| Directive | Meaning |
|---|---|
| `ORG expr` | Set the assembly address. Default `0100H` (the TPA). |
| `DB item {, item}` | Emit bytes. Items are expressions or quoted strings: `'HI'` / `"HI"`. |
| `DW expr {, expr}` | Emit 16-bit words, little-endian. |
| `DS expr` | Reserve `expr` zero bytes. |
| `name EQU expr` | Define a symbol. |
| `END` | Stop assembling. |

## Numbers and expressions

- Decimal `123`; hex `0FFH`, `0xFF`, `$FF`; binary `1010B`; character `'A'`.
- `$` is the current address.
- Operators `+ - * /`, evaluated left to right, with `( )` for grouping.
- Labels and `EQU` symbols may be used before they are defined: the assembler makes two passes. The operands of
  `ORG`, `DS` and `EQU` are the exception, because pass 1 has to know the location counter as it goes; a symbol
  used there must already be defined, or the assembler reports `undefined symbol`.

## Output and errors

The image runs from `ORG` to the last emitted byte; the loader places it at `0x0100`, sets SP and jumps to it. Do not set SP yourself unless you accept that the binary is then tied to one tape length (see `architecture.md` §12). End with `HLT` to return to the shell.

Errors stop assembly and report the first problem:

| Error | Example |
|---|---|
| `line N: unknown mnemonic 'X'` | `FOO A,B` |
| `line N: undefined symbol 'X'` | `JMP nowhere` |
| `line N: bad operand` | `MOV A` (`MVI M,H` reports `undefined symbol 'H'`: the second operand of `MVI` is a number) |
| `line N: value out of range` | `MVI A,300` or `RST 9` |

The `build/asm` CLI prints the error to stderr and exits 1; the shell prints it to the console and writes no `.COM`.

## Encoding reference

A few encodings worth knowing when reading the tape map: `MVI A,05H` is `3E 05`; `LXI H,1234H` is `21 34 12`; `JMP 0123H` is `C3 23 01`; `MOV A,M` is `7E`; `RST 3` is `DF`; `OUT 01H` is `D3 01`; `IN 03H` is `DB 03`; `HLT` is `76`; `DB 'HI',0` is `48 49 00`; `DW 1234H` is `34 12`.

## Example: `demos/asm/hello.asm`

```
; prints HELLO FROM ASM through BIOS CONOUT
        ORG 0100H
start:  LXI H,msg
loop:   MOV A,M
        ORA A           ; zero byte ends the string
        JZ done
        MOV C,A         ; C = byte
        MVI A,02H       ; CONOUT
        OUT 01H         ; BIOS call
        INX H
        JMP loop
done:   HLT
msg:    DB 'HELLO FROM ASM',0AH,0
```

`build/asm demos/asm/hello.asm out.com` then `run` prints `HELLO FROM ASM`. Talking to the machine from assembly is the same as from C: function id in A, argument in C or DE, `OUT 01H`; `OUT 02H` selects a tape; `IN 03H..05H` read keys, tape count and tape length (see `architecture.md` §6–7).

## Disassembler

`build/disasm <file.com> [orgHex]` prints one line per instruction:

```
0100: 21 0F 01  LXI H,010FH
0103: 7E        MOV A,M
0104: B7        ORA A
```

Format rules (`src/emu/disasm.c`): upper-case mnemonic, one space, operands separated by `,` with no spaces, 8-bit immediates as two hex digits + `H`, 16-bit as four + `H`. The undocumented aliases (`0x08 0x10 0x18 0x20 0x28 0x30 0x38` = NOP, `0xCB` = JMP, `0xD9` = RET, `0xDD 0xED 0xFD`
= CALL) are shown with a trailing `*`. Where one alias covers several bytes, the byte is part of the spelling, so
nothing is lost on the way back: `NOP*`, `NOP*10`, `NOP*18`, `NOP*20`, `NOP*28`, `NOP*30`, `NOP*38`, `JMP* 0123H`,
`RET*`, `CALL* 0123H`, `CALL*ED 0123H`, `CALL*FD 0123H`. The assembler accepts every one of those spellings.
`20H` and `30H` are the 8085's RIM and SIM; those two mnemonics still assemble, and this 8080 runs both bytes as
a NOP. The visualizer's detail panel uses the same routine, so what you see on screen is what `build/asm` would accept back.
