# Instruction Set Reference

This page lists the instruction forms implemented by `z80-asm`. It is not a full
Z80 manual; it documents what this assembler currently accepts.

Notation:

- `r` means `A`, `B`, `C`, `D`, `E`, `H`, or `L`.
- `rr` means `BC`, `DE`, `HL`, or `SP`.
- `cc` means `NZ`, `Z`, `NC`, `C`, `PO`, `PE`, `P`, or `M`.
- `n` means an 8-bit expression.
- `nn` means a 16-bit expression.
- `d` means an IX/IY displacement expression in the range `-128..127`.
- `b` means a bit number from `0` to `7`.

## Load Instructions

```asm
LD r,r
LD r,n
LD r,(HL)
LD r,(IX+d)
LD r,(IY+d)

LD (HL),r
LD (HL),n
LD (IX+d),r
LD (IX+d),n
LD (IY+d),r
LD (IY+d),n

LD A,(BC)
LD A,(DE)
LD A,(nn)
LD (BC),A
LD (DE),A
LD (nn),A

LD A,I
LD A,R
LD I,A
LD R,A

LD rr,nn
LD IX,nn
LD IY,nn

LD (nn),BC
LD (nn),DE
LD (nn),HL
LD (nn),IX
LD (nn),IY
LD (nn),SP

LD BC,(nn)
LD DE,(nn)
LD HL,(nn)
LD IX,(nn)
LD IY,(nn)
LD SP,(nn)

LD SP,HL
LD SP,IX
LD SP,IY
```

## Arithmetic and Logic

```asm
ADD A,r
ADD A,n
ADD A,(HL)
ADD A,(IX+d)
ADD A,(IY+d)

ADD HL,rr
ADD IX,BC
ADD IX,DE
ADD IX,IX
ADD IX,SP
ADD IY,BC
ADD IY,DE
ADD IY,IY
ADD IY,SP

ADC A,r
ADC A,n
ADC A,(HL)
ADC A,(IX+d)
ADC A,(IY+d)
ADC HL,rr

SBC A,r
SBC A,n
SBC A,(HL)
SBC A,(IX+d)
SBC A,(IY+d)
SBC r
SBC n
SBC (HL)
SBC (IX+d)
SBC (IY+d)
SBC HL,rr

SUB r
SUB n
SUB (HL)
SUB (IX+d)
SUB (IY+d)

AND r
AND n
AND (HL)
AND (IX+d)
AND (IY+d)

OR r
OR n
OR (HL)
OR (IX+d)
OR (IY+d)

XOR r
XOR n
XOR (HL)
XOR (IX+d)
XOR (IY+d)

CP r
CP n
CP (HL)
CP (IX+d)
CP (IY+d)

INC r
INC rr
INC IX
INC IY
INC (HL)
INC (IX+d)
INC (IY+d)

DEC r
DEC rr
DEC IX
DEC IY
DEC (HL)
DEC (IX+d)
DEC (IY+d)
```

`SBC operand` is accepted as shorthand for `SBC A,operand`. `SUB`, `AND`, `OR`,
`XOR`, and `CP` accept both the one-operand form and SDCC-style explicit
accumulator aliases such as `AND A,n` and `SUB A,r`.

## Jumps, Calls, and Returns

```asm
JP nn
JP cc,nn
JP (HL)
JP (IX)
JP (IY)

CALL nn
CALL cc,nn

RET
RET cc

JR nn
JR NZ,nn
JR Z,nn
JR NC,nn
JR C,nn

DJNZ nn
```

`JR` and `DJNZ` take target expressions. The assembler computes the signed
relative offset and reports an error when the target is outside `-128..127`
bytes from the following instruction.

## Stack, Exchange, and Restart

```asm
PUSH BC
PUSH DE
PUSH HL
PUSH AF
PUSH IX
PUSH IY

POP BC
POP DE
POP HL
POP AF
POP IX
POP IY

EX AF,AF'
EX DE,HL
EX (SP),HL
EX (SP),IX
EX (SP),IY
EXX

RST n
```

`RST n` accepts `0`, `8`, `10H`, `18H`, `20H`, `28H`, `30H`, or `38H`.

## Bit and Shift Instructions

```asm
BIT b,r
BIT b,(HL)
BIT b,(IX+d)
BIT b,(IY+d)

RES b,r
RES b,(HL)
RES b,(IX+d)
RES b,(IY+d)

SET b,r
SET b,(HL)
SET b,(IX+d)
SET b,(IY+d)

RLC r
RLC (HL)
RLC (IX+d)
RLC (IY+d)

RRC r
RRC (HL)
RRC (IX+d)
RRC (IY+d)

RL r
RL (HL)
RL (IX+d)
RL (IY+d)

RR r
RR (HL)
RR (IX+d)
RR (IY+d)

SLA r
SLA (HL)
SLA (IX+d)
SLA (IY+d)

SRA r
SRA (HL)
SRA (IX+d)
SRA (IY+d)

SLL r
SLL (HL)
SLL (IX+d)
SLL (IY+d)

SRL r
SRL (HL)
SRL (IX+d)
SRL (IY+d)
```

`SLL` is the common undocumented CB-prefix shift-left-and-set-bit-0 instruction.

## Input and Output

```asm
IN r,(C)
IN A,(n)

OUT (C),r
OUT (n),A
```

## Interrupt and CPU Control

```asm
DI
EI
HALT
IM 0
IM 1
IM 2
RETI
RETN
```

## Block and Fixed-Form Instructions

The following no-operand instructions are supported:

```text
CCF  CPD   CPDR  CPI   CPIR  CPL   DAA   DI
EI   EXX   HALT  IND   INDR  INI   INIR  LDD
LDDR LDI   LDIR  NEG   NOP   OTDR  OTIR  OUTD
OUTI RETI  RETN  RLA   RLCA  RLD   RRA   RRCA
RRD  SCF
```

## Notable Unsupported Forms

The assembler does not currently implement every valid Z80 syntax variant. Known
unsupported forms include:

- `IXH`, `IXL`, `IYH`, and `IYL` register names.
- Full object-file features such as section placement, relocations, imports,
  exports, macros, includes, and conditional assembly.
