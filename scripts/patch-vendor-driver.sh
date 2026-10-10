#!/bin/sh
# For machines with the vendor JIOSE driver already installed: install the native
# filter next to the vendor's Intel-only one and repoint the vendor PPDs and an
# existing print queue at it, keeping the queue's settings.
# Usage: make && sudo scripts/patch-vendor-driver.sh [queue-name]   (default: JIOSE_J500)
set -eu

QUEUE="${1:-JIOSE_J500}"
BIN="$(cd "$(dirname "$0")/.." && pwd)/build/rastertotspl"
FILTER=/Library/Printers/Jiose/Filter/rastertotspl
OLD='/Library/Printers/Jiose/Filter/rastertolabel'
VENDOR_PPD_GZ='/Library/Printers/PPDs/Contents/Resources/Jiose Label printer.ppd.gz'
VENDOR_PPD='/Library/Printers/Jiose/PPDs/Jiose Label printer.ppd'
# Backups must live outside the PPD search paths, or macOS lists a duplicate driver.
BACKUP_DIR=/Library/Printers/Jiose/Backup

[ "$(id -u)" = 0 ] || { echo "Run with sudo." >&2; exit 1; }
[ -x "$BIN" ] || { echo "Build first: make" >&2; exit 1; }

install -d -o root -g wheel -m 755 "$(dirname "$FILTER")"
install -o root -g wheel -m 755 "$BIN" "$FILTER"

# Vendor PPDs (so re-adding the printer later picks up the native filter).
install -d -o root -g wheel -m 755 "$BACKUP_DIR"
for f in "$VENDOR_PPD_GZ.orig" "$VENDOR_PPD.orig"; do  # migrate backups from older installs
  if [ -f "$f" ]; then mv "$f" "$BACKUP_DIR/$(basename "$f")"; fi
done
if [ -f "$VENDOR_PPD_GZ" ]; then
  B="$BACKUP_DIR/$(basename "$VENDOR_PPD_GZ").orig"
  [ -f "$B" ] || cp -p "$VENDOR_PPD_GZ" "$B"
  gunzip -c "$B" | sed "s#$OLD#$FILTER#" | gzip -9 > "$VENDOR_PPD_GZ"
fi
if [ -f "$VENDOR_PPD" ]; then
  B="$BACKUP_DIR/$(basename "$VENDOR_PPD").orig"
  [ -f "$B" ] || cp -p "$VENDOR_PPD" "$B"
  sed -i '' "s#$OLD#$FILTER#" "$VENDOR_PPD"
fi

# Existing queue: push an updated copy of its PPD (keeps its configured defaults).
QUEUE_PPD="/etc/cups/ppd/$QUEUE.ppd"
if [ -f "$QUEUE_PPD" ]; then
  TMP="$(mktemp -t jiose).ppd"
  sed "s#$OLD#$FILTER#" "$QUEUE_PPD" > "$TMP"
  lpadmin -p "$QUEUE" -P "$TMP"
  rm -f "$TMP"
  cupsenable "$QUEUE"
  echo "Updated queue $QUEUE."
else
  echo "Queue $QUEUE not found; re-add the printer in System Settings." >&2
fi

grep cupsFilter "$QUEUE_PPD" 2>/dev/null || true
echo "Done."
