#!/usr/bin/env bash
# Run Lightning Indexer Prolog Quant on a2a3 with profiling and generate swimlane.
#
# Prereqs:
#   - simpler repo at SIMPLER_ROOT (default: pypto_workspace/simpler)
#   - ptoas at PTOAS_DIR (default: pypto_workspace/ptoas/PTOAS-main); script will build .o from .pto
#
# Output:
#   - simpler/outputs/perf_swimlane_YYYYMMDD_HHMMSS.json
#   - simpler/outputs/merged_swimlane_YYYYMMDD_HHMMSS.json (open in https://ui.perfetto.dev/)

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR" && pwd)"
PYPTO_SRC="$PROJECT_DIR/pypto_src"
KERNELS_DIR="$PYPTO_SRC/build_output"
GOLDEN="$PYPTO_SRC/golden.py"
# Default: pypto_workspace/simpler (from .../pypto-lib/projects/<this_project>)
SIMPLER_ROOT="${SIMPLER_ROOT:-$PROJECT_DIR/../../../simpler}"
# ptoas: pypto_workspace/ptoas/PTOAS-main
PTOAS_DIR="${PTOAS_DIR:-$PROJECT_DIR/../../../ptoas/PTOAS-main}"
DEVICE_ID="${DEVICE_ID:-0}"
PLATFORM="${PLATFORM:-a2a3}"

if [[ ! -d "$SIMPLER_ROOT" ]]; then
  echo "SIMPLER_ROOT not found: $SIMPLER_ROOT"
  echo "Set SIMPLER_ROOT to the simpler repo root, or run from pypto_workspace."
  exit 1
fi
if [[ ! -f "$KERNELS_DIR/kernel_config.py" ]]; then
  echo "kernel_config.py not found: $KERNELS_DIR"
  exit 1
fi
if [[ ! -f "$GOLDEN" ]]; then
  echo "golden.py not found: $GOLDEN"
  exit 1
fi

# Step 1: Build .o from .pto using ptoas + simpler's compiler (if ptoas dir exists)
if [[ -d "$PTOAS_DIR" ]]; then
  echo "=== Building kernel .o from .pto (ptoas: $PTOAS_DIR) ==="
  cd "$SIMPLER_ROOT"
  if ! python examples/scripts/build_pto_kernels.py -k "$KERNELS_DIR" --ptoas-dir "$PTOAS_DIR" -p "$PLATFORM" -v; then
    echo "Tip: build PTOAS first (see $PTOAS_DIR/README.md): LLVM 19 + cmake -B build && ninja -C build"
    echo "     Or set PTOAS_BIN to path of ptoas and re-run. Continuing in case .o already exist."
  else
    echo "Kernel build done."
  fi
  cd - >/dev/null
else
  echo "PTOAS_DIR not found ($PTOAS_DIR); skipping .pto->.o build. Ensure .o files exist for each .pto."
fi

# Step 2: Run test with profiling and generate swimlane
cd "$SIMPLER_ROOT"
python examples/scripts/run_example.py \
  -k "$KERNELS_DIR" \
  -g "$GOLDEN" \
  -p "$PLATFORM" \
  -d "$DEVICE_ID" \
  --enable-profiling \
  "$@"

echo ""
echo "Swimlane: check simpler/outputs/ for merged_swimlane_*.json and open in https://ui.perfetto.dev/"
