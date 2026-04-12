#!/bin/zsh
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
RUN_SECS="${RUN_SECS:-10}"
ITERATIONS="${ITERATIONS:-1}"
RUN_ID_BASE="${RUN_ID:-$(date +%Y-%m-%d_%H-%M-%S)_mainline_probe_${RANDOM}}"
for i in $(seq 1 "$ITERATIONS"); do
  echo "[mainline-probe] iteration=$i run_secs=$RUN_SECS"
  RUN_SECS="$RUN_SECS" \
  RUN_ID="${RUN_ID_BASE}_iter${i}" \
  "$ROOT_DIR/run_ub_dual_node_probe.sh"
done
