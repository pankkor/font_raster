// Rasterize first 256 code points of TrueType font to a square .PGM file (16x16)
//
// Usage
//   font_raster <singe_char_size_px> <font.ttf> <out.pgm>
//
// Build
//   clang -Wall -Wextra -Wpedantic -O3 -g font_raster.c -o font_raster

#include <stdio.h>
#include <stdlib.h>

typedef unsigned char u8;
typedef int           i32;
typedef unsigned int  u32;
typedef float         f32;

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

int main(int argc, char **argv) {
  if (argc < 4) {
    fprintf(stderr, "Usage: font_raster <singe_char_size_px> <font.ttf> <out.pgm>\n");
    return 1;
  }

  i32 pxs = atoi(argv[1]);
  const char *in_filename = argv[2];
  const char *out_filename = argv[3];

  FILE *in_file = fopen(in_filename, "rb");
  if (!in_file) {
    perror("fopen");
    return 1;
  }

  i32 grid_cols = 16;
  i32 grid_rows = 16;
  u32 uimg_w = (u32)pxs * grid_cols; // avoid integer overflow optimizations
  u32 uimg_h = (u32)pxs * grid_rows;
  i32 img_w = (i32)uimg_w;
  i32 img_h = (i32)uimg_h;

  if (pxs <= 0) {
    fprintf(stderr, "Usage: font_raster <singe_char_size_px> <font.ttf> <out.pgm>\n");
    fprintf(stderr, "Error: <singe_char_size_px> shall be > 0. (%d <= 0)\n", pxs);
    return 1;
  }

  if (uimg_w > INT32_MAX || img_h > INT32_MAX) {
    fprintf(stderr, "Error: output image dimensions > %d (%ux%u)\n", INT32_MAX, uimg_w, uimg_h);
    return 1;
  }

  fseek(in_file, 0, SEEK_END);
  long size = ftell(in_file);
  fseek(in_file, 0, SEEK_SET);

  u8 *ttf_buf = (u8 *)malloc(size);
  if (fread(ttf_buf, size, 1, in_file) != 1) {
    perror("fread");
    return 1;
  }
  fclose(in_file);

  u8 *master_bitmap = (u8 *)malloc(img_w * img_h);

  if (!master_bitmap) {
    fprintf(stderr, "Failed: memory allocation of %.3f MB.\n", (img_w * img_h) / 1024.0f / 1024.0f);
    return 1;
  }

  stbtt_fontinfo font;
  if (!stbtt_InitFont(&font, ttf_buf, stbtt_GetFontOffsetForIndex(ttf_buf, 0))) {
    fprintf(stderr, "Failed: stbtt_InitFont().\n");
    return 1;
  }

  f32 scale = stbtt_ScaleForPixelHeight(&font, (f32)pxs);

  i32 ascent, descent, line_gap;
  stbtt_GetFontVMetrics(&font, &ascent, &descent, &line_gap);

  // Rescale
  ascent    *= scale; // distance from baseline to glyph top    in px (positive)
  descent   *= scale; // distance from baseline to glyph bottom in px (negative)
  line_gap  *= scale; // distance between lines
  (void)line_gap;     // not used

  i32 font_height = ascent - descent; // - dsecent, while descent is negative)
  i32 margin_y = (pxs - font_height) / 2;
  i32 baseline_in_cell = margin_y + ascent;

  for (i32 i = 0; i < 256; ++i) {
    i32 x0, y0, x1, y1;
    stbtt_GetCodepointBitmapBox(&font, i, scale, scale, &x0, &y0, &x1, &y1);

    i32 char_w = x1 - x0;
    i32 char_h = y1 - y0;

    i32 col = i % 16;
    i32 row = i / 16;

    i32 x_off = (pxs - char_w) / 2;

    i32 final_x = (col * pxs) + x_off;
    i32 final_y = (row * pxs) + baseline_in_cell + y0;

    u8 *dst = master_bitmap + (final_y * img_w) + final_x;

    stbtt_MakeCodepointBitmap(&font, dst, char_w, char_h, img_w, scale, scale, i);
  }

  // Write PGM
  FILE *out_file = fopen(out_filename, "wb");
  if (out_file) {
    fprintf(out_file, "P5\n%d %d\n255\n", img_w, img_h);
    if (fwrite(master_bitmap, img_w * img_h, 1, out_file) == 1) {
      printf("Saved 16x16 grid (%dx%d) to '%s'\n", img_w, img_h, out_filename);
    } else {
      fprintf(stderr, "Failed to save '%s'!\n", out_filename);
      perror("fopen");
      return 1;
    }
  }

  // TRU3 H4KK3RS N3V3R FR33!
  return 0;
}
