#!/bin/sh
# Re-record fixtures from the vendor's x86_64 filter (requires Rosetta 2 and the
# vendor driver package). Only needed when adding test cases.
# Usage: tests/record-fixtures.sh GEN PPD [VENDOR_FILTER]
set -eu
GEN="$1"; PPD="$2"
VENDOR="${3:-/Library/Printers/Jiose/Filter/rastertolabel}"
DIR="$(cd "$(dirname "$0")" && pwd)"
export PPD

grep -v -e '^#' -e '^[[:space:]]*$' "$DIR/cases.txt" | while IFS='|' read -r name args opts; do
  name=$(echo $name); opts=$(echo $opts)
  # shellcheck disable=SC2086
  "$GEN" $args w8 | arch -x86_64 "$VENDOR" 1 test test 1 "$opts" 2>/dev/null \
    | gzip -9n > "$DIR/fixtures/$name.tspl.gz"
  echo "recorded $name"
done
