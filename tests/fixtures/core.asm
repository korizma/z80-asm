ORG 0x8000
CONST EQU 12H

start:
    LD A,CONST
    LD B,A
    ADD A,B
    ADC A,(HL)
    SBC A,$34
    SBC (HL)
    SUB C
    AND (IX+2)
    OR (IY-1)
    XOR 0x55
next:
    CP B
    JR NZ,next
    DJNZ start
