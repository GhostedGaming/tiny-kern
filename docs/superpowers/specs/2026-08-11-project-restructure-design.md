# Project Restructure: bootloader / kernel / userspace

Date: 2026-08-11

## Goal

Reorganize the tiny-kern repository into a clearer top-level layout grouped by
component role: `bootloader/`, `kernel/`, `userspace/`. This is a pure
renaming/moving refactor — no behavior change to the OS. The repo must still
build and boot exactly as before, just with the new names.

## Current layout (what we have)

```
tiny-kern/
├── GNUmakefile              # orchestrates everything
├── limine.conf              # bootloader config
├── limine-binary/           # gitignored, downloaded by make
├── edk2-ovmf-bins/          # gitignored UEFI firmware
├── kernel/                  # the OS kernel
├── mlibc/                   # C library (git submodule, meson build)
├── test_programs/           # the userspace
│   ├── *.c                  # init.c, sh.c, edit.c, tcc_link.c, ...
│   ├── GNUmakefile
│   ├── linker.ld
│   ├── bins/                # build output
│   └── usr/                 # ramfs root tree: bin/, lib/, include/, src/
├── tools/                   # toolchain sources + build-* scripts + cross/
└── *.iso, disk.img          # gitignored build artifacts
```

The kernel mounts `test_programs.tar` (a Limine boot module) at `/ram` via
ustar, so userspace lives at `/ram/bins/init`, `/ram/usr/lib/libc.a`, etc.

## Target layout (after)

```
tiny-kern/
├── GNUmakefile              # orchestrates everything (paths updated)
├── bootloader/
│   ├── limine.conf          # moved from repo root
│   └── limine-binary/       # gitignored, downloaded by make (moved)
├── edk2-ovmf-bins/          # UEFI firmware — stays at root (not a bootloader)
├── kernel/                  # unchanged
├── mlibc/                   # unchanged (submodule)
├── userspace/               # renamed from test_programs/
│   ├── *.c
│   ├── GNUmakefile
│   ├── linker.ld
│   ├── bin/                 # build output (renamed from bins/)
│   └── usr/                 # ramfs root tree (unchanged: bin/, lib/, include/, src/)
├── tools/                   # unchanged (build-tcc path updated)
└── *.iso, disk.img          # build artifacts, stay at root
```

Notes on decisions:

- `edk2-ovmf-bins/` is UEFI *firmware*, not a bootloader, so it stays at the
  repo root next to the other downloaded build deps.
- Build artifacts (`template-x86_64.iso`, `disk.img`) stay at the repo root
  (gitignored) to avoid churn.
- `mlibc/` stays at top level — it is a shared build dependency of both the
  kernel headers work and the userspace libc.
- `tools/` stays at top level — toolchain sources and build scripts.

## Renames

| Old | New |
|---|---|
| `test_programs/` | `userspace/` |
| `test_programs/bins/` | `userspace/bin/` |
| `test_programs.tar` | `userspace.tar` |
| `/ram/bins` (mount layout) | `/ram/bin` |
| `limine.conf` (repo root) | `bootloader/limine.conf` |
| `limine-binary/` (repo root) | `bootloader/limine-binary/` |

`/ram/usr` is kept as-is (it mirrors a Unix root layout).

## Reference updates

All in-repo references to the old names must be updated in the same change:

1. Root `GNUmakefile`
   - `test_programs` phony target → `userspace`; recipe `$(MAKE) -C test_programs` → `userspace`
   - `test_programs.tar` rule: target, `$(wildcard test_programs/*.c test_programs/*.ld test_programs/usr/src/*.c)` glob, `tar -C test_programs` → `userspace`
   - ISO rule: copy `test_programs.tar` → `userspace.tar` into `iso_root`
   - `limine-binary/limine` download target → download into `bootloader/limine-binary`
   - all `limine-binary/...` paths in ISO/HDD recipes → `bootloader/limine-binary/...`
   - `edk2-ovmf-bins` targets/paths unchanged
2. `bootloader/limine.conf`
   - `MODULE_PATH: boot():/test_programs.tar` → `boot():/userspace.tar`
3. `kernel/src/main.c`
   - `/ram/bins/init` → `/ram/bin/init` (open + setup_user_stack calls)
4. `userspace/init.c`
   - `opendir("/ram/bins")` → `/ram/bin`
   - `open("/ram/bins/syscall_test", ...)` → `/ram/bin/...`
   - all `spawn("/ram/bins/...", ...)` → `/ram/bin/...`
5. `userspace/sh.c`
   - `snprintf(path, ..., "/ram/bins/%s", ...)` → `/ram/bin/%s`
6. `userspace/all_test.c`
   - `/ram/bins` → `/ram/bin` (stat, lstat, fstatat, open calls)
7. `userspace/tcc_link.c`
   - `/ram/bins/tcc`, `/ram/bins/edit2`, `/ram/bins/tls2` → `/ram/bin/...`
8. `userspace/GNUmakefile`
   - `BINDIR := bins` → `bin`
   - `../tools/tcc-0.9.27` and `../mlibc/...` paths unchanged
   - `usr/` handling unchanged
9. `tools/build-tcc`
   - `LDSCRIPT="$ROOT/test_programs/linker.ld"` → `userspace/linker.ld`
10. `.gitignore`
    - `/test_programs/bins` → `/userspace/bin`
    - `/limine-binary` → `/bootloader/limine-binary`

## Migration mechanics

- Use `git mv` for all tracked files/dirs so history is preserved.
- Uncommitted WIP (kernel multitasking/syscall changes, userspace tcc work,
  deleted `lib/`/`libexec/` install-tools dirs) is carried through untouched.
  Only paths are rewritten; file contents of WIP are not altered beyond the
  rename references above.
- `userspace.tar` stays a tracked artifact, matching how `test_programs.tar`
  was tracked before (only its name changes).
- Single commit for the whole restructure.

## Verification

1. Rebuild the ramdisk: `make userspace.tar` — must succeed, tar contains
   `bin/init`, `bin/sh`, `bin/tcc_link`, `usr/lib/libc.a`, etc.
2. Rebuild the kernel: `make kernel` — must succeed.
3. Rebuild the ISO: `make template-x86_64.iso` — must succeed.
4. Boot under QEMU (headless, via isa-debugcon) and confirm:
   - init runs, spawns user_idle/sig_test/syscall_test/all_test/sh
   - `tcc_link` runs on-OS tcc to link `edit.c` and `tls_test.c` and reports
     success (rc=0) for both
   - no stale `test_programs`/`bins` strings referenced at runtime

## Non-goals

- No restructuring of `kernel/` internals.
- No behavioral changes to the OS, mount layout beyond `/ram/bin` rename, or
  build process.
- No custom bootloader work.
