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
START_GAP_SECS="${START_GAP_SECS:-3}"
LINK_WAIT_SECS="${LINK_WAIT_SECS:-25}"
QEMU_KEEP_ALIVE_ON_POWEROFF="${QEMU_KEEP_ALIVE_ON_POWEROFF:-0}"
APPEND_EXTRA="${APPEND_EXTRA:-linqu_probe_skip=1 linqu_probe_load_helper=1 linqu_bizmsg_verify=1 linqu_force_ubase_bind=1 linqu_urma_dp_verify=1}"
OUT_DIR="$ROOT_DIR/out"
QMP_DIR="${UB_FM_SHARED_DIR:-/tmp/ub-qemu-links-dual}/qmp"

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
  local qmp_socket="$5"
  local qemu_extra=()

  if [[ "$QEMU_KEEP_ALIVE_ON_POWEROFF" == "1" ]]; then
    qemu_extra=(-no-shutdown)
  fi

  mkdir -p "$(dirname "$qmp_socket")"

  env \
    UB_FM_NODE_ID="$node_id" \
    UB_FM_TOPOLOGY_FILE="$TOPOLOGY_FILE" \
    UB_FM_SHARED_DIR="$SHARED_DIR" \
    "$QEMU_BIN" \
      -S \
      -M virt,gic-version=3,its=on,ummu=on,ub-cluster-mode=on \
      -cpu cortex-a57 \
      -m 8G \
      -nodefaults \
      -nographic \
      -qmp unix:"$qmp_socket",server=on,wait=off \
      -serial stdio \
      "${qemu_extra[@]}" \
      -kernel "$KERNEL_IMAGE" \
      -initrd "$INITRAMFS_IMAGE" \
      -append "console=ttyAMA0 rdinit=/init linqu_urma_dp_role=${role} ${APPEND_EXTRA}" \
      >"$serial_log" 2>&1 &
  echo $! > "$pid_file"
}

# Wait for FM links to be ready using Ready Contract
wait_for_fm_links_ready() {
  local nodea_log="$1"
  local nodeb_log="$2"
  local timeout_s="${3:-30}"
  local deadline=$((SECONDS + timeout_s))
  local nodea_status="$SHARED_DIR/nodeA_ubcdev0__1.status"
  local nodeb_status="$SHARED_DIR/nodeB_ubcdev0__1.status"

  echo "Waiting for FM links to be ready (timeout ${timeout_s}s)..."

  while (( SECONDS < deadline )); do
    local nodea_ready=false
    local nodeb_ready=false

    # Check nodeA status file - must be READY
    if [[ -f "$nodea_status" ]]; then
      local state=$(grep "^state=" "$nodea_status" 2>/dev/null | cut -d'=' -f2)
      if [[ "$state" == "READY" ]]; then
        nodea_ready=true
      fi
    fi

    # Check nodeB status file - must be READY
    if [[ -f "$nodeb_status" ]]; then
      local state=$(grep "^state=" "$nodeb_status" 2>/dev/null | cut -d'=' -f2)
      if [[ "$state" == "READY" ]]; then
        nodeb_ready=true
      fi
    fi

    if [[ "$nodea_ready" == "true" && "$nodeb_ready" == "true" ]]; then
      echo "FM links ready!"
      return 0
    fi

    sleep 0.2
  done

  echo "FM links NOT ready within timeout!" >&2
  echo "=== nodeA link status ===" >&2
  if [[ -f "$nodea_status" ]]; then
    cat "$nodea_status" >&2
  else
    echo "(no status file: $nodea_status)" >&2
  fi
  echo "=== nodeB link status ===" >&2
  if [[ -f "$nodeb_status" ]]; then
    cat "$nodeb_status" >&2
  else
    echo "(no status file: $nodeb_status)" >&2
  fi
  return 1
}

dump_link_diagnostics() {
  local nodea_log="$1"
  local nodeb_log="$2"
  local nodea_status="$SHARED_DIR/nodeA_ubcdev0__1.status"
  local nodeb_status="$SHARED_DIR/nodeB_ubcdev0__1.status"

  echo "=== Link Diagnostics ===" >&2
  printf "%-10s %-10s %-15s %-15s %-10s\n" "Node" "Socket" "RemoteGUID" "State" "Error" >&2
  printf "%-10s %-10s %-15s %-15s %-10s\n" "----" "------" "-----------" "-----" "-----" >&2

  for node in "nodeA" "nodeB"; do
    local status_file="${SHARED_DIR}/${node}_ubcdev0__1.status"
    if [[ -f "$status_file" ]]; then
      local socket=$(grep "^socket_connected=" "$status_file" 2>/dev/null | cut -d'=' -f2)
      local guid=$(grep "^remote_guid_valid=" "$status_file" 2>/dev/null | cut -d'=' -f2)
      local state=$(grep "^state=" "$status_file" 2>/dev/null | cut -d'=' -f2)
      local error=$(grep "^last_error=" "$status_file" 2>/dev/null | cut -d'=' -f2 | sed 's/"//g')

      socket=${socket:-false}
      guid=${guid:-false}
      state=${state:-UNKNOWN}
      error=${error:-none}

      printf "%-10s %-10s %-15s %-15s %-10s\n" \
        "$node" "$socket" "$guid" "$state" "$error" >&2
    else
      printf "%-10s %-10s %-15s %-15s %-10s\n" \
        "$node" "N/A" "N/A" "NO_STATUS" "missing_file" >&2
    fi
  done

  echo "=== Recent log markers ===" >&2
  echo "nodeA:" >&2
  rg -n "ub_link:|ub_fm:" "$nodea_log" 2>/dev/null | tail -10 >&2 || true
  echo "nodeB:" >&2
  rg -n "ub_link:|ub_fm:" "$nodeb_log" 2>/dev/null | tail -10 >&2 || true
}

# Resume paused QEMU instance via QMP
cont_qemu() {
  local qmp_socket="$1"
  local node_name="$2"
  local timeout_s="${3:-10}"

  echo "Resuming $node_name via QMP ($qmp_socket)..."

  # Wait for QMP socket to be ready
  local deadline=$((SECONDS + timeout_s))
  while (( SECONDS < deadline )); do
    if [[ -S "$qmp_socket" ]]; then
      break
    fi
    sleep 0.1
  done

  if [[ ! -S "$qmp_socket" ]]; then
    echo "QMP socket not ready: $qmp_socket" >&2
    # Don't fail - QEMU may already be running
    return 0
  fi

  # Send cont command via QMP using Python
  python3 -c "
import socket
import json
import sys
import traceback
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.settimeout(5)
try:
    s.connect('${qmp_socket}')
    # Read greeting
    s.recv(1024)
    # Send capabilities
    s.sendall(b'{\\\"execute\\\": \\\"qmp_capabilities\\\"}\r\n')
    s.recv(1024)
    # Send cont
    s.sendall(b'{\\\"execute\\\": \\\"cont\\\"}\r\n')
    resp = s.recv(1024)
    s.close()
    # Accept both return and error responses as QEMU may already be running
    if b'return' in resp or b'error' in resp or b'CommandNotFound' in resp:
        sys.exit(0)
    else:
        sys.exit(0)
except Exception as e:
    # Don't fail - QEMU may already be running
    sys.exit(0)
" 2>/dev/null || true

  # Always return success - QMP errors are not fatal
  if [[ $? -eq 0 ]]; then
    echo "$node_name resumed"
  else
    echo "QMP resume had issues, but continuing anyway" >&2
  fi
  return 0
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

check_link_early_or_fail() {
  local nodea_log="$1"
  local nodeb_log="$2"
  local timeout_s="$3"
  local deadline=$((SECONDS + timeout_s))
  local fail_pat="ub_link: server listen failed|ub_link: failed to connect remote server|bizmsg roundtrip fail: remote linkup not ready|\\[init\\] ub sysfs wait timed out"
  local ok_pat="ub_link: connected to remote server|ub_link: accepted connection|remote snapshot load done|remote cfg notify done"

  while (( SECONDS < deadline )); do
    if [[ -f "$nodea_log" ]] && rg -q "$fail_pat" "$nodea_log"; then
      echo "early link failure detected on nodeA" >&2
      dump_link_diagnostics "$nodea_log" "$nodeb_log"
      return 1
    fi
    if [[ -f "$nodeb_log" ]] && rg -q "$fail_pat" "$nodeb_log"; then
      echo "early link failure detected on nodeB" >&2
      dump_link_diagnostics "$nodea_log" "$nodeb_log"
      return 1
    fi

    if [[ -f "$nodea_log" && -f "$nodeb_log" ]] &&
       rg -q "$ok_pat" "$nodea_log" &&
       rg -q "$ok_pat" "$nodeb_log"; then
      return 0
    fi

    sleep 0.2
  done

  # No explicit link failure observed within link window; keep original flow.
  return 0
}

run_iteration() {
  local iter="$1"
  local nodea_log="$OUT_DIR/ub_nodeA.urma_dp.${iter}.log"
  local nodeb_log="$OUT_DIR/ub_nodeB.urma_dp.${iter}.log"
  local nodea_pid_file="$OUT_DIR/ub_nodeA.urma_dp.${iter}.pid"
  local nodeb_pid_file="$OUT_DIR/ub_nodeB.urma_dp.${iter}.pid"
  local nodea_qmp="$QMP_DIR/nodeA.${iter}.sock"
  local nodeb_qmp="$QMP_DIR/nodeB.${iter}.sock"
  local stale_files=()

  rm -f /tmp/ub-qemu/ub-bus-instance-*.lock
  cleanup_pid "$nodea_pid_file"
  cleanup_pid "$nodeb_pid_file"

  mkdir -p "$SHARED_DIR"
  mkdir -p "$QMP_DIR"
  stale_files=("$SHARED_DIR"/*.ini "$SHARED_DIR"/*.kick "$SHARED_DIR"/*.lock)
  if (( ${#stale_files[@]} )); then
    rm -f "${stale_files[@]}"
  fi
  rm -f "$nodea_log" "$nodeb_log"

  # Start both nodes in PAUSED state
  echo "Starting nodeA (paused)..."
  start_node "nodeA" "nodeA" "$nodea_log" "$nodea_pid_file" "$nodea_qmp"
  sleep 0.5
  echo "Starting nodeB (paused)..."
  start_node "nodeB" "nodeB" "$nodeb_log" "$nodeb_pid_file" "$nodeb_qmp"

  # Early fail-fast check - detect failures before waiting for ready
  if ! check_link_early_or_fail "$nodea_log" "$nodeb_log" 10; then
    echo "iteration ${iter}: early link failure detected" >&2
    return 11  # Exit code 11: link establishment failure
  fi

  # Wait for FM links to be ready (Ready Contract)
  if ! wait_for_fm_links_ready "$nodea_log" "$nodeb_log" 30; then
    echo "iteration ${iter}: FM links failed to reach READY state within timeout" >&2
    return 11  # Exit code 11: link establishment failure
  fi

  # Resume both nodes - now guest kernel enumeration sees stable topology
  cont_qemu "$nodea_qmp" "nodeA"
  cont_qemu "$nodeb_qmp" "nodeB"

  # Quick sanity check that kernel is booting
  sleep 1
  if ! kill -0 "$(cat "$nodea_pid_file" 2>/dev/null)" 2>/dev/null; then
    echo "iteration ${iter}: nodeA died after resume" >&2
    return 1
  fi
  if ! kill -0 "$(cat "$nodeb_pid_file" 2>/dev/null)" 2>/dev/null; then
    echo "iteration ${iter}: nodeB died after resume" >&2
    return 1
  fi

  wait_for_log_pass_or_fail "$nodea_log" "\\[init\\] urma dataplane pass" "\\[init\\] urma dataplane fail|\\[urma_dp\\] fail" "$RUN_SECS"
  case "$?" in
    0) ;;
    1)
      echo "iteration ${iter}: nodeA urma dataplane reported failure" >&2
      return 13  # Exit code 13: guest enumeration failure
      ;;
    *)
      echo "iteration ${iter}: nodeA urma dataplane did not pass within ${RUN_SECS}s" >&2
      return 13  # Exit code 13: guest enumeration failure
      ;;
  esac

  wait_for_log_pass_or_fail "$nodeb_log" "\\[init\\] urma dataplane pass" "\\[init\\] urma dataplane fail|\\[urma_dp\\] fail" "$RUN_SECS"
  case "$?" in
    0) ;;
    1)
      echo "iteration ${iter}: nodeB urma dataplane reported failure" >&2
      return 13  # Exit code 13: guest enumeration failure
      ;;
    *)
      echo "iteration ${iter}: nodeB urma dataplane did not pass within ${RUN_SECS}s" >&2
      return 13  # Exit code 13: guest enumeration failure
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

# Track overall test results
declare -a ITERATION_RESULTS
declare -a ITERATION_ERRORS

for ((i = 1; i <= ITERATIONS; i++)); do
  if run_iteration "$i"; then
    ITERATION_RESULTS[$i]=0
  else
    local ret=$?
    ITERATION_RESULTS[$i]=$ret
    ITERATION_ERRORS[$i]="iteration $i failed with exit code $ret"
  fi
done

# Report overall results
echo "=== Test Results ===" >&2
local passed=0
local failed=0
for ((i = 1; i <= ITERATIONS; i++)); do
  if [[ ${ITERATION_RESULTS[$i]:-255} -eq 0 ]]; then
    ((passed++))
    echo "iteration $i: PASS"
  else
    ((failed++))
    echo "iteration $i: FAIL (exit code ${ITERATION_RESULTS[$i]})" >&2
    if [[ -n "${ITERATION_ERRORS[$i]}" ]]; then
      echo "  ${ITERATION_ERRORS[$i]}" >&2
    fi
  fi
done

echo "=== Summary ===" >&2
echo "Passed: $passed / $ITERATIONS" >&2
echo "Failed: $failed / $ITERATIONS" >&2

if [[ $failed -gt 0 ]]; then
  echo "dual-node URMA dataplane workload validation FAILED" >&2
  exit 1
else
  echo "dual-node URMA dataplane workload validation passed (${ITERATIONS} iterations)"
  exit 0
fi
