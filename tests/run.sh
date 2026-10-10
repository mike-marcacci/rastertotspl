#!/bin/sh
# Run every case in cases.txt through a filter and compare with the recorded fixtures.
# Usage: tests/run.sh FILTER GEN PPD
set -u
FILTER="$1"; GEN="$2"; PPD="$3"
DIR="$(cd "$(dirname "$0")" && pwd)"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
export PPD
pass=0; fail=0

check() { # name expected actual
  if cmp -s "$2" "$3"; then echo "ok   $1"; pass=$((pass + 1)); else echo "FAIL $1"; fail=$((fail + 1)); fi
}

grep -v -e '^#' -e '^[[:space:]]*$' "$DIR/cases.txt" | while IFS='|' read -r name args opts; do
  name=$(echo $name); opts=$(echo $opts)
  gunzip -c "$DIR/fixtures/$name.tspl.gz" > "$TMP/expected"
  # shellcheck disable=SC2086
  "$GEN" $args w8 > "$TMP/in.ras"
  "$FILTER" 1 test test 1 "$opts" "$TMP/in.ras" > "$TMP/out" 2> "$TMP/err"
  check "$name" "$TMP/expected" "$TMP/out"
  # Same image as a 1-bit K raster must produce identical output.
  "$GEN" $args k1 | "$FILTER" 1 test test 1 "$opts" > "$TMP/out1" 2> "$TMP/err"
  check "$name (1-bit input, stdin)" "$TMP/expected" "$TMP/out1"
  echo "$pass $fail" > "$TMP/counts"
done

read -r pass fail < "$TMP/counts"
echo "$pass passed, $fail failed"
[ "$fail" = 0 ]
