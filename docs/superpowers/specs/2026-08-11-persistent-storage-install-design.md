# Persistent Storage, `install` Command, and Shell Improvements

Date: 2026-08-11

## Overview

Add persistent disk storage to tiny-kern. Drives attached to the AHCI
controller are exposed as block devices in devfs (`/dev/sda`, `/dev/sdb`,
...). A new `SYS_MKFS` syscall formats a drive as FAT16 (reusing the existing
kernel FAT16 driver). The `install` command formats a selected drive (given as
a device path such as `/dev/sda`), mounts it at `/bins`, and copies the base
binaries from `/ram/bin`. The kernel auto-mounts the first FAT16 drive at
`/bins` at boot. The shell starts in `/`, prints the current directory in its
prompt, and resolves commands through `PATH` with direct-path execution for
anything containing a slash.

## Design Principles

- Follow POSIX-style conventions where the platform supports them: block
  devices under `/dev`, device-path based mount sources, `PATH`-based command
  resolution, `<sys/mount.h>`-style `mount()` at the libc level.
- Reuse existing kernel infrastructure (FAT16 driver, AHCI block device
  layer, drive map, devfs) rather than adding parallel implementations.
- The ramfs boot path must keep working with no drives attached.

## Components

### 1. Kernel: devfs block devices

- On boot, after `drive_map_init()`, register one devfs node per drive map
  entry:
  - Name: `/dev/sda`, `/dev/sdb`, ... (drive map index `i` maps to device
    letter `'a' + i`).
  - `read`/`write` honor the byte offset passed by the VFS (`LBA = offset /
    512`), performing sector-aligned transfers via `disk_reader` /
    `disk_writer`. Unaligned edge bytes are handled by reading/writing whole
    sectors and masking.
  - Device size reported via `stat` is `drive->sector_count * 512`.
- The existing devfs device nodes for TTYs keep their current behavior
  (offset ignored); block devices get their own node ops that use the offset.

### 2. Kernel: `SYS_MKFS`

- New syscall `SYS_MKFS(path)`, entry `SYS_MKFS` (append to
  `kernel/include/abi/syscalls.h`, dispatched in `kernel/src/sys/handler.c`).
- Resolves the device path (`/dev/sda`, `/dev/sdb`, ...) to a drive number
  using the devfs name-to-drive mapping, builds a `vfs_blockdev_t` over that
  drive (same pattern as `vfs_mount`), and calls
  `fat16_format(&blockdev, drive->sector_count)`.
- Returns `0` on success, `-errno` on failure (invalid device, format error).

### 3. Kernel: `SYS_MOUNT(source, target)`

- Replaces the current `SYS_MOUNT(name, drive_number)` ABI. No existing
  userspace caller uses the old form, so the change is safe.
- `source` is a device path (`/dev/sda`), resolved to a drive number.
- `target` is a mount point name that becomes a top-level VFS node
  (`bins` -> `/bins`), reusing `vfs_mount` semantics.
- Validates that the drive carries a FAT16 volume (`vfs_get_type`) before
  mounting; returns `0` / `-errno`.
- Limitation: mount points are top-level only. Arbitrary-depth mount points
  are out of scope and noted as future work.

### 4. Kernel: boot auto-mount

- After devfs setup, probe drives 0..N. If a drive reports a FAT16 volume,
  mount it at `/bins`. Only the first FAT16 drive is auto-mounted; further
  drives are left for manual mounting.
- If no drive has a FAT16 volume, boot proceeds exactly as today (ramfs only).

### 5. Userspace: `install` command

- New program `userspace/install.c`, built and placed in `/ram/bin` like other
  programs.
- Usage: `install [dev]`, default `dev = /dev/sda`.
- Sequence:
  1. If the device is not a FAT16 volume, call `SYS_MKFS` on it.
  2. Call `SYS_MOUNT(source, "bins")` to mount it at `/bins`.
  3. Copy every entry of `/ram/bin` into `/bins` via normal file I/O.
  4. Print a summary and leave the volume mounted.
- Errors are reported with a message and a non-zero exit status.

### 6. Shell changes (`userspace/sh.c`)

- Start in `/` (change the `chdir("/ram")` call to `chdir("/")`).
- Prompt: print the current working directory followed by ` > ` (e.g.
  `/ram > `). If `getcwd` fails, fall back to `sh > `.
- Command resolution in `run_external()`:
  - If the command contains a `/`, `execve` it directly (absolute or relative
    to the shell's cwd). This makes `./a.out`, `./bin`, and `/ram/x` work.
  - Otherwise, search the `PATH` (shell-managed, default `/ram/bin:/bins`) and
    `execve` the first match. Report "command not found" if none match.
  - Fork, wait, background jobs, and error handling stay unchanged.

## Data Flow

### Install sequence (first use)

1. User runs `install /dev/sda`.
2. `install` probes the device; not FAT16, so it calls `SYS_MKFS("/dev/sda")`;
   the kernel formats the drive FAT16.
3. `install` calls `SYS_MOUNT("/dev/sda", "bins")`; the kernel mounts the
   volume at `/bins`.
4. `install` copies `/ram/bin/*` to `/bins/*`.
5. The shell's default `PATH` already includes `/bins`, so installed programs
   are immediately runnable by name.

### Boot sequence (subsequent boots)

1. Kernel mounts the ramfs (`/ram`) as today.
2. devfs block devices are registered.
3. The first FAT16 drive is auto-mounted at `/bins`.
4. The shell starts in `/`; installed programs resolve through `PATH`.

## Error Handling

- `SYS_MKFS`: invalid device path, unknown device, or format failure returns
  `-errno`; `install` prints the message and exits non-zero.
- `SYS_MOUNT`: invalid source, already-mounted port, or non-FAT16 volume
  returns `-errno` (existing `vfs_mount` error codes).
- `install`: partial copy failures are reported; the volume remains mounted so
  a re-run can complete the copy.
- Shell: `getcwd` failure falls back to `sh > `; command not found prints a
  clear message.

## Testing

1. `make` the kernel and userspace; boot in QEMU with a 64 MiB `disk.img`.
2. `install /dev/sda`:
   - Confirm `/bins` is populated with copies of `/ram/bin`.
   - Run an installed program by name (e.g. `echo hi`) and confirm it
     executes.
   - Confirm `install /dev/sda` again is idempotent (no re-format panic).
3. Persistence: reboot the VM with the same `disk.img`; confirm `/bins` is
   auto-mounted and installed programs still run.
4. Shell: `pwd`/prompt reflect cwd after `cd`; `./a.out` (slash path) and bare
   name (PATH) both execute; unknown command reports not found.
5. Regression: boot with an empty/unformatted `disk.img` and confirm the
   ramfs-only path still works (mount failures are non-fatal).

## Out of Scope / Future Work

- Arbitrary-depth mount points.
- Multiple auto-mounted drives with distinct mount points.
- Writing the FAT16 layout from userspace (kept in-kernel via `SYS_MKFS`).
- Filesystem drivers other than FAT16.
