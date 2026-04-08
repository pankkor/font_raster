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

enum {SDF_S = 4096, SDF_W = SDF_S, SDF_H = SDF_S};

int main(int argc, char **argv) {
  if (argc < 4) {
    fprintf(stderr, "Usage: font_raster <glyph_height> <font.ttf> <out.pgm>\n");
    return 1;
  }

  i32 gly_h = atoi(argv[1]);            // single glyph height
  const char *in_filename = argv[2];  // .ttf
  const char *out_filename = argv[3]; // .pgm

  FILE *in_file = fopen(in_filename, "rb");
  if (!in_file) {
    perror("fopen");
    return 1;
  }

  i32 grid_cols = 16;
  i32 grid_rows = 16;
  u32 uimg_w = (u32)gly_h * grid_cols; // avoid integer overflow UB optimizations
  u32 uimg_h = (u32)gly_h * grid_rows;
  i32 img_w = (i32)uimg_w;
  i32 img_h = (i32)uimg_h;
  i32 sdf_w = SDF_W;
  i32 sdf_h = SDF_H;

  if (gly_h <= 0) {
    fprintf(stderr, "Usage: font_raster <glyph_height> <font.ttf> <out.pgm>\n");
    fprintf(stderr, "Error: <glyph_height> shall be > 0. (%d <= 0)\n", gly_h);
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

  u8 *imt_bitmap = (u8 *)malloc(img_w * img_h);
  u8 *sdf_bitmap = (u8 *)malloc(sdf_w * sdf_h);

  if (!imt_bitmap || !sdf_bitmap) {
    fprintf(stderr, "Failed: memory allocation of %.3f MB.\n", (img_w * img_h + sdf_w * sdf_h) / 1024.0f / 1024.0f);
    return 1;
  }

  stbtt_fontinfo font;
  if (!stbtt_InitFont(&font, ttf_buf, stbtt_GetFontOffsetForIndex(ttf_buf, 0))) {
    fprintf(stderr, "Failed: stbtt_InitFont().\n");
    return 1;
  }

  f32 gly_scale = stbtt_ScaleForPixelHeight(&font, (f32)gly_h);
  f32 sdf_scale = stbtt_ScaleForPixelHeight(&font, (f32)sdf_h);

  i32 gly_ascent, gly_descent, gly_line_gap;
  i32 sdf_ascent, sdf_descent, sdf_line_gap;
  stbtt_GetFontVMetrics(&font, &gly_ascent, &gly_descent, &gly_line_gap);
  stbtt_GetFontVMetrics(&font, &sdf_ascent, &sdf_descent, &sdf_line_gap);

  gly_ascent    *= gly_scale; // distance from baseline to glyph top    in px (positive)
  gly_descent   *= gly_scale; // distance from baseline to glyph bottom in px (negative)
  sdf_ascent    *= sdf_scale;
  sdf_descent   *= sdf_scale;
  (void)gly_line_gap;
  (void)sdf_line_gap;

  i32 gly_font_height       = gly_ascent - gly_descent; // - dsecent, while descent is negative)
  i32 sdf_font_height       = sdf_ascent - sdf_descent;
  i32 gly_margin_y          = (gly_h - gly_font_height) / 2;
  i32 sdf_margin_y          = (sdf_h - sdf_font_height) / 2;
  i32 gly_baseline          = gly_margin_y + gly_ascent;
  i32 sdf_baseline          = sdf_margin_y + sdf_ascent;

  // for (i32 i = 0; i < 256; i++) {
  i32 i = 107;
  {
    i32 sdf_x0, sdf_y0, sdf_x1, sdf_y1;
    stbtt_GetCodepointBitmapBox(&font, i, sdf_scale, sdf_scale, &sdf_x0, &sdf_y0, &sdf_x1, &sdf_y1);

    i32 sdf_gly_w = sdf_x1 - sdf_x0;
    i32 sdf_gly_h = sdf_y1 - sdf_y0;
    i32 sdf_x = (sdf_w - sdf_gly_w) / 2;
    i32 sdf_y = sdf_baseline + sdf_y0;

    u8 *sdf_dst = sdf_bitmap + (sdf_y * sdf_w) + sdf_x;

    stbtt_MakeCodepointBitmap(&font, sdf_dst, sdf_gly_w, sdf_gly_h, sdf_w, sdf_scale, sdf_scale, i);
  }

  // TODO:
  // Write sdf PGM
  FILE *out_file = fopen("sdf.pgm", "wb");
  if (out_file) {
    fprintf(out_file, "P5\n%d %d\n255\n", sdf_w, sdf_h);
    if (fwrite(sdf_bitmap, sdf_w * sdf_h, 1, out_file) == 1) {
      printf("Saved SDF (%dx%d) to '%s'\n", sdf_w, sdf_h, "sdf.pgm");
    } else {
      fprintf(stderr, "Failed to save '%s'!\n", out_filename);
      perror("fopen");
      return 1;
    }
  }

  return 0;

  /*
  // Write PGM
  FILE *out_file = fopen(out_filename, "wb");
  if (out_file) {
    fprintf(out_file, "P5\n%d %d\n255\n", img_w, img_h);
    if (fwrite(imt_bitmap, img_w * img_h, 1, out_file) == 1) {
      printf("Saved 16x16 grid (%dx%d) to '%s'\n", img_w, img_h, out_filename);
    } else {
      fprintf(stderr, "Failed to save '%s'!\n", out_filename);
      perror("fopen");
      return 1;
    }
  }

  // TRU3 H4KK3RS N3V3R FR33!
  return 0;
  */
}
