/*
 * gen - write a deterministic CUPS raster test image to stdout.
 *
 * Usage: gen WIDTH HEIGHT PAGES PAGE_W_PT PAGE_H_PT [w8|k1]
 *
 *   w8  8-bit gray (CUPS_CSPACE_W, 0 = black), like macOS cgpdftoraster produces
 *   k1  1-bit black (CUPS_CSPACE_K, 1 = black), same image pre-thresholded
 *
 * Image: top half a horizontal gradient, then a 48-row noise band, then white.
 *
 * Copyright 2026 Mike Marcacci
 * SPDX-License-Identifier: Apache-2.0
 */
#include <cups/raster.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static uint32_t rng = 42;
static uint8_t next_byte(void) {
  rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
  return (uint8_t)rng;
}

int main(int argc, char **argv) {
  if (argc < 6) return 2;
  unsigned w = atoi(argv[1]), h = atoi(argv[2]), pages = atoi(argv[3]);
  int k1 = argc > 6 && !strcmp(argv[6], "k1");
  cups_raster_t *r = cupsRasterOpen(1, CUPS_RASTER_WRITE);
  cups_page_header2_t hd;
  uint8_t *gray = malloc(w), *line = malloc(w);

  memset(&hd, 0, sizeof hd);
  hd.cupsWidth = w; hd.cupsHeight = h; hd.NumCopies = 1;
  hd.HWResolution[0] = hd.HWResolution[1] = 203;
  hd.PageSize[0] = atoi(argv[4]); hd.PageSize[1] = atoi(argv[5]);
  hd.cupsBitsPerColor = hd.cupsBitsPerPixel = k1 ? 1 : 8;
  hd.cupsBytesPerLine = k1 ? (w + 7) / 8 : w;
  hd.cupsColorSpace = k1 ? CUPS_CSPACE_K : CUPS_CSPACE_W;

  for (unsigned p = 0; p < pages; p++) {
    cupsRasterWriteHeader2(r, &hd);
    for (unsigned y = 0; y < h; y++) {
      for (unsigned x = 0; x < w; x++)
        gray[x] = y < h / 2 ? x * 256 / w : y < h / 2 + 48 ? next_byte() : 255;
      if (k1) {
        memset(line, 0, hd.cupsBytesPerLine);
        for (unsigned x = 0; x < w; x++)
          if (gray[x] < 201) line[x >> 3] |= 0x80 >> (x & 7);
      } else {
        memcpy(line, gray, w);
      }
      cupsRasterWritePixels(r, line, hd.cupsBytesPerLine);
    }
  }
  cupsRasterClose(r);
  return 0;
}
