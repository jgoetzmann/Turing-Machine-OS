# Pong

Two paddles, one pixel of ball, 64x32 pixels of display. Player 1 is the left
paddle (`W` up, `S` down). Player 2 is the right paddle: the CPU drives it at
half speed until you press `UP` or `DOWN`, then it is yours. Scores are dots
along the top row, player 1 from the left and player 2 from the right. First
to five wins and the program returns to the shell.

What to watch:

- The framebuffer is 256 bytes at the top of tape 0 (`0xFE00` on a 64K tape).
  Pong never redraws the screen: each frame changes only the pixels that
  moved, a handful of writes just below the metadata block.
- `vsync()` is BIOS call 6. Each one stops the kernel with `KSTOP_VSYNC`, the
  host paints the display, and the frame counter goes up.
- `rand()` (BIOS 7) chooses each serve. Same seed lever, same game.
- `keys()` is `IN 3`. Set the input lever to keys.
- A serve waits 120 frames at the centre, so a game lasts well over 600 frames.

Run with `cc PONG.C` then `run PONG.COM`. On a 32K tape the display is at
`0x7E00`.
