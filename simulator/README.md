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

- `qemu-device`
  Drop-in QEMU device-side source skeleton for the `linqu-ub` MMIO/ring device.
