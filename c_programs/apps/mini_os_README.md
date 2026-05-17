# MiniOS app shell

`mini_os.c` is a small cooperative OS-style shell for GLIČ80/Z80 C programs.
It builds as one flat binary and contains a menu plus three compiled-in C
applets:

- `COUNTER`: saves the current counter value and run count.
- `MATH DRILL`: saves the current problem, answer, attempts, and score.
- `PIXEL PAD`: saves a 16 by 8 text-mode sketch and cursor position.

## Controls

In the OS menu:

- Up/down: choose an applet.
- Joystick center or `[A]`: run the selected applet.
- `[B]`: reset the selected applet's saved state.
- `[C]`: open help.

Inside an applet, `[C]` exits back to the OS menu. State is written into the
applet's save slot before returning, so opening the applet again resumes where
you left it while the MiniOS image remains loaded.

## Saved state

MiniOS uses fixed RAM starting at `0x6000`.

- `0x6000` to `0x6003`: `MOS1` magic header.
- `0x6004`: selected menu item.
- `0x6008` onward: counter state.
- `0x6010` onward: math drill state.
- `0x6020` onward: pixel pad state.

This is RAM state, matching the other examples in this repository. Restarting
or replacing the loaded Z80 program can clear or overwrite it.

## Adding another C applet

This mini OS does not load relocatable binaries. The project currently produces
flat binaries, so applets are compiled into `mini_os.c` and cooperate by
returning to the menu.

To add another applet:

1. Add a new `PROGRAM_*` id and increase `PROGRAM_COUNT`.
2. Reserve a non-overlapping state range in the `STORE_*` offsets.
3. Add a reset function for that range.
4. Add a `run_*` function that loops until `[C]` is pressed.
5. Route the new id through `program_name`, `reset_program_state`, and
   `run_program`.
