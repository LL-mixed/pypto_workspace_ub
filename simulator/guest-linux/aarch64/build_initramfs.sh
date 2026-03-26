#!/bin/zsh
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
OUT_DIR="$ROOT_DIR/out"
INITRAMFS_DIR="$OUT_DIR/initramfs"
PROBE_SRC="$ROOT_DIR/probe.c"
PROBE_BIN="$OUT_DIR/linqu_probe"
INIT_SRC="$ROOT_DIR/init.c"
INIT_BIN="$OUT_DIR/init"
INSMOD_SRC="$ROOT_DIR/insmod.c"
INSMOD_BIN="$OUT_DIR/insmod"
INITRAMFS_IMG="$OUT_DIR/initramfs.cpio.gz"
LINQU_MODULE="${LINQU_UB_GUEST_MODULE:-}"
HISI_UBUS_MODULE="${HISI_UBUS_GUEST_MODULE:-}"

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
"$AARCH64_LINUX_CC" -static -O2 -Wall -Wextra "$INIT_SRC" -o "$INIT_BIN"
"$AARCH64_LINUX_CC" -static -O2 -Wall -Wextra "$INSMOD_SRC" -o "$INSMOD_BIN"

cp "$INIT_BIN" "$INITRAMFS_DIR/init"
chmod +x "$INITRAMFS_DIR/init"
cp "$PROBE_BIN" "$INITRAMFS_DIR/bin/linqu_probe"
cp "$INSMOD_BIN" "$INITRAMFS_DIR/bin/insmod"

if [[ -n "$BUSYBOX" ]]; then
  cp "$BUSYBOX" "$INITRAMFS_DIR/bin/busybox"
  chmod +x "$INITRAMFS_DIR/bin/busybox"
fi

if [[ -n "$LINQU_MODULE" ]]; then
  cp "$LINQU_MODULE" "$INITRAMFS_DIR/lib/modules/linqu_ub_drv.ko"
fi

if [[ -n "$HISI_UBUS_MODULE" ]]; then
  cp "$HISI_UBUS_MODULE" "$INITRAMFS_DIR/lib/modules/hisi_ubus.ko"
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
