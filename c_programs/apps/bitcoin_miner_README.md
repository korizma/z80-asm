# Bitcoin miner demo

`bitcoin_miner.c` is a small GLIČ80/Z80 proof-of-work demo. It does not mine
real Bitcoin: real mining needs the Bitcoin network, double SHA-256 block
headers, and hardware far beyond this target. This program keeps the same core
idea small enough for the Z80 examples: try nonces until a toy 64-bit hash has
enough leading zero bits.

## Controls

Setup screen:

- Up/down: change difficulty by 1 bit.
- Left/right: change difficulty by 8 bits.
- `[B]`: create a new toy block id.
- Joystick center or `[A]`: start mining.
- `[C]`: show why this is a toy miner.

Mining screen:

- The screen shows the current nonce, attempts, latest hash, best leading-zero
  count, and a progress bar toward the selected difficulty.
- `[C]`: stop mining and show the best hash found so far.

Result screen:

- Center or `[C]`: return to the setup screen.
- `[A]`: create the next toy block id before returning.

## Difficulty

Difficulty is the number of leading zero bits required in the toy hash. The
range is 1 to 64 bits. Low values should finish interactively; high values are
intentionally slow so the progress display can keep running until you stop it.
