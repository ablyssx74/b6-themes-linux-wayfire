# b6-themes-linux-wayfire

xfwm4 window-frame themes (b6, b6+, b6+r, b5+, crux) as a decoration plugin for the Wayfire compositor. The frame is drawn by the
same rules as the Haiku port of these themes and the KWin plugin (`b6-themes-linux-kde`), here with cairo and pango.

**Status: early.** The theme loading and frame drawing are done and can be tried with the preview tool; the Wayfire plugin itself is
being written.

## Build
```
cmake -S . -B build
cmake --build build
./build/b6-preview themes/b6+r b6r.png 2
```
`b6-preview <theme folder> <out.png> [scale]` draws frames (active, inactive, long titles, hover, pressed, maximized) to a PNG, so a
change can be looked at without a compositor.

## Themes
The theme pictures are **not** in this repository: `themes/` is ignored. Take them from the xfwm4-themes collection (GPL-3). A theme is a folder with a `themerc`.
