; hello.asm -- prints HELLO FROM ASM through BIOS CONOUT.
; BIOS call: function id in A, argument byte in C, then OUT 1.
; The loader sets SP; a .com must not touch it.
;
; The message is stored with '_' (5FH) in place of each space: every data
; byte is then a documented one-byte opcode (letters are MOVs, 0AH is LDAX B,
; 00H is NOP), so `disasm HELLO.COM` reassembles to identical bytes. The
; loop turns 5FH back into 20H before printing.
        ORG     0100H
CONOUT  EQU     2               ; TOS_BIOS_CONOUT

START:  LXI     H,MSG           ; HL -> message (forward reference)
LOOP:   MOV     A,M             ; next byte
        ORA     A               ; zero terminator?
        JZ      DONE
        CPI     5FH             ; '_' stands for a space
        JNZ     SEND
        MVI     A,20H
SEND:   MOV     C,A             ; C = character
        MVI     A,CONOUT
        OUT     1               ; BIOS CONOUT
        INX     H
        JMP     LOOP
DONE:   HLT                     ; back to the shell

MSG:    DB      'HELLO_FROM_ASM',10,0
        END
