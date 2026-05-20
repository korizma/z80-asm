# GLIČ80 C programs

These examples are small SDCC Z80 programs for the GLIČ80 memory map.

The OLED graphics RAM is one bit per pixel, so these examples use only two
byte values:

- `GLIC_BLACK` leaves pixels off (`0x00`).
- `GLIC_WHITE` turns pixels on (`0xff`).

Build one program from the repository root:

```sh
./tools/sdcc-z80-bin -o c_programs/bin/france.bin c_programs/flags/france.c
```

Or build every C example:

```sh
make -C c_programs
```

Generated binaries are written under `c_programs/bin/`.
Each binary starts with a GLIČ80 title header using the source filename stem,
for example `games/snake.c` is named `snake`.

The `apps/` directory includes:

- `dvd_screensaver.c`: a monochrome DVD-logo screensaver demo. It draws a
  stylized `DVD` logo directly into the 128 by 128 OLED graphics RAM and
  bounces it around the screen.
- `bitcoin_miner.c`: a small offline 64-bit proof-of-work demo. Pick a toy
  difficulty from 1 to 64 leading zero bits, start mining, and watch the current
  nonce, attempts, hash, best zero count, and progress bar update. It does not
  mine real Bitcoin; it keeps the mining idea small enough for this Z80 target. See
  `apps/bitcoin_miner_README.md` for controls.
- `brainfuck.c`: an on-device Brainfuck compiler and bytecode runner. Edit a
  Brainfuck program and a separate input buffer, compile the source with bracket
  validation, then run it with text output on the GLIČ80 screen. `,` reads bytes
  from the input buffer and `.` writes output. See `apps/brainfuck_README.md`
  for controls and limits.
- `mini_os.c`: a cooperative mini OS shell with three built-in C applets:
  `COUNTER`, `MATH DRILL`, and `PIXEL PAD`. Pick an applet with up/down and
  run it with joystick center or `[A]`; `[C]` exits an applet back to the OS.
  Each applet saves its state in a fixed RAM block at `0x6000` before returning,
  so re-opening it resumes the previous counter value, math progress, or pixel
  sketch while this OS image remains loaded. `[B]` resets the selected applet's
  saved slot from the OS menu. See `apps/mini_os_README.md` for the memory
  layout and applet pattern.
- `text_editor.c`: a three-slot RAM text editor. Use up/down to pick a file,
  joystick center to edit, `[A]` to view, `[B]` to erase, and `[C]` for help.
  In the editor, move around the on-screen keyboard with the joystick, press
  center to type, `[A]` to save, `[B]` to delete, and `[C]` to return to the
  file list. Saved files stay in RAM while the editor is running; restarting a
  Z80 program reloads clean memory on GLIČ80 hardware.
- `oneaddr_basic.c`: a tiny on-device BASIC-like interpreter. The editable
  program text lives at fixed RAM address `0x6006` (`0x6000` holds a small
  header and length). Use joystick center to type from the on-screen keyboard,
  the `NL` key to insert a newline, `[A]` then `[A]` again to run, `[A]` then
  up/down to scroll, `[B]` to delete, and `[C]` to open the instruction browser
  from the menu. The editor shows visible newline markers and a source
  scrollbar. Supported statements are `PRINT`, `LET` or
  direct assignment such as `A=A+1`, `IF expr THEN statement`,
  `FOR var=start TO end` with optional `STEP`, `NEXT`, `INPUT var` for the
  current button mask, `WAIT expr`, `CLS`, `REM`, and `END`. Expressions
  support variables `A`-`Z`, decimal numbers, `+`, `-`, `*`, `/`, parentheses,
  comparisons, and `BTN` for the current button mask. See
  `apps/oneaddr_basic_README.md` for the full language notes.
- `mini_browser.c`: a tiny HTML/CSS viewer with a 64-column virtual page, four
  times the visible screen width. Up/down scroll vertically, left/right pan
  horizontally, `[A]`/`[B]` page down/up, center returns home, and `[C]` opens
  help. It renders a compact subset of block/inline HTML, entities, images as
  placeholders, and CSS spacing/border/hidden/pre/uppercase rules. It can use
  embedded HTML or a preloaded `HTM1` RAM page at `0x6000`; see
  `apps/mini_browser_README.md` for the loading format and supported CSS.
- `z80_emulator.c`: a small Z80-in-Z80 interpreter demo. The home screen starts
  with `[A]`, the bundled guest assembly draws the Japan flag through emulated
  GLIČ80 VRAM writes, and pressing `[A]` twice inside the emulator returns home.
  See `apps/z80_emulator_README.md` for the guest memory map and opcode scope.

The `games/` directory includes:

- `flappy_bird.c`: press joystick center, up, or `[A]` to flap.
- `snake.c`: move with the joystick directions.
- `sudoku.c`: choose level 1-5 on the title screen, press joystick center
  to start, use arrows to move, `[A]`/`[B]` to pick a number, joystick center
  to place it, and `[C]` to return to the title screen. Wrong placements blink
  the selected tile.
