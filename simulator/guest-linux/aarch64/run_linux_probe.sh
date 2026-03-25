#!/bin/zsh
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
WORKSPACE_ROOT="$(cd "$ROOT_DIR/../../.." && pwd)"
QEMU_DIR="$WORKSPACE_ROOT/simulator/vendor/qemu"
SCENARIO="$WORKSPACE_ROOT/simulator/scenarios/mvp_2host_single_domain.yaml"
DEFAULT_KERNEL_IMAGE="$WORKSPACE_ROOT/simulator/guest-probe/aarch64/linux_blobs/Image"
DEFAULT_INITRAMFS_IMAGE="$WORKSPACE_ROOT/simulator/guest-probe/aarch64/linux_blobs/initramfs.cpio.gz"
OUT_DIR="$ROOT_DIR/out"
PID_FILE="$OUT_DIR/linux_probe.qemu.pid"
SERIAL_LOG="$OUT_DIR/linux_probe.serial.log"
RUN_SECS="${RUN_SECS:-8}"
MACHINE="${MACHINE:-virt}"
QEMU_DEBUG_FLAGS="${QEMU_DEBUG_FLAGS:-}"
QEMU_DEBUG_LOG="${QEMU_DEBUG_LOG:-$OUT_DIR/linux_probe.qemu.log}"
EXTRA_QEMU_ARGS="${EXTRA_QEMU_ARGS:-}"
APPEND_EXTRA="${APPEND_EXTRA:-}"

: "${KERNEL_IMAGE:=$DEFAULT_KERNEL_IMAGE}"
: "${INITRAMFS_IMAGE:=$DEFAULT_INITRAMFS_IMAGE}"

if [[ ! -f "$KERNEL_IMAGE" ]]; then
  echo "KERNEL_IMAGE not found: $KERNEL_IMAGE" >&2
  exit 1
fi

if [[ ! -f "$INITRAMFS_IMAGE" ]]; then
  bash "$ROOT_DIR/build_initramfs.sh" >/dev/null
  INITRAMFS_IMAGE="$OUT_DIR/initramfs.cpio.gz"
fi

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

rm -f "$SERIAL_LOG" "$QEMU_DEBUG_LOG"

debug_args=()
if [[ -n "$QEMU_DEBUG_FLAGS" ]]; then
  debug_args=(-d ${=QEMU_DEBUG_FLAGS} -D "$QEMU_DEBUG_LOG")
fi

extra_args=()
if [[ -n "$EXTRA_QEMU_ARGS" ]]; then
  extra_args=(${=EXTRA_QEMU_ARGS})
fi

cd "$QEMU_DIR"
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
  -kernel "$KERNEL_IMAGE" \
  -initrd "$INITRAMFS_IMAGE" \
  -append "console=ttyAMA0 rdinit=/init ${APPEND_EXTRA}" \
  >"$SERIAL_LOG" 2>&1 &

echo $! > "$PID_FILE"
sleep "$RUN_SECS"
cleanup_current
cat "$SERIAL_LOG"
