# Game of Life

Conway's Life on the whole 64x32 display, edges wrapping, one generation per
`vsync()`. The board starts as an R-pentomino (bounding box top-left at
`(30,15)`), the five-cell seed that keeps growing for over a thousand
generations on an infinite plane; on a 64x32 torus it becomes a wrapping soup
much sooner. `SPACE` reseeds every cell from `rand()`; `ESC` returns to the
shell.

What to watch:

- The cells are not on the display. Two 2048-byte arrays sit in the banked
  window at `0x4000`. The generation is computed in place, row by row, with
  three small side buffers, so the tape map shows the board being rewritten
  top to bottom. `generation()` reads eight neighbours per cell from the first
  and writes the second, then copies it back: a read wave followed by a write
  wave.
- `render()` packs eight cells into each framebuffer byte, MSB first.
- A generation is about 290,000 8080 instructions and 3.0 M cycles: roughly one
  and a half seconds per frame at `hz=2000000`, and as fast as the host allows
  at `max`.

`gen100.expected` is the framebuffer after the 100th vsync, as 512 lowercase
hex characters (`gen100.py` regenerates it). Tests compare the display
against it after the 100th vsync.
