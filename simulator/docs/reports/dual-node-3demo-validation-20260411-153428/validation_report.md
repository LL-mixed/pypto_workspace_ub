# Dual-Node 3 Demo Validation Report (2026-04-11 15:34~15:44)

## 1. Scope

Validation target in dual-node simulated interconnect environment:

- `ub_chat.c`
- `ub_rpc_demo.c`
- `ub_rdma_demo.c`

Goal: rerun current dual-node demo flow and refresh verdict with reproducible commands + log evidence.

## 2. Environment

- Workspace: `/Volumes/repos/pypto_workspace`
- Guest test root: `/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64`
- QEMU binary (resolved by script): `simulator/vendor/qemu_8.2.0_ub/build/qemu-system-aarch64`
- Date: 2026-04-11

## 3. Run Method

Script used:

- `run_ub_dual_node_demo.sh`

Note:

- In restricted sandbox, QEMU QMP socket bind may fail with `Operation not permitted`.
- Actual validation run here was executed with host-level permission so QEMU could create `/tmp/ub-qemu-links-dual/qmp/*.sock`.

### 3.1 Baseline (chat + rpc)

Command:

```bash
cd /Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64 && \
REPORT_FILE=/Volumes/repos/pypto_workspace/simulator/docs/reports/dual-node-3demo-validation-20260411-153428/chat_rpc_baseline_report.txt \
ITERATIONS=1 RUN_SECS=120 START_GAP_SECS=1 ./run_ub_dual_node_demo.sh
```

Artifacts:

- `/Volumes/repos/pypto_workspace/simulator/docs/reports/dual-node-3demo-validation-20260411-153428/chat_rpc_baseline_report.txt`
- raw copied guest logs were trimmed from version control; line references below are kept as historical evidence

### 3.2 3-demo enabled (chat + rpc + rdma)

Command:

```bash
cd /Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64 && \
REPORT_FILE=/Volumes/repos/pypto_workspace/simulator/docs/reports/dual-node-3demo-validation-20260411-153428/three_demo_report.txt \
APPEND_EXTRA='linqu_probe_skip=1 linqu_probe_load_helper=1 linqu_force_ubase_bind=1 linqu_ub_chat=1 linqu_ub_rpc_demo=1 linqu_ub_rdma_demo=1' \
ITERATIONS=1 RUN_SECS=120 START_GAP_SECS=1 ./run_ub_dual_node_demo.sh
```

Artifacts:

- `/Volumes/repos/pypto_workspace/simulator/docs/reports/dual-node-3demo-validation-20260411-153428/three_demo_report.txt`
- no extra local log copies are kept in version control for this report

## 4. Results Summary

- Baseline (chat+rpc): **PASS**
- 3-demo (chat+rpc+rdma): **FAIL**

Report fields:

- `chat_rpc_baseline_report.txt`: `passed=1 failed=0 pass_rate_percent=100 iteration_1_result=0`
- `three_demo_report.txt`: `passed=0 failed=1 pass_rate_percent=0 iteration_1_result=13`

`iteration_1_result=13` meaning in script:

- `run_ub_dual_node_demo.sh:497-506` and `:510-519` map result `13` to `ub rpc demo` pass/fail marker not observed within timeout.

## 5. Log Evidence

### 5.1 Baseline pass evidence (chat + rpc)

NodeA:

- `chat_rpc_nodeA.log:8026` -> `[ub_chat] pass`
- `chat_rpc_nodeA.log:10251` -> `[ub_rpc] pass`
- `chat_rpc_nodeA.log:10252` -> `[init] ub rpc demo pass`

NodeB:

- `chat_rpc_nodeB.log:7865` -> `[ub_chat] pass`
- `chat_rpc_nodeB.log:9885` -> `[ub_rpc] pass`
- `chat_rpc_nodeB.log:9886` -> `[init] ub rpc demo pass`

### 5.2 3-demo failure evidence (current blocking point)

3-demo still has chat pass on both nodes:

- `three_demo_nodeA.log:7998` -> `[ub_chat] pass`
- `three_demo_nodeB.log:7811` -> `[ub_chat] pass`

RDMA chain progresses deep but fails at bind_jetty on both nodes:

NodeA:

- `three_demo_nodeA.log:8377` -> `[ub_rdma] step 1: query_dev_attr unavailable: -22 (continue)`
- `three_demo_nodeA.log:9345` -> `[ub_rdma] step 8: import_jetty -> ok`
- `three_demo_nodeA.log:9349` -> `[ub_rdma] step 9: bind_jetty failed: -22`
- `three_demo_nodeA.log:9347` -> `URMA|ubcore|...|trans mode is not rc type.`

NodeB:

- `three_demo_nodeB.log:8231` -> `[ub_rdma] step 1: query_dev_attr unavailable: -22 (continue)`
- `three_demo_nodeB.log:9158` -> `[ub_rdma] step 8: import_jetty -> ok`
- `three_demo_nodeB.log:9162` -> `[ub_rdma] step 9: bind_jetty failed: -22`
- `three_demo_nodeB.log:9160` -> `URMA|ubcore|...|trans mode is not rc type.`

In this run, no `[init] ub rpc demo pass` marker was found in `three_demo_nodeA.log` / `three_demo_nodeB.log`, which matches script result code 13.

### 5.3 Kernel warning / stacktrace health

No match found in all 4 logs for:

- `WARNING: CPU:`
- `Call trace:`
- `Kernel panic - not syncing`
- `kernel BUG at`
- `BUG: scheduling while atomic`

So guest-side warning/call-trace pollution remains clean in this rerun.

## 6. Current Simulated-System Status Analysis

### 6.1 What is confirmed working

- Dual-node boot and FM link-up are functional.
- `ub_chat` is functional in both baseline and 3-demo runs.
- `ub_rpc_demo` is functional in baseline run.

### 6.2 What is currently blocked

- `ub_rdma_demo` fails at `bind_jetty` with `-22` on both nodes.
- In 3-demo run, because the init path does not reach `[init] ub rpc demo pass`, the test script returns `iteration_1_result=13` (timeout waiting rpc pass/fail marker).

### 6.3 Immediate interpretation

- Current failure signature is **not** the previous cmd-id drift symptom (`query_dev_attr hard-fail and immediate exit`).
- New signature shows the chain reaches `import_jetty -> ok`, then fails at `bind_jetty` with kernel message `trans mode is not rc type`, indicating a transport-mode / bind parameter compatibility issue in current rdma demo path.

## 7. Conclusion

Current dual-node status after rerun:

- `ub_chat`: pass
- `ub_rpc_demo`: pass in baseline, but 3-demo iteration does not complete rpc pass marker
- `ub_rdma_demo`: fail at `bind_jetty -22`

Final verdict for "3 demos all healthy in dual-node": **No (still not met)**.
