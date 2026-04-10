#!/bin/zsh
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
OUT_DIR="$ROOT_DIR/out"
INITRAMFS_DIR="$OUT_DIR/initramfs"
PROBE_SRC="$ROOT_DIR/probe.c"
PROBE_BIN="$OUT_DIR/linqu_probe"
URMA_DP_SRC="$ROOT_DIR/urma_dp.c"
URMA_DP_BIN="$OUT_DIR/linqu_urma_dp"
INIT_SRC="$ROOT_DIR/init.c"
INIT_BIN="$OUT_DIR/init"
INSMOD_SRC="$ROOT_DIR/insmod.c"
INSMOD_BIN="$OUT_DIR/insmod"
INIT_MANUAL_BIND_SRC="$ROOT_DIR/init_manual_bind.c"
INIT_MANUAL_BIND_BIN="$OUT_DIR/init_manual_bind"
INIT_BIN_TO_USE="${INIT_TO_USE:-$INIT_BIN}"
INITRAMFS_IMG="$OUT_DIR/initramfs.cpio.gz"
LINQU_MODULE="${LINQU_UB_GUEST_MODULE:-}"
HISI_UBUS_MODULE="${HISI_UBUS_GUEST_MODULE:-}"
UDMA_MODULE="${UB_UDMA_GUEST_MODULE:-}"
IPOURMA_MODULE="${UB_IPOURMA_GUEST_MODULE:-}"
COPY_ALL_KO="${COPY_ALL_KO:-0}"

: "${AARCH64_LINUX_CC:=}"
: "${BUSYBOX:=}"

mkdir -p "$OUT_DIR"
rm -rf "$INITRAMFS_DIR"
mkdir -p "$INITRAMFS_DIR/bin" "$INITRAMFS_DIR/dev" "$INITRAMFS_DIR/proc" "$INITRAMFS_DIR/sys" "$INITRAMFS_DIR/tmp" "$INITRAMFS_DIR/lib/modules"

if [[ -z "$AARCH64_LINUX_CC" ]]; then
  echo "AARCH64_LINUX_CC is required" >&2
  echo "example: export AARCH64_LINUX_CC=aarch64-linux-gnu-gcc" >&2
  exit 1
fi

"$AARCH64_LINUX_CC" -static -O2 -Wall -Wextra "$PROBE_SRC" -o "$PROBE_BIN"
"$AARCH64_LINUX_CC" -static -O2 -Wall -Wextra "$URMA_DP_SRC" -o "$URMA_DP_BIN"
"$AARCH64_LINUX_CC" -static -O2 -Wall -Wextra "$INIT_SRC" -o "$INIT_BIN"
"$AARCH64_LINUX_CC" -static -O2 -Wall -Wextra "$INSMOD_SRC" -o "$INSMOD_BIN"
"$AARCH64_LINUX_CC" -static -O2 -Wall -Wextra "$INIT_MANUAL_BIND_SRC" -o "$INIT_MANUAL_BIND_BIN"

cp "$INIT_BIN_TO_USE" "$INITRAMFS_DIR/init"
chmod +x "$INITRAMFS_DIR/init"
cp "$PROBE_BIN" "$INITRAMFS_DIR/bin/linqu_probe"
cp "$URMA_DP_BIN" "$INITRAMFS_DIR/bin/linqu_urma_dp"
cp "$INSMOD_BIN" "$INITRAMFS_DIR/bin/insmod"

if [[ -n "$BUSYBOX" ]]; then
  cp "$BUSYBOX" "$INITRAMFS_DIR/bin/busybox"
  chmod +x "$INITRAMFS_DIR/bin/busybox"
fi

# Optionally copy all .ko files from out directory.
# Keep default off to avoid mixing stale modules with explicitly provided artifacts.
if [[ "$COPY_ALL_KO" == "1" ]] && [[ -d "$OUT_DIR" ]]; then
  for ko_file in "$OUT_DIR"/*.ko; do
    if [[ -f "$ko_file" ]]; then
      cp "$ko_file" "$INITRAMFS_DIR/lib/modules/"
    fi
  done
fi

if [[ -n "$LINQU_MODULE" ]]; then
  cp "$LINQU_MODULE" "$INITRAMFS_DIR/lib/modules/linqu_ub_drv.ko"
fi

if [[ -n "$HISI_UBUS_MODULE" ]]; then
  cp "$HISI_UBUS_MODULE" "$INITRAMFS_DIR/lib/modules/hisi_ubus.ko"
fi

if [[ -n "$UDMA_MODULE" ]]; then
  cp "$UDMA_MODULE" "$INITRAMFS_DIR/lib/modules/udma.ko"
fi

if [[ -n "$IPOURMA_MODULE" ]]; then
  cp "$IPOURMA_MODULE" "$INITRAMFS_DIR/lib/modules/ipourma.ko"
fi

# Copy UMMU modules if they exist
UMMU_CORE_MODULE="${UB_UMMU_CORE_GUEST_MODULE:-}"
UMMU_MODULE="${UB_UMMU_GUEST_MODULE:-}"
if [[ -n "$UMMU_CORE_MODULE" && -f "$UMMU_CORE_MODULE" ]]; then
  cp "$UMMU_CORE_MODULE" "$INITRAMFS_DIR/lib/modules/ummu-core.ko"
fi
if [[ -n "$UMMU_MODULE" && -f "$UMMU_MODULE" ]]; then
  cp "$UMMU_MODULE" "$INITRAMFS_DIR/lib/modules/ummu.ko"
fi

(
  cd "$INITRAMFS_DIR"
  printf 'console\0' | cpio -o --null -H newc --quiet > /dev/null 2>&1 || true
)

(
  cd "$INITRAMFS_DIR"
  find . -print | cpio -o -H newc --quiet | gzip -9 > "$INITRAMFS_IMG"
)

echo "$INITRAMFS_IMG"
echo "built $INITRAMFS_IMG"
