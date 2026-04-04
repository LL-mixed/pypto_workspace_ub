#!/bin/zsh
set -euo pipefail
setopt null_glob

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
WORKSPACE_ROOT="$(cd "$ROOT_DIR/../../.." && pwd)"
QEMU_BIN="${QEMU_BIN:-}"
KERNEL_IMAGE="${KERNEL_IMAGE:-$ROOT_DIR/out/Image}"
INITRAMFS_IMAGE="${INITRAMFS_IMAGE:-$ROOT_DIR/out/initramfs.cpio.gz}"
TOPOLOGY_FILE="${TOPOLOGY_FILE:-/Volumes/repos/pypto_workspace/simulator/vendor/ub_topology_two_node_v0.ini}"
SHARED_DIR="${UB_FM_SHARED_DIR:-/tmp/ub-qemu-links-dual}"
RUN_SECS="${RUN_SECS:-120}"
ITERATIONS="${ITERATIONS:-1}"
START_GAP_SECS="${START_GAP_SECS:-1}"
QEMU_KEEP_ALIVE_ON_POWEROFF="${QEMU_KEEP_ALIVE_ON_POWEROFF:-0}"
APPEND_EXTRA="${APPEND_EXTRA:-linqu_probe_skip=1 linqu_probe_load_helper=1 linqu_bizmsg_verify=1 linqu_force_ubase_bind=1 linqu_urma_dp_verify=1}"
OUT_DIR="$ROOT_DIR/out"

qemu_supports_ub_opts() {
  local bin="$1"
  "$bin" -M virt,help 2>/dev/null | rg -q "ub-cluster-mode|ummu"
}

resolve_qemu_bin() {
  local candidates=(
    "$WORKSPACE_ROOT/simulator/vendor/qemu_8.2.0_ub/build/qemu-system-aarch64"
    "/tmp/ub-qemu-build-dp/qemu-system-aarch64"
    "/tmp/ub-qemu-build-verify/qemu-system-aarch64"
    "$WORKSPACE_ROOT/simulator/vendor/qemu/build/qemu-system-aarch64"
  )
  local candidate=""

  if [[ -n "$QEMU_BIN" ]]; then
    if [[ ! -x "$QEMU_BIN" ]]; then
      echo "QEMU_BIN not found or not executable: $QEMU_BIN" >&2
      return 1
    fi
    if ! qemu_supports_ub_opts "$QEMU_BIN"; then
      echo "QEMU_BIN does not expose UB machine options (expected ummu/ub-cluster-mode): $QEMU_BIN" >&2
      return 1
    fi
    return 0
  fi

  for candidate in "${candidates[@]}"; do
    if [[ -x "$candidate" ]] && qemu_supports_ub_opts "$candidate"; then
      QEMU_BIN="$candidate"
      return 0
    fi
  done

  echo "No UB-capable qemu-system-aarch64 found." >&2
  echo "Checked candidates:" >&2
  printf '  %s\n' "${candidates[@]}" >&2
  echo "Set QEMU_BIN explicitly to a UB-enabled build." >&2
  return 1
}

resolve_qemu_bin

cleanup_pid() {
  local pid_file="$1"
  local pid=""
  if [[ -f "$pid_file" ]]; then
    pid="$(cat "$pid_file" 2>/dev/null || true)"
  fi
  if [[ -n "${pid:-}" ]] && kill -0 "$pid" 2>/dev/null; then
    kill "$pid" 2>/dev/null || true
    sleep 0.2
    kill -9 "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
  fi
  rm -f "$pid_file"
}

cleanup_all_urma_dp_pid_files() {
  local pid_file=""
  for pid_file in "$OUT_DIR"/ub_nodeA.urma_dp.*.pid "$OUT_DIR"/ub_nodeB.urma_dp.*.pid; do
    cleanup_pid "$pid_file"
  done
}

wait_for_log_pattern() {
  local file="$1"
  local pattern="$2"
  local timeout_s="$3"
  local deadline=$((SECONDS + timeout_s))
  while (( SECONDS < deadline )); do
    if [[ -f "$file" ]] && rg -q "$pattern" "$file"; then
      return 0
    fi
    sleep 0.2
  done
  return 1
}

wait_for_log_pass_or_fail() {
  local file="$1"
  local pass_pattern="$2"
  local fail_pattern="$3"
  local timeout_s="$4"
  local deadline=$((SECONDS + timeout_s))

  while (( SECONDS < deadline )); do
    if [[ -f "$file" ]]; then
      if rg -q "$pass_pattern" "$file"; then
        return 0
      fi
      if rg -q "$fail_pattern" "$file"; then
        return 1
      fi
    fi
    sleep 0.2
  done
  return 2
}

assert_log_has() {
  local file="$1"
  local pattern="$2"
  local label="$3"
  if ! rg -q "$pattern" "$file"; then
    echo "missing log marker: $label in $file" >&2
    return 1
  fi
}

assert_log_absent() {
  local file="$1"
  local pattern="$2"
  local label="$3"
  if rg -q "$pattern" "$file"; then
    echo "unexpected log marker: $label in $file" >&2
    return 1
  fi
}

start_node() {
  local node_id="$1"
  local role="$2"
  local serial_log="$3"
  local pid_file="$4"
  local qemu_extra=()

  if [[ "$QEMU_KEEP_ALIVE_ON_POWEROFF" == "1" ]]; then
    qemu_extra=(-no-shutdown)
  fi

  env \
    UB_FM_NODE_ID="$node_id" \
    UB_FM_TOPOLOGY_FILE="$TOPOLOGY_FILE" \
    UB_FM_SHARED_DIR="$SHARED_DIR" \
    "$QEMU_BIN" \
      -M virt,gic-version=3,its=on,ummu=on,ub-cluster-mode=on \
      -cpu cortex-a57 \
      -m 512M \
      -nodefaults \
      -nographic \
      -monitor none \
      -serial stdio \
      "${qemu_extra[@]}" \
      -kernel "$KERNEL_IMAGE" \
      -initrd "$INITRAMFS_IMAGE" \
      -append "console=ttyAMA0 rdinit=/init linqu_urma_dp_role=${role} ${APPEND_EXTRA}" \
      >"$serial_log" 2>&1 &
  echo $! > "$pid_file"
}

validate_urma_dp_log() {
  local node_name="$1"
  local log_file="$2"

  assert_log_has "$log_file" "Register ubcore client success\\." "${node_name} ubcore register"
  assert_log_has "$log_file" "\\[ipourma\\] Register netlink success\\." "${node_name} ipourma register"
  assert_log_has "$log_file" "\\[urma_dp\\] iface=ipourma[0-9]+" "${node_name} ipourma iface"
  assert_log_has "$log_file" "\\[urma_dp\\] rx peer src=" "${node_name} peer rx"
  assert_log_has "$log_file" "\\[urma_dp\\] pass" "${node_name} urma dp pass"
  assert_log_has "$log_file" "\\[init\\] urma dataplane pass" "${node_name} init pass"
  assert_log_absent "$log_file" "\\[urma_dp\\] fail" "${node_name} urma dp failure"
  assert_log_absent "$log_file" "\\[init\\] urma dataplane fail" "${node_name} init failure"
}

run_iteration() {
  local iter="$1"
  local nodea_log="$OUT_DIR/ub_nodeA.urma_dp.${iter}.log"
  local nodeb_log="$OUT_DIR/ub_nodeB.urma_dp.${iter}.log"
  local nodea_pid_file="$OUT_DIR/ub_nodeA.urma_dp.${iter}.pid"
  local nodeb_pid_file="$OUT_DIR/ub_nodeB.urma_dp.${iter}.pid"
  local stale_files=()

  rm -f /tmp/ub-qemu/ub-bus-instance-*.lock
  cleanup_pid "$nodea_pid_file"
  cleanup_pid "$nodeb_pid_file"

  mkdir -p "$SHARED_DIR"
  stale_files=("$SHARED_DIR"/*.ini "$SHARED_DIR"/*.kick "$SHARED_DIR"/*.lock)
  if (( ${#stale_files[@]} )); then
    rm -f "${stale_files[@]}"
  fi
  rm -f "$nodea_log" "$nodeb_log"

  start_node "nodeA" "nodeA" "$nodea_log" "$nodea_pid_file"
  sleep "$START_GAP_SECS"
  start_node "nodeB" "nodeB" "$nodeb_log" "$nodeb_pid_file"

  wait_for_log_pass_or_fail "$nodea_log" "\\[init\\] urma dataplane pass" "\\[init\\] urma dataplane fail|\\[urma_dp\\] fail" "$RUN_SECS"
  case "$?" in
    0) ;;
    1)
      echo "iteration ${iter}: nodeA urma dataplane reported failure" >&2
      return 1
      ;;
    *)
      echo "iteration ${iter}: nodeA urma dataplane did not pass within ${RUN_SECS}s" >&2
      return 1
      ;;
  esac

  wait_for_log_pass_or_fail "$nodeb_log" "\\[init\\] urma dataplane pass" "\\[init\\] urma dataplane fail|\\[urma_dp\\] fail" "$RUN_SECS"
  case "$?" in
    0) ;;
    1)
      echo "iteration ${iter}: nodeB urma dataplane reported failure" >&2
      return 1
      ;;
    *)
      echo "iteration ${iter}: nodeB urma dataplane did not pass within ${RUN_SECS}s" >&2
      return 1
      ;;
  esac

  sleep 1
  cleanup_pid "$nodea_pid_file"
  cleanup_pid "$nodeb_pid_file"

  echo "=== nodeA(urma_dp:${iter}) ==="
  tail -n 120 "$nodea_log"
  echo "=== nodeB(urma_dp:${iter}) ==="
  tail -n 120 "$nodeb_log"

  validate_urma_dp_log "nodeA" "$nodea_log"
  validate_urma_dp_log "nodeB" "$nodeb_log"

  echo "iteration ${iter}: dual-node URMA dataplane workload pass"
}

trap cleanup_all_urma_dp_pid_files EXIT INT TERM

for ((i = 1; i <= ITERATIONS; i++)); do
  run_iteration "$i"
done

echo "dual-node URMA dataplane workload validation passed (${ITERATIONS} iterations)"
