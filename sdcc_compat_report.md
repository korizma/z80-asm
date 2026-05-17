# SDCC compatibility report

Source file: `c_programs/test_main.c`

Generated on: 2026-05-17

## Commands run

```sh
make test
sdcc-sdcc c_programs/test_main.c -o test_main.asm -S
./z80-asm -o test_main.bin test_main.asm
sdcc-sdcc -mz80 c_programs/test_main.c -o test_main_z80.asm -S
./z80-asm -o test_main_z80.bin test_main_z80.asm
```

## Baseline

`make test` passed before the SDCC check:

```text
15 checks passed
```

## Important target note

The command shape `sdcc-sdcc c_programs/test_main.c -o test_main.asm -S` does not target Z80 on this install. It generated MCS-51 assembly:

```asm
.optsdcc -mmcs51 --model-small
```

That file is not useful input for this Z80 assembler. It contains MCS-51-specific directives and instructions such as `.area`, `.org`, `.ds`, `ljmp`, `mov`, `clr`, `swap`, `anl`, `xch`, `xrl`, and `lcall`.

The direct assembler attempt failed immediately:

```text
test_main.asm:5: instruction .module takes no operands
```

For the meaningful compatibility check, I also generated Z80 assembly with:

```sh
sdcc-sdcc -mz80 c_programs/test_main.c -o test_main_z80.asm -S
```

## Z80 SDCC output result

Assembling the Z80-targeted SDCC file failed at the first SDCC/ASxxxx directive:

```text
test_main_z80.asm:5: instruction .module takes no operands
```

After applying temporary syntax-only rewrites in `/tmp/test_main_z80_stage4.asm`, the assembler succeeded and produced a 179-byte binary at `/tmp/test_main_z80_stage4.bin`. That means the Z80 opcodes emitted for this small C file are mostly covered; the immediate blockers are SDCC assembly syntax and object-format directives.

## Follow-up

The Z80 SDCC blockers listed below are now supported directly by `z80-asm` for
flat binary output. The assembler accepts the generated `test_main_z80.asm`
without preprocessing, and `tools/sdcc-z80-bin` can compile `c_programs/test_main.c` with
SDCC, prepend a startup stub that calls `_main`, and emit a `.bin`.

The regression suite now includes these checks and reports `18 checks passed`
on this environment.

The MCS-51 output from `sdcc-sdcc c_programs/test_main.c -o test_main.asm -S` remains out
of scope for this Z80 assembler.

## Previously unsupported SDCC syntax found

| SDCC construct | Examples from `test_main_z80.asm` | Current behavior | Needed support |
| --- | --- | --- | --- |
| ASxxxx metadata directives | `.module test_main`, `.optsdcc -mz80` | Treated as instructions; `.module` fails first. | Ignore or parse SDCC metadata directives. |
| ASxxxx global/section directives | `.globl _main`, `.area _CODE`, `.area _DABS (ABS)` | Treated as unknown or operand-taking instructions. | Ignore `.globl` for flat binaries, and either ignore or implement `.area` section handling. |
| Exported labels with double colon | `_turnPixelOn::`, `_setPixelColor::`, `_main::` | After removing directives, fails as `unknown instruction ':'`. | Accept `::` as a label definition, probably marking it public/exported if object support is added later. |
| SDCC temporary numeric labels | `00103$:`, `jr Z, 00103$`, reused in multiple functions | Labels cannot start with digits or contain `$`; naive global renaming also causes duplicate symbols when SDCC reuses local labels. | Accept ASxxxx numeric/local labels with `$`, scoped so repeated temporary labels in different functions do not collide. |
| Immediate prefix `#` | `ld ix,#0`, `ld b, #0x00`, `ld hl, #0x0007` | Once earlier blockers are removed, fails with `expected expression`. | Treat leading `#` as an immediate-expression marker or strip it before expression parsing. |
| SDCC indexed operand syntax | `ld e, 5 (ix)`, `ld c, 4 (ix)`, `ld a, 6 (ix)` | Fails with `unexpected text in expression: '(ix)'`. | Accept `N (ix)` / `N (iy)` as aliases for `(ix+N)` / `(iy+N)`. |
| Explicit accumulator form for unary ALU ops | `and a, #0x07`, `sub a, c`, `or a, c` | Fails as `invalid operands for AND` before other later syntax issues are reached. | Accept `AND A,x`, `SUB A,x`, `OR A,x` as aliases for the assembler's current `AND x`, `SUB x`, `OR x` forms. The same compatibility rule should likely cover `XOR A,x` and `CP A,x`. |

## Practical next steps

To support this SDCC Z80 output directly, the smallest compatibility layer would be:

1. Ignore `.module`, `.optsdcc`, and `.globl`.
2. Treat `.area` as a section directive, or ignore it while continuing to emit flat binary in source order.
3. Accept `::` labels.
4. Add ASxxxx local-label support for labels like `00103$`.
5. Accept `#expr` as `expr`.
6. Accept `N (ix)` and `N (iy)` indexed operands.
7. Accept explicit `A,` forms for accumulator-only ALU instructions.

With those changes, this specific `c_programs/test_main.c` Z80 SDCC output should assemble without pre-processing.
