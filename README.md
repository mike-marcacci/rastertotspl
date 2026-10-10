# rastertotspl

A native CUPS driver for the **JIOSE J500** thermal label printer (and likely other
TSPL-based 4×6" shipping label printers) on macOS, including Apple Silicon.

## Why

JIOSE's macOS driver ships a single Intel-only (`x86_64`) CUPS filter. Starting with
macOS 27, `cupsd` no longer runs Intel filters through Rosetta 2, so printing fails
with:

> The printer software is not compatible with this device.

…and `/var/log/cups/error_log` shows:

```text
execv of /Library/Printers/Jiose/Filter/rastertolabel failed. err:86, Bad CPU type in executable
```

Installing Rosetta or reinstalling the vendor driver doesn't help.

`rastertotspl` is an independent replacement, built as a universal binary (arm64 + x86_64).
It contains no vendor code. It was written from the filter's observable behavior: the
TSPL it emits for a given raster and set of options.
Its output is **byte-for-byte identical** to the vendor filter's for the same input and
options. The test suite checks this against fixtures recorded from the vendor filter.

## Install

Requires the Xcode Command Line Tools (`xcode-select --install`).

```sh
git clone https://github.com/mike-marcacci/rastertotspl.git
cd rastertotspl
make && make test
sudo make install
```

Then open **System Settings → Printers & Scanners → Add Printer**, select the J500, and
choose the driver **JIOSE J500 TSPL (rastertotspl, native)**.

To uninstall: `sudo make uninstall`, then remove the printer in System Settings.

### Already have the vendor driver installed?

To keep your existing print queue and its settings, patch the vendor driver in place
instead of running `make install`:

```sh
make
sudo scripts/patch-vendor-driver.sh            # queue name defaults to JIOSE_J500
```

This installs the filter at `/Library/Printers/Jiose/Filter/rastertotspl` and repoints the
vendor PPDs and your queue at it. It backs up the originals to
`/Library/Printers/Jiose/Backup` (outside the PPD search path, so macOS doesn't list a
duplicate driver). Reinstalling JIOSE's package undoes this.

## Printer options

These map to TSPL commands sent before each label:

| Option | TSPL |
|---|---|
| Darkness (0–15, or printer default) | `DENSITY n` |
| Speed (1–8 in/s, or printer default) | `SPEED n` |
| Media Type: Gap / Black Mark / Continuous | `GAP h mm,o mm` / `BLINE h mm,o mm` / `GAP 0 mm,0 mm` |
| Gap/Mark Height, Offset (mm) | arguments to `GAP` / `BLINE` |
| Print Direction | `DIRECTION n,0` |
| Horizontal / Vertical Adjust (mm) | `REFERENCE x,y` (8 dots/mm) |
| Feed after Print (mm) | `OFFSET n mm` |
| Dotted Line Between Labels | `SETC AUTODOTTED ON/OFF` |

The page image is sent as a 1-bit `BITMAP`. Gray values below 201 (out of 255) print
black, which matches the vendor filter.

The option keywords match JIOSE's PPD, including its `AdjustHoriaontal` spelling, so
either PPD works with this filter. The vendor PPD offered 90°/270° rotation, but TSPL's
`DIRECTION` only supports 0 and 1. This PPD exposes just those two.

## Other printers

Many cheap 203 dpi label printers speak TSPL and ship the same Intel-only filter under
other brands. If yours does, this driver will probably work. Pick it manually when adding
the printer. Reports of working models are welcome.

## Development

```text
src/rastertotspl.c        the filter
ppd/genppd.py             generates the PPD (ppd/rastertotspl.ppd is the committed output)
tests/cases.txt           test cases: raster dimensions + print options
tests/fixtures/           expected output, recorded from the vendor filter
tests/gen.c               deterministic test raster generator
```

`make test` runs each case twice: once as 8-bit gray from a file (like macOS's
`cgpdftoraster` produces) and once as pre-thresholded 1-bit raster on stdin. Both must
match the fixture exactly.

To add a test case, add a line to `tests/cases.txt` and run `make record-fixtures`. This
needs an Intel Mac or Rosetta 2, plus the vendor driver installed, because it runs the
vendor filter to record the expected output.

To debug a real print job, set `LogLevel debug` in `/etc/cups/cupsd.conf`
(`sudo cupsctl --debug-logging`) and watch `/var/log/cups/error_log`.

## License

Apache License 2.0. See [LICENSE](LICENSE).

Not affiliated with or endorsed by JIOSE. Product names are trademarks of their
respective owners.

## Special Thanks

Claude Code put this together in all of 10 minutes. It's truly incredible that we
live in a world where we can bark "My Jiose J500 label printer is not working on
MacOS 27 despite having Rosetta 2 installed. Diagnose the issue and create a fix."
and have a complete solution 10 minutes later.

I'm publishing this in case somebody is looking for the answer online first, as I
did, out of habbit.

Jiose and everybody else: you should open source everything. Somebody else will
spend their tokens on a solution to this kind of thing once, and provide you a PR
before you even know there's a problem.
