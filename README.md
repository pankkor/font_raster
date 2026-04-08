## font_raster
Use stb_truetype.h to rasterize first 256 code points of TrueType font to a square .PGM file (16x16)

### Build
```
clang -Wall -Wextra -Wpedantic -O3 -g font_raster.c -o font_raster
```

### Usage
```
./font_raster <singe_char_size_px> <font.ttf> <out.pgm>
```
