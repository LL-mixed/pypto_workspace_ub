# 运行双节点 UB 互联系统指南

本文基于当前仓库（`/Volumes/repos/pypto_workspace`）里已存在的脚本和目录，给出可直接执行的双节点 UB 仿真构建与运行流程。

## 1. 目标与范围

本指南覆盖以下组件：
- UB 版 QEMU（含 `ubfm` / `ub_link` 路径）
- VM 中 Linux 内核与 UB 相关模块产物同步
- guest initramfs 构建
- 双节点启动与验证（probe / snapshot-sync / dataplane / bizmsg / ubcore+urma）

## 2. 关键路径总览（cwd 内）

- QEMU 源码（UB 版）：`simulator/vendor/qemu_8.2.0_ub`
- QEMU 可执行文件（本地已构建）：`simulator/vendor/qemu_8.2.0_ub/build/qemu-system-aarch64`
- 双节点拓扑文件：`simulator/vendor/ub_topology_two_node_v0.ini`
- 拓扑格式说明：`simulator/vendor/ub_topology_format_v0.md`

- Guest 脚本目录：`simulator/guest-linux/aarch64`
- initramfs 构建脚本：`simulator/guest-linux/aarch64/build_initramfs.sh`
- VM 同步脚本：`simulator/guest-linux/aarch64/sync_ub_kernel_artifacts_from_vm.sh`
- 双节点 URMA E2E 入口：`simulator/guest-linux/aarch64/run_ub_dual_node_ubcore_urma_e2e.sh`

- 本地产物目录：`simulator/guest-linux/aarch64/out`
  - 内核镜像：`out/Image`
  - initramfs：`out/initramfs.cpio.gz`
  - 模块目录：`out/modules/`

## 3. UBFM 编译与启动要点

### 3.1 编译侧（QEMU）

UB 相关开关在：
- `simulator/vendor/qemu_8.2.0_ub/meson_options.txt`
  - `ub`：`feature=auto`
  - `ubmem_vmmu`：`feature=auto`
  - `urma_migration`：`feature=disabled`
- `simulator/vendor/qemu_8.2.0_ub/meson.build`
  - `have_ub = get_option('ub').allowed()`

说明：当前树里 `ub` 已按 `auto/allowed` 走编译路径；`virt` 机型上暴露 `ummu` 和 `ub-cluster-mode` 属性。

### 3.2 启动侧（UBFM 配置注入）

双节点脚本统一通过环境变量注入 UBFM 配置：
- `UB_FM_NODE_ID`：节点标识（`nodeA` / `nodeB`）
- `UB_FM_TOPOLOGY_FILE`：拓扑文件路径
- `UB_FM_SHARED_DIR`：共享目录（默认 `/tmp/ub-qemu-links-dual`）

QEMU 启动核心机器参数：
- `-M virt,gic-version=3,its=on,ummu=on,ub-cluster-mode=on`

## 4. 编译与同步流程

### 4.1 编译 QEMU（如需重编）

参考 `simulator/vendor/qemu_8.2.0_ub_macos_build_notes.md`，建议最小化构建：

```bash
cd /tmp
rm -rf ub-qemu-build-verify
mkdir -p ub-qemu-build-verify
cd ub-qemu-build-verify

/Volumes/repos/pypto_workspace/simulator/vendor/qemu_8.2.0_ub/configure \
  --target-list=aarch64-softmmu \
  --disable-vmnet \
  --disable-coreaudio \
  --disable-cocoa \
  --disable-sdl \
  --disable-gtk \
  --disable-opengl \
  --disable-vnc \
  --disable-tools \
  --disable-slirp \
  --disable-linux-user \
  --disable-bsd-user \
  --disable-docs

ninja -j8 qemu-system-aarch64
```

### 4.2 VM 内核与模块编译并回传

脚本默认 VM 路径：
- `VM_HOST=ll@192.168.64.3`
- `VM_KERNEL_SRC=/home/ll/share/shared_data/kernel_ub`
- `VM_KERNEL_BUILD=/home/ll/share/shared_data/kernel_build`
- `VM_LINQU_DRIVER_DIR=/home/ll/share/shared_data/linqu_guest_driver`

VM 内执行的关键编译命令（由脚本触发）：
- `make O=$VM_KERNEL_BUILD ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -jN Image drivers/ub/ubus/vendor/hisilicon/hisi_ubus.ko`
- 可选编译 `linqu_ub_drv.ko`

本地一键同步：

```bash
cd /Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64
BUILD_IN_VM=1 BUILD_LINQU_DRIVER_IN_VM=1 ./sync_ub_kernel_artifacts_from_vm.sh
```

同步产物：
- `out/Image`
- `out/modules/hisi_ubus.ko`
- `out/modules/udma.ko`
- `out/modules/linqu_ub_drv.ko`（若 VM 侧存在）

### 4.3 编译配置位置

- 本地内核 build 树：`simulator/guest-linux/aarch64/out/kernel_build`
- 配置文件：`simulator/guest-linux/aarch64/out/kernel_build/.config`
- 源码链接：`simulator/guest-linux/aarch64/out/kernel_build/source -> /Volumes/repos/pypto_workspace/simulator/guest-linux/kernel_ub`

## 5. initramfs 构建

执行：

```bash
cd /Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64
export AARCH64_LINUX_CC=aarch64-linux-gnu-gcc
export HISI_UBUS_GUEST_MODULE=/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/out/modules/hisi_ubus.ko
export UB_UDMA_GUEST_MODULE=/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/out/modules/udma.ko
export LINQU_UB_GUEST_MODULE=/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/out/modules/linqu_ub_drv.ko
./build_initramfs.sh
```

产物与内容：
- `out/initramfs.cpio.gz`
- `out/initramfs/init`（由 `init.c` 编译）
- `out/initramfs/bin/linqu_probe`
- `out/initramfs/bin/linqu_urma_dp`
- `out/initramfs/bin/insmod`
- `out/initramfs/lib/modules/hisi_ubus.ko`
- `out/initramfs/lib/modules/udma.ko`
- `out/initramfs/lib/modules/linqu_ub_drv.ko`（可选）

## 6. 部署要义（双节点）

- 共享目录默认 `/tmp/ub-qemu-links-dual`，运行前需清理旧 `.ini/.kick/.lock`。
- `/tmp/ub-qemu/ub-bus-instance-*.lock` 旧锁文件需清理。
- 节点通常按 `nodeA` 先启动，间隔后再起 `nodeB`。
- 拓扑按 `nodeX.device_id` 前缀做本地视角解析，靠 `UB_FM_NODE_ID` 生效。
- 核心拓扑文件：`simulator/vendor/ub_topology_two_node_v0.ini`。

该文件当前定义了 `nodeA.ubcdev0:1 <-> nodeB.ubcdev0:1` 直连链路。

## 7. 运行与验证

### 7.1 最小双节点 UB probe

```bash
cd /Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64
./run_ub_dual_node_probe.sh
```

### 7.2 快照同步验证

```bash
./run_ub_dual_node_snapshot_sync_test.sh
```

### 7.3 dataplane 验证

```bash
ITERATIONS=1 RUN_SECS=180 START_GAP_SECS=1 ./run_ub_dual_node_dataplane_test.sh
```

### 7.4 business message 验证

```bash
ITERATIONS=1 RUN_SECS=120 START_GAP_SECS=1 ./run_ub_dual_node_business_msg_test.sh
```

### 7.5 ubcore/urma 双节点 E2E（推荐主验证入口）

```bash
ITERATIONS=1 RUN_SECS=180 START_GAP_SECS=1 ./run_ub_dual_node_ubcore_urma_e2e.sh
```

该入口会调用 `run_ub_dual_node_urma_dataplane_workload_test.sh`，并注入：
- `linqu_probe_skip=1`
- `linqu_probe_load_helper=1`
- `linqu_bizmsg_verify=1`
- `linqu_force_ubase_bind=1`
- `linqu_urma_dp_verify=1`

同时按节点角色注入：
- `linqu_urma_dp_role=nodeA|nodeB`

## 8. 通过判据（日志关键字）

- `ub_fm: accept topology file link ...`
- `ub_link: published local endpoint ...`
- `ub_link: remote snapshot load done ...`
- `ub_link: remote cfg notify done ...`
- `ub_fm: remote kick received on ubcdev0:1 ...`
- `Register ubcore client success.`
- `[ipourma] Register netlink success.`
- `[urma_dp] rx peer src=...`
- `[urma_dp] pass`
- `[init] urma dataplane pass`

## 9. 常见失败点快速定位

- `No UB-capable qemu-system-aarch64 found`：`QEMU_BIN` 未指向 UB 版 QEMU，或二进制不含 `ummu/ub-cluster-mode`。
- `KERNEL_IMAGE not found` / `INITRAMFS_IMAGE not found`：先确认 `out/Image` 与 `out/initramfs.cpio.gz` 已生成。
- URMA 超时：优先检查 `hisi_ubus.ko`、`linqu_ub_drv.ko` 是否进了 initramfs，日志里是否有 `Register ubcore client success.` 与 `ipourma` 网卡。

---

如果只跑一条主线：
1. `sync_ub_kernel_artifacts_from_vm.sh`
2. `build_initramfs.sh`
3. `run_ub_dual_node_ubcore_urma_e2e.sh`

即可完成“运行双节点 UB 互联系统”的最小闭环。
