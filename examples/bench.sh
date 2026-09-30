#!/usr/bin/env bash
# ============================================================
#  bench.sh — measure --jobs scaling for birun
# ============================================================
set -euo pipefail

cd "$(dirname "$0")"

BIRUN="${BIRUN:-../build/birun}"
CFG="benchmark.birun.bi"

if [[ ! -x "$BIRUN" ]]; then
    echo "error: birun binary not found at $BIRUN" >&2
    echo "       build first:  cmake --build ../build -j" >&2
    exit 1
fi

if ! command -v bc >/dev/null 2>&1; then
    echo "error: 'bc' not installed (needed for timing math)" >&2
    exit 1
fi

run_one() {
    local jobs=$1
    local label="birun --jobs $jobs all"
    printf '  %-24s ' "$label"
    local t0 t1
    t0=$(date +%s.%N)
    "$BIRUN" --file "$CFG" --no-cache --quiet --jobs "$jobs" all
    t1=$(date +%s.%N)
    printf '%.2fs\n' "$(echo "$t1 - $t0" | bc -l)"
}

echo
echo "  birun --jobs benchmark"
echo "  config: $CFG   (8 independent 1s tasks under 'all')"
echo
echo "  sequential baseline"
run_one 1
echo
echo "  parallel speedups"
run_one 2
run_one 4
run_one 8
echo