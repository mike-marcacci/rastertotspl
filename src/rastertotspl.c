/*
 * rastertotspl - native CUPS filter for JIOSE J500 (and similar TSPL) label printers.
 *
 * Drop-in replacement for the vendor's Intel-only "rastertolabel" filter, which
 * cupsd on macOS 27 can no longer execute ("Bad CPU type in executable").
 * Output is byte-for-byte compatible with the vendor filter: it reads the same
 * PPD options and emits the same TSPL command stream.
 *
 * Usage (as invoked by CUPS): rastertotspl job-id user title copies options [file]
 *
 * Copyright 2026 Mike Marcacci
 * SPDX-License-Identifier: Apache-2.0
 */

#include <cups/cups.h>
#include <cups/ppd.h>
#include <cups/raster.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define WHITE_THRESHOLD 201 /* 8-bit gray >= this prints as white (matches vendor filter) */
#define DOTS_PER_MM     8   /* 203 dpi */
#define PREAMBLE_NULS   200 /* vendor filter sends 200 NUL bytes before every page */

static volatile sig_atomic_t canceled = 0;

static void cancel_job(int sig) { (void)sig; canceled = 1; }

/* Integer value of the marked choice for an option, or `def` if unset or non-numeric ("Default"). */
static int choice_int(ppd_file_t *ppd, const char *option, int def, int *is_default) {
  ppd_choice_t *c = ppd ? ppdFindMarkedChoice(ppd, option) : NULL;
  char *end;
  long v;

  if (is_default) *is_default = 1;
  if (!c) return def;
  v = strtol(c->choice, &end, 10);
  if (end == c->choice || *end) return def;
  if (is_default) *is_default = 0;
  return (int)v;
}

static const char *choice_str(ppd_file_t *ppd, const char *option, const char *def) {
  ppd_choice_t *c = ppd ? ppdFindMarkedChoice(ppd, option) : NULL;
  return c ? c->choice : def;
}

static void start_page(ppd_file_t *ppd, const cups_page_header2_t *h, unsigned bytes_per_row) {
  static const char nuls[PREAMBLE_NULS];
  int dflt;
  const char *media = choice_str(ppd, "zeMediaTracking", "Gap");
  int gap_height = choice_int(ppd, "GapOrMarkHeight", 3, NULL);
  int gap_offset = choice_int(ppd, "GapOrMarkOffset", 0, NULL);
  int density, speed;

  fwrite(nuls, 1, sizeof nuls, stdout);
  /* Label size in whole mm, rounded up from the raster's dot dimensions. */
  printf("SIZE %u mm,%u mm\r\n",
         (h->cupsWidth + DOTS_PER_MM - 1) / DOTS_PER_MM,
         (h->cupsHeight + DOTS_PER_MM - 1) / DOTS_PER_MM);
  printf("REFERENCE %d,%d\r\n",
         choice_int(ppd, "AdjustHoriaontal", 0, NULL) * DOTS_PER_MM, /* sic: PPD spelling */
         choice_int(ppd, "AdjustVertical", 0, NULL) * DOTS_PER_MM);
  printf("DIRECTION %d,0\r\n", choice_int(ppd, "Rotate", 0, NULL));

  if (!strcmp(media, "Continuous"))
    printf("GAP 0 mm,0 mm\r\n");
  else if (!strcmp(media, "BLine"))
    printf("BLINE %d mm,%d mm\r\n", gap_height, gap_offset);
  else
    printf("GAP %d mm,%d mm\r\n", gap_height, gap_offset);

  printf("OFFSET %d mm\r\n", choice_int(ppd, "FeedOffset", 0, NULL));

  density = choice_int(ppd, "Darkness", 0, &dflt);
  if (!dflt) printf("DENSITY %d\r\n", density);
  speed = choice_int(ppd, "zePrintRate", 0, &dflt);
  if (!dflt) printf("SPEED %d\r\n", speed);

  printf("SETC AUTODOTTED %s\r\n", choice_int(ppd, "AutoDotted", 0, NULL) ? "ON" : "OFF");
  printf("SETC PAUSEKEY ON\r\n");
  printf("SETC WATERMARK OFF\r\n");
  printf("CLS\r\n");
  printf("BITMAP 0,0,%u,%u,1,", bytes_per_row, h->cupsHeight);
}

/* Convert one raster line to TSPL bitmap bits (1 = white, 0 = black, MSB first). */
static void pack_line(const cups_page_header2_t *h, const unsigned char *in, unsigned char *out,
                      unsigned bytes_per_row) {
  int black_is_high = h->cupsColorSpace == CUPS_CSPACE_K; /* K: higher value = more ink */

  memset(out, 0xff, bytes_per_row);
  for (unsigned x = 0; x < h->cupsWidth; x++) {
    int black;

    if (h->cupsBitsPerPixel == 1) {
      int v = (in[x >> 3] >> (7 - (x & 7))) & 1;
      black = black_is_high ? v : !v;
    } else {
      unsigned char v = in[x * (h->cupsBitsPerPixel / 8)];
      if (black_is_high) v = 255 - v;
      black = v < WHITE_THRESHOLD;
    }
    if (black) out[x >> 3] &= (unsigned char)~(0x80 >> (x & 7));
  }
}

int main(int argc, char *argv[]) {
  int fd = 0;
  cups_raster_t *ras;
  cups_page_header2_t header;
  ppd_file_t *ppd;
  int page = 0;

  if (argc < 6 || argc > 7) {
    fputs("ERROR: rastertotspl job-id user title copies options [file]\n", stderr);
    return 1;
  }

  if (argc == 7 && (fd = open(argv[6], O_RDONLY)) < 0) {
    perror("ERROR: Unable to open raster file");
    return 1;
  }

  signal(SIGTERM, cancel_job);
  signal(SIGPIPE, SIG_IGN);

  ppd = ppdOpenFile(getenv("PPD"));
  if (ppd) {
    cups_option_t *options = NULL;
    int num_options = cupsParseOptions(argv[5], 0, &options);
    ppdMarkDefaults(ppd);
    cupsMarkOptions(ppd, num_options, options);
    cupsFreeOptions(num_options, options);
  } else {
    fprintf(stderr, "DEBUG: Unable to open PPD (%s), using built-in defaults.\n",
            ppdErrorString(ppdLastError(NULL)));
  }

  ras = cupsRasterOpen(fd, CUPS_RASTER_READ);

  while (!canceled && cupsRasterReadHeader2(ras, &header)) {
    unsigned bytes_per_row = (header.cupsWidth + 7) / 8;
    unsigned char *line, *bits;

    page++;
    fprintf(stderr, "PAGE: %d %d\n", page, header.NumCopies);
    fprintf(stderr, "DEBUG: cupsWidth=%u cupsHeight=%u cupsBitsPerPixel=%u cupsColorSpace=%d\n",
            header.cupsWidth, header.cupsHeight, header.cupsBitsPerPixel, header.cupsColorSpace);

    if (header.cupsBitsPerPixel != 1 && header.cupsBitsPerPixel % 8) {
      fprintf(stderr, "ERROR: Unsupported cupsBitsPerPixel %u\n", header.cupsBitsPerPixel);
      break;
    }

    line = malloc(header.cupsBytesPerLine);
    bits = malloc(bytes_per_row);
    if (!line || !bits) {
      fputs("ERROR: Out of memory\n", stderr);
      return 1;
    }

    start_page(ppd, &header, bytes_per_row);

    for (unsigned y = 0; y < header.cupsHeight && !canceled; y++) {
      if (cupsRasterReadPixels(ras, line, header.cupsBytesPerLine) < 1) {
        memset(line, header.cupsColorSpace == CUPS_CSPACE_K ? 0 : 0xff, header.cupsBytesPerLine);
      }
      pack_line(&header, line, bits, bytes_per_row);
      fwrite(bits, 1, bytes_per_row, stdout);
    }

    printf("\nPRINT 1,1\r\n");
    fflush(stdout);
    free(line);
    free(bits);
  }

  cupsRasterClose(ras);
  if (ppd) ppdClose(ppd);
  if (fd) close(fd);

  if (page == 0) {
    fputs("ERROR: No pages found!\n", stderr);
    return 1;
  }
  return canceled ? 1 : 0;
}
