#!/bin/zsh
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
WORKSPACE_ROOT="$(cd "$ROOT_DIR/../../.." && pwd)"
QEMU_DIR="${QEMU_DIR:-}"
SCENARIO="$WORKSPACE_ROOT/simulator/scenarios/mvp_2host_single_domain.yaml"
PID_FILE="$ROOT_DIR/out/linqu_ub_probe.qemu.pid"
LOG_FILE="$ROOT_DIR/out/linqu_ub_probe.serial.log"
RUN_SECS="${RUN_SECS:-2}"
QEMU_DEBUG_FLAGS="${QEMU_DEBUG_FLAGS:-}"
QEMU_DEBUG_LOG="${QEMU_DEBUG_LOG:-$ROOT_DIR/out/linqu_ub_probe.qemu.log}"
MACHINE="${MACHINE:-virt}"
EXTRA_QEMU_ARGS="${EXTRA_QEMU_ARGS:-}"

usage() {
  cat <<'EOF'
Usage: run_probe.sh --legacy [--help]

This is a legacy linqu-ub probe script and is intentionally guarded.
Set QEMU_DIR explicitly, for example:
  QEMU_DIR=/path/to/legacy/qemu ./run_probe.sh --legacy
EOF
}

LEGACY_MODE=0
for arg in "$@"; do
  case "$arg" in
    --legacy)
      LEGACY_MODE=1
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $arg" >&2
      usage >&2
      exit 2
      ;;
  esac
done

if [[ "$LEGACY_MODE" -ne 1 ]]; then
  echo "Refusing to run legacy linqu-ub probe without --legacy." >&2
  usage >&2
  exit 2
fi

if [[ -z "$QEMU_DIR" ]]; then
  echo "QEMU_DIR is required for legacy linqu-ub probe script." >&2
  echo "Active dual-node flow uses simulator/vendor/qemu_8.2.0_ub scripts." >&2
  exit 2
fi

bash "$ROOT_DIR/build_probe.sh" >/dev/null

BIN="$ROOT_DIR/out/linqu_ub_probe.bin"
LOAD_ADDR="$(tr -d '\n' < "$ROOT_DIR/out/linqu_ub_probe.loadaddr")"

cleanup_previous() {
  if [[ -f "$PID_FILE" ]]; then
    local old_pid
    old_pid="$(cat "$PID_FILE" 2>/dev/null || true)"
    if [[ -n "${old_pid:-}" ]] && kill -0 "$old_pid" 2>/dev/null; then
      kill "$old_pid" 2>/dev/null || true
      sleep 0.2
      kill -9 "$old_pid" 2>/dev/null || true
    fi
    rm -f "$PID_FILE"
  fi
}

cleanup_current() {
  local pid=""
  if [[ -f "$PID_FILE" ]]; then
    pid="$(cat "$PID_FILE" 2>/dev/null || true)"
  fi
  if [[ -n "${pid:-}" ]] && kill -0 "$pid" 2>/dev/null; then
    kill "$pid" 2>/dev/null || true
    sleep 0.2
    kill -9 "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
  fi
  rm -f "$PID_FILE"
}

cleanup_previous
trap cleanup_current EXIT INT TERM

cd "$QEMU_DIR"
rm -f "$LOG_FILE"
rm -f "$QEMU_DEBUG_LOG"
debug_args=()
if [[ -n "$QEMU_DEBUG_FLAGS" ]]; then
  debug_args=(${=QEMU_DEBUG_FLAGS} -D "$QEMU_DEBUG_LOG")
fi
extra_args=()
if [[ -n "$EXTRA_QEMU_ARGS" ]]; then
  extra_args=(${=EXTRA_QEMU_ARGS})
fi

./build/qemu-system-aarch64 \
  -M "$MACHINE" \
  -cpu cortex-a57 \
  -m 512M \
  -nodefaults \
  -nographic \
  -monitor none \
  -serial stdio \
  "${debug_args[@]}" \
  "${extra_args[@]}" \
  -device "linqu-ub,scenario-path=$SCENARIO" \
  -device "loader,file=$BIN,addr=$LOAD_ADDR,cpu-num=0,force-raw=on" \
  >"$LOG_FILE" 2>&1 &

echo $! > "$PID_FILE"
sleep "$RUN_SECS"
cleanup_current
cat "$LOG_FILE"
