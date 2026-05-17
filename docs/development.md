# Development Notes

## Project Layout

```text
.
|-- Makefile
|-- src/main.c
|-- tests/run_tests.sh
|-- tests/fixtures/
|-- minesweeper.asm
|-- masm.txt
`-- sdcc_compat_report.md
```

`src/main.c` contains the complete assembler implementation. There are no
runtime dependencies beyond the C standard library.

## Build and Test

Build:

```sh
make
```

Run the regression tests:

```sh
make test
```

Clean generated files:

```sh
make clean
```

The test harness writes temporary files under `tests/out/`.

## Implementation Overview

The assembler works in two passes:

1. Pass 1 parses every source line, tracks the program counter, and defines
   labels and `EQU` symbols.
2. Pass 2 parses the source again, evaluates expressions fully, emits bytes, and
   writes the flat binary output.

The main pieces in `src/main.c` are:

- Source loading and line tracking.
- Comment stripping and comma-list splitting that respect quotes and
  parentheses.
- Symbol table management and local dot-label qualification.
- Recursive expression parsing for literals, symbols, parentheses, unary signs,
  addition, and subtraction.
- Operand parsing for registers, memory operands, indexed operands, and
  immediates.
- A small SDCC/ASxxxx compatibility layer for Z80 assembly emitted by
  `sdcc -mz80 -S`.
- Instruction encoders grouped by instruction family.
- Output buffering with zero-filled gaps for forward `ORG` movement.

## Output Model

Output is a flat byte buffer, not an object file. The first emitted address is
treated as file offset 0. Later forward `ORG` movement grows the buffer and fills
the gap with zero bytes.

For example:

```asm
ORG $100
DB 1
ORG $103
DB 2
```

emits this four-byte file:

```text
01 00 00 02
```

## Adding Instructions

Instruction handling is centralized in `encode_instruction()`. For a new family:

1. Add a small encoder helper when the instruction has operand variants.
2. Register the mnemonic in `encode_instruction()`.
3. Add fixture coverage under `tests/fixtures/`.
4. Add the expected byte sequence to `tests/run_tests.sh`.
5. Update `docs/instruction-set.md`.

For a simple no-operand instruction, add it to the fixed-instruction table in
`encode_instruction()` and cover it in `tests/fixtures/fixed.asm`.

## Adding Syntax

Syntax changes usually touch one of these parser stages:

- Label syntax: `parse_label_at_start()`.
- `EQU` syntax: `parse_equ_line()` and the `EQU` handling in `process_line()`.
- Expression syntax: the `ExprParser` functions.
- Operand syntax: `parse_operand()` and `parse_index_inner()`.
- Directives: `process_line()` plus directive helpers such as `handle_db()`.

Keep pass-1 and pass-2 behavior in sync. If a directive changes the program
counter, pass 1 must be able to compute the same size as pass 2.

## Current Scope and Limitations

The assembler is best suited for flat binary Z80 programs written in its native
syntax. It intentionally omits many features found in larger assemblers:

- No include files.
- No macros.
- No conditional assembly.
- No object files, sections, relocations, imports, or exports.
- No listing or symbol-map output.
- No full ASxxxx/SDCC object-file compatibility layer; supported SDCC syntax is
  limited to flat Z80 assembly accepted by the parser.

See `sdcc_compat_report.md` for a concrete compatibility check against SDCC Z80
assembly output.
