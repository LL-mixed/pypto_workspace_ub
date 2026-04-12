# ARM64 Linux Guest Probe

This directory holds the next-step guest-side validation path for the
`linqu-ub` device on `qemu-system-aarch64`.

The goal is narrow:

1. Boot a minimal ARM64 Linux guest on `virt`
2. Inject a tiny initramfs
3. Run a userspace MMIO probe from `/init`
4. Prove that guest Linux can:
   - discover the `linqu-ub` DT node
   - read/write the device MMIO window
   - observe `cmdq/cq/irq/last_error` state
5. Optionally load a minimal guest-side `linqu-ub` platform driver

This avoids going straight to a kernel driver. We want guest-visible evidence
first.

## Expected Inputs

The scripts here expect a few external dependencies to be provided through
environment variables:

- `KERNEL_IMAGE`
  - path to a bootable ARM64 Linux kernel image for QEMU `virt`
- `AARCH64_LINUX_CC`
  - compiler command that can build a Linux AArch64 userspace binary
  - example: `aarch64-linux-gnu-gcc`
- `BUSYBOX`
  - optional path to a static ARM64 busybox binary
  - if provided, the initramfs becomes a small userspace image with a shell
  - `run_demo` is copied to `/bin/run_demo` and can be used as `rdinit`

## Files

- `probe.c`
  - userspace probe that reads `/proc/device-tree` and `/dev/mem`
- `initramfs/init`
  - early init script for the guest
- `scripts/build_initramfs.sh`
  - builds `linqu_probe` and packs the initramfs
- `scripts/run_linux_probe.sh`
  - launches `qemu-system-aarch64` with the kernel + initramfs
- `driver/linqu_ub_drv.c`
  - minimal guest-side platform driver
- `scripts/build_driver.sh`
  - builds the external kernel module when a matching kernel build tree exists

## Current State

This is a runnable scaffold. On a clean host you still need:

- a bootable ARM64 Linux kernel image
- a Linux AArch64 userspace toolchain
- a static ARM64 busybox

Once those are supplied, the intended loop is:

```sh
export KERNEL_IMAGE=/path/to/Image
export AARCH64_LINUX_CC=aarch64-linux-gnu-gcc
export BUSYBOX=/path/to/busybox-aarch64

simulator/guest-linux/aarch64/scripts/build_initramfs.sh
simulator/guest-linux/aarch64/scripts/run_linux_probe.sh
```

## Build busybox on Linux VM (if needed)

If you need a full busybox rootfs and no local cross toolchain package has a
ready-made ARM64 busybox binary, you have two options:

### Option 1: Build locally (recommended if SSH is unavailable)

```sh
cd simulator/guest-linux/aarch64/third_party
curl -L https://busybox.net/downloads/busybox-1.36.1.tar.bz2 -o busybox-1.36.1.tar.bz2
tar -xf busybox-1.36.1.tar.bz2
cd busybox-1.36.1
make defconfig
sed -i 's/^# CONFIG_STATIC is not set/CONFIG_STATIC=y/' .config
sed -i '/^CONFIG_CROSS_COMPILER_PREFIX=/d' .config
printf 'CONFIG_CROSS_COMPILER_PREFIX="aarch64-unknown-linux-gnu-"\n' >> .config
sed -i '/^CONFIG_EXTRA_CFLAGS=/d' .config
printf 'CONFIG_EXTRA_CFLAGS="-static"\n' >> .config
make -j8
cp busybox ../busybox-aarch64
chmod +x ../busybox-aarch64
```

### Option 2: Copy from Linux VM

If you can access the VM, copy an existing ARM64 static busybox:

```sh
scp ll@192.168.64.3:/usr/bin/busybox_aarch64 ./busybox-aarch64
chmod +x ./busybox-aarch64
export BUSYBOX=$PWD/busybox-aarch64
```

Then rebuild the initramfs (`build_initramfs.sh`).

Inside guest, you can run:

```sh
/bin/run_demo chat
/bin/run_demo rpc
/bin/run_demo rdma
/bin/run_demo all
/bin/run_demo shell
```

`run_demo` can also be used as `rdinit` (`rdinit=/bin/run_demo`) so the guest
enters this entrypoint immediately after boot and keeps dropping to an interactive
shell after startup tasks.

## Minimal ubcore/urma E2E (Dual Node)

For the ubcore/urma minimal send/recv loop, use this order:

1. Build kernel artifacts in VM (`ll@192.168.64.3`) and sync to local workspace
2. Rebuild initramfs with synced modules
3. Run dual-node e2e validation

Example:

```sh
# 1) Build in VM and pull Image + hisi_ubus.ko + udma.ko (+linqu_ub_drv.ko if available)
simulator/guest-linux/aarch64/scripts/sync_ub_kernel_artifacts_from_vm.sh

# 2) Build guest initramfs
export AARCH64_LINUX_CC=aarch64-linux-gnu-gcc
export BUSYBOX=/path/to/busybox-aarch64
export HISI_UBUS_GUEST_MODULE=/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/out/modules/hisi_ubus.ko
export UB_UDMA_GUEST_MODULE=/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/out/modules/udma.ko
export LINQU_UB_GUEST_MODULE=/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/out/modules/linqu_ub_drv.ko
simulator/guest-linux/aarch64/scripts/build_initramfs.sh

# 3) Run minimal ubcore/urma end-to-end send/recv
export RDINIT=/bin/run_demo
simulator/guest-linux/aarch64/scripts/run_ub_dual_node_ubcore_urma_e2e.sh
```

`RDINIT` is also optional override; by default the dual-node scripts use
`/bin/run_demo` when the image contains that script.

`scripts/run_ub_dual_node_ubcore_urma_e2e.sh` validates:
- `Register ubcore client success.`
- `[ipourma] Register netlink success.`
- guest workload `/bin/linqu_urma_dp` does bidirectional socket send/recv over `ipourma`
- both guests emit `[urma_dp] rx peer src=...` and `[init] urma dataplane pass`
