# Stock menu captures

Sixteen 192×64 PNGs of the stock fx-580VN X menu: eight page-1 states
(keys 1–8) and eight page-2 states (keys 5–C). Each capture has exactly one
rounded inverse selection cell; the title band below row 49 is not used.

`tools/build_menu_bitmaps.py` loads each capture as an MSB-first, 32-byte
stride framebuffer, validates the selected cell against the per-page majority
image, and emits `launcher/menu_icons.cwiz`:

- 12 apps × 2 variants (unselected / selected) = 24 sprites
- 48×24 pixels (6 bytes per row) per sprite
- the selected sprite keeps the captured rounded inverse box

The launcher composes the 4×2 grid from the sprites and draws the label, so
the captures are only needed when the icon art or selection style changes.
