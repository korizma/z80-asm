# Compiler Support Implementation

This document summarizes the compiler support added around `z80-asm`. The goal
was to let small C programs compiled by SDCC for Z80 become runnable flat binary
images for the target machine.

## Scope

The supported compiler path is:

```sh
sdcc-sdcc -mz80 -S program.c -o program.asm
./z80-asm -o program.bin program.asm
```

For runnable C binaries with a startup stub, the supported command is:

```sh
./tools/sdcc-z80-bin -o program.bin program.c
```

Only Z80-targeted SDCC assembly is in scope. Assembly produced for other SDCC
targets, such as MCS-51 output from running SDCC without `-mz80`, is not
supported by this assembler.

The output is still a flat binary. The implementation does not add relocatable
objects, a linker, real ASxxxx section placement, symbol export files, or a C
standard library.

## Assembler Compatibility Layer

The assembler now accepts the SDCC/ASxxxx syntax needed by the generated Z80
assembly used in this repository.

Implemented directive support:

- `.module`, `.optsdcc`, `.globl`, and `.area` are accepted as no-ops for flat
  binary output.
- Dotted data directives such as `.db`, `.dw`, and `.ds` are accepted as aliases
  for `DB`, `DW`, and `DS`.
- `.ascii` emits quoted string bytes, matching SDCC string literal output.
- `.area` does not reorder or relocate sections. Bytes are emitted in source
  order.

Implemented label support:

- Export-style labels with a double colon are accepted, for example `_main::`.
- SDCC temporary numeric labels such as `00103$` are accepted.
- Numeric temporary labels are scoped to the most recent non-local label, so the
  same temporary label can be reused in different generated functions without
  causing duplicate symbol errors.
- Existing dot-local labels continue to use the same local scope mechanism.

Implemented expression support:

- A leading SDCC immediate marker is accepted, so `#0x7700` is parsed as
  `0x7700`.
- Unary low-byte and high-byte operators are accepted for SDCC-style relocation
  fragments: `<expr` and `>expr`.
- Symbol tokens may contain `$` where SDCC uses it for generated labels.

Implemented operand and instruction syntax:

- SDCC indexed operands such as `5 (ix)`, `-1 (iy)`, and `0 (iy)` are parsed as
  indexed memory operands equivalent to `(ix+5)`, `(iy-1)`, and `(iy+0)`.
- Accumulator ALU forms emitted by SDCC are accepted, including `SUB A,x`,
  `AND A,x`, `OR A,x`, `XOR A,x`, and `CP A,x`.
- Existing Z80 forms remain valid, so handwritten assembly is still supported.

## SDCC-to-Binary Wrapper

The `tools/sdcc-z80-bin` script automates the C-to-binary flow:

1. Finds `sdcc-sdcc` first, then falls back to `sdcc`.
2. Runs SDCC with `-mz80 -S` to generate Z80 assembly.
3. Prepends a minimal startup stub:

   ```asm
   LD SP,#0x7700
   CALL _main
   __sdcc_z80_bin_halt:
       JR __sdcc_z80_bin_halt
   ```

4. Invokes `./z80-asm` on the combined assembly.
5. Optionally writes the combined assembly with `--asm-out`.

Supported options:

- `-o output.bin` chooses the binary output path.
- `--stack 0x7700` changes the stack pointer used by the startup stub.
- `--asm-out output.asm` keeps the generated combined assembly for inspection or
  debugging.

## C Example Support

The `c_programs/` directory contains SDCC-friendly C programs that build through
the wrapper. `c_programs/common/glic80.h` provides the small target support layer
used by those examples:

- Memory-mapped text and graphics RAM constants.
- Screen size and color constants.
- Button bit masks and an inline port read helper.
- Small drawing and screen-fill helpers implemented as static C functions.

The C examples are deliberately self-contained. They do not rely on a linker or
external SDCC runtime libraries beyond what SDCC emits into the single generated
assembly file.

Build one example:

```sh
./tools/sdcc-z80-bin -o c_programs/bin/france.bin c_programs/flags/france.c
```

Build all examples:

```sh
make -C c_programs
```

## Test Coverage

Regression coverage was added for the compiler-support path:

- `tests/fixtures/sdcc_compat.asm` covers SDCC metadata directives, double-colon
  labels, scoped numeric temporary labels, `#` immediates, indexed operands, and
  accumulator ALU syntax.
- `tests/run_tests.sh` assembles `test_main_z80.asm` directly to verify real
  SDCC Z80 output is accepted.
- When SDCC is installed, the test suite also runs `tools/sdcc-z80-bin` against
  `c_programs/test_main.c` and checks that the output starts with the startup
  stub.

The SDCC compatibility report in `sdcc_compat_report.md` records the original
blockers and the current supported result.

## Current Limitations

This is compiler-input support for flat binaries, not a full SDCC toolchain:

- No object files, linker, relocation records, imports, or exports are produced.
- `.area` is only accepted syntactically; it does not implement ASxxxx section
  placement.
- Programs should be single-translation-unit or otherwise already flattened into
  one SDCC-generated assembly file.
- Code that requires external SDCC helper routines or libc symbols may still
  fail unless those routines are present in the generated assembly.
- Startup is intentionally minimal: set `SP`, call `_main`, then halt in a loop
  if `main` returns.
