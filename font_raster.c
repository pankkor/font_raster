// Rasterize first 256 code points of TrueType font to a square .PGM file (16x16)
//
// Usage
//   font_raster <singe_char_size_px> <font.ttf> <out.pgm>
//
// Build
//   clang -Wall -Wextra -Wpedantic -O3 -g font_raster.c -o font_raster

#include <stdio.h>
#include <stdlib.h>

typedef signed char         i8;
typedef unsigned char       u8;
typedef int                 i32;
typedef unsigned int        u32;
typedef unsigned long long  u64;
typedef float               f32;
typedef i32                 b32;

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

// Is in (0, INT32_MAX] range
b32 is_gt_zero_i32(u64 v) {
  return v > 0 && v <= INT32_MAX;
}

// Write single channel `bitmap` buffer with `w` and `h` dimensions to filename
b32 fwrite_pgm(const char *filename, u8 *bitmap, i32 w, i32 h) {
  b32 ret = 0;
  FILE *out_file = fopen(filename, "wb");
  if (out_file) {
    fprintf(out_file, "P5\n%d %d\n255\n", w, h);
    ret = fwrite(bitmap, w * h, 1, out_file);
    if (ret != 1) {
      fprintf(stderr, "Failed to save '%s'!\n", filename);
      perror("fwrite");
    }
    fclose(out_file);
  } else {
    perror("fopen");
  }
  return ret;
}

// Make SDF font
//  img_cell_h    - hight of glyph cell in resulting image. Cell will fit glyph with 2*spread.
//  spread        - SDF is calculated in spread range. 6-8 is fine. Use bigger spread for effects (glow).
//                  More spread -> more precesion outside of glyph shape and less in shape
//  hires_factor  - Factor to scale glyph to calculate SDF later. 32 or 64 is fine.
b32 sdf(const stbtt_fontinfo *font, i32 img_cell_h, i32 spread, i32 hires_factor, const char *out_filename) {
  u8 *img_bitmap;             // Resulting out image buffer
  u8 *hires_bitmap;           // Helper high-resolution image buffer
  u8 *sdf_bitmap;             // Helper high-resolution SDF image buffer
  u8 *hires_dst;              // Rasterize glyph to this point in high-resolution buffer

  u64 img_usz[2];             // Unsigned version for checks to avoid integer overflow UB optimizations
  u64 hires_usz[2];           // Unsigned version for checks to avoid integer overflow UB optimizations
  i32 img_cell_sz[2];         // Cell size to contain resulting SDF image
  i32 img_gly_sz[2];          // Glyph size inside of the image accounting for `spread`
  i32 img_grid_sz[2];         // Cells arranged in grid dimensions [cols, rows]
  i32 img_sz[2];              // Resulting image total size
  i32 hires_sz[2];            // Size of high-resolution glyph image
  i32 hires_gly_sz[2];        // Size of glyph in high-resolution image considering spread
  i32 sdf_sz[2];              // Size of high-resolution SDF image

  u64 img_bitmap_bytes;       // Size in bytes of resulting bitmap.
  u64 hires_bitmap_bytes;     // Size in bytes of helper high-resolution bitmap.
  u64 sdf_bitmap_bytes;       // Size in bytes of helper SDF image. Equals high-resolution bitmap.
  u64 total_bitmap_bytes;     // Size in bytes of all bitmaps.

  i32 font_ascent;            // Distance from baseline to glyph top    (positive)
  i32 font_descent;           // Distance from baseline to glyph bottom (negative)
  i32 font_line_gap;          // Distance between lines (not used)
  f32 hires_font_scale;       // (dimenions * hires_font_scale) to get px in high-resolution image
  i32 hires_font_ascent;      // Font ascent  in high-resolution image
  i32 hires_font_descent;     // Font descent in high-resolution image
  i32 hires_font_height;      // Font height  in high-resolution image
  i32 hires_margin_y;         // Distance from glyph to top or bottom (includes spread) in hight-resolution image
  i32 hires_baseline_bitmap;  // Offset of baseline in bitmap (font_ascent + margin_y)

  i32 hires_bb_p0[2];         // High-resolution bounding box point 0
  i32 hires_bb_p1[2];         // High-resolution bounding box point 1
  i32 hires_bb_sz[2];         // High-resolution bounding box size
  i32 hires_p[2];             // High-resolution position in buffer
  i32 hires_spread;           // Spread rescaled by hires_factor

  img_cell_sz [0]     = img_cell_h;
  img_cell_sz [1]     = img_cell_h; // height - monospace font
  img_gly_sz  [0]     = img_cell_sz[0] - 2 * spread;
  img_gly_sz  [1]     = img_cell_sz[1] - 2 * spread;
  img_grid_sz [0]     = 16;
  img_grid_sz [1]     = 16;
  img_usz     [0]     = (u64)img_cell_sz[0] * img_grid_sz[0];
  img_usz     [1]     = (u64)img_cell_sz[1] * img_grid_sz[1];
  img_sz      [0]     = (i32)img_usz[0];
  img_sz      [1]     = (i32)img_usz[1];

  hires_spread        = spread              * hires_factor;
  hires_usz   [0]     = (u64)img_cell_sz[0] * hires_factor;
  hires_usz   [1]     = (u64)img_cell_sz[1] * hires_factor;
  hires_gly_sz[0]     = img_gly_sz[0]       * hires_factor;
  hires_gly_sz[1]     = img_gly_sz[1]       * hires_factor;
  hires_sz    [0]     = (i32)hires_usz[0];
  hires_sz    [1]     = (i32)hires_usz[1];
  sdf_sz      [0]     = (i32)hires_usz[0];
  sdf_sz      [1]     = (i32)hires_usz[1];

  // Alloc
  img_bitmap_bytes    = img_sz[0]   * img_sz[1];
  hires_bitmap_bytes  = hires_sz[0] * hires_sz[1];
  sdf_bitmap_bytes    = sdf_sz[0]   * sdf_sz[1];
  total_bitmap_bytes  = img_bitmap_bytes + hires_bitmap_bytes + sdf_bitmap_bytes;
  img_bitmap          = (u8 *)malloc(img_bitmap_bytes);
  hires_bitmap        = (u8 *)malloc(hires_bitmap_bytes);
  sdf_bitmap          = (u8 *)malloc(sdf_bitmap_bytes);

  // Sanity checks
  if (!img_bitmap || !hires_bitmap || !sdf_bitmap) {
    fprintf(stderr, "Failed: memory allocation of %.2f MB.\n", total_bitmap_bytes / 1024.0f / 1024.0f);
    return 1;
  }

  if (!is_gt_zero_i32(img_usz[0]) || !is_gt_zero_i32(img_usz[1])) {
    fprintf(stderr, "Error: bad output image dimensions %llux%llu. Max allowed dimensions %dx%d\n", img_usz[0], img_usz[1], INT32_MAX, INT32_MAX);
    return 1;
  }

  if (!is_gt_zero_i32(hires_usz[0]) || !is_gt_zero_i32(hires_usz[1])) {
    fprintf(stderr, "Error: bad high-resolution image dimensions %llux%llu. Max allowed dimensions %dx%d\n", hires_usz[0], hires_usz[1], INT32_MAX, INT32_MAX);
    return 1;
  }

  if (img_gly_sz[0] <= 0 || img_gly_sz[1] <= 0) {
    fprintf(stderr, "Error: spread (%d) is too big relative to glyph cell dimensions (%dx%d). Decrease spread or increase glyph cell dimensions.\n", spread, img_cell_sz[0], img_cell_sz[1]);
    return 1;
  }

  hires_font_scale = stbtt_ScaleForPixelHeight(font, (f32)hires_gly_sz[1]);
  stbtt_GetFontVMetrics(font, &font_ascent, &font_descent, &font_line_gap);
  (void)font_line_gap; // not used

  // Rescale to high resolution bitmap pixels
  hires_font_ascent     = font_ascent       * hires_font_scale;
  hires_font_descent    = font_descent      * hires_font_scale;
  hires_font_height     = hires_font_ascent - hires_font_descent; // - dsecent, while font_descent is negative

  hires_margin_y        = (hires_sz[1] - hires_font_height) / 2;
  hires_baseline_bitmap = hires_margin_y    + hires_font_ascent;

  // for (i32 i = 0; i < 256; i++) {
  i32 i = 103;
  {
    stbtt_GetCodepointBitmapBox(font, i,
        hires_font_scale,     hires_font_scale,
        &hires_bb_p0[0], &hires_bb_p0[1],
        &hires_bb_p1[0], &hires_bb_p1[1]);

    hires_bb_sz[0]  = hires_bb_p1[0] - hires_bb_p0[0];
    hires_bb_sz[1]  = hires_bb_p1[1] - hires_bb_p0[1];

    hires_p[0]      = (hires_sz[0] - hires_bb_sz[0]) / 2;
    hires_p[1]      = hires_baseline_bitmap + hires_bb_p0[1]; // spread is alraedy included

    hires_dst       = hires_bitmap + hires_p[1] * hires_sz[0] + hires_p[0];

    stbtt_MakeCodepointBitmap(font, hires_dst, hires_bb_sz[0], hires_bb_sz[1], hires_sz[1], hires_font_scale, hires_font_scale, i);

    // Is point IN or OUT?
    //   IN  - inside  the glyph -> positive distance
    //   OUT - outside the glyph -> negative distance

    // Calculate SDF
    u64 bitmap_from_off;  // offset of a point in SDF bitmap buffer to be calculated
    i32 from_p[2];        // calculate distance from point
    i32 to_p[2];          // calculate distance to   point
    i32 spread_min[2];    // minimum spread, ensure from_p - hires_spread >= 0
    i32 spread_max[2];    // maximum spread, ensure from_p + hires_spread < sdf_sz
    i32 spread_it[2];     // iterate over spread in [spread_min, spread_max] range
    i32 d_p[2];           // from_p - to_p
    i32 dist2_old;        // distance^2 between from_p and to_p stored in SDF
    i32 dist2_new;        // distance^2 between from_p and to_p
    i32 dist_new_n;       // distance normalized
    b32 is_from_in;       // is from point IN the glyph
    b32 is_to_in;         // is   to point IN the glyph
    u8 from_v;            // from point high-resolution bitmap value
    u8 to_v;              // to   point high-resolution bitmap value

    // For each from_p
    for   (from_p[0] = 0; from_p[0] < sdf_sz[0]; ++from_p[0]) {
      for (from_p[1] = 0; from_p[1] < sdf_sz[1]; ++from_p[1]) {
        // Is from_p IN?
        bitmap_from_off = from_p[1] * sdf_sz[0] + from_p[0];
        from_v          = hires_bitmap[bitmap_from_off];
        is_from_in      = from_v > 128;
        dist2_old       = hires_spread * hires_spread;

        sdf_bitmap[bitmap_from_off] = is_from_in ? 255 : 0;

        // Ensure that to_p is not out of bounds
        spread_min[0]   = MAX(-hires_spread, -from_p[0]);
        spread_min[1]   = MAX(-hires_spread, -from_p[1]);
        spread_max[0]   = MIN( hires_spread, sdf_sz[0] - from_p[0] - 1);
        spread_max[1]   = MIN( hires_spread, sdf_sz[1] - from_p[1] - 1);

        // For each spread in [spread_min, spread_max];
        for   (spread_it[0] = spread_min[0]; spread_it[0] <= spread_max[0]; ++spread_it[0]) {
          for (spread_it[1] = spread_min[1]; spread_it[1] <= spread_max[1]; ++spread_it[1]) {
            // Get to_p
            to_p[0]   = from_p[0] + spread_it[0];
            to_p[1]   = from_p[1] + spread_it[1];

            // Is to_p IN?
            to_v      = hires_bitmap[to_p[1] * hires_sz[0] + to_p[0]];
            is_to_in  = to_v > 128;

            d_p[0]    = from_p[0] - to_p[0];
            d_p[1]    = from_p[1] - to_p[1];

            dist2_new  = d_p[0] * d_p[0] + d_p[1] * d_p[1];

            if (is_from_in != is_to_in && dist2_new < dist2_old) {
              dist_new_n                  = (i32)(sqrtf(dist2_new) / hires_spread * 128.0f);
              dist2_old                   = dist2_new;
              sdf_bitmap[bitmap_from_off] = is_from_in ? 128 + dist_new_n : 128 - dist_new_n;

              // TODO:
              // printf("From {%d,%d}[%s] to {%d,%d} [%s], old d^2=%d, new d^2=%d, written=%d\n",
              //    from_p[0], from_p[1],  is_from_in  ? "IN" : "OUT",
              //    to_p[0],   to_p[1],    is_to_in    ? "IN" : "OUT",
              //    dist2_old, dist2_new,
              //    (i32)sqrtf(dist2_old) + (is_from_in ? 128 : 0)
              // );
            }
          }
        }
      }
    }

    // TODO: debug
    // Write sdf PGM
    if (fwrite_pgm("hr.pgm", hires_bitmap, hires_sz[0], hires_sz[1])) {
        printf("Saved High-Res bitmap (%dx%d) to '%s'.\n", hires_sz[0], hires_sz[1], "hr.pgm");
    }

    if (fwrite_pgm(out_filename, sdf_bitmap, sdf_sz[0], sdf_sz[1])) {
        printf("Saved SDF bitmap (%dx%d) to '%s'.\n", sdf_sz[0], sdf_sz[1], out_filename);
    }
  }

  return 0;
}

int main(int argc, char **argv) {
  if (argc < 4) {
    fprintf(stderr, "Usage: font_raster <glyph_cell_height> <font.ttf> <out.pgm>\n");
    return 1;
  }

  i32 gly_h = atoi(argv[1]);          // single glyph height
  // i32 gly_w = gly_h;                  // monospace font
  const char *in_filename = argv[2];  // .ttf
  const char *out_filename = argv[3]; // .pgm

  FILE *in_file = fopen(in_filename, "rb");
  if (!in_file) {
    perror("fopen");
    return 1;
  }

  // i32 high_res_factor = 64; // 32 or 64 is fine
  // i32 spread = 8;           // spread 6-8 is fine, more spread -> more precesion outside of the shape and less in shape
  //
  // i32 sdf_w = gly_w * high_res_factor;
  // i32 sdf_h = gly_h * high_res_factor;
  // i32 sdf_gly_w = sdf_w - spread * 2; // allow at least spread distance between SDF glyph and border
  // i32 sdf_gly_h = sdf_h - spread * 2;
  //
  // i32 grid_cols = 16;
  // i32 grid_rows = 16;
  // u32 uimg_w = (u32)gly_w * grid_cols; // avoid integer overflow UB optimizations
  // u32 uimg_h = (u32)gly_h * grid_rows;
  // i32 img_w = (i32)uimg_w;
  // i32 img_h = (i32)uimg_h;
  //
  // if (gly_h <= 0) {
  //   fprintf(stderr, "Usage: font_raster <glyph_cell_height> <font.ttf> <out.pgm>\n");
  //   fprintf(stderr, "Error: <glyph_cell_height> shall be > 0. (%d <= 0)\n", gly_h);
  //   return 1;
  // }
  //
  // if (uimg_w > INT32_MAX || uimg_h > INT32_MAX) {
  //   fprintf(stderr, "Error: output image dimensions > %d (%ux%u)\n", INT32_MAX, uimg_w, uimg_h);
  //   return 1;
  // }
  //
  // if (sdf_gly_w <= 0 || sdf_gly_h <= 0) {
  //   fprintf(stderr, "Error: spread (%d) is too big relative to glyph dimension (%dx%d). Decrease spread or increase glyph dimensions.\n", spread, gly_w, gly_h);
  //   return 1;
  // }

  fseek(in_file, 0, SEEK_END);
  long size = ftell(in_file);
  fseek(in_file, 0, SEEK_SET);

  u8 *ttf_buf = (u8 *)malloc(size);
  if (fread(ttf_buf, size, 1, in_file) != 1) {
    perror("fread");
    return 1;
  }
  fclose(in_file);

  stbtt_fontinfo font;
  if (!stbtt_InitFont(&font, ttf_buf, stbtt_GetFontOffsetForIndex(ttf_buf, 0))) {
    fprintf(stderr, "Failed: stbtt_InitFont().\n");
    return 1;
  }

  i32 hires_factor = 32;    // 32 or 64 is fine
  i32 spread = 6;           // spread 6-8 is fine, more spread -> more precesion outside of the shape and less in shape
  sdf(&font, gly_h, spread, hires_factor, out_filename);


  /*
  u8 *img_bitmap = (u8 *)malloc(img_w * img_h);
  u8 *sdf_bitmap = (u8 *)malloc(sdf_w * sdf_h);

  if (!img_bitmap || !sdf_bitmap) {
    fprintf(stderr, "Failed: memory allocation of %.3f MB.\n", (img_w * img_h + sdf_w * sdf_h) / 1024.0f / 1024.0f);
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

  i32 gly_font_height       = gly_ascent - gly_descent; // - dsecent, while font_descent is negative)
  i32 sdf_font_height       = sdf_ascent - sdf_descent;
  i32 gly_margin_y          = (gly_h - gly_font_height) / 2;
  i32 sdf_margin_y          = (sdf_h - sdf_font_height) / 2;
  i32 gly_baseline          = gly_margin_y + gly_ascent;
  i32 sdf_baseline          = sdf_margin_y + sdf_ascent;

  // for (i32 i = 0; i < 256; i++) {
  i32 i = 103;
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
  */

  /*
  // Write PGM
  FILE *out_file = fopen(out_filename, "wb");
  if (out_file) {
    fprintf(out_file, "P5\n%d %d\n255\n", img_w, img_h);
    if (fwrite(img_bitmap, img_w * img_h, 1, out_file) == 1) {
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
  /*
  // Write PGM
  FILE *out_file = fopen(out_filename, "wb");
  if (out_file) {
    fprintf(out_file, "P5\n%d %d\n255\n", img_w, img_h);
    if (fwrite(img_bitmap, img_w * img_h, 1, out_file) == 1) {
      printf("Saved 16x16 grid (%dx%d) to '%s'\n", img_w, img_h, out_filename);
    } else {
      fprintf(stderr, "Failed to save '%s'!\n", out_filename);
      perror("fopen");
      return 1;
    }
  }

  return 0;
}
  */
