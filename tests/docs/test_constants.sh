#!/usr/bin/env sh
# WS0-02: the generated block between <!-- constants:begin --> and <!-- constants:end --> in
# docs/architecture.md must be exactly the output of `build/dump_constants --markdown`.
# Run from anywhere; prints PASS: docs_constants and exits 0, or FAIL + a diff and exits 1.
set -u
cd "$(dirname "$0")/../.." || exit 1

DOC=docs/architecture.md
TOOL=build/dump_constants
OUT=build/tests
BEGIN='<!-- constants:begin -->'
END='<!-- constants:end -->'

mkdir -p "$OUT"

if [ ! -x "$TOOL" ]; then
  echo "FAIL: docs_constants ($TOOL missing; run 'make tools' first)"
  exit 1
fi
if [ ! -f "$DOC" ]; then
  echo "FAIL: docs_constants ($DOC missing)"
  exit 1
fi
if ! grep -q -F "$BEGIN" "$DOC" || ! grep -q -F "$END" "$DOC"; then
  echo "FAIL: docs_constants ($DOC has no $BEGIN ... $END block)"
  exit 1
fi

# Body of the marker block (markers excluded), blank lines dropped on both sides so surrounding
# whitespace in the doc never matters. CRs are stripped in case the doc was saved with CRLF.
awk -v b="$BEGIN" -v e="$END" '
  index($0, b) { inblk = 1; next }
  index($0, e) { inblk = 0 }
  inblk { print }
' "$DOC" | tr -d '\r' | sed '/^[[:space:]]*$/d' > "$OUT/docs_constants.doc"

"$TOOL" --markdown | sed '/^[[:space:]]*$/d' > "$OUT/docs_constants.gen"

if [ ! -s "$OUT/docs_constants.gen" ]; then
  echo "FAIL: docs_constants ($TOOL --markdown produced no output)"
  exit 1
fi
if [ ! -s "$OUT/docs_constants.doc" ]; then
  echo "FAIL: docs_constants (constants block in $DOC is empty)"
  exit 1
fi

if diff -u "$OUT/docs_constants.gen" "$OUT/docs_constants.doc" > "$OUT/docs_constants.diff" 2>&1; then
  echo "PASS: docs_constants"
  exit 0
fi

echo "FAIL: docs_constants (block in $DOC differs from '$TOOL --markdown'; regenerate the block)"
cat "$OUT/docs_constants.diff"
exit 1
