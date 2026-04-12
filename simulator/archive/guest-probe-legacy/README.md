# Guest Probe Legacy Archive

This folder archives the old `linqu-ub` guest probe toolchain.

Contents:
- `aarch64/linqu_ub_probe.S`: bare-metal probe payload
- `aarch64/build_probe.sh`: builds probe binary
- `aarch64/run_probe.sh`: legacy runner (requires `--legacy` and explicit `QEMU_DIR`)

Notes:
- This is not part of the active dual-node URMA data-plane flow.
- The active flow uses guest Linux scripts under `simulator/guest-linux/aarch64` with `simulator/vendor/qemu_8.2.0_ub`.
