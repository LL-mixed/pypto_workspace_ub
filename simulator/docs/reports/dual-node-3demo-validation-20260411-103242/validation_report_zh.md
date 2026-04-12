# 双节点 3 个 Demo 复测报告（2026-04-11）

## 1. 目标

在双节点模拟互联环境中复测以下 3 个 demo：

- `ub_chat.c`
- `ub_rpc_demo.c`
- `ub_rdma_demo.c`

并给出：运行方式、日志证据、当前模拟系统实现状态分析。

## 2. 测试环境

- 工作区：`/Volumes/repos/pypto_workspace`
- 测试目录：`/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64`
- QEMU：`simulator/vendor/qemu_8.2.0_ub/build/qemu-system-aarch64`
- 日期：2026-04-11

## 3. 运行方式

统一脚本：`run_ub_dual_node_demo.sh`

前置条件：

- 需要有权限在 `/tmp/ub-qemu-links-dual/qmp` 创建 QMP Unix Socket。
- 在受限沙箱中会出现 `Failed to bind socket ... Operation not permitted`，这是环境权限问题，不是 demo 功能结论。

### 3.1 基线（chat + rpc）

```bash
cd /Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64 && \
REPORT_FILE=/Volumes/repos/pypto_workspace/simulator/docs/reports/dual-node-3demo-validation-20260411-103242/chat_rpc_baseline_report.txt \
ITERATIONS=1 RUN_SECS=120 START_GAP_SECS=1 ./run_ub_dual_node_demo.sh
```

产物：

- `/Volumes/repos/pypto_workspace/simulator/docs/reports/dual-node-3demo-validation-20260411-103242/chat_rpc_baseline_report.txt`
- 原始 guest 日志副本已从版本库裁剪，下面保留的是当时提取的行号证据

### 3.2 三 demo 同开（chat + rpc + rdma）

```bash
cd /Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64 && \
APPEND_EXTRA='linqu_probe_skip=1 linqu_probe_load_helper=1 linqu_force_ubase_bind=1 linqu_ub_chat=1 linqu_ub_rpc_demo=1 linqu_ub_rdma_demo=1' \
ITERATIONS=1 RUN_SECS=120 START_GAP_SECS=1 ./run_ub_dual_node_demo.sh
```

产物：

- `/Volumes/repos/pypto_workspace/simulator/docs/reports/dual-node-3demo-validation-20260411-103242/three_demo_report.txt`
- 补充摘要：
  - `/Volumes/repos/pypto_workspace/simulator/docs/reports/dual-node-3demo-validation-20260411-103242/three_demo_after_fix_report.txt`
  - `/Volumes/repos/pypto_workspace/simulator/docs/reports/dual-node-3demo-validation-20260411-103242/three_demo_after_tlv_fix_report.txt`

## 4. 结果总览

- chat+rpc 基线：**PASS**
- chat+rpc+rdma：**FAIL**

报告文件关键字段：

- `chat_rpc_baseline_report.txt`：`passed=1 failed=0 pass_rate_percent=100`
- `three_demo_report.txt`：`passed=0 failed=1 pass_rate_percent=0`

## 5. 日志证据

### 5.1 chat + rpc 通过

在 `chat_rpc_nodeA.log` / `chat_rpc_nodeB.log`：

- `chat_rpc_nodeA.log:12736` -> `[ub_chat] pass`
- `chat_rpc_nodeA.log:12737` -> `[init] ub chat pass`
- `chat_rpc_nodeA.log:20301` -> `[ub_rpc] pass`
- `chat_rpc_nodeA.log:20302` -> `[init] ub rpc demo pass`
- `chat_rpc_nodeB.log:12166` -> `[ub_chat] pass`
- `chat_rpc_nodeB.log:12167` -> `[init] ub chat pass`
- `chat_rpc_nodeB.log:18651` -> `[ub_rpc] pass`
- `chat_rpc_nodeB.log:18652` -> `[init] ub rpc demo pass`

### 5.2 rdma 在 step1 失败

在 `three_demo_nodeA.log` / `three_demo_nodeB.log`：

- `three_demo_nodeA.log:13112` -> `[ub_rdma] step 1: query_dev_attr failed: -22`
- `three_demo_nodeA.log:13154` -> `[ub_rdma] fail`
- `three_demo_nodeA.log:13155` -> `[init] ub rdma demo fail exit=1`
- `three_demo_nodeB.log:12557` -> `[ub_rdma] step 1: query_dev_attr failed: -22`
- `three_demo_nodeB.log:12599` -> `[ub_rdma] fail`
- `three_demo_nodeB.log:12600` -> `[init] ub rdma demo fail exit=1`

同一轮里 chat/rpc 仍然通过：

- `three_demo_nodeA.log:12734` -> `[ub_chat] pass`
- `three_demo_nodeA.log:19742` -> `[ub_rpc] pass`
- `three_demo_nodeB.log:12179` -> `[ub_chat] pass`
- `three_demo_nodeB.log:18467` -> `[ub_rpc] pass`

### 5.3 guest warning/call trace 清理状态

在两组日志中均未匹配到：

- `WARNING: CPU:`
- `Call trace:`
- `BUG: scheduling while atomic`
- `kernel BUG at`
- `insmod signal`

说明此前 guest warning/call trace 问题在当前运行中仍保持已修复状态。

## 6. 当前实现状态分析

### 6.1 已可用部分

- 双节点拉起、链路联通、实体就绪（脚本检查通过）。
- UDMA + U-Link 路径能够支撑：
  - `ub_chat` 双端通过
  - `ub_rpc_demo` 双端通过

### 6.2 当前缺口

- `ub_rdma_demo` 无法通过第一步设备属性查询（`query_dev_attr` 返回 `-22`）。
- 因此“当前 3 个 demo 在双节点环境都正常”这一结论**不成立**。

### 6.3 代码层根因判断（高置信）

`ub_rdma_demo` 使用的用户态兼容头：

- `/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/uburma_cmd_user_compat.h`

与内核真实命令枚举：

- `/Volumes/repos/pypto_workspace/simulator/guest-linux/kernel_ub/drivers/ub/urma/uburma/uburma_cmd.h`

存在枚举漂移：兼容头缺少

- `UBURMA_CMD_ADVISE_JFR`
- `UBURMA_CMD_UNADVISE_JFR`

导致后续命令号整体偏移。实测关键值：

- 内核：`UBURMA_CMD_QUERY_DEV_ATTR=40`，`UBURMA_CMD_ALLOC_JFC=61`
- 用户兼容头：`UBURMA_CMD_QUERY_DEV_ATTR=38`，`UBURMA_CMD_ALLOC_JFC=59`

这与 rdma demo 在 step1 的 `-EINVAL(-22)` 现象一致。

## 7. 结论

当前双节点模拟环境结论：

- `ub_chat`：通过
- `ub_rpc_demo`：通过
- `ub_rdma_demo`：失败（step1 `query_dev_attr=-22`）

所以当前状态是 **2/3 通过**，尚不能给出“3 个 demo 全部正常”的结论。

## 8. 建议后续修复

1. 由内核头自动生成或严格同步 `uburma_cmd_user_compat.h`。
2. 对关键命令号加编译期断言（`QUERY_DEV_ATTR`、`ALLOC_JFC`、`ACTIVE_JFC` 等）。
3. 修复后按本报告同样命令重跑，要求双端出现 `[ub_rdma] pass`。
