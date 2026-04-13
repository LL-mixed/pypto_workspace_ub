# 2026-04-13 Dual-Node 3-Demo Detailed Validation Report

## 1. Scope

This report documents the validation basis for the statement:

- latest real regression run `2026-04-13_14-37-59_25932` is `3 / 3 PASS`
- `chat + rpc + rdma + obmm` all passed in the dual-node simulator environment

This report is intentionally evidence-driven. It separates:

- environment and runtime constraints;
- component versions and commit hashes;
- code-level validation logic of each demo;
- actual run procedure and actual logs;
- current system status and the alignment boundary against the Linqu data system / Linqu runtime hierarchy.

## 2. Environment Setup And Constraints

### 2.1 Host-side harness and QEMU runtime model

The validation is driven by:

- [run_ub_dual_node_demo.sh](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/scripts/run_ub_dual_node_demo.sh)
- [qemu_ub_common.sh](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/scripts/qemu_ub_common.sh)

Key runtime model from the harness:

- two QEMU guests are started, one as `nodeA`, one as `nodeB`
- each QEMU instance is started paused via `-S`, then resumed through QMP
- QEMU command line uses:
  - `-M virt,gic-version=3,its=on,ummu=on,ub-cluster-mode=on`
  - `-cpu cortex-a57`
  - `-m 8G`
  - `-serial file:<guest_log>`
  - `-kernel out/Image`
  - `-initrd out/initramfs.cpio.gz`
- QMP sockets are used per node to `cont` the paused VM after early link setup checks
- topology and entity plan are provided by:
  - [ub_topology_two_node_v0.ini](/Volumes/repos/pypto_workspace/simulator/vendor/ub_topology_two_node_v0.ini)
  - [ub_topology_two_node_v2_entity.ini](/Volumes/repos/pypto_workspace/simulator/vendor/ub_topology_two_node_v2_entity.ini)

  Relevant code references:

- QEMU binary selection and rebuild: [qemu_ub_common.sh:84](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/scripts/qemu_ub_common.sh:84)
- QEMU machine options and kernel append: [run_ub_dual_node_demo.sh:275](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/scripts/run_ub_dual_node_demo.sh:275)
- iteration lifecycle: [run_ub_dual_node_demo.sh:545](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/scripts/run_ub_dual_node_demo.sh:545)

### 2.2 Guest-side runtime model

The guest init process launches demo binaries based on kernel cmdline switches, not based on whether a feature is built-in or shipped as a `.ko`.

Relevant code references:

- `linqu_ub_chat=1`: [init.c:200](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/init.c:200)
- `linqu_ub_rdma_demo=1`: [init.c:205](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/init.c:205)
- `linqu_obmm_demo=1`: [init.c:210](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/init.c:210)
- `linqu_ub_rpc_demo=1`: [init.c:221](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/init.c:221)

Probe launch / pass-fail markers:

- chat: [init.c:638](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/init.c:638)
- rdma: [init.c:671](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/init.c:671)
- rpc: [init.c:731](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/init.c:731)
- obmm: [init.c:764](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/init.c:764)

### 2.3 Runtime constraints used in this validation

This validation is not “no-constraint bare metal”. It is subject to explicit simulator constraints.

1. `obmm.skip_cache_maintain=1` is injected by default.
- This is done by [qemu_ub_common.sh:3](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/scripts/qemu_ub_common.sh:3)
- It avoids simulation-only cache maintenance noise on the OBMM path.
- This means current validation is done in the simulator's explicit “skip cache maintain” mode.

2. QEMU is rebuilt from a single canonical source/build location before running.
- binary path: [qemu_ub_common.sh:11](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/scripts/qemu_ub_common.sh:11)
- rebuild path: [qemu_ub_common.sh:84](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/scripts/qemu_ub_common.sh:84)
- no fallback `/tmp/ub-qemu-build-*` binary is involved in this flow.

3. Validation is a simulator regression, not a production deployment proof.
- It proves correctness within the current dual-node QEMU + guest-kernel simulator stack.
- It does not, by itself, prove production hardware timing behavior.

## 3. Component Commits

Main simulator repo:

- `simulator`: `e1f5aae3502f078a5bd59acdbf4801fe5d0a9584`

Guest kernel submodule:

- `kernel_ub`: `7ea063ff48e1f2740ae526fa835d3e64de5d3d73`

QEMU submodule:

- `qemu_8.2.0_ub`: `3ea83513ecf3d32819fd29bc5b4316dc77e71abc`

Relevant recent fixes included in this validated state:

- guest kernel JFR recv completion hardening:
  - `kernel_ub` commit `7ea063ff48e1`
- QEMU buffered URMA RX deferred-until-ready fix:
  - `qemu_8.2.0_ub` commit `3ea83513ec`
- main-repo submodule bump for the QEMU fix:
  - `simulator` commit `e1f5aae`

## 4. Demo Locations And Code-Level Validation Logic

### 4.1 Chat demo

Code location:

- [ub_chat.c](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/ub_chat.c)

Semantic payload constants:

- request payload: `greeting from NodeA` at [ub_chat.c:27](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/ub_chat.c:27)
- reply payload: `copy, greeting back from NodeB` at [ub_chat.c:28](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/ub_chat.c:28)

Guest-side pass marker:

- [ub_chat.c:645](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/ub_chat.c:645)

Harness-side validation logic:

- [run_ub_dual_node_demo.sh:130](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/scripts/run_ub_dual_node_demo.sh:130)

What is actually verified:

- `[ub_chat] pass` must exist
- `[ub_chat] fail` must not exist
- summary must show `tx=5 rx=5`
- nodeA must receive exact reply payload `copy, greeting back from NodeB`
- nodeB must receive exact request payload `greeting from NodeA`

This is not just “socket send/recv happened”. It is payload-semantic validation.

### 4.2 RPC demo

Code location:

- [ub_rpc_demo.c](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/ub_rpc_demo.c)

Semantic payload constants:

- ECHO payload: `greeting from NodeA` at [ub_rpc_demo.c:27](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/ub_rpc_demo.c:27)
- CRC payload: `buffer from NodeA for CRC verification over ub_link` at [ub_rpc_demo.c:28](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/ub_rpc_demo.c:28)

Server-side RPC handling:

- server handles ECHO / CRC32 requests and logs `server handled`: [ub_rpc_demo.c:792](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/ub_rpc_demo.c:792)
- client validates returned result and prints `verified=1`: [ub_rpc_demo.c:939](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/ub_rpc_demo.c:939)

Harness-side validation logic:

- [run_ub_dual_node_demo.sh:145](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/scripts/run_ub_dual_node_demo.sh:145)

What is actually verified:

- `[ub_rpc] pass` must exist
- `[ub_rpc] fail` must not exist
- nodeA client must verify:
  - ECHO result equals expected payload
  - CRC32 result equals expected CRC string
- nodeB server must log that it handled both `ECHO` and `CRC32`

This is function-semantic validation, not just message liveness.

### 4.3 RDMA demo

Code location:

- [ub_rdma_demo.c](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/ub_rdma_demo.c)

Key correctness points in code:

- UMMU TID allocation: [ub_rdma_demo.c:1853](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/ub_rdma_demo.c:1853)
- segment register/unregister TLV path: [ub_rdma_demo.c:1260](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/ub_rdma_demo.c:1260)
- import/bind/send/recv path: [ub_rdma_demo.c:2786](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/ub_rdma_demo.c:2786)
- request/reply payload validation:
  - request send: [ub_rdma_demo.c:2874](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/ub_rdma_demo.c:2874)
  - reply recv and payload check: [ub_rdma_demo.c:2892](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/ub_rdma_demo.c:2892)
  - request recv and payload check on nodeB: [ub_rdma_demo.c:2911](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/ub_rdma_demo.c:2911)
  - reply send on nodeB: [ub_rdma_demo.c:2931](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/ub_rdma_demo.c:2931)
- cleanup correctness:
  - unregister segment: [ub_rdma_demo.c:2036](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/ub_rdma_demo.c:2036)
  - free token id: [ub_rdma_demo.c:2061](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/ub_rdma_demo.c:2061)
  - free UMMU tid: [ub_rdma_demo.c:2087](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/ub_rdma_demo.c:2087)

  Harness-side validation logic:

- [run_ub_dual_node_demo.sh:163](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/scripts/run_ub_dual_node_demo.sh:163)

What is actually verified:

- `[ub_rdma] pass` must exist
- `[ub_rdma] fail` must not exist
- positive lifecycle markers must all exist:
  - alloc UMMU tid
  - alloc token id
  - register segment
  - import jetty
  - bind jetty
  - post recv
  - ready sync
  - unbind jetty
  - unimport jetty
  - unregister segment cleanup
  - free token id cleanup
  - free UMMU tid cleanup
- payload semantics must hold:
  - nodeA sends request and receives exact reply payload `rdma reply payload from NodeB`
  - nodeB receives exact request payload `rdma request payload from NodeA` and sends reply
- negative patterns must not exist:
  - invalid port speed
  - failed device status query
  - topo map missing
  - wait resp timeout
  - save-tp failure
  - unimport jetty async failure
  - failed uobject cleanup
  - invalidate cfg table failure

  This is payload-level RDMA validation, not just object lifecycle smoke.

### 4.4 OBMM / cross-node memory access demo

Code location:

- [ub_obmm_demo.c](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/ub_obmm_demo.c)

Key correctness points in code:

- nodeB discovers a legal import PA window from guest sysfs instead of hardcoding a BAR address:
  - [ub_obmm_demo.c:307](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/ub_obmm_demo.c:307)
  - reads `/sys/bus/ub/devices/00001/mem_windows` at [ub_obmm_demo.c:319](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/ub_obmm_demo.c:319)
- nodeA does `OBMM_CMD_EXPORT` and logs returned `mem_id/uba/tokenid`:
  - [ub_obmm_demo.c:379](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/ub_obmm_demo.c:379)
- nodeB does `OBMM_CMD_IMPORT` with sim-decoder private metadata carrying `remote_uba`:
  - [ub_obmm_demo.c:450](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/ub_obmm_demo.c:450)
- nodeA waits for explicit `IMPORT_OK` and `UNIMPORT_OK` before unexporting:
  - import ack: [ub_obmm_demo.c:541](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/ub_obmm_demo.c:541)
  - unimport ack: [ub_obmm_demo.c:564](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/ub_obmm_demo.c:564)

  Harness-side validation logic:

- [run_ub_dual_node_demo.sh:221](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/scripts/run_ub_dual_node_demo.sh:221)

What is actually verified:

- `[ub_obmm] pass` must exist
- `[ub_obmm] fail` must not exist
- nodeA must show:
  - export success
  - nodeB import acknowledged
  - nodeB unimport acknowledged
  - unexport success
- nodeB must show:
  - mem window discovery from sysfs
  - import success with `local_pa/local_cna/remote_cna`
  - unimport success

  This is a real `export -> import -> sim decoder MAP -> unimport -> UNMAP -> unexport` closure, not just local ioctl smoke.

## 5. Actual Run Procedure

### 5.1 Command used

The validated regression run was executed with:

```bash
cd /Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64 && \
UB_SYNC_ARTIFACTS=0 \
UB_REBUILD_INITRAMFS=0 \
ITERATIONS=3 \
RUN_SECS=150 \
START_GAP_SECS=1 \
APPEND_EXTRA="linqu_probe_skip=1 linqu_probe_load_helper=1 linqu_ub_chat=1 linqu_ub_rpc_demo=1 linqu_ub_rdma_demo=1 linqu_obmm_demo=1" \
scripts/run_ub_dual_node_demo.sh
```

Result summary:

- [demo_report.latest.txt](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/out/demo_report.latest.txt)

Key summary fields:

- `run_id=2026-04-13_14-37-59_25932`
- `passed=3`
- `failed=0`
- `pass_rate_percent=100`

### 5.2 Per-iteration log locations

Latest validated run directories:

- [iter1](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-13_14-37-59_25932_demo_iter1)
- [iter2](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-13_14-37-59_25932_demo_iter2)
- [iter3](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-13_14-37-59_25932_demo_iter3)

Each iteration contains:

- `nodeA_guest.log`
- `nodeB_guest.log`
- `nodeA_qemu.log`
- `nodeB_qemu.log`

### 5.3 Harness gating sequence

The harness does more than “grep final pass”. The sequence is:

1. start both nodes paused
2. detect early link/QEMU failures
3. resume via QMP
4. wait for FM links to reach `READY`
5. wait for entity injection readiness
6. wait for each enabled demo to emit `[init] ... pass`
7. run per-demo semantic validators
8. run kernel-health validators rejecting warning/stacktrace/panic

Relevant code:

- FM / entity readiness: [run_ub_dual_node_demo.sh:309](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/scripts/run_ub_dual_node_demo.sh:309)
- demo pass waits: [run_ub_dual_node_demo.sh:585](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/scripts/run_ub_dual_node_demo.sh:585)
- semantic validators: [run_ub_dual_node_demo.sh:706](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/scripts/run_ub_dual_node_demo.sh:706)
- kernel health validation: [run_ub_dual_node_demo.sh:245](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/scripts/run_ub_dual_node_demo.sh:245)

## 6. Key Validation Logs From The Latest Run

All examples below are from iteration 1 of the latest validated run.

### 6.1 Chat

nodeA received correct reply payload:

- [nodeA_guest.log:1996](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-13_14-37-59_25932_demo_iter1/nodeA_guest.log:1996)
- [nodeA_guest.log:2003](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-13_14-37-59_25932_demo_iter1/nodeA_guest.log:2003)

nodeB received correct request payload:

- [nodeB_guest.log:1992](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-13_14-37-59_25932_demo_iter1/nodeB_guest.log:1992)
- [nodeB_guest.log:1999](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-13_14-37-59_25932_demo_iter1/nodeB_guest.log:1999)

### 6.2 RPC

nodeA client verified ECHO and CRC32 semantics:

- [nodeA_guest.log:2016](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-13_14-37-59_25932_demo_iter1/nodeA_guest.log:2016)
- [nodeA_guest.log:2017](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-13_14-37-59_25932_demo_iter1/nodeA_guest.log:2017)
- [nodeA_guest.log:2021](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-13_14-37-59_25932_demo_iter1/nodeA_guest.log:2021)

nodeB server handled both operations:

- [nodeB_guest.log:2012](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-13_14-37-59_25932_demo_iter1/nodeB_guest.log:2012)
- [nodeB_guest.log:2013](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-13_14-37-59_25932_demo_iter1/nodeB_guest.log:2013)
- [nodeB_guest.log:2017](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-13_14-37-59_25932_demo_iter1/nodeB_guest.log:2017)

### 6.3 RDMA

Guest-side payload round-trip evidence:

- nodeA send + recv reply:
  - [nodeA_guest.log:2056](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-13_14-37-59_25932_demo_iter1/nodeA_guest.log:2056)
  - [nodeA_guest.log:2057](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-13_14-37-59_25932_demo_iter1/nodeA_guest.log:2057)
  - [nodeA_guest.log:2066](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-13_14-37-59_25932_demo_iter1/nodeA_guest.log:2066)
- nodeB recv + send reply:
  - [nodeB_guest.log:2052](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-13_14-37-59_25932_demo_iter1/nodeB_guest.log:2052)
  - [nodeB_guest.log:2053](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-13_14-37-59_25932_demo_iter1/nodeB_guest.log:2053)
  - [nodeB_guest.log:2062](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-13_14-37-59_25932_demo_iter1/nodeB_guest.log:2062)

  QEMU-side transport evidence for the same path:

- nodeA sends to remote hardware jetty `128`:
  - [nodeA_qemu.log:7387](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-13_14-37-59_25932_demo_iter1/nodeA_qemu.log:7387)
  - [nodeA_qemu.log:7403](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-13_14-37-59_25932_demo_iter1/nodeA_qemu.log:7403)
- nodeB receives request and completes CQE:
  - [nodeB_qemu.log:7095](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-13_14-37-59_25932_demo_iter1/nodeB_qemu.log:7095)
  - [nodeB_qemu.log:7126](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-13_14-37-59_25932_demo_iter1/nodeB_qemu.log:7126)
- nodeB sends reply back to jetty `128`:
  - [nodeB_qemu.log:7178](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-13_14-37-59_25932_demo_iter1/nodeB_qemu.log:7178)
  - [nodeB_qemu.log:7192](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-13_14-37-59_25932_demo_iter1/nodeB_qemu.log:7192)
- nodeA receives reply CQE:
  - [nodeA_qemu.log:7425](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-13_14-37-59_25932_demo_iter1/nodeA_qemu.log:7425)
  - [nodeA_qemu.log:7452](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-13_14-37-59_25932_demo_iter1/nodeA_qemu.log:7452)

### 6.4 OBMM / cross-node memory access

Guest-side export/import/unimport/unexport evidence:

- nodeA export + unexport:
  - [nodeA_guest.log:2072](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-13_14-37-59_25932_demo_iter1/nodeA_guest.log:2072)
  - [nodeA_guest.log:2076](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-13_14-37-59_25932_demo_iter1/nodeA_guest.log:2076)
  - [nodeA_guest.log:2078](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-13_14-37-59_25932_demo_iter1/nodeA_guest.log:2078)
- nodeB import + unimport:
  - [nodeB_guest.log:2073](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-13_14-37-59_25932_demo_iter1/nodeB_guest.log:2073)
  - [nodeB_guest.log:2079](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-13_14-37-59_25932_demo_iter1/nodeB_guest.log:2079)
  - [nodeB_guest.log:2081](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-13_14-37-59_25932_demo_iter1/nodeB_guest.log:2081)

  QEMU-side SIM_DEC evidence:

- [nodeB_qemu.log:8048](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-13_14-37-59_25932_demo_iter1/nodeB_qemu.log:8048)
- [nodeB_qemu.log:8101](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-13_14-37-59_25932_demo_iter1/nodeB_qemu.log:8101)

These two lines are the strongest backend-side proof that the sim decoder MAP/UNMAP path really executed.

### 6.5 Negative health checks

The latest `3 / 3` validated run does **not** show the following guest-side markers:

- `drop stale recv cqe`
- `WARNING: CPU`
- `Call trace:`
- `Oops:`
- `ida_free called`

Kernel-health validator in harness:

- [run_ub_dual_node_demo.sh:245](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/scripts/run_ub_dual_node_demo.sh:245)

Residual non-fatal QEMU-side cleanup item still exists in iteration 3:

- [nodeB_qemu.log:3031](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-13_14-37-59_25932_demo_iter3/nodeB_qemu.log:3031)

This line does not currently convert into guest warning, stale-CQE drop, or workload failure.

## 7. Current Overall Status

### 7.1 What is currently proven

Within the current dual-node simulator stack, the latest validated state proves:

1. `ub_link` / dual-node interconnect setup is functional enough to support all four validated user-space paths.
2. `chat` proves bidirectional semantic payload exchange over the simulated dual-node data path.
3. `rpc` proves function-level request/response semantics with exact payload and CRC verification.
4. `rdma` proves UMMU/UBURMA/UDMA lifecycle correctness plus bidirectional payload round-trip over the URMA path.
5. `obmm` proves `export -> remote import -> SIM_DEC MAP -> unimport -> SIM_DEC UNMAP -> unexport` closure.
6. The previously observed guest-kernel recv-completion regression is fixed in this validated state.

### 7.2 What is **not** proven yet

This validation does **not** yet prove:

1. long-duration acceptance stability beyond the current short 3-iteration regression window;
2. throughput / packet-loss / restart-recovery matrix completeness;
3. full Linqu L4-L7 runtime semantics;
4. real `simpler` L0-L2 execution semantics beyond the currently exposed UB guest-visible boundary.

## 8. Alignment With The Linqu Data System 7-Layer Design

Relevant design references:

- [linqu_data_system.md](/Volumes/repos/pypto_workspace/docs/pypto_top_level_design_documents/linqu_data_system.md)
- [linqu_runtime_design.md](/Volumes/repos/pypto_workspace/docs/pypto_top_level_design_documents/linqu_runtime_design.md)

Key hierarchy statement from `linqu_data_system.md`:

- commands at `L0-L2` are device-side / PTO-side
- commands at `L3-L7` are CPU-runtime / host-side
- completion still flows back to the Level-2 orchestrator
- reference: [linqu_data_system.md:87](/Volumes/repos/pypto_workspace/docs/pypto_top_level_design_documents/linqu_data_system.md:87)

Key communication-tier statement from `linqu_runtime_design.md`:

- Tier 1: `L0-L2` shared device GM
- Tier 2: `L3 ↔ L2` host-device DMA boundary
- Tier 3: `L4-L6 ↔ L3` message passing boundary
- reference: [linqu_runtime_design.md:640](/Volumes/repos/pypto_workspace/docs/pypto_top_level_design_documents/linqu_runtime_design.md:640)

### 8.1 Current alignment

The current simulator validation aligns best with the following subset:

1. **L2 guest-visible chip boundary: partially validated**
- UDMA / UMMU / UBURMA / OBMM / SIM_DEC are all exercised as guest-visible device interfaces.
- This is the closest currently validated approximation of the Linqu `L2` chip boundary in this simulator stack.

2. **L3 host view: validated**
- each guest Linux instance acts as an `L3 host` runtime view.
- user-space demo binaries run here, discover device interfaces here, and drive URMA/OBMM control and data paths here.

3. **Tier 2 (`L3 ↔ L2`) bridge: materially validated**
- `rdma` validates guest-host-side control plus device-side queue / completion path
- `obmm` validates host-side export/import control and remote decoder mapping
- together they provide current evidence that the simulator's `L3 ↔ L2` boundary is not merely stubbed

4. **Remote L3↔L3 substrate: validated as simulator transport, not yet as full Linqu L4+ runtime**
- `chat` and `rpc` show that two guest hosts can exchange semantically correct data over the current UB/URMA/u-link substrate
- but this is still demo-level validation, not Linqu `L4-L7` orchestrator/runtime validation

### 8.2 Current non-alignment / remaining gap

The following parts of the Linqu 7-layer design are **not** closed by this report:

1. **L0-L2 `simpler` runtime semantics are not validated end-to-end here**
- the report validates guest-visible UB device semantics, not the full `simpler` task-ring / buffer-ring / DAG orchestration model.

2. **L4-L7 runtime hierarchy is not under test here**
- no `NodeDaemon`, `RemoteDispatcher`, or multi-level `CALL_TASK / TASK_COMPLETE` orchestration is being exercised in this report.
- therefore this report should not be read as “Linqu 7-layer runtime is fully validated”.

3. **Acceptance-matrix completeness is still pending**
- current result is strong short regression evidence, not the final acceptance bar.

## 9. Bottom-Line Assessment

The strongest technically defensible conclusion from this report is:

- in the current simulator state (`simulator=e1f5aae`, `kernel_ub=7ea063ff48e1`, `qemu_8.2.0_ub=3ea83513ec`), the dual-node environment has real evidence for:
  - semantic `chat` pass
  - semantic `rpc` pass
  - payload-level `rdma` pass
  - real `obmm + sim_decoder MAP/UNMAP` pass
- the validated result is based on real guest and QEMU logs from run `2026-04-13_14-37-59_25932`, with `3 / 3 PASS`
- this proves the simulator's current `L3 host ↔ L2 guest-visible device boundary` and remote dual-node substrate are functioning for these four validated paths
- this does **not** yet prove full Linqu `L4-L7` runtime closure or final long-duration acceptance readiness

## 10. Recommended Next Step

1. run a longer acceptance matrix on the same validated component set;
2. continue cleaning the remaining non-fatal QEMU `RQE null va or zero len` log;
3. only after that, extend the report from “validated dual-node substrate + device boundary” to “acceptance-ready Linqu runtime substrate”.


## Appendix

下面这张表是“默认 probe 流程”在当前 guest 启动路径里到底做了什么。

  先说结论：

  - linqu_probe_skip=1 跳过的，只是 init.c 里最后那一步 run_probe()，以及 run_demo 默认动作里的 /bin/linqu_probe
  - 它不跳过前面的：
      - 驱动 bootstrap
      - UB sysfs readiness 等待
      - 可选的 bizmsg/urma_dp/chat/rpc/rdma/obmm 显式 demo/verify

        直接证据在 simulator/guest-linux/aarch64/init.c:1170 到 simulator/guest-linux/aarch64/init.c:1205。

        默认 Probe 流程表

| 阶段 | 是否受 linqu_probe_skip=1 影响 | 实际动作 | 代码位置 | 说明 |
|---|---|---|---|---|
| 挂载基础 fs | 否 | 挂载 /proc /sys /dev /dev/pts | simulator/guest-linux/aarch64/init.c:1156 | init 基础启动准备 |
| 初始状态 dump | 否 | dump UB/guest payload 状态 | simulator/guest-linux/aarch64/init.c:1168 | 主要用于诊断 |
| 驱动 bootstrap | 否 | best-effort insmod ubus/ummu/ubase/hisi_ubus/obmm/ub-sim-decoder/ubcore/udma/ipourma 等 | simulator/guest-linux/aarch64/init.c:1015||
| built-in 或 .ko 都能兼容；不是 probe 语义本身 |||||
| UB sysfs readiness 等待 | 否 | 等 /sys/bus/ub/devices/00001/... ready | simulator/guest-linux/aarch64/init.c:1036 | 这是接口 ready 检查 |
| bizmsg verify | 否 | 若带 linqu_bizmsg_verify=1，执行 bizmsg roundtrip + payload consistency probe | simulator/guest-linux/aarch64/init.c:1175, simulator/||
|  guest-linux/aarch64/init.c:438 | 这是显式 verify，不受 probe_skip 影响 ||||
| ubase force bind | 否 | 若带 linqu_force_ubase_bind=1，强制设备 rebind 到 ubase | simulator/guest-linux/aarch64/init.c:1178, simulator/guest-linux/||
|  aarch64/init.c:1088 | QEMU/动态注入场景的补救路径 ||||
| URMA dataplane verify | 否 | 若带 linqu_urma_dp_verify=1，等待 ipourma 后执行 /bin/linqu_urma_dp | simulator/guest-linux/aarch64/init.c:1180, simulator/||
|  guest-linux/aarch64/init.c:598 | 显式 verify ||||
| chat demo | 否 | 若带 linqu_ub_chat=1，执行 /bin/linqu_ub_chat | simulator/guest-linux/aarch64/init.c:1185, simulator/guest-linux/aarch64/init.c:638 |  |
|  式 demo |||||
| rpc demo | 否 | 若带 linqu_ub_rpc_demo=1，执行 /bin/linqu_ub_rpc | simulator/guest-linux/aarch64/init.c:1189, simulator/guest-linux/aarch64/init.c:731 ||
|  显式 demo |||||
| rdma demo | 否 | 若带 linqu_ub_rdma_demo=1，执行 /bin/linqu_ub_rdma_demo | simulator/guest-linux/aarch64/init.c:1193, simulator/guest-linux/aarch64/||
|  init.c:671 | 显式 demo ||||
| obmm demo | 否 | 若带 linqu_obmm_demo=1，执行 /bin/linqu_ub_obmm_demo | simulator/guest-linux/aarch64/init.c:1197, simulator/guest-linux/aarch64/||
|  init.c:764 | 显式 demo ||||
| 默认 linqu_probe | 是 | 若未带 linqu_probe_skip=1，执行 run_probe() | simulator/guest-linux/aarch64/init.c:1201 | 这就是 probe_skip 真正跳过的部分 |
| hold 不关机 | 否 | 若带 linqu_probe_hold=1，probe/demo 后不 poweroff，直接 pause() | simulator/guest-linux/aarch64/init.c:1214 | 方便 harness 直接杀 QEMU|
  |

  run_demo 层的对应关系

  initramfs/run_demo 里也有同样语义：

  - 默认动作里，只有没带 linqu_probe_skip=1 才会跑 /bin/linqu_probe
  - 其它 chat/rpc/rdma/obmm 是各自独立按 flag 跑

  对应代码：

  - simulator/guest-linux/aarch64/initramfs/run_demo:45
  - simulator/guest-linux/aarch64/initramfs/run_demo:49
  - simulator/guest-linux/aarch64/initramfs/run_demo:54
  - simulator/guest-linux/aarch64/initramfs/run_demo:59
  - simulator/guest-linux/aarch64/initramfs/run_demo:64
  - simulator/guest-linux/aarch64/initramfs/run_demo:69

  所以 linqu_probe_skip=1 的准确含义是

  - 跳过“默认 linqu_probe 这一步”
  - 不跳过启动准备
  - 不跳过接口 ready 检查
  - 不跳过你显式打开的 verify/demo