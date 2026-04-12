#!/bin/zsh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
OUT_DIR="${OUT_DIR:-$ROOT_DIR/out}"
MODULES_DIR="${MODULES_DIR:-$OUT_DIR/modules}"

VM_HOST="${VM_HOST:-ll@192.168.64.3}"
VM_KERNEL_SRC="${VM_KERNEL_SRC:-/home/ll/share/shared_data/kernel_ub}"
VM_KERNEL_BUILD="${VM_KERNEL_BUILD:-/home/ll/share/shared_data/kernel_build}"
VM_LINQU_DRIVER_DIR="${VM_LINQU_DRIVER_DIR:-/home/ll/share/shared_data/linqu_guest_driver}"

VM_IMAGE_PATH="${VM_IMAGE_PATH:-$VM_KERNEL_BUILD/arch/arm64/boot/Image}"
VM_HISI_MODULE_PATH="${VM_HISI_MODULE_PATH:-$VM_KERNEL_BUILD/drivers/ub/ubus/vendor/hisilicon/hisi_ubus.ko}"
VM_UBUS_MODULE_PATH="${VM_UBUS_MODULE_PATH:-$VM_KERNEL_BUILD/drivers/ub/ubus/ubus.ko}"
VM_UBASE_MODULE_PATH="${VM_UBASE_MODULE_PATH:-$VM_KERNEL_BUILD/drivers/ub/ubase/ubase.ko}"
VM_UBCORE_MODULE_PATH="${VM_UBCORE_MODULE_PATH:-$VM_KERNEL_BUILD/drivers/ub/urma/ubcore/ubcore.ko}"
VM_UDMA_MODULE_PATH="${VM_UDMA_MODULE_PATH:-$VM_KERNEL_BUILD/drivers/ub/urma/hw/udma/udma.ko}"
VM_IPOURMA_MODULE_PATH="${VM_IPOURMA_MODULE_PATH:-$VM_KERNEL_BUILD/drivers/ub/urma/ulp/ipourma/ipourma.ko}"
VM_UBURMA_MODULE_PATH="${VM_UBURMA_MODULE_PATH:-$VM_KERNEL_BUILD/drivers/ub/urma/uburma/uburma.ko}"
VM_UMMU_CORE_MODULE_PATH="${VM_UMMU_CORE_MODULE_PATH:-$VM_KERNEL_BUILD/drivers/ub/ubfi/ummu-core.ko}"
VM_UMMU_MODULE_PATH="${VM_UMMU_MODULE_PATH:-$VM_KERNEL_BUILD/drivers/ub/ubfi/ummu.ko}"
VM_LINQU_MODULE_PATH="${VM_LINQU_MODULE_PATH:-$VM_LINQU_DRIVER_DIR/linqu_ub_drv.ko}"

BUILD_IN_VM="${BUILD_IN_VM:-1}"
BUILD_LINQU_DRIVER_IN_VM="${BUILD_LINQU_DRIVER_IN_VM:-1}"
JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 8)}"
VM_CROSS_COMPILE="${VM_CROSS_COMPILE:-aarch64-linux-gnu-}"
VM_ARCH="${VM_ARCH:-arm64}"

copy_optional() {
  local remote_path="$1"
  local local_path="$2"
  if scp "$VM_HOST:$remote_path" "$local_path"; then
    :
  else
    echo "[sync] warn: failed to copy $remote_path" >&2
  fi
}

if [[ "$BUILD_IN_VM" == "1" ]]; then
  ssh "$VM_HOST" "
    set -euo pipefail
    cd '$VM_KERNEL_SRC'
    make O='$VM_KERNEL_BUILD' ARCH='$VM_ARCH' CROSS_COMPILE='$VM_CROSS_COMPILE' -j'$JOBS' \
      Image \
      drivers/ub/ubus/vendor/hisilicon/hisi_ubus.ko \
      drivers/ub/ubus/ubus.ko \
      drivers/ub/ubase/ubase.ko \
      drivers/ub/urma/ubcore/ubcore.ko \
      drivers/ub/urma/uburma/uburma.ko
    make O='$VM_KERNEL_BUILD' ARCH='$VM_ARCH' CROSS_COMPILE='$VM_CROSS_COMPILE' -j'$JOBS' M=drivers/ub/urma/hw/udma modules
    make O='$VM_KERNEL_BUILD' ARCH='$VM_ARCH' CROSS_COMPILE='$VM_CROSS_COMPILE' -j'$JOBS' M=drivers/ub/urma/ulp/ipourma modules
    make O='$VM_KERNEL_BUILD' ARCH='$VM_ARCH' CROSS_COMPILE='$VM_CROSS_COMPILE' -j'$JOBS' M=drivers/ub/ubfi modules || true
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

copy_optional "$VM_UBUS_MODULE_PATH" "$MODULES_DIR/ubus.ko"
copy_optional "$VM_UBASE_MODULE_PATH" "$MODULES_DIR/ubase.ko"
copy_optional "$VM_UBCORE_MODULE_PATH" "$MODULES_DIR/ubcore.ko"
copy_optional "$VM_UDMA_MODULE_PATH" "$MODULES_DIR/udma.ko"
copy_optional "$VM_IPOURMA_MODULE_PATH" "$MODULES_DIR/ipourma.ko"
copy_optional "$VM_UBURMA_MODULE_PATH" "$MODULES_DIR/uburma.ko"
copy_optional "$VM_UMMU_CORE_MODULE_PATH" "$MODULES_DIR/ummu-core.ko"
copy_optional "$VM_UMMU_MODULE_PATH" "$MODULES_DIR/ummu.ko"
copy_optional "$VM_LINQU_MODULE_PATH" "$MODULES_DIR/linqu_ub_drv.ko"

echo "[sync] done:"
echo "[sync]   $OUT_DIR/Image"
for mod in hisi_ubus.ko ubus.ko ubase.ko ubcore.ko udma.ko ipourma.ko uburma.ko ummu-core.ko ummu.ko linqu_ub_drv.ko; do
  if [[ -f "$MODULES_DIR/$mod" ]]; then
    echo "[sync]   $MODULES_DIR/$mod"
  fi
done
