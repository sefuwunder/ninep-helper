#!/bin/sh
# p9-integration.sh — plan9port 9p CLI against Bun stub 9P trees.
# Proves the desk's 9P server speaks real 9P to a real Plan 9 client.
# Usage: sh tests/p9-integration.sh (from c/)
set -e
cd "$(dirname "$0")/.."
export PLAN9="$HOME/workspace/plan9port"
bun tests/stubs.ts tests/ports.txt &
STUBS=$!
trap 'kill $STUBS 2>/dev/null; rm -f tests/ports.txt' EXIT
for i in 1 2 3 4 5 6 7 8 9 10; do
  [ -f tests/ports.txt ] && break
  sleep 0.5
done
CADADDR=$(grep CADENCE tests/ports.txt | cut -d= -f2)
CAD=$(echo "$CADADDR" | sed 's/:/!/')
echo "== cadence stub at tcp!$CAD =="
"$PLAN9/bin/9p" -a "tcp!$CAD" ls /
"$PLAN9/bin/9p" -a "tcp!$CAD" read /today
"$PLAN9/bin/9p" -a "tcp!$CAD" ls /staging
"$PLAN9/bin/9p" -a "tcp!$CAD" read /staging/relay-t1/contact
"$PLAN9/bin/9p" -a "tcp!$CAD" stat /today
