#!/bin/zsh
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
DRIVER_DIR="$ROOT_DIR/driver"
OUT_DIR="$ROOT_DIR/out/driver"

: "${KERNEL_BUILD_DIR:=}"
: "${CROSS_COMPILE:=aarch64-unknown-linux-gnu-}"
: "${ARCH:=arm64}"

if [[ -z "$KERNEL_BUILD_DIR" ]]; then
  echo "KERNEL_BUILD_DIR is required" >&2
  echo "example: export KERNEL_BUILD_DIR=/path/to/linux-build" >&2
  exit 1
fi

mkdir -p "$OUT_DIR"

make -C "$KERNEL_BUILD_DIR" \
  M="$DRIVER_DIR" \
  O="$KERNEL_BUILD_DIR" \
  ARCH="$ARCH" \
  CROSS_COMPILE="$CROSS_COMPILE" \
  modules

cp "$DRIVER_DIR"/linqu_ub_drv.ko "$OUT_DIR"/
echo "$OUT_DIR/linqu_ub_drv.ko"
