# z80-asm

`z80-asm` is a small two-pass Z80 assembler that emits a flat binary file.
It is aimed at compact hand-written programs, including the bundled
`minesweeper.asm` example.

## Build

```sh
make
```

This builds the `z80-asm` executable from `src/main.c`.

## Usage

```sh
./z80-asm [-o output.bin] file.asm
```

If `-o` is omitted, the output path is derived from the input path by replacing
the extension with `.bin`.

Examples:

```sh
./z80-asm -o minesweeper.bin minesweeper.asm
./z80-asm tests/fixtures/core.asm
```

The assembler writes only a raw binary image. It does not produce relocatable
objects, listings, symbol files, or Intel HEX output.

## SDCC Z80 C to BIN

The assembler accepts the Z80 assembly syntax emitted by SDCC/ASxxxx for simple
flat binaries:

```sh
sdcc-sdcc -mz80 -S c_programs/test_main.c -o test_main_z80.asm
./z80-asm -o test_main_z80.bin test_main_z80.asm
```

For a binary that starts by calling C `main`, use the wrapper tool:

```sh
./tools/sdcc-z80-bin [-o output.bin] [--stack 0x7700] [--asm-out combined.asm] file.c
```

The wrapper detects `sdcc-sdcc` or `sdcc`, runs SDCC with `-mz80 -S`, prepends a
small startup stub that sets `SP`, calls `_main`, and halts if `main` returns,
then assembles the combined file with `./z80-asm`.

Only Z80-targeted SDCC output is in scope. Assembly generated for other SDCC
targets, such as MCS-51 output from a plain `sdcc-sdcc file.c`, is not supported.

GLIČ80 C examples live under `c_programs/`, including several monochrome flag
programs and mini games. Build all of them with:

```sh
make -C c_programs
```

## Minimal Example

```asm
ORG 0x8000

start:
    LD SP,$7700
    LD HL,message
.halt:
    JR .halt

message:
    DB "Hello",0
```

## Documentation

- [Syntax reference](docs/syntax.md)
- [Instruction set reference](docs/instruction-set.md)
- [Development notes](docs/development.md)
- [Compiler support implementation](docs/compiler-support.md)
- [SDCC compatibility report](sdcc_compat_report.md)

## Tests

```sh
make test
```

The test script assembles fixture programs under `tests/fixtures/` and compares
their binary output with expected byte sequences. It also checks representative
failure cases such as duplicate labels, invalid operands, byte range errors, and
relative jump range errors.
