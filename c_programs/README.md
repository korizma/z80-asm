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

The `games/` directory includes:

- `flappy_bird.c`: press joystick center, up, or `[A]` to flap.
- `snake.c`: move with the joystick directions.
- `sudoku.c`: choose level 1-5 on the title screen, press joystick center
  to start, use arrows to move, `[A]`/`[B]` to pick a number, joystick center
  to place it, and `[C]` to return to the title screen. Wrong placements blink
  the selected tile.
