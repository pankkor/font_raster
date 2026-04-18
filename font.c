// "Monospace font rasterizer and single channel Signed Distance Field (SDF) generator
//
// Build
//   clang -Wall -Wextra -Wpedantic -O3 font.c -o font

const char * usage =
"Monospace font rasterizer and single channel Signed Distance Field (SDF) generator.\n"
"\n"
"USAGE\n"
"  font <-h|help|rst|sdf> ...\n"
"\n"
"To rasterize first 256 code points of TrueType font to a .PGM file of 16x16 cells:\n"
"  font rst <glyph_cell_height> <font.ttf> <out.pgm>\n"
"\n"
"To create SDF from the first 256 code points of TrueType font to a .PGM file of 16x16 cells:\n"
"  font sdf <glyph_cell_height> <font.ttf> <out.pgm> [sdf_hi_res_factor=32] [sdf_spread=6]\n"
"\n"
"ARGUMENTS\n"
"<glyph_cell_height>  - Height of glyph cell in resulting image. For SDF cell will fit glyph with 2 * sdf_spread.\n"
"<font.ttf>           - File for TrueType font (the first font it taken)\n"
"<out.pgm>            - Resulting .PGM image\n"
"\n"
"OPTIONAL SDF ARGUMENTS:\n"
"[sdf_hi_res_factor]  - SDF: glyph is rendered to high-resolution bitmap, SDF is calculated on high-resolution bitmap it and then it's donwscaled back.\n"
"                       Default: 32\n"
"[sdf_spread]         - SDF is calculated in spread range. Usually 6-8 is fine. Use bigger spread for effects (glow).\n"
"                       More spread -> more precision outside of glyph shape and less in shape.\n"
"                       Default: 6\n";

#include <stdio.h>
#include <stdlib.h>

typedef signed char         i8;
typedef unsigned char       u8;
typedef int                 i32;
typedef unsigned int        u32;
typedef unsigned long long  u64;
typedef float               f32;
typedef i32                 b32;

#define MAX(a, b)           ((a) > (b) ? (a) : (b))
#define MIN(a, b)           ((a) < (b) ? (a) : (b))

#if defined(__clang__)
  #pragma clang diagnostic ignored "-Wnullability-extension"
  #define NONNULL           _Nonnull
#else
  #define NONNULL
#endif

#if defined(__GNUC__)
  #define INLINE            inline __attribute__((always_inline))
  #define ALIGNED(N)        __attribute__((aligned(N)))
#else
  #define INLINE
  #define ALIGNED(N)
#endif

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

// Is in (0, INT32_MAX] range
b32 is_gt_zero_i32(u64 v) {
  return v > 0 && v <= INT32_MAX;
}

// Write single channel `bitmap` buffer with `w` and `h` dimensions to filename
b32 fwrite_pgm(const char * NONNULL filename, u8 *bitmap, i32 w, i32 h) {
  b32 ret = 0;
  FILE *out_file = fopen(filename, "wb");
  if (out_file) {
    fprintf(out_file, "P5\n%d %d\n255\n", w, h);
    ret = bitmap ? fwrite(bitmap, w * h, 1, out_file) : 1;
    if (ret != 1) {
      fprintf(stderr, "Failed to save '%s'.\n", filename);
      perror("fwrite");
    }
    fclose(out_file);
  } else {
    fprintf(stderr, "Failed to open '%s'.\n", filename);
    perror("fopen");
  }
  return ret;
}

void* alloc_aligned(u64 size, u64 alignment, void** out_raw) {
  void *raw;
  void *aligned;
  b32 is_pow2_aligned;

  aligned = 0;

  is_pow2_aligned = alignment >= sizeof(void *) && (alignment & (alignment - 1)) == 0;
  assert(is_pow2_aligned);

  if (is_pow2_aligned)
  {
    raw = malloc(size + alignment - 1);
    if (raw) {
      aligned = (void *)(((u64)raw + alignment - 1) & ~(alignment - 1));
    }
    if (out_raw) {
      *out_raw = raw;
    }
  }

  return aligned;
}

// Calculate SDF
//  sdf_bitmap    - bitmap to fill with SDF
//  hires_bitmap  - high-resolution bitmap to calculate SDF from
//  sdf_sz        - size of sdf_bitmap and hires_bitmap
//  hires_spread  - spread already rescaled by hires_factor
void calc_sdf(u8 * NONNULL restrict sdf_bitmap, u8 * NONNULL restrict hires_bitmap, i32 sdf_sz[NONNULL restrict 2], i32 hires_spread) {
    u64 bitmap_from_off;  // offset of a point in SDF bitmap buffer to be calculated
    i32 from_p[2];        // calculate distance from point
    i32 to_p[2];          // calculate distance to   point
    i32 spread_min[2];    // minimum spread, ensure from_p - hires_spread >= 0
    i32 spread_max[2];    // maximum spread, ensure from_p + hires_spread < sdf_sz
    i32 d_p[2];           // displacement vector (from_p - to_p), get it iterating over [spread_min, spread_max] range
    i32 min_dist2;        // min distance^2 between from_p and to_p to be stored in SDF
    i32 cur_dist2;        // current distance^2 between from_p and to_p
    f32 dist_n;           // distance normalized
    b32 is_from_in;       // is from point IN the glyph
    b32 is_to_in;         // is   to point IN the glyph
    i32 hires_spread2;    // hires_spread^2
    u8 from_v;            // from point high-resolution bitmap value
    u8 to_v;              // to   point high-resolution bitmap value

    hires_spread2       = hires_spread * hires_spread;

    // For each from_p
    for   (from_p[1] = 0; from_p[1] < sdf_sz[1]; ++from_p[1]) {
      for (from_p[0] = 0; from_p[0] < sdf_sz[0]; ++from_p[0]) {
        // Is from_p IN?
        bitmap_from_off = from_p[1] * sdf_sz[0] + from_p[0];
        from_v          = hires_bitmap[bitmap_from_off];
        is_from_in      = from_v > 128;
        min_dist2       = hires_spread2;

        // Ensure that to_p is not out of bounds
        spread_min[0]   = MAX(-hires_spread, -from_p[0]);
        spread_min[1]   = MAX(-hires_spread, -from_p[1]);
        spread_max[0]   = MIN( hires_spread, sdf_sz[0] - from_p[0] - 1);
        spread_max[1]   = MIN( hires_spread, sdf_sz[1] - from_p[1] - 1);

        // TODO: try searching in rings?
        // For each spread in [spread_min, spread_max];
        for   (d_p[1] = spread_min[1]; d_p[1] <= spread_max[1]; ++d_p[1]) {
          for (d_p[0] = spread_min[0]; d_p[0] <= spread_max[0]; ++d_p[0]) {
            // Get to_p
            to_p[0]   = from_p[0] + d_p[0];
            to_p[1]   = from_p[1] + d_p[1];

            // Is to_p IN?
            to_v      = hires_bitmap[to_p[1] * sdf_sz[0] + to_p[0]];
            is_to_in  = to_v > 128;

            cur_dist2 = d_p[0] * d_p[0] + d_p[1] * d_p[1];

            if (is_from_in != is_to_in && cur_dist2 < min_dist2) {
              min_dist2 = cur_dist2;
              // TODO: why is this faster?
              // dist_n                      = sqrtf((f32)min_dist2 / hires_spread2);
              // sdf_bitmap[bitmap_from_off] = is_from_in ? 128 + (i32)(dist_n * 127) : 128 - (i32)(dist_n * 128);
            }
          }
        }

        dist_n                      = sqrtf((f32)min_dist2 / hires_spread2);
        sdf_bitmap[bitmap_from_off] = is_from_in ? 128 + (i32)(dist_n * 127) : 128 - (i32)(dist_n * 128);
      }
    }
}

// Downsample using box filter. Every output filter is average values in factor * factor box
// out_width  * factor = in_width
// out_height * factor = in_height
void downsample_box(u8 * NONNULL restrict out_bitmap, i32 out_stride, u8 * NONNULL restrict in_bitmap, i32 in_w, i32 in_h, i32 factor) {
  i32 in_sz[2];
  i32 out_sz[2];
  i32 in[2];
  i32 out[2];
  i32 f[2];
  u64 factor2;
  u64 sum;

  in_sz [0] = in_w;
  in_sz [1] = in_h;
  out_sz[0] = in_sz[0] / factor;
  out_sz[1] = in_sz[1] / factor;
  factor2   = factor * factor;

  for   (out[1] = 0; out[1] < out_sz[1]; ++out[1]) {
    for (out[0] = 0; out[0] < out_sz[0]; ++out[0]) {
      sum = 0;
      for   (f[1] = 0; f[1] < factor; ++f[1]) {
        for (f[0] = 0; f[0] < factor; ++f[0]) {
          in[0] = out[0] * factor + f[0];
          in[1] = out[1] * factor + f[1];
          sum += in_bitmap[in[1] * in_sz[0] + in[0]];
        }
      }
      out_bitmap[out[1] * out_sz[0] + out[0]] = sum / factor2;
    }
  }
}

// Make single channel SDF font
//  img_cell_h    - hight of glyph cell in resulting image. Cell will fit glyph with 2*spread.
//  spread        - SDF is calculated in spread range. 6-8 is fine. Use bigger spread for effects (glow).
//                  More spread -> more precision outside of glyph shape and less in shape
//  hires_factor  - Factor to scale glyph to calculate SDF later. 32 or 64 is fine.
b32 main_sdf(const stbtt_fontinfo * NONNULL font, i32 img_cell_h, const char * NONNULL out_filename, i32 hires_factor, i32 spread) {
  u8 * restrict img_bitmap;   // Resulting out image buffer
  u8 * restrict hires_bitmap; // Helper high-resolution image buffer
  u8 * restrict sdf_bitmap;   // Helper high-resolution SDF image buffer
  u8 * restrict hires_dst;    // Rasterize glyph to this point in high-resolution buffer
  u8 * restrict img_dst;      // Rasterize glyph to this point in final bitmap buffer

  u64 img_usz[2];             // Unsigned version for checks to avoid integer overflow UB optimizations
  u64 hires_usz[2];           // Unsigned version for checks to avoid integer overflow UB optimizations
  i32 img_cell_sz[2];         // Cell size to contain resulting SDF image
  i32 img_gly_sz[2];          // Glyph size inside of the image accounting for `spread`
  i32 img_grid_sz[2];         // Cells arranged in grid dimensions [cols, rows]
  i32 img_sz[2];              // Resulting image total size
  i32 hires_sz[2];            // Size of high-resolution glyph image
  i32 hires_gly_sz[2];        // Size of glyph in high-resolution image considering spread
  i32 sdf_sz[2];              // Size of high-resolution SDF image
  i32 pos_grid[2];            // Position in grid {column, row}
  i32 img_p[2];               // Position in buffer

  u64 img_bitmap_bytes;       // Size in bytes of resulting bitmap.
  u64 hires_bitmap_bytes;     // Size in bytes of helper high-resolution bitmap.
  u64 sdf_bitmap_bytes;       // Size in bytes of helper SDF image. Equals high-resolution bitmap.
  u64 total_bitmap_bytes;     // Size in bytes of all bitmaps.

  i32 font_ascent;            // Distance from baseline to glyph top    (positive)
  i32 font_descent;           // Distance from baseline to glyph bottom (negative)
  i32 font_line_gap;          // Distance between lines (not used)
  f32 hires_font_scale;       // (dimensions * hires_font_scale) to get px in high-resolution image
  i32 hires_font_ascent;      // Font ascent  in high-resolution image
  i32 hires_font_descent;     // Font descent in high-resolution image
  i32 hires_font_height;      // Font height  in high-resolution image
  i32 hires_margin_y;         // Distance from glyph to top or bottom (includes spread) in high-resolution image
  i32 hires_baseline_bitmap;  // Offset of baseline in bitmap (font_ascent + margin_y)

  i32 codepoint;              // Codepoint to rasterize and calculate SDF
  i32 hires_bb_p0[2];         // High-resolution bounding box point 0
  i32 hires_bb_p1[2];         // High-resolution bounding box point 1
  i32 hires_bb_sz[2];         // High-resolution bounding box size
  i32 hires_p[2];             // High-resolution position in buffer
  i32 hires_spread;           // Spread rescaled by hires_factor

  img_cell_sz [0]     = img_cell_h;
  img_cell_sz [1]     = img_cell_h; // height - monospace font
  img_gly_sz  [0]     = img_cell_sz[0]      - 2 * spread;
  img_gly_sz  [1]     = img_cell_sz[1]      - 2 * spread;
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
  img_bitmap_bytes    = img_sz[0]           * img_sz[1];
  hires_bitmap_bytes  = hires_sz[0]         * hires_sz[1];
  sdf_bitmap_bytes    = sdf_sz[0]           * sdf_sz[1];
  total_bitmap_bytes  = img_bitmap_bytes    + hires_bitmap_bytes + sdf_bitmap_bytes;
  img_bitmap          = (u8 *)alloc_aligned(img_bitmap_bytes,   64, 0 /* THNF! */);
  hires_bitmap        = (u8 *)alloc_aligned(hires_bitmap_bytes, 64, 0 /* THNF! */);
  sdf_bitmap          = (u8 *)alloc_aligned(sdf_bitmap_bytes,   64, 0 /* THNF! */);

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
  hires_font_height     = hires_font_ascent - hires_font_descent; // - descent, while font_descent is negative

  hires_margin_y        = (hires_sz[1] - hires_font_height) / 2;
  hires_baseline_bitmap = hires_margin_y    + hires_font_ascent;

  // TODO
  u8 *test;
  // for (codepoint = 0; codepoint < 256; ++codepoint) {
  codepoint = 0;
  {
    stbtt_GetCodepointBitmapBox(font, codepoint,
        hires_font_scale, hires_font_scale,
        &hires_bb_p0[0],  &hires_bb_p0[1],
        &hires_bb_p1[0],  &hires_bb_p1[1]);

    hires_bb_sz[0]  = hires_bb_p1[0] - hires_bb_p0[0];
    hires_bb_sz[1]  = hires_bb_p1[1] - hires_bb_p0[1];

    hires_p[0]      = (hires_sz[0] - hires_bb_sz[0]) / 2;
    hires_p[1]      = hires_baseline_bitmap + hires_bb_p0[1]; // spread is already included

    hires_dst       = hires_bitmap + hires_p[1] * hires_sz[0] + hires_p[0];

    memset(hires_bitmap, 0, hires_bitmap_bytes);
    stbtt_MakeCodepointBitmap(font, hires_dst, hires_bb_sz[0], hires_bb_sz[1], hires_sz[0], hires_font_scale, hires_font_scale, codepoint);

    calc_sdf(sdf_bitmap, hires_bitmap, sdf_sz, hires_spread);

    test = malloc(sdf_sz[0] / hires_factor * sdf_sz[1] / hires_factor);
    downsample_box(test, 0, sdf_bitmap, sdf_sz[0], sdf_sz[1], hires_factor);

    /*
    // TODO:
    pos_grid[0]   = codepoint     % img_grid_sz[0];
    pos_grid[1]   = codepoint     / img_grid_sz[0];

    img_p[0]      = pos_grid[0]   * img_cell_sz[0] + hires_p[0] / hires_factor;
    img_p[1]      = pos_grid[1]   * img_cell_sz[1] + hires_p[1] / hires_factor;

    img_dst       = img_bitmap + img_p[1] * img_sz[0] + img_p[0];
    // TODO stride is not used
    downsample_box(img_dst, img_sz[0], sdf_bitmap, sdf_sz[0], sdf_sz[1], hires_factor);
    */
  }

  // TODO: test
  if (fwrite_pgm("test.pgm", test, sdf_sz[0] / hires_factor, sdf_sz[1] / hires_factor)) {
      printf("Saved Test bitmap (%dx%d) to '%s'.\n",  sdf_sz[0] / hires_factor, sdf_sz[1] / hires_factor, "test.pgm");
  }

  // TODO: debug
  // Write sdf PGM
  if (fwrite_pgm("hr.pgm", hires_bitmap, hires_sz[0], hires_sz[1])) {
      printf("Saved High-Res bitmap (%dx%d) to '%s'.\n", hires_sz[0], hires_sz[1], "hr.pgm");
  }

  if (fwrite_pgm(out_filename, sdf_bitmap, sdf_sz[0], sdf_sz[1])) {
      printf("Saved SDF bitmap (%dx%d) to '%s'.\n", sdf_sz[0], sdf_sz[1], out_filename);
  }

  return 0;
}

// Rasterize font
//  img_cell_h    - hight of glyph cell in resulting image. Cell will fit glyph with 2*spread.
b32 main_rst(const stbtt_fontinfo * NONNULL font, i32 img_cell_h, const char * NONNULL out_filename) {
  u8 * restrict img_bitmap;   // Resulting out image buffer
  u8 * restrict img_dst;      // Rasterize glyph to this point in final bitmap buffer

  u64 img_usz[2];             // Unsigned version for checks to avoid integer overflow UB optimizations
  i32 img_cell_sz[2];         // Cell size to contain resulting SDF image
  i32 img_grid_sz[2];         // Cells arranged in grid dimensions [cols, rows]
  i32 img_sz[2];              // Resulting image total size

  u64 img_bitmap_bytes;       // Size in bytes of resulting bitmap.
  u64 total_bitmap_bytes;     // Size in bytes of all bitmaps.

  i32 font_ascent;            // Distance from baseline to glyph top    (positive)
  i32 font_descent;           // Distance from baseline to glyph bottom (negative)
  i32 font_line_gap;          // Distance between lines (not used)
  f32 img_font_scale;         // (dimensions * img_font_scale) to get px in final image
  i32 img_font_ascent;        // Font ascent  in final image pixels
  i32 img_font_descent;       // Font descent in final image pixels
  i32 img_font_height;        // Font height  in final image pixels
  i32 img_margin_y;           // Distance from glyph to top or bottom (includes spread) in final image
  i32 img_baseline_bitmap;    // Offset of baseline in final bitmap (font_ascent + margin_y)

  i32 codepoint;              // Codepoint to raster
  i32 pos_grid[2];            // Position in grid {column, row}
  i32 img_bb_p0[2];           // Bounding box point 0
  i32 img_bb_p1[2];           // Bounding box point 1
  i32 img_bb_sz[2];           // Bounding box size
  i32 img_p[2];               // Position in buffer

  img_cell_sz [0]     = img_cell_h;
  img_cell_sz [1]     = img_cell_h; // height - monospace font
  img_grid_sz [0]     = 16;
  img_grid_sz [1]     = 16;
  img_usz     [0]     = (u64)img_cell_sz[0] * img_grid_sz[0];
  img_usz     [1]     = (u64)img_cell_sz[1] * img_grid_sz[1];
  img_sz      [0]     = (i32)img_usz[0];
  img_sz      [1]     = (i32)img_usz[1];

  // Alloc
  img_bitmap_bytes    = img_sz[0]   * img_sz[1];
  total_bitmap_bytes  = img_bitmap_bytes;
  img_bitmap          = (u8 *)alloc_aligned(img_bitmap_bytes,   64, 0 /* THNF! */);

  // Sanity checks
  if (!img_bitmap) {
    fprintf(stderr, "Failed: memory allocation of %.2f MB.\n", total_bitmap_bytes / 1024.0f / 1024.0f);
    return 1;
  }

  if (!is_gt_zero_i32(img_usz[0]) || !is_gt_zero_i32(img_usz[1])) {
    fprintf(stderr, "Error: bad output image dimensions %llux%llu. Max allowed dimensions %dx%d\n", img_usz[0], img_usz[1], INT32_MAX, INT32_MAX);
    return 1;
  }

  img_font_scale = stbtt_ScaleForPixelHeight(font, (f32)img_cell_sz[1]);
  stbtt_GetFontVMetrics(font, &font_ascent, &font_descent, &font_line_gap);
  (void)font_line_gap; // not used

  // Rescale to high resolution bitmap pixels
  img_font_ascent     = font_ascent       * img_font_scale;
  img_font_descent    = font_descent      * img_font_scale;
  img_font_height     = img_font_ascent   - img_font_descent; // - descent, while font_descent is negative

  img_margin_y        = (img_cell_sz[1]   - img_font_height) / 2;
  img_baseline_bitmap = img_margin_y      + img_font_ascent;

  memset(img_bitmap, 0, img_bitmap_bytes);

  for (codepoint = 0; codepoint < 256; ++codepoint) {
    stbtt_GetCodepointBitmapBox(font, codepoint,
        img_font_scale, img_font_scale,
        &img_bb_p0[0],  &img_bb_p0[1],
        &img_bb_p1[0],  &img_bb_p1[1]);

    pos_grid[0]   = codepoint     % img_grid_sz[0];
    pos_grid[1]   = codepoint     / img_grid_sz[0];

    img_bb_sz[0]  = img_bb_p1[0]  - img_bb_p0[0];
    img_bb_sz[1]  = img_bb_p1[1]  - img_bb_p0[1];

    img_p[0]      = pos_grid[0]   * img_cell_sz[0] + (img_cell_sz[0]      - img_bb_sz[0]) / 2;
    img_p[1]      = pos_grid[1]   * img_cell_sz[1] + img_baseline_bitmap  + img_bb_p0[1];

    img_dst       = img_bitmap + img_p[1] * img_sz[0] + img_p[0];

    stbtt_MakeCodepointBitmap(font, img_dst, img_bb_sz[0], img_bb_sz[1], img_sz[0], img_font_scale, img_font_scale, codepoint);
  }

  if (fwrite_pgm(out_filename, img_bitmap, img_sz[0], img_sz[1])) {
      printf("Saved rasterized font bitmap (%dx%d) to '%s'.\n", img_sz[0], img_sz[1], out_filename);
  }

  return 0;
}

int main(int argc, char **argv) {
  stbtt_fontinfo font;
  FILE          *ttf_file;
  u8 * restrict ttf_buf;                // Buffer to hold font.ttf_file
  const char    *arg_ttf_filename;      // font.ttf in TrueType font file
  const char    *arg_out_filename;      // out.pgm to write resulting bitmap
  i32           arg_gly_cell_height;    // Hight of glyph cell in resulting image. For SDF cell will fit glyph with 2*spread.
  b32           arg_is_help;            // Is HELP requested
  b32           arg_is_rst;             // Is RST routine or simple rasterization
  b32           arg_is_sdf;             // Is SDF routine or SDF calculation
  i32           arg_sdf_hires_factor;   // SDF: glyph is rendered to high-resolution bitmap, SDF is calculated on high-resolution bitmap it and then it's downscaled back.
  i32           arg_sdf_spread;         // SDF is calculated in spread range. 6-8 is fine. Use bigger spread for effects (glow).
  i32           ttf_file_size;          // font.ttf file size
  i32           exit_code;              // exit_code

  exit_code = 1;

  arg_is_help           = argc > 1
    && (strncmp(argv[1], "-h", 2) == 0 || strncmp(argv[1], "help", 4) == 0);

  if (arg_is_help) {
    printf("%s", usage);
    exit_code = 0;
    goto main_cleanup;
  }

  // Arguments
  if (argc < 5) {
    fprintf(stderr, "%s", usage);
    goto main_cleanup;
  }

  arg_is_sdf            = strncmp(argv[1], "sdf", 3) == 0;
  arg_is_rst            = strncmp(argv[1], "rst", 3) == 0;
  arg_gly_cell_height   = atoi(argv[2]);
  arg_ttf_filename      = argv[3];
  arg_out_filename      = argv[4];
  arg_sdf_hires_factor  = argc > 5 ? atoi(argv[5]) : 32;
  arg_sdf_spread        = argc > 6 ? atoi(argv[6]) : 6;

  if (!(arg_is_sdf ^ arg_is_rst)) {
    fprintf(stderr, "Error: first argument shall be 'rst' or 'sdf'\n");
    fprintf(stderr, "%s", usage);
    goto main_cleanup;
  }

  if (!is_gt_zero_i32(arg_gly_cell_height)) {
    fprintf(stderr, "Error: bad glyph_cell_height (%d), out of range [0, %d]\n",  arg_gly_cell_height,  INT32_MAX);
    goto main_cleanup;
  }
  if (!is_gt_zero_i32(arg_sdf_spread)) {
    fprintf(stderr, "Error: bad sdf_spread (%d), out of range [0, %d]\n",         arg_sdf_spread,       INT32_MAX);
    goto main_cleanup;
  }
  if (!is_gt_zero_i32(arg_sdf_hires_factor)) {
    fprintf(stderr, "Error: bad sdf_hi_res_factor (%d), out of range [0, %d]\n",  arg_sdf_hires_factor, INT32_MAX);
    goto main_cleanup;
  }

  ttf_file = fopen(arg_ttf_filename, "rb");
  if (!ttf_file) {
    fprintf(stderr, "Error: failed to open TrueType font file '%s'\n",            arg_ttf_filename);
    perror("fopen");
    goto main_cleanup;
  }

  ttf_file = fopen(arg_ttf_filename, "rb");
  if (!ttf_file) {
    fprintf(stderr, "Error: failed to open TrueType font file '%s'\n",            arg_ttf_filename);
    perror("fopen");
    goto main_cleanup;
  }

  fseek(ttf_file, 0, SEEK_END);
  ttf_file_size = ftell(ttf_file);
  fseek(ttf_file, 0, SEEK_SET);

  ttf_buf = (u8 *)malloc(ttf_file_size);
  if (fread(ttf_buf, ttf_file_size, 1, ttf_file) != 1) {
    perror("fread");
    goto main_cleanup;
  }
  fclose(ttf_file);

  if (!stbtt_InitFont(&font, ttf_buf, stbtt_GetFontOffsetForIndex(ttf_buf, 0))) {
    fprintf(stderr, "Failed: stbtt_InitFont().\n");
    goto main_cleanup;
  }

  // Touch output file
  if (!fwrite_pgm(arg_out_filename, 0, 0, 0)) {
    goto main_cleanup;
  }

  if (arg_is_rst) {
    main_rst(&font, arg_gly_cell_height, arg_out_filename);
  } else {
    main_sdf(&font, arg_gly_cell_height, arg_out_filename, arg_sdf_hires_factor, arg_sdf_spread);
  }
  exit_code = 0;

main_cleanup:
  // THNF! - TRU3 H4KK3RS N3V3R FR33!

  return exit_code;
}
