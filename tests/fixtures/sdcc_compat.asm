    .module sdcc_compat
    .optsdcc -mz80
    .globl _main
    .area _CODE

_main::
    LD IX,#0
    LD E, 5 (ix)
    LD L, 6 (iy)
    LD A,#0x07
    AND A,#0x03
    SUB A,E
    OR A,E
    XOR A,#0x01
    CP A,#0x06
    JR Z, 00103$
    LD A,#0xff
00103$:
    RET

_other::
    JR 00103$
00103$:
    LD A,#0
    RET

    .area _INITIALIZER
