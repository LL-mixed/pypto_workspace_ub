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
  - if provided, the initramfs gains a shell fallback and standard applets

## Files

- `probe.c`
  - userspace probe that reads `/proc/device-tree` and `/dev/mem`
- `initramfs/init`
  - early init script for the guest
- `build_initramfs.sh`
  - builds `linqu_probe` and packs the initramfs
- `run_linux_probe.sh`
  - launches `qemu-system-aarch64` with the kernel + initramfs
- `driver/linqu_ub_drv.c`
  - minimal guest-side platform driver
- `build_driver.sh`
  - builds the external kernel module when a matching kernel build tree exists

## Current State

This is a runnable scaffold, but not yet turnkey on this host because the
workspace does not currently contain:

- a bootable ARM64 Linux kernel image
- a Linux AArch64 userspace toolchain
- a static ARM64 busybox

Once those are supplied, the intended loop is:

```sh
export KERNEL_IMAGE=/path/to/Image
export AARCH64_LINUX_CC=aarch64-linux-gnu-gcc
export BUSYBOX=/path/to/busybox-aarch64

simulator/guest-linux/aarch64/build_initramfs.sh
simulator/guest-linux/aarch64/run_linux_probe.sh
```
