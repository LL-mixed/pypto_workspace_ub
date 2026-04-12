# Dual-Node RDMA Demo Fix Validation

Date: 2026-04-12

## Scope

This report covers the RDMA demo fix in guest userspace only:
- [ub_rdma_demo.c](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/ub_rdma_demo.c)
- [uburma_cmd_user_compat.h](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/uburma_cmd_user_compat.h)

It does not include separate uncommitted guest-kernel changes under `simulator/guest-linux/kernel_ub/`.

## Root Cause

Two userspace bugs were present in the RDMA demo path.

1. `token_id` source was invalid.

The demo fabricated a constant user token id instead of allocating a legal TID from UMMU:
- previous code path used `uint32_t user_token_id = (1u << UDMA_TID_SHIFT_USER);`
- UDMA then consumed this user payload directly in `udma_get_key_id_from_user()`
- this bypassed the intended `/dev/ummu/tid` + `UMMU_IOCALLOC_TID` path

2. `UNREGISTER_SEG` ioctl TLV packaging was missing.

The demo called `UBURMA_CMD_UNREGISTER_SEG` during cleanup, but its local TLV builder had no spec encoder / switch case for that command.
That made the unregister request malformed from userspace, which then caused cleanup to fail and left the token refcount uncleared.

## Fix

### 1. Allocate TID from UMMU core

`ub_rdma_demo` now:
- opens `/dev/ummu/tid`
- calls `UMMU_IOCALLOC_TID`
- uses returned `tid << UDMA_TID_SHIFT_USER` as the `user_token_id` payload for `UBURMA_CMD_ALLOC_TOKEN_ID`
- calls `UMMU_IOCFREE_TID` in cleanup

### 2. Add explicit cleanup

`ub_rdma_demo` cleanup now explicitly performs:
- `UBURMA_CMD_UNREGISTER_SEG`
- `UBURMA_CMD_FREE_TOKEN_ID`
- `UMMU_IOCFREE_TID`

### 3. Fix missing `UNREGISTER_SEG` userspace TLV support

Added userspace compat / TLV support for:
- `struct uburma_cmd_unregister_seg`
- `UNREGISTER_SEG` input spec enum
- TLV fill helper
- `build_tlv_specs()` dispatch case

## Validation Runs

### Failing run before final TLV fix

Run:
- `2026-04-12_16-46-04_29348_demo_iter1`

Evidence:
- [nodeA_guest.log](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-12_16-46-04_29348_demo_iter1/nodeA_guest.log)
- [nodeB_guest.log](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-12_16-46-04_29348_demo_iter1/nodeB_guest.log)

Observed markers:
- `alloc_ummu_tid -> ok`
- `cleanup: unregister_seg failed: -12`
- `cleanup: free_token_id failed: -16`
- `UDMA: invalidate cfg_table failed, ret=-29`

Interpretation:
- the fake token-id path had already been removed
- remaining failure was caused by malformed `UNREGISTER_SEG` userspace TLV packaging

### Passing run after final TLV fix

Run:
- `2026-04-12_16-48-41_1950_demo_iter1`

Evidence:
- [demo_report.latest.txt](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/out/demo_report.latest.txt)
- [nodeA_guest.log](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-12_16-48-41_1950_demo_iter1/nodeA_guest.log)
- [nodeB_guest.log](/Volumes/repos/pypto_workspace/simulator/guest-linux/aarch64/logs/2026-04-12_16-48-41_1950_demo_iter1/nodeB_guest.log)

Summary from harness:
- `passed=1`
- `failed=0`
- `pass_rate_percent=100`

Observed behavior:
- dual-node environment booted successfully
- entity readiness passed on both nodes
- chat + rpc + rdma demo sequence completed
- harness summary reported `dual-node demo validation passed`
- the previous cleanup failure markers were absent

## Conclusion

This userspace RDMA demo issue was not one single kernel/QEMU problem.
It was a pair of demo-side implementation bugs:
- illegal token-id sourcing
- missing `UNREGISTER_SEG` TLV adaptation

After fixing both, the real dual-node demo harness passed.
