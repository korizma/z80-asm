# Brainfuck Compiler

`brainfuck.c` is a small on-device Brainfuck compiler and bytecode runner for
GLIČ80. It keeps an editable Brainfuck source buffer and a separate input buffer
in RAM, compiles the source to a compact bytecode, validates bracket pairs, and
then executes it with text output on the OLED character screen.

## Memory Layout

- `0x6000` to `0x6003`: storage magic header.
- `0x6004` to `0x6005`: source length, little endian.
- `0x6006`: input length.
- `0x6008`: first byte of editable Brainfuck source.
- `0x6208`: first byte of editable input text.
- Maximum source size: 512 bytes.
- Maximum input size: 64 bytes.

The default demo source is `,[.,]` and the default input is `HELLO` followed by
a newline, so running the demo echoes the input text.

## Controls

Main menu:

- Up/down: choose `EDIT PROGRAM`, `EDIT INPUT`, `RUN`, or `HELP`.
- Center or `A`: open the selected item.
- `B`: reload the demo source and input.
- `C`: open help.

Editor:

- Direction buttons: move around the on-screen keyboard.
- Center: type the selected key.
- `B`: delete the last character.
- `A`, then `A` again: compile and run.
- `A`, then up/down: scroll the preview.
- `C`: return to the menu.

Run screen:

- `C`: stop a running program.
- After the program stops, press any button to return.

## Brainfuck Runtime

Supported Brainfuck instructions:

```text
+ - < > . , [ ]
```

Non-Brainfuck characters are ignored, so comments and line breaks are allowed.
The compiler compresses repeated `+`, `-`, `<`, and `>` commands into single
bytecode operations and checks matching brackets before execution.

Runtime limits:

- 384 compiled bytecode operations.
- 16 nested loops.
- 256 unsigned 8-bit tape cells.
- Tape pointer and cell values wrap.
- `,` reads from the input buffer.
- End of input returns byte `0`.
- `.` writes printable ASCII to text VRAM; newline advances the output row.
