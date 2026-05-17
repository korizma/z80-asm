# OneAddr BASIC

`oneaddr_basic.c` is a tiny BASIC-like interpreter for GLIČ80. It includes an
on-device source editor, an instruction browser, and a simple runtime that
executes the text directly from one fixed RAM buffer.

## Memory Layout

- `0x6000` to `0x6003`: storage magic header.
- `0x6004` to `0x6005`: source length, little endian.
- `0x6006`: first byte of editable program text.
- Maximum source size: 512 bytes.

The interpreter reads the source from `0x6006` when you press run. The source is
plain text with newline-separated statements.

## Controls

Main menu:

- Center: edit the program.
- `A`: run the program.
- `B`: load the demo program.
- `C`: open the instruction browser.

Editor:

- Direction buttons: move around the on-screen keyboard.
- Center: type the selected key.
- `B`: delete the last source character.
- `A`, then `A` again: run the program.
- `A`, then up/down: scroll the source preview once.
- `C`: return to the menu.

The keyboard has a two-letter `NL` key for newline. In the source preview,
newlines are shown as `\` so line breaks are visible. The right side of the
source preview is a scrollbar.

Instruction browser:

- Up/down: choose an instruction.
- Center or `A`: open the selected instruction.
- Left/right or up/down on a detail page: move to the next instruction.
- `C` or center on a detail page: return to the instruction list.

Run screen:

- `C`: stop a running program.
- After the program stops, press center, `A`, `B`, or `C` to return.

## Language

Each statement is written on its own line. Keywords are case-insensitive.
Variables are `A` through `Z` and hold signed integer values.

Supported statements:

```text
PRINT expr
PRINT "text"
LET A=expr
A=expr
IF expr THEN statement
FOR I=start TO end
FOR I=start TO end STEP step
NEXT
NEXT I
INPUT A
WAIT expr
CLS
REM comment
END
```

Expressions support:

```text
numbers
A through Z variables
BTN
+ - * /
( )
= < > <= >= <>
```

`BTN` is the current button mask. `INPUT A` stores the current button mask into
variable `A`. A comparison returns `1` when true and `0` when false.

## Examples

Print text and a number:

```text
PRINT "HELLO"
LET A=5
PRINT A
END
```

Loop and sum values:

```text
LET A=0
FOR I=1 TO 5
LET A=A+I
NEXT
PRINT A
END
```

Use an inline condition:

```text
LET A=3
IF A=3 THEN PRINT "OK"
END
```

Read buttons:

```text
PRINT "PRESS BUTTON"
WAIT 50
INPUT A
PRINT A
END
```

## Limits

- Source size is limited to 512 bytes.
- Loop nesting is limited to 4 active `FOR` loops.
- `IF` executes one statement after `THEN`; block-style `IF` is not supported.
- Program text is stored in RAM while this app is running. Restarting a Z80
  program reloads clean memory on GLIČ80 hardware.
