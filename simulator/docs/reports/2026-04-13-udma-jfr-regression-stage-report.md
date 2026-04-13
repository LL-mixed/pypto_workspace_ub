# 2026-04-13 UDMA JFR Regression Stage Report

## Scope

This stage focused on the URMA regression introduced while advancing cross-node memory access support, specifically the guest-side `UDMA/JFR` receive-completion path that previously triggered:

- `ida_free called for id=0 which is not allocated`
- `WARNING: CPU ... ida_free+...`
- `Call trace: udma_update_jfr_idx -> udma_poll_jfc -> ubcore_poll_jfc -> ipourma_rx_cr_event`

The goal was:

1. remove the guest kernel warning / stacktrace;
2. verify that dual-node `chat + rpc + rdma + obmm` still works;
3. confirm the fix is not a one-off pass;
4. close the residual stale-CQE symptom, not just guard it.

## Code Changes

Kernel submodule commit:

- `kernel_ub`: `7ea063ff48e1` `udma: harden jfr recv completion tracking for simulation`

Main repo pointer bump:

- `simulator`: `8aeeed9` `simulator: bump kernel_ub for udma jfr completion fix`

Follow-up QEMU source change:

- `qemu_8.2.0_ub`: uncommitted at the time of this report update
- file: [ub_ubc.c](/Volumes/repos/pypto_workspace/simulator/vendor/qemu_8.2.0_ub/hw/ub/ub_ubc.c)

Modified kernel files:

- [udma_jfr.h](/Volumes/repos/pypto_workspace/simulator/guest-linux/kernel_ub/drivers/ub/urma/hw/udma/udma_jfr.h)
- [udma_jfr.c](/Volumes/repos/pypto_workspace/simulator/guest-linux/kernel_ub/drivers/ub/urma/hw/udma/udma_jfr.c)
- [udma_jfc.c](/Volumes/repos/pypto_workspace/simulator/guest-linux/kernel_ub/drivers/ub/urma/hw/udma/udma_jfc.c)

Modified QEMU file:

- [ub_ubc.c](/Volumes/repos/pypto_workspace/simulator/vendor/qemu_8.2.0_ub/hw/ub/ub_ubc.c)

## Root Cause

There were two layers of bugs.

### 1. Guest JFR completion accounting was unsafe

`udma_update_jfr_idx()` unconditionally freed `entry_idx` from the JFR IDA table.

That meant a duplicate or stale receive CQE could free the same JFR slot twice, causing the earlier `ida_free(id=0)` warning path.

The first fix iteration also only initialized recv-tracking state on the `alloc_jfr -> active_jfr` path. Kernel-side users such as `ping` and `ipourma` go through `ubcore_create_jfr -> udma_create_jfr`, so `posted_idx` remained `NULL` there. That caused a temporary follow-up `Oops` in `udma_post_jfr_wr()` when `__set_bit()` dereferenced `jfr->posted_idx`.

### 2. QEMU could emit an early bogus recv completion

After the guest-side guard fix, one residual signal remained:

- `UDMA: drop stale recv cqe entry_idx=0 ...`

Tracing the corresponding QEMU logs showed the deeper origin was not guest cleanup anymore. It was a buffered URMA packet being flushed before the destination jetty/RQ entry was actually ready.

The old QEMU flow could:

1. buffer an early packet when `dst_jetty` was not active yet;
2. flush it later;
3. observe `RQE null va or zero len at ci=0`;
4. still generate an error CQE for `wqe_idx=0`;
5. later generate the real completion for the same recv slot.

That early error CQE was the stale/duplicate completion source seen by the guest.

## Implemented Fix

### 1. Track posted recv entries explicitly in guest kernel

`struct udma_jfr` now has a `posted_idx` bitmap that records which receive WQE indices are currently in flight.

### 2. Initialize tracking on both JFR creation models

Tracking is now allocated in both:

- `udma_create_jfr()`
- `udma_active_jfr()`

This covers both:

- one-phase kernel-created JFR objects;
- two-phase user-created alloc/active JFR objects.

### 3. Guard completion free path

`udma_update_jfr_idx()` now:

- rejects out-of-range `entry_idx`;
- uses `test_and_clear_bit(entry_idx, jfr->posted_idx)`;
- drops stale/duplicate recv CQEs instead of blindly calling `udma_id_free()`.

### 4. Reset and teardown tracking correctly

Tracking state is also reset/freed in:

- `udma_reset_sw_k_jfr_queue()`
- `udma_put_jfr_buf()`

so cleanup paths do not leave stale software state behind.

### 5. QEMU now keeps buffered URMA packets until the receive side is actually ready

`ubc_flush_urma_rx_buffer()` no longer blindly delivers and removes buffered packets.

Instead it now retries delivery through `ubc_try_handle_urma_rx_data(..., allow_buffer=false)` and only removes the packet from the buffer when delivery really succeeds.

If the destination jetty, JFR, or RQE is still not ready, the packet stays buffered and no bogus CQE is generated.

That is the key change that removes the stale-CQE source rather than merely masking its effect inside the guest.

## Validation Runs

### Single-run functional validation

Run id:

- `2026-04-13_14-21-53_6640`

Log directory:

- [2026-04-13_14-21-53_6640_demo_iter1](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-13_14-21-53_6640_demo_iter1)

Outcome:

- `chat`: pass
- `rpc`: pass
- `rdma`: pass
- `obmm`: pass
- no `WARNING: CPU`
- no `Call trace:`
- no `Oops:`
- no `ida_free called`

Summary:

- [demo_report.latest.txt](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/out/demo_report.latest.txt)

### First multi-run regression after guest-kernel fix

Run id:

- `2026-04-13_14-30-06_16462`

Summary:

- `passed=3`
- `failed=0`
- `pass_rate_percent=100`

Important observation:

- guest no longer crashed or warned;
- but iter1 still logged one guarded stale-CQE drop in guest log.

This confirmed the guest guard fix was working, but it also showed a deeper source still existed outside the guest.

### Final multi-run regression after QEMU buffered-RX fix

Run id:

- `2026-04-13_14-37-59_25932`

Log directories:

- [iter1](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-13_14-37-59_25932_demo_iter1)
- [iter2](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-13_14-37-59_25932_demo_iter2)
- [iter3](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-13_14-37-59_25932_demo_iter3)

Summary:

- `passed=3`
- `failed=0`
- `pass_rate_percent=100`

Reference:

- [demo_report.latest.txt](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/out/demo_report.latest.txt)

Per-iteration pass markers:

- Iter1:
  - [nodeA_guest.log:2003](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-13_14-37-59_25932_demo_iter1/nodeA_guest.log:2003)
  - [nodeA_guest.log:2021](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-13_14-37-59_25932_demo_iter1/nodeA_guest.log:2021)
  - [nodeA_guest.log:2066](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-13_14-37-59_25932_demo_iter1/nodeA_guest.log:2066)
  - [nodeA_guest.log:2078](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-13_14-37-59_25932_demo_iter1/nodeA_guest.log:2078)
  - [nodeB_guest.log:1999](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-13_14-37-59_25932_demo_iter1/nodeB_guest.log:1999)
  - [nodeB_guest.log:2017](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-13_14-37-59_25932_demo_iter1/nodeB_guest.log:2017)
  - [nodeB_guest.log:2062](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-13_14-37-59_25932_demo_iter1/nodeB_guest.log:2062)
  - [nodeB_guest.log:2081](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-13_14-37-59_25932_demo_iter1/nodeB_guest.log:2081)
- Iter2:
  - [nodeA_guest.log:2003](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-13_14-37-59_25932_demo_iter2/nodeA_guest.log:2003)
  - [nodeA_guest.log:2021](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-13_14-37-59_25932_demo_iter2/nodeA_guest.log:2021)
  - [nodeA_guest.log:2066](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-13_14-37-59_25932_demo_iter2/nodeA_guest.log:2066)
  - [nodeA_guest.log:2078](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-13_14-37-59_25932_demo_iter2/nodeA_guest.log:2078)
  - [nodeB_guest.log:1999](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-13_14-37-59_25932_demo_iter2/nodeB_guest.log:1999)
  - [nodeB_guest.log:2019](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-13_14-37-59_25932_demo_iter2/nodeB_guest.log:2019)
  - [nodeB_guest.log:2064](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-13_14-37-59_25932_demo_iter2/nodeB_guest.log:2064)
  - [nodeB_guest.log:2083](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-13_14-37-59_25932_demo_iter2/nodeB_guest.log:2083)
- Iter3:
  - [nodeA_guest.log:2003](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-13_14-37-59_25932_demo_iter3/nodeA_guest.log:2003)
  - [nodeA_guest.log:2021](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-13_14-37-59_25932_demo_iter3/nodeA_guest.log:2021)
  - [nodeA_guest.log:2066](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-13_14-37-59_25932_demo_iter3/nodeA_guest.log:2066)
  - [nodeA_guest.log:2078](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-13_14-37-59_25932_demo_iter3/nodeA_guest.log:2078)
  - [nodeB_guest.log:1999](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-13_14-37-59_25932_demo_iter3/nodeB_guest.log:1999)
  - [nodeB_guest.log:2018](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-13_14-37-59_25932_demo_iter3/nodeB_guest.log:2018)
  - [nodeB_guest.log:2063](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-13_14-37-59_25932_demo_iter3/nodeB_guest.log:2063)
  - [nodeB_guest.log:2082](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-13_14-37-59_25932_demo_iter3/nodeB_guest.log:2082)

Negative checks on this final run:

- no `drop stale recv cqe`
- no `WARNING: CPU`
- no `Call trace:`
- no `Oops:`
- no `ida_free called`

## Current Assessment

### What is now solid

- The previous guest-kernel-fatal regression is fixed.
- The stale-CQE symptom no longer reproduces in guest logs across the latest `3 / 3` regression run.
- Dual-node `chat + rpc + rdma + obmm` passed `3 / 3` real runs using the rebuilt guest artifacts and rebuilt QEMU.
- `rdma` remains payload-level validated, not just lifecycle-smoke validated.
- `obmm` remains integrated and does not break the already-working URMA path in this validation set.

### Residual cleanup item

One non-fatal QEMU-side signal remains in the latest run:

- [nodeB_qemu.log:3031](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-13_14-37-59_25932_demo_iter3/nodeB_qemu.log:3031)

That line is:

- `ubc URMA RX: RQE null va or zero len at ci=0`

Current interpretation:

- this reflects an early receive-side readiness window still being observed inside QEMU;
- after the buffered-RX fix, it no longer generates a bogus CQE and no longer surfaces as a guest warning, stale-CQE drop, or workload failure;
- it is therefore a cleanup item, not a current functional blocker.

## Recommended Next Step

1. keep both fixes:
   - guest JFR posted-recv accounting hardening;
   - QEMU buffered-URMA flush readiness gating;
2. submit the QEMU fix as its own component commit and bump the submodule pointer in `simulator`;
3. optionally tighten QEMU logging so the remaining `RQE null va or zero len` line is either suppressed during buffered retry or reworded as a deferred-not-ready condition;
4. run a longer acceptance matrix after that instead of stopping at short 3-iteration demo regression.
