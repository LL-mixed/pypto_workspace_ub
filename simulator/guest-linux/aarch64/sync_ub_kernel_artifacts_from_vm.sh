#!/bin/zsh
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
OUT_DIR="${OUT_DIR:-$ROOT_DIR/out}"
MODULES_DIR="${MODULES_DIR:-$OUT_DIR/modules}"

VM_HOST="${VM_HOST:-ll@192.168.64.3}"
VM_KERNEL_SRC="${VM_KERNEL_SRC:-/home/ll/share/shared_data/kernel_ub}"
VM_KERNEL_BUILD="${VM_KERNEL_BUILD:-/home/ll/share/shared_data/kernel_build}"
VM_LINQU_DRIVER_DIR="${VM_LINQU_DRIVER_DIR:-/home/ll/share/shared_data/linqu_guest_driver}"

VM_IMAGE_PATH="${VM_IMAGE_PATH:-$VM_KERNEL_BUILD/arch/arm64/boot/Image}"
VM_HISI_MODULE_PATH="${VM_HISI_MODULE_PATH:-$VM_KERNEL_BUILD/drivers/ub/ubus/vendor/hisilicon/hisi_ubus.ko}"
VM_UDMA_MODULE_PATH="${VM_UDMA_MODULE_PATH:-$VM_KERNEL_BUILD/drivers/ub/urma/hw/udma/udma.ko}"
VM_LINQU_MODULE_PATH="${VM_LINQU_MODULE_PATH:-$VM_LINQU_DRIVER_DIR/linqu_ub_drv.ko}"

BUILD_IN_VM="${BUILD_IN_VM:-1}"
BUILD_LINQU_DRIVER_IN_VM="${BUILD_LINQU_DRIVER_IN_VM:-1}"
JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 8)}"
VM_CROSS_COMPILE="${VM_CROSS_COMPILE:-aarch64-linux-gnu-}"
VM_ARCH="${VM_ARCH:-arm64}"

if [[ "$BUILD_IN_VM" == "1" ]]; then
  ssh "$VM_HOST" "
    set -euo pipefail
    cd '$VM_KERNEL_SRC'
    make O='$VM_KERNEL_BUILD' ARCH='$VM_ARCH' CROSS_COMPILE='$VM_CROSS_COMPILE' -j'$JOBS' Image drivers/ub/ubus/vendor/hisilicon/hisi_ubus.ko
    make O='$VM_KERNEL_BUILD' ARCH='$VM_ARCH' CROSS_COMPILE='$VM_CROSS_COMPILE' -j'$JOBS' M=drivers/ub/urma/hw/udma modules
  "

  if [[ "$BUILD_LINQU_DRIVER_IN_VM" == "1" ]]; then
    ssh "$VM_HOST" "
      set -euo pipefail
      if [[ -d '$VM_LINQU_DRIVER_DIR' ]] && [[ -f '$VM_LINQU_DRIVER_DIR/Makefile' ]]; then
        make -C '$VM_KERNEL_BUILD' M='$VM_LINQU_DRIVER_DIR' O='$VM_KERNEL_BUILD' ARCH='$VM_ARCH' CROSS_COMPILE='$VM_CROSS_COMPILE' modules
      else
        echo '[sync] skip linqu_ub_drv.ko build in VM: missing $VM_LINQU_DRIVER_DIR or Makefile' >&2
      fi
    "
  fi
fi

mkdir -p "$OUT_DIR" "$MODULES_DIR"

scp "$VM_HOST:$VM_IMAGE_PATH" "$OUT_DIR/Image"
scp "$VM_HOST:$VM_HISI_MODULE_PATH" "$MODULES_DIR/hisi_ubus.ko"

if scp "$VM_HOST:$VM_UDMA_MODULE_PATH" "$MODULES_DIR/udma.ko"; then
  :
else
  echo "[sync] warn: failed to copy udma.ko from $VM_HOST:$VM_UDMA_MODULE_PATH" >&2
fi

if scp "$VM_HOST:$VM_LINQU_MODULE_PATH" "$MODULES_DIR/linqu_ub_drv.ko"; then
  :
else
  echo "[sync] warn: failed to copy linqu_ub_drv.ko from $VM_HOST:$VM_LINQU_MODULE_PATH" >&2
fi

echo "[sync] done:"
echo "[sync]   $OUT_DIR/Image"
echo "[sync]   $MODULES_DIR/hisi_ubus.ko"
if [[ -f "$MODULES_DIR/udma.ko" ]]; then
  echo "[sync]   $MODULES_DIR/udma.ko"
fi
if [[ -f "$MODULES_DIR/linqu_ub_drv.ko" ]]; then
  echo "[sync]   $MODULES_DIR/linqu_ub_drv.ko"
fi
