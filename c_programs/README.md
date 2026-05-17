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

- `text_editor.c`: a three-slot RAM text editor. Use up/down to pick a file,
  joystick center to edit, `[A]` to view, `[B]` to erase, and `[C]` for help.
  In the editor, move around the on-screen keyboard with the joystick, press
  center to type, `[A]` to save, `[B]` to delete, and `[C]` to return to the
  file list. Saved files stay in RAM while the editor is running; restarting a
  Z80 program reloads clean memory on GLIČ80 hardware.

The `games/` directory includes:

- `flappy_bird.c`: press joystick center, up, or `[A]` to flap.
- `snake.c`: move with the joystick directions.
- `sudoku.c`: choose level 1-5 on the title screen, press joystick center
  to start, use arrows to move, `[A]`/`[B]` to pick a number, joystick center
  to place it, and `[C]` to return to the title screen. Wrong placements blink
  the selected tile.
