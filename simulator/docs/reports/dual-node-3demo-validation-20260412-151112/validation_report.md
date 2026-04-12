# Dual-Node 3-Demo Validation Report

Date: 2026-04-12

## Scope

This report records one real host-side validation run of the dual-node demo harness after the script migration into `simulator/guest-linux/aarch64/scripts/`.

Validated workload set:

1. `ub_chat`
2. `ub_rpc_demo`
3. `ub_rdma_demo`

## Run Command

Executed from:

`/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64`

Command:

```sh
AARCH64_LINUX_CC=/opt/homebrew/bin/aarch64-unknown-linux-gnu-gcc \
ITERATIONS=1 \
RUN_SECS=120 \
START_GAP_SECS=1 \
APPEND_EXTRA="linqu_probe_skip=1 linqu_probe_load_helper=1 linqu_ub_chat=1 linqu_ub_rpc_demo=1 linqu_ub_rdma_demo=1" \
zsh scripts/run_ub_dual_node_demo.sh
```

## Result

Overall result: `PASS`

Harness summary from [demo_report.latest.txt](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/out/demo_report.latest.txt):

```text
scenario=dual-node-demo
iterations=1
run_secs=120
start_gap_secs=1
run_id=2026-04-12_15-10-22_10560
logs_dir=/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs
passed=1
failed=0
pass_rate_percent=100
iteration_1_result=0
```

This run used log directory:

`/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-12_15-10-22_10560_demo_iter1`

## Evidence

Guest-side pass markers:

1. [nodeA_guest.log](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-12_15-10-22_10560_demo_iter1/nodeA_guest.log):1864 shows `[init] ub chat pass`
2. [nodeA_guest.log](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-12_15-10-22_10560_demo_iter1/nodeA_guest.log):1882 shows `[init] ub rpc demo pass`
3. [nodeA_guest.log](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-12_15-10-22_10560_demo_iter1/nodeA_guest.log):1936 shows `[init] ub rdma demo pass`
4. [nodeB_guest.log](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-12_15-10-22_10560_demo_iter1/nodeB_guest.log):1857 shows `[init] ub chat pass`
5. [nodeB_guest.log](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-12_15-10-22_10560_demo_iter1/nodeB_guest.log):1875 shows `[init] ub rpc demo pass`
6. [nodeB_guest.log](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-12_15-10-22_10560_demo_iter1/nodeB_guest.log):1929 shows `[init] ub rdma demo pass`

QEMU-side fabric and entity evidence:

1. [nodeA_qemu.log](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-12_15-10-22_10560_demo_iter1/nodeA_qemu.log):38 shows `entity_table_init`
2. [nodeA_qemu.log](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-12_15-10-22_10560_demo_iter1/nodeA_qemu.log):88 shows `entity_plan: loaded entity 0`
3. [nodeA_qemu.log](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-12_15-10-22_10560_demo_iter1/nodeA_qemu.log):251 shows `entity_reg inject SUCCESS`
4. [nodeB_qemu.log](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-12_15-10-22_10560_demo_iter1/nodeB_qemu.log):90 shows `ub_link: configured remote link`
5. [nodeB_qemu.log](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-12_15-10-22_10560_demo_iter1/nodeB_qemu.log):237 shows `entity_reg inject SUCCESS`

## Kernel/Guest Health

During this run, no guest log match was found for:

1. `WARNING: CPU`
2. `Call trace`
3. `BUG:`
4. `panic`
5. `Oops:`

Within this validation scope, there was no captured guest kernel stacktrace.

## Analysis

Current status from this run:

1. The migrated `scripts/` entrypoint works in a real host execution environment.
2. Dual-node QEMU startup, QMP resume, FM link ready, and entity-ready checks all completed successfully.
3. Guest-side `ub_chat`, `ub_rpc_demo`, and `ub_rdma_demo` all reached explicit pass markers on both nodes.

Important caveat:

1. A previous attempt from the restricted Codex sandbox failed before guest boot because QEMU could not bind its local QMP sockets under `/tmp/ub-qemu-links-dual/qmp/*.sock`.
2. That failure was environmental, not a functional regression in the harness or demos.
3. The harness now includes broader early-failure detection for such QEMU/socket startup failures so future runs surface the real root cause earlier.

## Artifacts

Primary logs:

1. [nodeA_guest.log](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-12_15-10-22_10560_demo_iter1/nodeA_guest.log)
2. [nodeB_guest.log](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-12_15-10-22_10560_demo_iter1/nodeB_guest.log)
3. [nodeA_qemu.log](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-12_15-10-22_10560_demo_iter1/nodeA_qemu.log)
4. [nodeB_qemu.log](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-12_15-10-22_10560_demo_iter1/nodeB_qemu.log)

Compatibility links:

1. [ub_nodeA.demo.1.log](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/out/ub_nodeA.demo.1.log)
2. [ub_nodeB.demo.1.log](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/out/ub_nodeB.demo.1.log)
3. [ub_nodeA.demo.1.qemu.log](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/out/ub_nodeA.demo.1.qemu.log)
4. [ub_nodeB.demo.1.qemu.log](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/out/ub_nodeB.demo.1.qemu.log)
