# aarch64 Debug Helpers

This archive keeps ad-hoc guest init test programs that were used during early
UB bus / driver bind bring-up on the aarch64 simulator image.

Status:

- `init_test.c`
- `init_test2.c`

These files are not part of the current demo or probe harness.
They are preserved for historical debugging reference only.

Active path kept in the main tree:

- `simulator/guest-linux/aarch64/init_manual_bind.c`

That file is still built by the initramfs tooling and used by the manual bind
wrapper.
