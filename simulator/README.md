# Simulator Workspace

This directory is a standalone Rust workspace for the UB/Linqu simulator work.

Current status:

- workspace skeleton only
- crate boundaries follow `draft/simulator_rust_interface_sketch_v0.md`
- implementation is intentionally minimal until core interfaces are stabilized

Crates:

- `sim-core`
- `sim-config`
- `sim-topology`
- `sim-runtime`
- `sim-services`
- `sim-qemu`
- `sim-uapi`
- `sim-workloads`
- `sim-cli`

Additional non-Rust device work:

- Archived legacy stack:
  - `archive/qemu-device-legacy/qemu-device`
    Drop-in QEMU device-side source skeleton for the historical `linqu-ub` MMIO/ring path.
  - `archive/qemu-device-legacy/qemu`
    Historical QEMU tree used by that legacy path.

Active QEMU path for dual-node UB simulation:

- `vendor/qemu_8.2.0_ub`
