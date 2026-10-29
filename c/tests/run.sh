#!/bin/sh
# run.sh — start Bun stub 9P trees, run the C tests against them.
# Usage: sh tests/run.sh (from c/)
set -e
cd "$(dirname "$0")/.."
bun tests/stubs.ts tests/ports.txt &
STUBS=$!
trap 'kill $STUBS 2>/dev/null; rm -f tests/ports.txt' EXIT
for i in 1 2 3 4 5 6 7 8 9 10; do
  [ -f tests/ports.txt ] && break
  sleep 0.5
done
./helper_test tests/ports.txt
