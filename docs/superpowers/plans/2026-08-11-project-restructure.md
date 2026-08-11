# Project Restructure Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reorganize the tiny-kern repo into a clear `bootloader/`, `kernel/`, `userspace/` top-level layout — a pure rename refactor with zero behavior change.

**Architecture:** Three top-level component dirs. `bootloader/` holds `limine.conf` + the gitignored `limine-binary/` (downloaded by make). `userspace/` replaces `test_programs/` (build output `bin/` replaces `bins/`). The boot-module tar `test_programs.tar` becomes `userspace.tar`, and the ramfs mount path `/ram/bins` becomes `/ram/bin` (`/ram/usr` unchanged). `kernel/`, `mlibc/`, `tools/`, `edk2-ovmf-bins/` stay at top level.

**Tech Stack:** GNU make, GCC cross toolchain (`x86_64-tinykern-elf-*`), Limine bootloader, ustar ramdisk, QEMU (headless verification via isa-debugcon).

## Global Constraints

- Rename `test_programs` → `userspace` EVERYWHERE in the repo (dir, make targets, tar name, `/ram/bins` → `/ram/bin`).
- Rename `limine.conf` + `limine-binary/` → live under `bootloader/`.
- `edk2-ovmf-bins/` stays at repo root (UEFI firmware, not a bootloader).
- Build artifacts (`*.iso`, `*.hdd`, `disk.img`) stay at repo root (gitignored).
- `mlibc/` and `tools/` stay at top level.
- `/ram/usr` is unchanged.
- No behavior change to the OS, kernel internals, or build process beyond the renames.
- Use `git mv` for tracked paths (preserves history AND keeps uncommitted WIP unstaged); plain `mv` only for gitignored `limine-binary/`.
- `userspace.tar` remains a tracked artifact (as `test_programs.tar` was before).
- Baseline (verified 2026-08-11): current ISO boots; kernel logs `mounted /test_programs.tar -> status 0`; `tcc_link: tcc rc=0`; `tcc_link: link OK, running edit2`.

Baseline headless boot command (used by Task 3 verification):

```bash
timeout 45 qemu-system-x86_64 -M q35 -display none \
  -drive if=pflash,unit=0,format=raw,file=edk2-ovmf-bins/ovmf-code-x86_64.fd,readonly=on \
  -cdrom template-x86_64.iso \
  -drive id=disk0,if=none,format=raw,file=disk.img \
  -device ahci,id=ahci0 -device ide-hd,drive=disk0,bus=ahci0.0 \
  -m 2G -device isa-debugcon,chardev=debug -chardev stdio,id=debug \
  > /tmp/opencode/boot.log 2>&1
tr -d '\r' < /tmp/opencode/boot.log > /tmp/opencode/boot_clean.log
```

---

### Task 1: Relocate bootloader (`limine.conf` + `limine-binary/` under `bootloader/`)

**Files:**
- Create: `bootloader/` (dir)
- Move: `limine.conf` → `bootloader/limine.conf` (tracked, `git mv`)
- Move: `limine-binary/` → `bootloader/limine-binary/` (gitignored, plain `mv`)
- Modify: `GNUmakefile` (limine download target + all `limine-binary/` paths + `distclean`)
- Modify: `.gitignore` (`/limine-binary` → `/bootloader/limine-binary`)

**Interfaces:**
- Consumes: existing `limine.conf`, existing `limine-binary/` dir.
- Produces: `bootloader/limine.conf` and `bootloader/limine-binary/` — the paths Task 3's ISO build reads. ISO/HDD recipes must reference `bootloader/limine-binary/limine` and `bootloader/limine-binary/limine-*.{sys,bin,EFI}`.

- [ ] **Step 1: Move the files**

```bash
cd /home/gavin/Repos/tiny-kern
mkdir -p bootloader
git mv limine.conf bootloader/limine.conf
mv limine-binary bootloader/limine-binary
```

- [ ] **Step 2: Update `.gitignore`**

Change:
```gitignore
/limine-binary
```
to:
```gitignore
/bootloader/limine-binary
```

- [ ] **Step 3: Update `GNUmakefile` limine paths**

Edit `GNUmakefile`. Replace every occurrence of `limine-binary/` with `bootloader/limine-binary/` **except** in the `rm -rf limine-binary` and `$(MAKE) -C limine-binary` lines which become `rm -rf bootloader/limine-binary` and `$(MAKE) -C bootloader/limine-binary`. The affected places (match by content):

1. Download target:
```make
limine-binary/limine:
	rm -rf limine-binary
```
→
```make
bootloader/limine-binary/limine:
	rm -rf bootloader/limine-binary
```
and inside that recipe `$(MAKE) -C limine-binary \` → `$(MAKE) -C bootloader/limine-binary \`.

2. ISO rule deps: `$(IMAGE_NAME).iso: limine-binary/limine kernel test_programs.tar` → `$(IMAGE_NAME).iso: bootloader/limine-binary/limine kernel test_programs.tar`.

3. ISO recipe: `cp -v limine-binary/limine-bios.sys limine-binary/limine-bios-cd.bin limine-binary/limine-uefi-cd.bin iso_root/boot/limine/` → prefix each with `bootloader/`; same for `cp -v limine-binary/BOOTX64.EFI iso_root/EFI/BOOT/` and `cp -v limine-binary/BOOTIA32.EFI iso_root/EFI/BOOT/`.

4. ISO recipe: `./limine-binary/limine bios-install $(IMAGE_NAME).iso` → `./bootloader/limine-binary/limine bios-install $(IMAGE_NAME).iso`.

5. HDD rule deps: `$(IMAGE_NAME).hdd: limine-binary/limine kernel` → `$(IMAGE_NAME).hdd: bootloader/limine-binary/limine kernel`.

6. HDD recipe: `./limine-binary/limine bios-install $(IMAGE_NAME).hdd` → `./bootloader/limine-binary/limine bios-install $(IMAGE_NAME).hdd`; `mcopy -i ... limine-binary/limine-bios.sys ::/boot/limine` (and `BOOTX64.EFI`, `BOOTIA32.EFI`, `BOOTAA64.EFI`, `BOOTRISCV64.EFI`, `BOOTLOONGARCH64.EFI`) → prefix each with `bootloader/`.

7. `distclean`: `rm -rf iso_root *.iso *.hdd limine-binary edk2-ovmf-bins` → `rm -rf iso_root *.iso *.hdd bootloader/limine-binary edk2-ovmf-bins`.

Do NOT touch the `test_programs.tar` deps yet (Task 2).

- [ ] **Step 4: Verify paths are consistent**

```bash
grep -n "limine-binary" GNUmakefile
```
Expected: every `limine-binary` occurrence is prefixed `bootloader/`. Also:
```bash
test -f bootloader/limine-binary/limine && echo OK
```
Expected: `OK`.

- [ ] **Step 5: Commit**

```bash
git add -A -- GNUmakefile .gitignore bootloader
git commit -m "chore: move limine config and binaries under bootloader/"
```

---

### Task 2: Rename `test_programs` → `userspace` (dirs, tar, ramfs layout)

**Files:**
- Move: `test_programs/` → `userspace/` (`git mv` — moves untracked `bin/` too, carries WIP unstaged)
- Move: `test_programs.tar` → `userspace.tar` (`git mv`)
- Modify: `userspace/GNUmakefile` (`BINDIR`, `bins/tcc`)
- Modify: `kernel/src/main.c` (`/ram/bins/init`)
- Modify: `userspace/init.c`, `userspace/sh.c`, `userspace/all_test.c`, `userspace/tcc_link.c` (`/ram/bins` → `/ram/bin`)
- Modify: `GNUmakefile` (userspace target, `userspace.tar` rule, ISO rule)
- Modify: `tools/build-tcc` (LDSCRIPT path)
- Modify: `bootloader/limine.conf` (`MODULE_PATH`)
- Modify: `.gitignore` (`/test_programs/bins` → `/userspace/bin`)

**Interfaces:**
- Consumes: `bootloader/limine-binary/limine` (Task 1) for the ISO; the userspace sources built by the root `GNUmakefile`.
- Produces: `userspace.tar` (ustar root = `userspace/`, containing `bin/init`, `bin/tcc_link`, `usr/lib/libc.a`, ...), kernel `bin-x86_64/kernel`, and the `/ram/bin` mount layout consumed by the booted userspace in Task 3.

- [ ] **Step 1: Move the directories and tar**

```bash
cd /home/gavin/Repos/tiny-kern
git mv test_programs userspace
git mv test_programs.tar userspace.tar
```

- [ ] **Step 2: Update `userspace/GNUmakefile`**

Edit `userspace/GNUmakefile`:
- `BINDIR   := bins` → `BINDIR   := bin`
- `bins/tcc: $(TCCSRC)/tcc` → `bin/tcc: $(TCCSRC)/tcc`
- `usr: bins/tcc` → `usr: bin/tcc`

- [ ] **Step 3: Update `kernel/src/main.c` ramfs paths**

Edit `kernel/src/main.c`:
- `int fd = open("/ram/bins/init", O_RDONLY);` → `open("/ram/bin/init", ...)`
- `setup_user_stack(p->addr_space, init_argv, init_envp, "/ram/bins/init",` → `"/ram/bin/init"`

- [ ] **Step 4: Update userspace `.c` files**

Edit `userspace/init.c`: replace `/ram/bins` → `/ram/bin` in `opendir("/ram/bins")`, `open("/ram/bins/syscall_test", ...)`, and every `spawn("/ram/bins/...", ...)` (user_idle, sig_test, syscall_test, all_test, sh, tcc_link).

Edit `userspace/sh.c`: `snprintf(path, sizeof(path), "/ram/bins/%s", argv[0]);` → `/ram/bin/%s`.

Edit `userspace/all_test.c`: replace `/ram/bins` → `/ram/bin` in the `stat`, `lstat`, `fstatat`, and the two `open(...)` calls (`/ram/bins/init` and `/ram/bins`).

Edit `userspace/tcc_link.c`: replace `/ram/bins/tcc`, `/ram/bins/edit2`, `/ram/bins/tls2` → `/ram/bin/...`.

- [ ] **Step 5: Update root `GNUmakefile`**

Edit `GNUmakefile`:

1. Phony target and rule:
```make
.PHONY: test_programs
test_programs: mlibc
	$(MAKE) -C test_programs clean
	$(MAKE) -C test_programs
```
→
```make
.PHONY: userspace
userspace: mlibc
	$(MAKE) -C userspace clean
	$(MAKE) -C userspace
```

2. Tar rule:
```make
test_programs.tar: test_programs/GNUmakefile $(wildcard test_programs/*.c test_programs/*.ld test_programs/usr/src/*.c) tools/tcc-0.9.27/tcc tools/tcc-0.9.27/libtcc1.a
	$(MAKE) -C test_programs
	tar --format=ustar -C test_programs -cf $@ .
```
→
```make
userspace.tar: userspace/GNUmakefile $(wildcard userspace/*.c userspace/*.ld userspace/usr/src/*.c) tools/tcc-0.9.27/tcc tools/tcc-0.9.27/libtcc1.a
	$(MAKE) -C userspace
	tar --format=ustar -C userspace -cf $@ .
```

3. ISO rule deps: `$(IMAGE_NAME).iso: bootloader/limine-binary/limine kernel test_programs.tar` → `$(IMAGE_NAME).iso: bootloader/limine-binary/limine kernel userspace.tar`.

4. ISO recipe: `cp -v test_programs.tar iso_root/` → `cp -v userspace.tar iso_root/`.

- [ ] **Step 6: Update `tools/build-tcc`**

Edit `tools/build-tcc`:
```
LDSCRIPT="$ROOT/test_programs/linker.ld"
```
→
```
LDSCRIPT="$ROOT/userspace/linker.ld"
```

- [ ] **Step 7: Update `bootloader/limine.conf`**

Edit `bootloader/limine.conf`:
```
    MODULE_PATH: boot():/test_programs.tar
```
→
```
    MODULE_PATH: boot():/userspace.tar
```

- [ ] **Step 8: Update `.gitignore`**

Change:
```gitignore
/test_programs/bins
```
to:
```gitignore
/userspace/bin
```

- [ ] **Step 9: Verify no stale references**

```bash
grep -rln "test_programs" --exclude-dir=.git --exclude-dir=mlibc --exclude-dir=cross --exclude-dir=obj-x86_64 --exclude-dir=bin-x86_64 --exclude-dir=iso_root --exclude-dir=gcc-14.1.0 --exclude-dir=binutils-2.42 --exclude-dir=tcc-0.9.27 --exclude-dir=bootloader .
```
Expected: only `docs/superpowers/specs/2026-08-11-project-restructure-design.md` and `template-x86_64.iso` (stale artifact, rebuilt in Task 3).

```bash
grep -rn "/ram/bins" kernel userspace
```
Expected: no output.

- [ ] **Step 10: Build userspace and kernel**

```bash
make userspace.tar
make kernel
```
Expected: both succeed. Then:
```bash
tar -tf userspace.tar | grep -E "\./(bin/init|bin/tcc_link|usr/lib/libc.a|usr/bin/tcc)$"
```
Expected: all four entries listed.

- [ ] **Step 11: Commit**

Stage the renames (already staged by `git mv`) plus the edited files:

```bash
git add -A -- GNUmakefile .gitignore tools/build-tcc kernel/src/main.c userspace/GNUmakefile userspace/init.c userspace/sh.c userspace/all_test.c userspace/tcc_link.c bootloader/limine.conf
git commit -m "refactor: rename test_programs to userspace with /ram/bin layout"
```

Note: `userspace/init.c` and `userspace/tcc_link.c` had pre-existing uncommitted edits (the tcc work); editing them here brings those edits into this commit. That is expected and acceptable. The remaining kernel WIP (multitasking/syscall files) is left untouched and uncommitted.

---

### Task 3: Full rebuild and headless boot verification

**Files:**
- None modified (verification only; fix only if something fails).

**Interfaces:**
- Consumes: `bootloader/limine-binary/limine` (Task 1), `userspace.tar` + kernel (Task 2), all three wired through the root `GNUmakefile`.

- [ ] **Step 1: Rebuild the ISO end-to-end**

```bash
make template-x86_64.iso
```
Expected: succeeds; output shows `cp -v userspace.tar iso_root/` and limine stages installed from `bootloader/limine-binary/`.

- [ ] **Step 2: Boot headless and capture output**

Run the baseline boot command from Global Constraints with `timeout 45`.

- [ ] **Step 3: Check runtime markers**

```bash
grep -a "mounted .* -> status 0" /tmp/opencode/boot_clean.log
grep -a "init: start" /tmp/opencode/boot_clean.log
grep -a "tcc_link: tcc rc=0" /tmp/opencode/boot_clean.log
grep -a "tcc_link: link OK, running edit2" /tmp/opencode/boot_clean.log
grep -ac "test_programs" /tmp/opencode/boot_clean.log
grep -ac "/ram/bins" /tmp/opencode/boot_clean.log
```
Expected:
- `mounted /userspace.tar -> status 0`
- `init: start ...`
- `tcc_link: tcc rc=0`
- `tcc_link: link OK, running edit2`
- `test_programs` count = 0
- `/ram/bins` count = 0

- [ ] **Step 4: Confirm WIP untouched**

```bash
git status --short | grep -E "^ M kernel/src/(multitasking|sys|fs|input)"
```
Expected: the pre-existing WIP kernel files still listed as modified (not staged, not lost).

- [ ] **Step 5: Fix any failures**

If a marker is missing, the boot log `/tmp/opencode/boot_clean.log` shows where it stopped. Common causes and fixes:
- Kernel not loading: check `bootloader/limine-binary/limine` paths in `GNUmakefile`.
- Tar not mounted: check `bootloader/limine.conf` `MODULE_PATH` says `/userspace.tar` and that `userspace.tar` exists at ISO root.
- Programs not found: stale `/ram/bins` strings — re-run Task 2 Step 8.
Fix, re-run Steps 1–4, then commit the fix as its own commit (e.g. `fix: correct stale path after restructure`).

No commit is made if all markers pass — the reorg is already committed in Tasks 1–2.
