# Linqu UB QEMU Device

This directory contains the first real QEMU-side device model work for the
UB/Linqu simulator.

It is intentionally kept outside the Rust workspace because it is meant to be
copied into, or developed alongside, a real QEMU source tree.

Current contents:

- `hw/misc/linqu_ub.c`
  Minimal SysBus/MMIO device model.
- `include/hw/misc/linqu_ub.h`
  Device state and public QEMU type definitions.
- `include/hw/misc/linqu_ub_regs.h`
  Guest-visible MMIO register layout.
- `include/hw/misc/linqu_ub_backend.h`
  Host-side backend ABI that a Rust bridge can implement.
- `backend/linqu_ub_rust_bridge.c`
  Thin C glue from QEMU backend ops to Rust `sim-qemu` FFI.
- `meson.build`
  Drop-in build snippet for a QEMU tree.

Scope of this first cut:

- one MMIO endpoint block per logical endpoint
- guest-provided cmdq/cq guest memory base addresses
- guest tail/head register updates
- doorbell-triggered submission
- CQ writeback into guest memory
- IRQ status and IRQ acknowledge flow

What it is not yet:

- integrated into an external QEMU checkout
- wired to a concrete Rust FFI bridge implementation
- MSI-X or multi-vector interrupt handling
- migration/vmstate-complete

Bridge side:

- Rust exports are declared in
  [sim-qemu/include/linqu_ub_bridge.h](/Volumes/repos/pypto_workspace/simulator/crates/sim-qemu/include/linqu_ub_bridge.h)
- QEMU-side glue is in `backend/linqu_ub_rust_bridge.c`
