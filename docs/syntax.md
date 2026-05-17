# Syntax Reference

This assembler is intentionally simple. Input is parsed line by line, mnemonics
and register names are case-insensitive, and symbol names are case-sensitive.

## Comments and Whitespace

Comments start with `;` outside quoted strings.

```asm
LD A,1      ; comment
DB "a;b"    ; semicolon inside the string is data
```

Blank lines are ignored. Extra whitespace between tokens is allowed.

## Labels

Labels are written at the start of a line and end with `:`.

```asm
start:
    NOP

loop: LD A,1
```

Multiple labels can be placed before the same directive or instruction:

```asm
entry: start:
    JP main
```

Normal label names must start with a letter, `_`, or `.`, and subsequent
characters may also contain digits or `$`. SDCC/ASxxxx exported labels written
with a double colon are also accepted:

```asm
_main::
    RET
```

Symbols are case-sensitive.

## Local Dot Labels

A label beginning with `.` is local to the most recent non-dot label. References
to a dot label are resolved through the current non-dot scope.

```asm
loop:
    DJNZ .again
.again:
    JR loop
```

Internally, `.again` in this example is resolved as `loop.again`.

Defining a non-dot `EQU` symbol also updates the current local-label scope.

SDCC/ASxxxx numeric temporary labels such as `00103$` are accepted and are also
scoped to the most recent non-local label. The same temporary label can be
reused in separate functions without colliding.

## Constants with EQU

`EQU` defines a symbol value during the first pass.

```asm
screen  EQU $7800
count:  EQU 16
```

Because `EQU` is evaluated on pass 1, it must not depend on a symbol defined
later in the file.

## Expressions

Expressions support:

- Integer literals in decimal, `0x` hex, `$` hex, `H` suffix hex, and `%` binary.
- Character literals with one byte of data.
- Symbol references.
- SDCC/ASxxxx immediate prefixes such as `#5`, which are parsed as `5`.
- Parentheses.
- Unary `+` and `-`.
- Binary `+` and `-`.

Examples:

```asm
DB 10, 0x0A, $0A, 0AH, %00001010
DB 'A', '\n', '\x41'
DW start + 4
LD A,-1
```

For hex suffix notation, values that would otherwise start with `A` through `F`
should be written with a leading digit, for example `0FFH`.

The expression parser does not support multiplication, division, bitwise
operators, or a current-PC symbol.

## Directives

### ORG and FORG

`ORG expression` and `FORG expression` set the program counter. They are
equivalent in the current assembler.

```asm
ORG $8000
    NOP
ORG $8010
    DB $EE
```

Moving the program counter backward is an error. Forward gaps are filled with
zero bytes in the output file. The first emitted address becomes output file
offset 0, so `ORG $8000` does not write 32768 leading zero bytes.

### DB and DEFB

`DB` and `DEFB` emit bytes. Items may be byte expressions or quoted strings.

```asm
DB "Text",0
DB 1,2,3,-1
DEFB '\n'
```

Byte expressions must be in the range `-128..255`. Negative values are emitted
as two's-complement bytes.

Supported string escapes are `\n`, `\r`, `\t`, `\0`, `\\`, `\'`, `\"`, and
`\xHH`.

### DW and DEFW

`DW` and `DEFW` emit 16-bit little-endian words.

```asm
DW start, $1234
DEFW -1
```

Word expressions must be in the range `-32768..65535`. Negative values are
emitted in two's-complement little-endian form.

Unary `<expr` and `>expr` are accepted for SDCC/ASxxxx compatibility and return
the low and high byte of an expression.

### DS and DEFS

`DS` and `DEFS` reserve and emit bytes. SDCC/ASxxxx dotted forms such as
`.db`, `.dw`, and `.ds` are accepted as aliases for `DB`, `DW`, and `DS`.

```asm
DS 16          ; sixteen zero bytes
DS 8,$FF       ; eight bytes filled with $FF
```

The size must be a non-negative expression known during pass 1. The optional
fill value must be in the range `-128..255` and defaults to zero.

### ASCII

`.ascii` emits one or more quoted strings without adding a terminator. It is
accepted for SDCC/ASxxxx compatibility.

```asm
.ascii "Text"
.ascii "A","B"
```

### SDCC/ASxxxx Metadata

The following SDCC/ASxxxx directives are accepted as no-ops for flat binary
output:

```asm
.module name
.optsdcc -mz80
.globl _main
.area _CODE
```

`.area` does not implement object-file section placement; input is still emitted
in source order.

## Operands

Supported register names:

- 8-bit: `A`, `B`, `C`, `D`, `E`, `H`, `L`, `I`, `R`
- 16-bit: `AF`, `BC`, `DE`, `HL`, `SP`, `IX`, `IY`
- Conditions: `NZ`, `Z`, `NC`, `C`, `PO`, `PE`, `P`, `M`

Memory operands use parentheses:

```asm
LD A,(BC)
LD A,(DE)
LD A,(HL)
LD A,($4000)
LD A,(IX+5)
LD A,(IY-2)
LD A,(IX)
```

IX/IY displacements must evaluate to `-128..127`.

The assembler accepts `(IX+d)`, `(IY+d)`, and ASxxxx/SDCC displacement syntax:

```asm
LD A,5 (ix)
LD L,6 (iy)
```

## Error Handling

Assembly stops at the first error. Diagnostics include the source file and line
number when the error is associated with a source line.

The output file is written through a temporary `output.tmp` path and renamed only
after successful assembly, so failed assemblies do not leave a partial output at
the requested output path.
