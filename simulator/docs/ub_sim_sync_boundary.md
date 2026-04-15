# `ub_sim.git` Sync Boundary

This note defines the boundary between:

- content synchronized from the main `simulator` repo into `ub_sim.git`
- validation/build responsibilities owned by `ub_sim.git` itself

## What Must Be Synchronized From Main

When updating `ub_sim.git` from the main repo, sync all linked surfaces together:

- `vendor/qemu_8.2.0_ub` submodule updates
- `guest-linux/kernel_ub` submodule updates
- guest demos and helpers under `guest-linux/aarch64`
- shell harnesses and launchers under `guest-linux/aarch64/scripts`
- compat/UAPI headers used by guest demos
- topology files under `vendor/`
- validation/reporting docs

Sync is incomplete if only the obvious demo files move but linked headers, scripts, or submodules do not.

## What `ub_sim.git` Validation Must Own

`ub_sim.git` validation/tooling must be self-sufficient after sync. It must not rely on a human remembering to rebuild artifacts.

Specifically, `ub_sim.git` validation owns:

- detecting stale QEMU build outputs after `vendor/qemu_8.2.0_ub` moves
- detecting stale guest kernel/images/modules after `guest-linux/kernel_ub` moves
- detecting stale initramfs outputs after guest demo/script/header changes
- rebuilding only the stale artifacts
- refusing validation only when required rebuild inputs are unavailable

## Freshness Rules

The current tooling uses stamps/signatures instead of unconditional rebuilds:

- QEMU build freshness:
  - submodule `HEAD`
  - configure target list
  - configure args
- guest kernel/image freshness:
  - `guest-linux/kernel_ub` submodule `HEAD`
- initramfs freshness:
  - kernel stamp
  - busybox binary
  - guest demo sources
  - guest headers
  - initramfs scripts
  - packaged module binaries

This is the intended model:

- sync moves code and submodule references
- validation detects stale outputs and rebuilds only what changed

## Explicit Non-Goal

The person syncing from main to `ub_sim.git` is **not** responsible for manually rebuilding QEMU or guest artifacts after every update.

That responsibility belongs to `ub_sim.git` validation/tooling.
