# Dual-Node 3 Demo Validation Report (2026-04-11)

## 1. Scope

Validation target in dual-node simulated interconnect environment:

- `ub_chat.c`
- `ub_rpc_demo.c`
- `ub_rdma_demo.c`

Goal: re-run and confirm whether the 3 demos are currently healthy, with runnable commands, raw evidence, and implementation-status analysis.

## 2. Environment

- Workspace: `/Volumes/repos/pypto_workspace`
- Guest test root: `/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64`
- QEMU binary resolved by script: `simulator/vendor/qemu_8.2.0_ub/build/qemu-system-aarch64`
- Date: 2026-04-11

## 3. Run Method

All runs use script:

- `run_ub_dual_node_demo.sh`

Prerequisite:

- Need host-level permission to create QMP Unix sockets under `/tmp/ub-qemu-links-dual/qmp`.
- In restricted sandbox, QEMU may fail with:
  - `Failed to bind socket ... Operation not permitted`
  - This is an environment permission issue, not a demo functional verdict.

### 3.1 Baseline (chat + rpc)

Command:

```bash
cd /Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64 && \
REPORT_FILE=/Volumes/repos/pypto_workspace/simulator/docs/reports/dual-node-3demo-validation-20260411-103242/chat_rpc_baseline_report.txt \
ITERATIONS=1 RUN_SECS=120 START_GAP_SECS=1 ./run_ub_dual_node_demo.sh
```

Artifacts:

- report: `/Volumes/repos/pypto_workspace/simulator/docs/reports/dual-node-3demo-validation-20260411-103242/chat_rpc_baseline_report.txt`
- raw copied guest logs were trimmed from version control; line references below are kept as historical evidence

### 3.2 3-demos enabled (chat + rpc + rdma)

Command:

```bash
cd /Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64 && \
APPEND_EXTRA='linqu_probe_skip=1 linqu_probe_load_helper=1 linqu_force_ubase_bind=1 linqu_ub_chat=1 linqu_ub_rpc_demo=1 linqu_ub_rdma_demo=1' \
ITERATIONS=1 RUN_SECS=120 START_GAP_SECS=1 ./run_ub_dual_node_demo.sh
```

Artifacts:

- report: `/Volumes/repos/pypto_workspace/simulator/docs/reports/dual-node-3demo-validation-20260411-103242/three_demo_report.txt`
- supplemental summaries:
  - `/Volumes/repos/pypto_workspace/simulator/docs/reports/dual-node-3demo-validation-20260411-103242/three_demo_after_fix_report.txt`
  - `/Volumes/repos/pypto_workspace/simulator/docs/reports/dual-node-3demo-validation-20260411-103242/three_demo_after_tlv_fix_report.txt`

## 4. Results Summary

- Baseline chat+rpc: **PASS**
- 3-demos (chat+rpc+rdma): **FAIL**

Report key fields:

- `chat_rpc_baseline_report.txt`: `passed=1 failed=0 pass_rate_percent=100`
- `three_demo_report.txt`: `passed=0 failed=1 pass_rate_percent=0`

## 5. Log Evidence

### 5.1 chat + rpc baseline is healthy

From `chat_rpc_nodeA.log` and `chat_rpc_nodeB.log`:

- `chat_rpc_nodeA.log:12736` -> `[ub_chat] pass`
- `chat_rpc_nodeA.log:12737` -> `[init] ub chat pass`
- `chat_rpc_nodeA.log:20301` -> `[ub_rpc] pass`
- `chat_rpc_nodeA.log:20302` -> `[init] ub rpc demo pass`
- `chat_rpc_nodeB.log:12166` -> `[ub_chat] pass`
- `chat_rpc_nodeB.log:12167` -> `[init] ub chat pass`
- `chat_rpc_nodeB.log:18651` -> `[ub_rpc] pass`
- `chat_rpc_nodeB.log:18652` -> `[init] ub rpc demo pass`

### 5.2 rdma demo fails on both nodes

From `three_demo_nodeA.log` and `three_demo_nodeB.log`:

- `three_demo_nodeA.log:13112` -> `[ub_rdma] step 1: query_dev_attr failed: -22`
- `three_demo_nodeA.log:13154` -> `[ub_rdma] fail`
- `three_demo_nodeA.log:13155` -> `[init] ub rdma demo fail exit=1`
- `three_demo_nodeB.log:12557` -> `[ub_rdma] step 1: query_dev_attr failed: -22`
- `three_demo_nodeB.log:12599` -> `[ub_rdma] fail`
- `three_demo_nodeB.log:12600` -> `[init] ub rdma demo fail exit=1`

At the same time, chat/rpc still pass in the same 3-demo run:

- `three_demo_nodeA.log:12734` -> `[ub_chat] pass`
- `three_demo_nodeA.log:19742` -> `[ub_rpc] pass`
- `three_demo_nodeB.log:12179` -> `[ub_chat] pass`
- `three_demo_nodeB.log:18467` -> `[ub_rpc] pass`

### 5.3 Kernel health check

For both baseline and 3-demo runs, no matches were found for:

- `WARNING: CPU:`
- `Call trace:`
- `BUG: scheduling while atomic`
- `kernel BUG at`
- `insmod signal`

So previous guest warning/call-trace issue remains fixed in these runs.

## 6. Current Simulated-System Status Analysis

### 6.1 What is working

- Dual-node boot/link orchestration works (`FM links ready`, entity readiness passes).
- UDMA + U-Link data path is sufficient for:
  - `ub_chat` end-to-end pass
  - `ub_rpc_demo` end-to-end pass
- Guest kernel warning/call-trace pollution is cleaned in current runs.

### 6.2 What is not working

- `ub_rdma_demo` cannot pass step 1 (`query_dev_attr`), returns `-EINVAL` (`-22`) on both nodes.
- Therefore, the conclusion "3 demos all healthy" is **not成立** currently.

### 6.3 Most likely immediate root cause (code-based)

`ub_rdma_demo` uses user-compat command enum in:

- `/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/uburma_cmd_user_compat.h`

Kernel driver uses enum in:

- `/Volumes/repos/pypto_workspace/simulator/guest-linux/kernel_ub/drivers/ub/urma/uburma/uburma_cmd.h`

There is enum drift in compat header: it misses

- `UBURMA_CMD_ADVISE_JFR`
- `UBURMA_CMD_UNADVISE_JFR`

As a result, subsequent command IDs are shifted. Evidence:

- Kernel enum values:
  - `UBURMA_CMD_QUERY_DEV_ATTR=40`
  - `UBURMA_CMD_ALLOC_JFC=61`
- User-compat enum values:
  - `UBURMA_CMD_QUERY_DEV_ATTR=38`
  - `UBURMA_CMD_ALLOC_JFC=59`

This mismatch can directly explain why first ioctl in rdma demo (`UBURMA_CMD_QUERY_DEV_ATTR`) returns `-EINVAL`.

## 7. Conclusion

Current dual-node simulation status:

- `ub_chat`: pass
- `ub_rpc_demo`: pass
- `ub_rdma_demo`: fail (step1 query_dev_attr `-22`)

So current answer to "3 demos in dual-node simulated interconnect are all normal" is:

- **No** (2/3 pass, rdma demo still blocked by user/kernel ioctl command enum mismatch).

## 8. Suggested Next Fix (for execution)

1. Regenerate/synchronize `uburma_cmd_user_compat.h` from kernel `uburma_cmd.h` exactly.
2. Add compile-time checks for critical command IDs (`QUERY_DEV_ATTR`, `ALLOC_JFC`, `ACTIVE_JFC`, etc.).
3. Re-run same 3-demo command and require `[ub_rdma] pass` on both nodes.
