# QEMU Device Legacy Archive

This folder archives an older `linqu-ub` QEMU-device development stack to keep the active simulator tree clean.

Archived content:
- `qemu/`: historical QEMU fork/worktree used with `linqu-ub` (`CONFIG_LINQU_UB` path)
- `qemu-device/`: standalone `linqu-ub` device model + bridge code intended to be developed alongside that QEMU tree

Reason for archive:
- The active dual-node UB simulation flow uses `simulator/vendor/qemu_8.2.0_ub` (UB native path under `hw/ub/*`), not this legacy `linqu-ub` stack.
- Moving these directories out of active top-level paths reduces accidental edits and context noise.

Compatibility note:
- Legacy probe scripts are explicitly guarded and require `--legacy`:
  - `simulator/guest-linux/aarch64/scripts/run_linux_probe.sh`
  - `simulator/archive/guest-probe-legacy/aarch64/run_probe.sh`
- Both scripts require explicit `QEMU_DIR=/your/qemu/path`.
